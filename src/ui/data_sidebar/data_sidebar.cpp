// data_sidebar.cpp — docked Data sidebar (ImGui view over DataSidebarModel).

#include "ui/data_sidebar/data_sidebar.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

#include "ui/layout/layout_manager.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>

    #include "ui/theme/design_tokens.hpp"
    #include "ui/theme/icons.hpp"
    #include "ui/theme/theme.hpp"
    #include "ui/theme/theme_imgui.hpp"
#endif

namespace spectra::ui::data_sidebar
{

#ifndef SPECTRA_USE_IMGUI

void DataSidebar::draw(ImFont*, ImFont*) {}
void DataSidebar::draw_rows(ImFont*) {}
void DataSidebar::run_pending_actions() {}

#else

namespace
{

// Seconds between provider re-lists while the sidebar is open.
constexpr double REFRESH_INTERVAL_S = 0.5;

bool is_container(RowKind k)
{
    return k == RowKind::Provider || k == RowKind::Source || k == RowKind::Group;
}

// Draw `icon` with the icon font at `size`, vertically centred in a row.
float draw_icon(ImDrawList* dl, Icon icon, float size, ImVec2 pos, float row_h, ImU32 col)
{
    ImFont* font = ui::icon_font(size);
    if (!font)
        return 0.0f;
    const char*  glyph = ui::icon_str(icon);
    const ImVec2 sz    = font->CalcTextSizeA(size, FLT_MAX, 0.0f, glyph);
    dl->AddText(font, size, ImVec2(pos.x, pos.y + std::floor((row_h - sz.y) * 0.5f)), col, glyph);
    return sz.x;
}

}   // namespace

void DataSidebar::draw(ImFont* heading_font, ImFont* /*icon_font*/)
{
    if (!layout_)
        return;
    const Rect bounds = layout_->left_panel_rect();
    if (bounds.w < 1.0f || bounds.h < 1.0f)
    {
        was_visible_ = false;
        return;
    }
    if (!was_visible_)
    {
        // Just opened: fresh lists, and typing goes straight to the search.
        focus_search_      = true;
        scroll_to_sel_     = true;
        last_refresh_time_ = -1.0;
    }
    was_visible_ = true;

    const double now = ImGui::GetTime();
    if (last_refresh_time_ < 0.0 || now - last_refresh_time_ >= REFRESH_INTERVAL_S)
    {
        model_.refresh();
        last_refresh_time_ = now;
    }

    const auto& th = ui::theme();

    // ── Resize grip on the right edge ──
    {
        const float handle_w = LayoutManager::RESIZE_HANDLE_WIDTH;
        const float handle_x = bounds.x + bounds.w - handle_w * 0.5f;
        ImGui::SetNextWindowPos(ImVec2(handle_x, bounds.y));
        ImGui::SetNextWindowSize(ImVec2(handle_w, bounds.h));
        const ImGuiWindowFlags handle_flags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground
            | ImGuiWindowFlags_NoFocusOnAppearing;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (ImGui::Begin("##data_sidebar_resize", nullptr, handle_flags))
        {
            ImGui::SetCursorScreenPos(ImVec2(handle_x, bounds.y));
            ImGui::InvisibleButton("##grip", ImVec2(handle_w, bounds.h));
            const bool hovered = ImGui::IsItemHovered();
            const bool active  = ImGui::IsItemActive();
            if (hovered || active)
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                const float x = bounds.x + bounds.w;
                ImGui::GetWindowDrawList()->AddLine(ImVec2(x, bounds.y),
                                                    ImVec2(x, bounds.y + bounds.h),
                                                    ui::to_imu32(th.accent, active ? 1.0f : 0.5f),
                                                    active ? 3.0f : 2.0f);
            }
            if (active)
                layout_->set_left_panel_width(ImGui::GetIO().MousePos.x - bounds.x);
            if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                layout_->set_left_panel_width(LayoutManager::LEFT_PANEL_DEFAULT_WIDTH);
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    // ── Panel ──
    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y));
    ImGui::SetNextWindowSize(ImVec2(bounds.w, bounds.h));
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoBackground;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(ui::tokens::SPACE_3, ui::tokens::PANEL_PADDING));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (ImGui::Begin("##data_sidebar", nullptr, flags))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // Solid surface (themes without glass draw no backdrop of their own) and
        // a hairline on the canvas side.
        dl->AddRectFilled(ImVec2(bounds.x, bounds.y),
                          ImVec2(bounds.x + bounds.w, bounds.y + bounds.h),
                          ui::to_imu32(th.bg_secondary, 0.96f));
        dl->AddLine(ImVec2(bounds.x + bounds.w - 1.0f, bounds.y),
                    ImVec2(bounds.x + bounds.w - 1.0f, bounds.y + bounds.h),
                    ui::to_imu32(th.border_subtle, 0.6f),
                    1.0f);

        // ── Eyebrow header + close ──
        {
            if (heading_font)
                ImGui::PushFont(heading_font);
            ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(th.text_tertiary));
            ImGui::TextUnformatted("DATA");
            ImGui::PopStyleColor();
            if (heading_font)
                ImGui::PopFont();

            const float  btn = ImGui::GetTextLineHeight();
            const ImVec2 cur = ImGui::GetItemRectMin();
            ImGui::SameLine();
            ImGui::SetCursorScreenPos(
                ImVec2(bounds.x + bounds.w - ui::tokens::SPACE_3 - btn, cur.y));
            const bool clicked = ImGui::InvisibleButton("##close", ImVec2(btn, btn));
            const bool hovered = ImGui::IsItemHovered();
            draw_icon(dl,
                      Icon::Close,
                      ui::tokens::ICON_XS,
                      ImVec2(ImGui::GetItemRectMin().x + 2.0f, ImGui::GetItemRectMin().y),
                      btn,
                      ui::to_imu32(hovered ? th.text_primary : th.text_tertiary));
            if (hovered)
                ImGui::SetTooltip("Close the Data sidebar");
            if (clicked)
                layout_->set_left_panel_visible(false);
            ImGui::Dummy(ImVec2(0.0f, ui::tokens::SPACE_1));
        }

        // ── Search ──
        bool search_active = false;
        {
            if (focus_search_)
            {
                ImGui::SetKeyboardFocusHere();
                focus_search_ = false;
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputTextWithHint("##data_search",
                                         "Search sources and fields",
                                         search_buf_,
                                         sizeof(search_buf_),
                                         ImGuiInputTextFlags_EscapeClearsAll))
            {
                model_.set_query(search_buf_);
                scroll_to_sel_ = true;
            }
            search_active = ImGui::IsItemActive();
        }

        // ── Summary + status ──
        {
            char summary[96];
            if (model_.filtering())
                std::snprintf(summary,
                              sizeof(summary),
                              "%zu %s",
                              model_.match_count(),
                              model_.match_count() == 1 ? "match" : "matches");
            else
                std::snprintf(summary,
                              sizeof(summary),
                              "%zu fields in %zu sources",
                              model_.field_count(),
                              model_.source_count());
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                ui::to_imvec4(model_.filtering() && model_.match_count() == 0 ? th.warning
                                                                              : th.text_secondary));
            ImGui::TextUnformatted(summary);
            ImGui::PopStyleColor();
            if (!model_.status().empty())
            {
                ImGui::PushStyleColor(
                    ImGuiCol_Text,
                    ui::to_imvec4(model_.status_is_error() ? th.error : th.text_secondary));
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(model_.status().c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
            }
            ImGui::Dummy(ImVec2(0.0f, ui::tokens::SPACE_1));
        }

        // ── Keyboard: up / down / left / right / enter ──
        const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (focused)
        {
            // Arrows and Enter belong to the list, not the plot behind it.
            ImGui::SetNextFrameWantCaptureKeyboard(true);
            const int before = model_.selected();
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
                model_.move_selection(1);
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
                model_.move_selection(-1);
            if (ImGui::IsKeyPressed(ImGuiKey_PageDown))
                model_.move_selection(10);
            if (ImGui::IsKeyPressed(ImGuiKey_PageUp))
                model_.move_selection(-10);
            if (!search_active)
            {
                if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
                    model_.expand_selected();
                if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
                    model_.collapse_selected();
            }
            if ((ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
                && model_.selected() >= 0)
            {
                pending_     = Pending::PlotRow;   // PlotRow toggles containers
                pending_row_ = static_cast<size_t>(model_.selected());
            }
            if (model_.selected() != before)
                scroll_to_sel_ = true;
        }

        // ── Tree ──
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::to_imvec4(th.bg_secondary, 0.0f));
        if (ImGui::BeginChild("##data_tree", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None))
            draw_rows(nullptr);
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    ImGui::End();
    ImGui::PopStyleVar(3);

    run_pending_actions();
}

