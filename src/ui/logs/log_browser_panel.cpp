// log_browser_panel.cpp — log file browser panel implementation.

#include "log_browser_panel.hpp"

#include <spectra/axes.hpp>
#include <spectra/series.hpp>

#include <algorithm>
#include <filesystem>
#include <span>

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>

    #include "ui/theme/theme_imgui.hpp"
#endif

namespace spectra::ui::logs
{

namespace sl = spectra::logs;

LogBrowserPanel::LogBrowserPanel()  = default;
LogBrowserPanel::~LogBrowserPanel() = default;

void LogBrowserPanel::set_registry(const sl::LogReaderRegistry* r)
{
    registry_ = r;
}

const sl::LogReaderRegistry& LogBrowserPanel::registry() const
{
    if (registry_)
        return *registry_;
    if (!own_registry_)
    {
        // Lazily built so panels wired to an app registry never pay for it.
        own_registry_ = std::make_unique<sl::LogReaderRegistry>();
        sl::register_builtin_log_readers(*own_registry_);
    }
    return *own_registry_;
}

void LogBrowserPanel::set_status(std::string msg, bool error)
{
    status_       = std::move(msg);
    status_error_ = error;
}

bool LogBrowserPanel::can_open(const std::string& path) const
{
    return registry().can_open(path);
}

bool LogBrowserPanel::open_file(const std::string& path)
{
    visible_ = true;

    std::string error;
    auto        reader = registry().open(path, &error);
    if (!reader)
    {
        set_status(error, true);
        return false;
    }

    // Re-opening a file replaces its entry (fresh contents, new id so stale
    // drag payloads cannot address the new field list).
    files_.erase(std::remove_if(files_.begin(),
                                files_.end(),
                                [&](const OpenFile& f) { return f.path == path; }),
                 files_.end());

    OpenFile f;
    f.id     = next_file_id_++;
    f.path   = path;
    f.name   = std::filesystem::path(path).filename().string();
    f.tree   = sl::build_field_tree(reader->fields());
    f.reader = std::move(reader);

    std::string msg = "Opened " + f.name + " (" + std::string(f.reader->format_name()) + ", "
                      + std::to_string(f.reader->fields().size()) + " fields)";
    const bool has_warnings = !f.reader->warnings().empty();
    if (has_warnings)
        msg += ". " + f.reader->warnings().front();
    set_status(std::move(msg), false);

    files_.push_back(std::move(f));
    return true;
}

void LogBrowserPanel::close_file(uint32_t file_id)
{
    files_.erase(std::remove_if(files_.begin(),
                                files_.end(),
                                [&](const OpenFile& f) { return f.id == file_id; }),
                 files_.end());
}

const LogBrowserPanel::OpenFile* LogBrowserPanel::find_file(uint32_t file_id) const
{
    for (const auto& f : files_)
    {
        if (f.id == file_id)
            return &f;
    }
    return nullptr;
}

LineSeries* LogBrowserPanel::plot_field(uint32_t file_id, size_t field_index, Axes* target)
{
    const OpenFile* file = find_file(file_id);
    if (!file || !file->reader)
    {
        set_status("That log file is no longer open", true);
        return nullptr;
    }
    const auto& fields = file->reader->fields();
    if (field_index >= fields.size())
    {
        set_status("Unknown field", true);
        return nullptr;
    }
    const sl::LogField& field = fields[field_index];

    sl::LogSeries data;
    if (!file->reader->read(field_index, data))
    {
        set_status(field.full_name() + ": " + file->reader->last_error(), true);
        return nullptr;
    }
    if (data.time.empty())
    {
        set_status(field.full_name() + " has no samples", true);
        return nullptr;
    }

    Axes* ax = target ? target : (axes_provider_ ? axes_provider_() : nullptr);
    if (!ax)
    {
        set_status("No plot to add " + field.full_name() + " to", true);
        return nullptr;
    }

    const bool first_series = ax->series().empty();
    auto&      line         = ax->line();
    line.set_x(std::span<const double>(data.time));
    line.set_y(std::span<const float>(data.values));
    line.label(field.full_name());
    if (first_series && ax->xlabel().empty())
        ax->xlabel("Time (s)");
    ax->auto_fit();

    set_status(
        "Plotted " + field.full_name() + " (" + std::to_string(data.time.size()) + " samples)",
        false);
    return &line;
}

// ─── UI ──────────────────────────────────────────────────────────────────────

void LogBrowserPanel::set_filter(const std::string& text)
{
    const size_t n = std::min(text.size(), sizeof(filter_buf_) - 1);
    std::copy_n(text.begin(), n, filter_buf_);
    filter_buf_[n] = '\0';
}

bool LogBrowserPanel::subtree_matches(const OpenFile& file, const sl::LogFieldNode& node) const
{
    if (node.field_index >= 0)
    {
        const auto& fields = file.reader->fields();
        return sl::field_matches_filter(fields[static_cast<size_t>(node.field_index)], filter_buf_);
    }
    return std::any_of(node.children.begin(),
                       node.children.end(),
                       [&](const sl::LogFieldNode& c) { return subtree_matches(file, c); });
}

#ifndef SPECTRA_USE_IMGUI

void LogBrowserPanel::draw() {}
void LogBrowserPanel::draw_file(OpenFile&, bool) {}
bool LogBrowserPanel::draw_node(const OpenFile&, const sl::LogFieldNode&, bool)
{
    return false;
}

#else

void LogBrowserPanel::draw()
{
    if (!visible_)
        return;

    ImGui::SetNextWindowSize(ImVec2(380.0f, 480.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Log Browser", &visible_))
    {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Open..."))
    {
        if (open_dialog_cb_)
            open_dialog_cb_();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Open a log file (or drop one onto the window)");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##log_filter", "Search fields...", filter_buf_, sizeof(filter_buf_));

    if (!status_.empty())
    {
        const auto& th = ui::theme();
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ui::to_imvec4(status_error_ ? th.error : th.text_secondary));
        ImGui::TextWrapped("%s", status_.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Separator();

    ImGui::BeginChild("##log_tree", ImVec2(0, 0), 0);
    if (files_.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(ui::theme().text_secondary));
        ImGui::TextWrapped(
            "Open a ULog (.ulg) or MCAP (.mcap) file with File > Open Log..., or drop one onto "
            "the window. Drag a field onto a plot, or double-click it, to plot it.");
        ImGui::PopStyleColor();
    }
    const bool filtering = filter_buf_[0] != '\0';
    for (auto& f : files_)
        draw_file(f, filtering);
    ImGui::EndChild();

    ImGui::End();

    if (pending_close_ != 0)
    {
        close_file(pending_close_);
        pending_close_ = 0;
    }
}

void LogBrowserPanel::draw_file(OpenFile& file, bool filtering)
{
    ImGui::PushID(static_cast<int>(file.id));
    const std::string header =
        file.name + "  [" + std::string(file.reader->format_name()) + "]###file";
    ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
    const bool open = ImGui::CollapsingHeader(header.c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", file.path.c_str());
    if (ImGui::BeginPopupContextItem("##file_ctx"))
    {
        if (ImGui::MenuItem("Close"))
            pending_close_ = file.id;
        ImGui::EndPopup();
    }

    if (open)
    {
        for (const auto& w : file.reader->warnings())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(ui::theme().warning));
            ImGui::TextWrapped("%s", w.c_str());
            ImGui::PopStyleColor();
        }

        bool any = false;
        for (const auto& topic : file.tree.children)
            any |= draw_node(file, topic, filtering);
        if (!any && filtering)
            ImGui::TextDisabled("No fields match \"%s\".", filter_buf_);
        else if (!any)
            ImGui::TextDisabled("No plottable fields.");
    }
    ImGui::PopID();
}

bool LogBrowserPanel::draw_node(const OpenFile& file, const sl::LogFieldNode& node, bool filtering)
{
    if (filtering && !subtree_matches(file, node))
        return false;

    if (node.field_index >= 0)
    {
        const auto&         fields = file.reader->fields();
        const sl::LogField& field  = fields[static_cast<size_t>(node.field_index)];

        ImGui::PushID(node.field_index);
        ImGui::TreeNodeEx(node.name.c_str(),
                          ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen
                              | ImGuiTreeNodeFlags_SpanAvailWidth);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            plot_field(file.id, static_cast<size_t>(node.field_index));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        {
            ImGui::SetTooltip("%s\ntype: %s%s%s\nsamples: %zu\nDrag onto a plot or double-click",
                              field.full_name().c_str(),
                              field.type.empty() ? "?" : field.type.c_str(),
                              field.unit.empty() ? "" : "\nunit: ",
                              field.unit.c_str(),
                              field.sample_count);
        }
        if (ImGui::BeginDragDropSource())
        {
            LogFieldDragPayload payload;
            payload.file_id     = file.id;
            payload.field_index = static_cast<uint32_t>(node.field_index);
            ImGui::SetDragDropPayload(LOG_FIELD_DRAG_TYPE, &payload, sizeof(payload));
            ImGui::TextUnformatted(field.full_name().c_str());
            ImGui::EndDragDropSource();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%zu", field.sample_count);
        ImGui::PopID();
        return true;
    }

    // Group: open while searching so matches are visible.
    if (filtering)
        ImGui::SetNextItemOpen(true);
    const bool open = ImGui::TreeNodeEx(node.name.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
    if (open)
    {
        for (const auto& child : node.children)
            draw_node(file, child, filtering);
        ImGui::TreePop();
    }
    return true;
}

#endif   // SPECTRA_USE_IMGUI

}   // namespace spectra::ui::logs
