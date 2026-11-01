#include "layout_session.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <spectra/figure_registry.hpp>
#include <spectra/logger.hpp>
#include <spectra/series.hpp>

#include "data_binding.hpp"
#include "../dialog_env_guard.hpp"
#include "../native_dialog_policy.hpp"
#include "../../../third_party/tinyfiledialogs.h"

#ifdef SPECTRA_USE_IMGUI
    #include "imgui.h"
#endif

namespace spectra
{

LayoutSession::LayoutSession() : recent_(RecentLayouts::default_path())
{
    recent_.load();
}

void LayoutSession::poll()
{
    if (!open_fn_ || pending_opens_.empty())
        return;
    std::vector<std::string> paths;
    paths.swap(pending_opens_);
    for (const auto& p : paths)
        open_fn_(p);
}

void LayoutSession::adopt(LayoutOpenResult result)
{
    for (auto& slot : result.slots)
        slots_.push_back(std::move(slot));
    if (unresolved_count() > 0)
        visible_ = true;
}

size_t LayoutSession::unresolved_count() const
{
    return static_cast<size_t>(std::count_if(
        slots_.begin(),
        slots_.end(),
        [](const LayoutBindingSlot& s) { return s.result.status == BindingStatus::Unresolved; }));
}

bool LayoutSession::rebind(size_t index, const std::string& uri)
{
    if (index >= slots_.size() || !registry_)
        return false;
    return rebind_slot(slots_[index], uri, *registry_, resolver());
}

size_t LayoutSession::use_csv_file(const std::string& file_name, const std::string& path)
{
    csv_.set_file(file_name, path);
    if (!registry_)
        return 0;
    size_t bound = 0;
    for (auto& slot : slots_)
    {
        if (slot.result.status != BindingStatus::Unresolved)
            continue;
        const auto b = parse_data_binding(slot.uri);
        if (!b || b->scheme != BindingScheme::Csv || b->source != file_name)
            continue;
        if (rebind_slot(slot, slot.uri, *registry_, resolver())
            && slot.result.status == BindingStatus::Bound)
            ++bound;
    }
    return bound;
}

void LayoutSession::prune()
{
    if (!registry_)
        return;
    slots_.erase(
        std::remove_if(slots_.begin(),
                       slots_.end(),
                       [this](const LayoutBindingSlot& s)
                       { return find_series(*registry_, s.figure_id, s.series) == nullptr; }),
        slots_.end());
    if (selected_ >= static_cast<int>(slots_.size()))
        selected_ = -1;
}

void LayoutSession::with_saved_labels(const std::function<void()>& fn)
{
    std::vector<std::pair<Series*, std::string>> changed;
    if (registry_)
    {
        for (const auto& slot : slots_)
        {
            if (slot.result.status != BindingStatus::Unresolved)
                continue;
            if (Series* s = find_series(*registry_, slot.figure_id, slot.series))
            {
                changed.emplace_back(s, s->label());
                s->label(slot.label);
            }
        }
    }
    fn();
    for (auto& [s, lbl] : changed)
        s->label(lbl);
}

void LayoutSession::pick_csv_for_selected()
{
    if (selected_ < 0 || selected_ >= static_cast<int>(slots_.size()))
        return;
    const auto b = parse_data_binding(slots_[selected_].uri);
    if (!b || b->scheme != BindingScheme::Csv)
        return;
    if (!native_dialogs_enabled())
        return;
    DialogEnvGuard env_guard;
    char const*    filters[3] = {"*.csv", "*.tsv", "*.txt"};
    std::string    start      = last_dir;
    if (start.empty())
    {
        const char* home = std::getenv("HOME");
        start            = home ? home : "/";
    }
    if (start.back() != '/')
        start += '/';
    const std::string title  = "Locate " + b->source;
    const char*       result = tinyfd_openFileDialog(title.c_str(),
                                               (start + b->source).c_str(),
                                               3,
                                               filters,
                                               "CSV files",
                                               0);
    if (!result)
        return;
    const std::string path = result;
    const size_t      n    = use_csv_file(b->source, path);
    status_                = n > 0 ? "Bound " + std::to_string(n) + " series to " + path
                                   : "Could not bind " + b->source + " to " + path;
}

void LayoutSession::draw()
{
#ifdef SPECTRA_USE_IMGUI
    if (!visible_)
        return;
    prune();

    ImGui::SetNextWindowSize(ImVec2(680.0f, 420.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Data Bindings", &visible_))
    {
        ImGui::End();
        return;
    }

    const size_t unresolved = unresolved_count();
    if (slots_.empty())
        ImGui::TextDisabled("No layout is open. Use File > Open Layout...");
    else if (unresolved == 0)
        ImGui::TextUnformatted("All data bindings are resolved.");
    else
        ImGui::Text("%zu unresolved binding%s: select one to pick a replacement.",
                    unresolved,
                    unresolved == 1 ? "" : "s");

    // Tall enough for every row up to 8, then it scrolls (row height follows the UI scale).
    const float row_h = ImGui::GetTextLineHeight() + ImGui::GetStyle().CellPadding.y * 2.0f + 1.0f;
    const float table_h =
        row_h * static_cast<float>(std::min<size_t>(slots_.size(), 8) + 1) + row_h * 0.5f;
    if (!slots_.empty()
        && ImGui::BeginTable("##layout_bindings",
                             3,
                             ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH
                                 | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                             ImVec2(0.0f, table_h)))
    {
        ImGui::TableSetupColumn("Status",
                                ImGuiTableColumnFlags_WidthFixed,
                                ImGui::CalcTextSize("Unresolved").x + 8.0f);
        ImGui::TableSetupColumn("Series", ImGuiTableColumnFlags_WidthStretch, 0.35f);
        ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthStretch, 0.65f);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < slots_.size(); ++i)
        {
            const auto& slot = slots_[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(static_cast<int>(i));
            const char* status = "Bound";
            ImVec4      color(0.45f, 0.80f, 0.45f, 1.0f);
            if (slot.result.status == BindingStatus::Waiting)
            {
                status = "Waiting";
                color  = ImVec4(0.90f, 0.75f, 0.30f, 1.0f);
            }
            else if (slot.result.status == BindingStatus::Unresolved)
            {
                status = "Unresolved";
                color  = ImVec4(0.95f, 0.40f, 0.40f, 1.0f);
            }
            ImGui::TextColored(color, "%s", status);
            ImGui::TableSetColumnIndex(1);
            if (ImGui::Selectable(slot.label.empty() ? "(unnamed)" : slot.label.c_str(),
                                  selected_ == static_cast<int>(i),
                                  ImGuiSelectableFlags_SpanAllColumns))
            {
                selected_ = static_cast<int>(i);
                std::snprintf(edit_buf_, sizeof(edit_buf_), "%s", slot.uri.c_str());
                status_.clear();
            }
            if (ImGui::IsItemHovered() && !slot.result.message.empty())
                ImGui::SetTooltip("%s", slot.result.message.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(slot.uri.empty() ? "(none)" : slot.uri.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (selected_ >= 0 && selected_ < static_cast<int>(slots_.size()))
    {
        auto& slot = slots_[selected_];
        ImGui::Separator();
        ImGui::TextWrapped("%s", slot.result.message.c_str());
        const auto& style     = ImGui::GetStyle();
        const float buttons_w = ImGui::CalcTextSize("Apply").x
                                + ImGui::CalcTextSize("Locate file...").x
                                + style.FramePadding.x * 4.0f + style.ItemSpacing.x * 2.0f;
        ImGui::SetNextItemWidth(-buttons_w);
        const bool enter = ImGui::InputTextWithHint("##binding_uri",
                                                    "topic://name  or  csv://file.csv?x=col#col",
                                                    edit_buf_,
                                                    sizeof(edit_buf_),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ImGui::Button("Apply") || enter)
        {
            rebind(static_cast<size_t>(selected_), edit_buf_);
            status_ = slots_[selected_].result.message;
        }
        const auto b = parse_data_binding(slot.uri);
        if (b && b->scheme == BindingScheme::Csv)
        {
            ImGui::SameLine();
            if (ImGui::Button("Locate file..."))
                pick_csv_for_selected();
        }
        if (topic_names_fn)
        {
            const auto names = topic_names_fn();
            if (!names.empty())
            {
                ImGui::SetNextItemWidth(-buttons_w);
                if (ImGui::BeginCombo("##topic_pick", "Use a live topic..."))
                {
                    for (const auto& n : names)
                    {
                        if (ImGui::Selectable(n.c_str()))
                        {
                            const std::string uri = topic_binding_uri(n);
                            std::snprintf(edit_buf_, sizeof(edit_buf_), "%s", uri.c_str());
                            rebind(static_cast<size_t>(selected_), uri);
                            status_ = slots_[selected_].result.message;
                        }
                    }
                    ImGui::EndCombo();
                }
            }
        }
    }
    if (!status_.empty())
        ImGui::TextDisabled("%s", status_.c_str());

    ImGui::End();
#endif
}

}   // namespace spectra