void DataSidebar::draw_rows(ImFont* /*icon_font*/)
{
    const auto&                 th   = ui::theme();
    const std::vector<DataRow>& rows = model_.rows();
    ImDrawList*                 dl   = ImGui::GetWindowDrawList();

    const float row_h  = ImGui::GetTextLineHeight() + 2.0f * ui::tokens::ROW_PADDING_V;
    const float indent = ui::tokens::SPACE_4;
    const float chev_w = ui::tokens::ICON_XS + ui::tokens::SPACE_1;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()), row_h);
    const int sel = model_.selected();
    if (scroll_to_sel_ && sel >= 0)
        clipper.IncludeItemByIndex(sel);

    while (clipper.Step())
    {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
        {
            const DataRow& r = rows[static_cast<size_t>(i)];
            ImGui::PushID(i);
            const ImVec2 p     = ImGui::GetCursorScreenPos();
            const float  avail = ImGui::GetContentRegionAvail().x;
            float        x     = p.x + ui::tokens::SPACE_1 + static_cast<float>(r.depth) * indent;
            const float  ty    = p.y + std::floor((row_h - ImGui::GetTextLineHeight()) * 0.5f);

            if (r.kind == RowKind::Empty)
            {
                // Hint; clicking it runs the provider's "open" action if any.
                DataProvider*     prov    = model_.provider(r.provider);
                const std::string open    = prov ? prov->open_label() : std::string();
                const bool        clicked = ImGui::InvisibleButton("##empty", ImVec2(avail, row_h));
                const bool        hovered = ImGui::IsItemHovered();
                dl->PushClipRect(p, ImVec2(p.x + avail, p.y + row_h), true);
                dl->AddText(
                    ImVec2(x + chev_w, ty),
                    ui::to_imu32(hovered && !open.empty() ? th.text_secondary : th.text_tertiary),
                    r.label.c_str());
                dl->PopClipRect();
                if (hovered && !r.label.empty())
                {
                    if (open.empty())
                        ImGui::SetTooltip("%s", r.label.c_str());
                    else
                        ImGui::SetTooltip("%s\nClick to %s", r.label.c_str(), open.c_str());
                }
                if (clicked && !open.empty())
                {
                    pending_     = Pending::OpenProvider;
                    pending_row_ = static_cast<size_t>(i);
                }
                ImGui::PopID();
                continue;
            }

            DataProvider*     prov = model_.provider(r.provider);
            const DataSource* src =
                r.kind != RowKind::Provider ? &model_.sources(r.provider)[r.source] : nullptr;

            const bool selected = sel == i;
            if (ImGui::Selectable("##row",
                                  selected,
                                  ImGuiSelectableFlags_AllowDoubleClick,
                                  ImVec2(avail, row_h)))
            {
                model_.select(static_cast<size_t>(i));
                if (is_container(r.kind))
                {
                    pending_     = Pending::ToggleRow;
                    pending_row_ = static_cast<size_t>(i);
                }
                else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    pending_     = Pending::PlotRow;
                    pending_row_ = static_cast<size_t>(i);
                }
            }
            if (scroll_to_sel_ && selected)
            {
                if (!ImGui::IsItemVisible())
                    ImGui::SetScrollHereY(0.5f);
                scroll_to_sel_ = false;
            }
            const bool hovered = ImGui::IsItemHovered();
            if (selected)
            {
                // Clear selection marker (the keyboard target): tint + accent bar.
                dl->AddRectFilled(p,
                                  ImVec2(p.x + avail, p.y + row_h),
                                  ui::to_imu32(th.accent_muted, 0.55f),
                                  ui::tokens::RADIUS_SM);
                dl->AddRectFilled(p, ImVec2(p.x + 2.0f, p.y + row_h), ui::to_imu32(th.accent));
            }

            // Drag a field onto a plot.
            if (r.kind == RowKind::Field && ImGui::BeginDragDropSource())
            {
                const DragPayload payload = model_.drag_payload(static_cast<size_t>(i));
                if (!payload.empty())
                    ImGui::SetDragDropPayload(payload.type.c_str(),
                                              payload.data.data(),
                                              payload.data.size());
                ImGui::TextUnformatted(r.label.c_str());
                ImGui::EndDragDropSource();
            }

            if (r.kind == RowKind::Field && hovered && src)
            {
                const DataField& f = src->fields[r.field];
                ImGui::SetItemTooltip("%s / %s%s%s%s%s\nDouble-click or Enter to plot, or drag "
                                      "onto a plot",
                                      src->name.c_str(),
                                      f.group.c_str(),
                                      f.group.empty() ? "" : "/",
                                      f.name.c_str(),
                                      f.detail.empty() ? "" : "  ",
                                      f.detail.c_str());
            }

            // Context menu.
            if (ImGui::BeginPopupContextItem("##ctx"))
            {
                model_.select(static_cast<size_t>(i));
                auto pick = [&](Pending what)
                {
                    pending_     = what;
                    pending_row_ = static_cast<size_t>(i);
                };
                if (r.kind == RowKind::Field)
                {
                    if (ImGui::MenuItem("Plot", "Enter"))
                        pick(Pending::PlotRow);
                    if (ImGui::MenuItem("Copy Name") && src)
                    {
                        const DataField&  f    = src->fields[r.field];
                        const std::string full = f.group.empty() ? f.name : f.group + "/" + f.name;
                        ImGui::SetClipboardText(full.c_str());
                    }
                }
                if (r.kind == RowKind::Source && src)
                {
                    if (src->state == DataSource::State::Stopped && ImGui::MenuItem("Start"))
                        pick(Pending::StartSource);
                    if (src->state == DataSource::State::Running && ImGui::MenuItem("Stop"))
                        pick(Pending::StopSource);
                    if (prov && prov->supports_close() && ImGui::MenuItem("Close"))
                        pick(Pending::CloseSource);
                }
                if (is_container(r.kind) && !model_.filtering()
                    && ImGui::MenuItem(r.expanded ? "Collapse" : "Expand"))
                    pick(Pending::ToggleRow);
                if (prov && (r.kind == RowKind::Provider || r.kind == RowKind::Source))
                {
                    const std::string open = prov->open_label();
                    if (!open.empty() && ImGui::MenuItem(open.c_str()))
                        pick(Pending::OpenProvider);
                    const std::string details = prov->details_label();
                    if (!details.empty() && ImGui::MenuItem(details.c_str()))
                        pick(Pending::DetailsProvider);
                }
                ImGui::EndPopup();
            }

            // ── Row contents ──
            dl->PushClipRect(p, ImVec2(p.x + avail, p.y + row_h), true);
            if (is_container(r.kind))
            {
                draw_icon(dl,
                          r.expanded ? Icon::ChevronDown : Icon::ChevronRight,
                          ui::tokens::ICON_XS * 0.8f,
                          ImVec2(x, p.y),
                          row_h,
                          ui::to_imu32(th.text_tertiary));
                x += chev_w;
            }
            else
            {
                x += chev_w;
            }

            ImU32 label_col = ui::to_imu32(th.text_primary);
            if (r.kind == RowKind::Provider && prov)
            {
                x += draw_icon(dl,
                               prov->icon(),
                               ui::tokens::ICON_XS,
                               ImVec2(x, p.y),
                               row_h,
                               ui::to_imu32(th.accent))
                     + ui::tokens::SPACE_2;
            }
            else if (r.kind == RowKind::Source && src && src->state != DataSource::State::Static)
            {
                const ui::Color dot = src->state == DataSource::State::Running   ? th.success
                                      : src->state == DataSource::State::Faulted ? th.error
                                                                                 : th.text_tertiary;
                const float     rad = 3.5f;
                dl->AddCircleFilled(ImVec2(x + rad, p.y + row_h * 0.5f), rad, ui::to_imu32(dot));
                x += rad * 2.0f + ui::tokens::SPACE_2;
            }
            else if (r.kind == RowKind::Group)
            {
                label_col = ui::to_imu32(th.text_secondary);
            }

            // Right-aligned detail / count.
            std::string right;
            if (is_container(r.kind))
                right = std::to_string(r.count);
            else
                right = r.detail;
            float right_w = 0.0f;
            if (!right.empty())
            {
                right_w = ImGui::CalcTextSize(right.c_str()).x;
                dl->AddText(ImVec2(p.x + avail - ui::tokens::SPACE_2 - right_w, ty),
                            ui::to_imu32(th.text_tertiary),
                            right.c_str());
                right_w += ui::tokens::SPACE_3;
            }
            dl->PopClipRect();

            const float label_max = p.x + avail - ui::tokens::SPACE_2 - right_w;
            dl->PushClipRect(p, ImVec2(std::max(x, label_max), p.y + row_h), true);
            dl->AddText(ImVec2(x, ty), label_col, r.label.c_str());
            dl->PopClipRect();

            ImGui::PopID();
        }
    }
    clipper.End();
    ImGui::PopStyleVar();

    if (rows.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(th.text_tertiary));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(model_.filtering() ? "Nothing matches the search."
                                                  : "No data sources are available.");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
}

void DataSidebar::run_pending_actions()
{
    const Pending what = pending_;
    pending_           = Pending::None;
    if (what == Pending::None)
        return;
    const auto& rows = model_.rows();
    if (pending_row_ >= rows.size())
        return;
    const DataRow r    = rows[pending_row_];
    DataProvider* prov = model_.provider(r.provider);
    if (!prov)
        return;
    const uint64_t src_key = r.kind != RowKind::Provider && r.kind != RowKind::Empty
                                 ? model_.sources(r.provider)[r.source].key
                                 : 0;

    switch (what)
    {
        case Pending::PlotRow:
            if (r.kind == RowKind::Field)
                model_.plot_row(pending_row_);
            else
                model_.toggle_expanded(pending_row_);
            break;
        case Pending::ToggleRow:
            model_.toggle_expanded(pending_row_);
            break;
        case Pending::StartSource:
        case Pending::StopSource:
            prov->set_source_running(src_key, what == Pending::StartSource);
            last_refresh_time_ = -1.0;
            break;
        case Pending::CloseSource:
            prov->close_source(src_key);
            model_.refresh();
            break;
        case Pending::OpenProvider:
            prov->open();
            last_refresh_time_ = -1.0;
            break;
        case Pending::DetailsProvider:
            prov->open_details();
            break;
        case Pending::None:
            break;
    }
}

#endif   // SPECTRA_USE_IMGUI

}   // namespace spectra::ui::data_sidebar
