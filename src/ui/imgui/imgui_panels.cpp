#ifdef SPECTRA_USE_IMGUI

    #include "imgui_integration_internal.hpp"

namespace spectra
{

void ImGuiIntegration::draw_tab_bar()
{
    if (!layout_manager_ || !tab_bar_)
        return;
    if (!layout_manager_->is_tab_bar_visible())
        return;

    Rect bounds = layout_manager_->tab_bar_rect();
    if (bounds.w < 1.0f || bounds.h < 1.0f)
        return;

    // Create an ImGui window for the tab bar so that GetWindowDrawList(),
    // OpenPopup(), and BeginPopup() all work correctly inside TabBar::draw()
    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y));
    ImGui::SetNextWindowSize(ImVec2(bounds.w, bounds.h));
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    ImGui::PushStyleColor(ImGuiCol_WindowBg,
                          ImVec4(ui::theme().bg_secondary.r,
                                 ui::theme().bg_secondary.g,
                                 ui::theme().bg_secondary.b,
                                 1.0f));

    if (ImGui::Begin("##tabbar_host", nullptr, flags))
    {
        ImDrawList* dl   = ImGui::GetWindowDrawList();
        ImVec2      wpos = ImGui::GetWindowPos();
        ImVec2      wsz  = ImGui::GetWindowSize();
        for (int i = 0; i < 3; ++i)
        {
            float t     = static_cast<float>(i) / 3.0f;
            float alpha = 0.08f * (1.0f - t);
            float off   = 1.0f + t * ui::tokens::ELEVATION_1_SPREAD;
            dl->AddRectFilled(ImVec2(wpos.x, wpos.y + wsz.y),
                              ImVec2(wpos.x + wsz.x, wpos.y + wsz.y + off),
                              IM_COL32(0, 0, 0, static_cast<int>(alpha * 255)));
        }
        dl->AddLine(ImVec2(wpos.x, std::floor(wpos.y + wsz.y) - 1.0f),
                    ImVec2(wpos.x + wsz.x, std::floor(wpos.y + wsz.y) - 1.0f),
                    IM_COL32(static_cast<uint8_t>(ui::theme().border_default.r * 255),
                             static_cast<uint8_t>(ui::theme().border_default.g * 255),
                             static_cast<uint8_t>(ui::theme().border_default.b * 255),
                             90),
                    1.0f);
        tab_bar_->draw(bounds, dock_system_);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

void ImGuiIntegration::draw_canvas(Figure& figure)
{
    if (!layout_manager_)
        return;

    Rect bounds = layout_manager_->canvas_rect();

    // Canvas is primarily handled by the Vulkan renderer
    // We just set up the viewport here for ImGui coordination
    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y));
    ImGui::SetNextWindowSize(ImVec2(bounds.w, bounds.h));

    // Show welcome screen only when figure has no axes at all (truly blank).
    // A figure created via "New Figure" gets default axes, so it shows the
    // normal empty plot grid instead of the welcome page.
    bool has_any_axes = !figure.axes().empty() || !figure.all_axes().empty();

    if (!has_any_axes)
    {
        ImGuiWindowFlags empty_flags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;

        ImGui::PushStyleColor(ImGuiCol_WindowBg,
                              ImVec4(ui::theme().bg_canvas.r,
                                     ui::theme().bg_canvas.g,
                                     ui::theme().bg_canvas.b,
                                     1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

        if (ImGui::Begin("##canvas_welcome", nullptr, empty_flags))
        {
            const auto& colors = ui::theme();
            float       cx     = bounds.x + bounds.w * 0.5f;
            float       cy     = bounds.y + bounds.h * 0.5f;

            // Title
            ImGui::PushFont(font_title_);
            const char* title    = "Welcome to Spectra";
            ImVec2      title_sz = ImGui::CalcTextSize(title);
            ImGui::SetCursorScreenPos(ImVec2(cx - title_sz.x * 0.5f, cy - 60.0f));
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                ImVec4(colors.text_primary.r, colors.text_primary.g, colors.text_primary.b, 0.9f));
            ImGui::TextUnformatted(title);
            ImGui::PopStyleColor();
            ImGui::PopFont();

            // Subtitle
            ImGui::PushFont(font_heading_);
            const char* subtitle    = "Drop a CSV file or use the command palette to get started";
            ImVec2      subtitle_sz = ImGui::CalcTextSize(subtitle);
            ImGui::SetCursorScreenPos(ImVec2(cx - subtitle_sz.x * 0.5f, cy - 28.0f));
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(colors.text_tertiary.r,
                                         colors.text_tertiary.g,
                                         colors.text_tertiary.b,
                                         0.7f));
            ImGui::TextUnformatted(subtitle);
            ImGui::PopStyleColor();
            ImGui::PopFont();

            // Action buttons
            float btn_w       = 140.0f;
            float btn_h       = 32.0f;
            float btn_spacing = ui::tokens::SPACE_3;
            float total_w     = btn_w * 3.0f + btn_spacing * 2.0f;
            float btn_y       = cy + 10.0f;

            ImGui::PushFont(font_heading_);

            // Button style: accent-tinted with rounded corners
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::tokens::RADIUS_MD);
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 0.15f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 0.30f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                  ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 0.45f));
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f));

            // Open CSV button — triggers the native file dialog (same as menu Data > Load from CSV)
            ImGui::SetCursorScreenPos(ImVec2(cx - total_w * 0.5f, btn_y));
            if (ImGui::Button("Open CSV", ImVec2(btn_w, btn_h)))
            {
                pending_open_csv_ = true;
            }

            // New Plot button — adds default axes to this figure, dismissing welcome screen
            ImGui::SetCursorScreenPos(ImVec2(cx - total_w * 0.5f + btn_w + btn_spacing, btn_y));
            if (ImGui::Button("New Plot", ImVec2(btn_w, btn_h)))
            {
                figure.subplot(1, 1, 1);
            }

            // Command Palette button
            ImGui::SetCursorScreenPos(
                ImVec2(cx - total_w * 0.5f + (btn_w + btn_spacing) * 2.0f, btn_y));
            if (ImGui::Button("Commands", ImVec2(btn_w, btn_h)))
            {
                if (command_registry_)
                    command_registry_->execute("app.command_palette");
            }

            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar();
            ImGui::PopFont();

            // Keyboard hint
            ImGui::PushFont(font_body_);
            const char* hint = "Ctrl+K  Command Palette  |  Ctrl+T  New Tab  |  Ctrl+S  Export PNG";
            ImVec2      hint_sz = ImGui::CalcTextSize(hint);
            ImGui::SetCursorScreenPos(ImVec2(cx - hint_sz.x * 0.5f, btn_y + btn_h + 24.0f));
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(colors.text_tertiary.r,
                                         colors.text_tertiary.g,
                                         colors.text_tertiary.b,
                                         0.5f));
            ImGui::TextUnformatted(hint);
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
        return;
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings
                             | ImGuiWindowFlags_NoBringToFrontOnFocus
                             | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground
                             | ImGuiWindowFlags_NoInputs;

    // Transparent window for canvas area
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

    // Canvas border draws into a dedicated overlay window so it respects
    // ImGui z-ordering and renders behind menus/popups.
    ImDrawList* dl = nullptr;
    {
        ImGui::SetNextWindowPos(ImVec2(bounds.x - 8.0f, bounds.y - 8.0f));
        ImGui::SetNextWindowSize(ImVec2(bounds.w + 16.0f, bounds.h + 16.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGuiWindowFlags border_flags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus
            | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground
            | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
            | ImGuiWindowFlags_NoInputs;
        if (ImGui::Begin("##canvas_border", nullptr, border_flags))
            dl = ImGui::GetWindowDrawList();
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    if (ImGui::Begin("##canvas", nullptr, flags))
    {
        // Canvas content is rendered by Vulkan, not ImGui
        // This window is just for input handling coordination
    }
    ImGui::End();
    ImGui::PopStyleColor(2);

    if (!dl)
        return;

    ImDrawList* bg_dl = ImGui::GetBackgroundDrawList();
    auto        outer = ui::theme().bg_primary.lerp(ui::theme().bg_secondary, 0.20f);
    bg_dl->AddRectFilled(ImVec2(bounds.x - 8.0f, bounds.y - 8.0f),
                         ImVec2(bounds.x + bounds.w + 8.0f, bounds.y + bounds.h + 8.0f),
                         IM_COL32(static_cast<uint8_t>(outer.r * 255),
                                  static_cast<uint8_t>(outer.g * 255),
                                  static_cast<uint8_t>(outer.b * 255),
                                  96),
                         14.0f);
    for (int i = 0; i < 3; ++i)
    {
        float expand = static_cast<float>(i + 1);
        float alpha  = 0.12f - static_cast<float>(i) * 0.03f;
        dl->AddRect(ImVec2(bounds.x - expand, bounds.y - expand),
                    ImVec2(bounds.x + bounds.w + expand, bounds.y + bounds.h + expand),
                    IM_COL32(0, 0, 0, static_cast<int>(alpha * 255)),
                    10.0f);
    }
    dl->AddRect(ImVec2(bounds.x, bounds.y),
                ImVec2(bounds.x + bounds.w, bounds.y + bounds.h),
                IM_COL32(static_cast<uint8_t>(ui::theme().border_default.r * 255),
                         static_cast<uint8_t>(ui::theme().border_default.g * 255),
                         static_cast<uint8_t>(ui::theme().border_default.b * 255),
                         180),
                10.0f);
    dl->AddRect(ImVec2(bounds.x + 1.0f, bounds.y + 1.0f),
                ImVec2(bounds.x + bounds.w - 1.0f, bounds.y + bounds.h - 1.0f),
                IM_COL32(static_cast<uint8_t>(ui::theme().border_subtle.r * 255),
                         static_cast<uint8_t>(ui::theme().border_subtle.g * 255),
                         static_cast<uint8_t>(ui::theme().border_subtle.b * 255),
                         120),
                9.0f);
    dl->AddLine(ImVec2(bounds.x + 4.0f, bounds.y + 1.0f),
                ImVec2(bounds.x + bounds.w - 4.0f, bounds.y + 1.0f),
                IM_COL32(static_cast<uint8_t>(ui::theme().border_strong.r * 255),
                         static_cast<uint8_t>(ui::theme().border_strong.g * 255),
                         static_cast<uint8_t>(ui::theme().border_strong.b * 255),
                         56),
                1.0f);
    dl->AddRectFilledMultiColor(ImVec2(bounds.x + 1.0f, bounds.y + 1.0f),
                                ImVec2(bounds.x + bounds.w - 1.0f, bounds.y + bounds.h * 0.42f),
                                IM_COL32(0, 0, 0, 22),
                                IM_COL32(0, 0, 0, 22),
                                IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, 0));
    dl->AddRectFilledMultiColor(ImVec2(bounds.x + 1.0f, bounds.y + bounds.h * 0.58f),
                                ImVec2(bounds.x + bounds.w - 1.0f, bounds.y + bounds.h - 1.0f),
                                IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, 28),
                                IM_COL32(0, 0, 0, 28));
    dl->AddRectFilledMultiColor(ImVec2(bounds.x + 1.0f, bounds.y + 1.0f),
                                ImVec2(bounds.x + bounds.w * 0.16f, bounds.y + bounds.h - 1.0f),
                                IM_COL32(0, 0, 0, 18),
                                IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, 18));
    dl->AddRectFilledMultiColor(ImVec2(bounds.x + bounds.w * 0.84f, bounds.y + 1.0f),
                                ImVec2(bounds.x + bounds.w - 1.0f, bounds.y + bounds.h - 1.0f),
                                IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, 18),
                                IM_COL32(0, 0, 0, 18),
                                IM_COL32(0, 0, 0, 0));

    // Draw interactive page scrollbar when subplots overflow the visible canvas area
    if (figure.needs_scroll(bounds.h))
    {
        float content_h  = figure.content_height();
        float scroll_off = figure.scroll_offset_y();
        float max_scroll = std::max(0.0f, content_h - bounds.h);

        constexpr float SCROLLBAR_WIDTH = 12.0f;
        constexpr float SCROLLBAR_PAD   = 2.0f;
        constexpr float MIN_THUMB_H     = 20.0f;

        float sb_x = bounds.x + bounds.w - SCROLLBAR_WIDTH - SCROLLBAR_PAD;
        float sb_y = bounds.y + SCROLLBAR_PAD;
        float sb_h = bounds.h - SCROLLBAR_PAD * 2.0f;

        // Thumb size proportional to visible/total ratio
        float ratio   = bounds.h / content_h;
        float thumb_h = std::max(MIN_THUMB_H, sb_h * ratio);
        float track_h = sb_h - thumb_h;
        float thumb_y = sb_y + (max_scroll > 0.0f ? (scroll_off / max_scroll) * track_h : 0.0f);

        // Interactive scrollbar: invisible window over the scrollbar track
        ImGui::SetNextWindowPos(ImVec2(sb_x, sb_y));
        ImGui::SetNextWindowSize(ImVec2(SCROLLBAR_WIDTH, sb_h));
        ImGuiWindowFlags sb_flags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground
            | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing
            | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (ImGui::Begin("##page_scrollbar", nullptr, sb_flags))
        {
            ImGui::SetCursorScreenPos(ImVec2(sb_x, sb_y));
            ImGui::InvisibleButton("##sb_track", ImVec2(SCROLLBAR_WIDTH, sb_h));
            bool track_hovered = ImGui::IsItemHovered();
            bool track_active  = ImGui::IsItemActive();

            if (track_active && max_scroll > 0.0f)
            {
                float mouse_y    = ImGui::GetIO().MousePos.y;
                float rel        = (mouse_y - sb_y - thumb_h * 0.5f) / track_h;
                rel              = std::clamp(rel, 0.0f, 1.0f);
                float new_scroll = rel * max_scroll;
                figure.set_scroll_offset_y(new_scroll);
                scroll_off = new_scroll;
                thumb_y    = sb_y + rel * track_h;
            }

            // Determine thumb visual state
            bool thumb_hovered = track_hovered && ImGui::GetIO().MousePos.y >= thumb_y
                                 && ImGui::GetIO().MousePos.y <= thumb_y + thumb_h;

            ImDrawList* dl = ImGui::GetForegroundDrawList();

            // Track background
            auto  bg        = ui::theme().bg_elevated;
            ImU32 track_col = IM_COL32(static_cast<uint8_t>(bg.r * 255),
                                       static_cast<uint8_t>(bg.g * 255),
                                       static_cast<uint8_t>(bg.b * 255),
                                       100);
            dl->AddRectFilled(ImVec2(sb_x, sb_y),
                              ImVec2(sb_x + SCROLLBAR_WIDTH, sb_y + sb_h),
                              track_col,
                              SCROLLBAR_WIDTH * 0.5f);

            // Thumb with hover/active highlight
            auto    accent    = ui::theme().accent;
            uint8_t alpha     = track_active ? 240 : (thumb_hovered ? 210 : 180);
            ImU32   thumb_col = IM_COL32(static_cast<uint8_t>(accent.r * 255),
                                       static_cast<uint8_t>(accent.g * 255),
                                       static_cast<uint8_t>(accent.b * 255),
                                       alpha);
            dl->AddRectFilled(ImVec2(sb_x, thumb_y),
                              ImVec2(sb_x + SCROLLBAR_WIDTH, thumb_y + thumb_h),
                              thumb_col,
                              SCROLLBAR_WIDTH * 0.5f);
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }
}

void ImGuiIntegration::draw_inspector(Figure& figure)
{
    if (!layout_manager_)
        return;

    Rect bounds = layout_manager_->inspector_rect();
    if (bounds.w < 1.0f)
        return;   // Fully collapsed

    // Draw resize handle as a separate invisible window so it extends outside the inspector
    {
        float handle_w = LayoutManager::RESIZE_HANDLE_WIDTH;
        float handle_x = bounds.x - handle_w * 0.5f;
        ImGui::SetNextWindowPos(ImVec2(handle_x, bounds.y));
        ImGui::SetNextWindowSize(ImVec2(handle_w, bounds.h));
        ImGuiWindowFlags handle_flags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground
            | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (ImGui::Begin("##inspector_resize_handle", nullptr, handle_flags))
        {
            ImGui::SetCursorScreenPos(ImVec2(handle_x, bounds.y));
            ImGui::InvisibleButton("##resize_grip", ImVec2(handle_w, bounds.h));
            bool hovered = ImGui::IsItemHovered();
            bool active  = ImGui::IsItemActive();
            layout_manager_->set_inspector_resize_hovered(hovered);

            if (hovered || active)
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            }
            if (ImGui::IsItemClicked())
            {
                layout_manager_->set_inspector_resize_active(true);
            }
            if (active)
            {
                float right_edge = bounds.x + bounds.w;
                float new_width  = right_edge - ImGui::GetIO().MousePos.x;
                layout_manager_->set_inspector_width(new_width);
            }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                layout_manager_->set_inspector_resize_active(false);
            }

            // Visual resize indicator line
            if (hovered || active)
            {
                ImDrawList* dl       = ImGui::GetWindowDrawList();
                float       line_x   = bounds.x;
                auto        accent   = ui::theme().accent;
                ImU32       line_col = active ? IM_COL32(uint8_t(accent.r * 255),
                                                   uint8_t(accent.g * 255),
                                                   uint8_t(accent.b * 255),
                                                   255)
                                              : IM_COL32(uint8_t(accent.r * 255),
                                                   uint8_t(accent.g * 255),
                                                   uint8_t(accent.b * 255),
                                                   120);
                dl->AddLine(ImVec2(line_x, bounds.y),
                            ImVec2(line_x, bounds.y + bounds.h),
                            line_col,
                            active ? 3.0f : 2.0f);
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    // Inspector panel itself
    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y));
    ImGui::SetNextWindowSize(ImVec2(bounds.w, bounds.h));

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(ui::tokens::PANEL_PADDING + 4.0f, ui::tokens::PANEL_PADDING));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);   // No outer border
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
                          ImVec4(ui::theme().bg_secondary.r,
                                 ui::theme().bg_secondary.g,
                                 ui::theme().bg_secondary.b,
                                 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));   // Inner hairline drawn manually

    if (ImGui::Begin("##inspector", nullptr, flags))
    {
        // Floating surface depth cue: soft shadow gradient on left edge
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();

            // Multi-layer soft shadow (simulates elevation from canvas)
            float shadow_spread = ui::tokens::ELEVATION_2_SPREAD;
            for (int i = 0; i < 4; ++i)
            {
                float t     = static_cast<float>(i) / 4.0f;
                float alpha = 0.12f * (1.0f - t);
                float off   = shadow_spread * t;
                dl->AddRectFilled(ImVec2(bounds.x - off - 1.0f, bounds.y),
                                  ImVec2(bounds.x, bounds.y + bounds.h),
                                  IM_COL32(0, 0, 0, static_cast<int>(alpha * 255)));
            }

            // Crisp material edge: hairline border on left
            dl->AddLine(ImVec2(bounds.x, bounds.y),
                        ImVec2(bounds.x, bounds.y + bounds.h),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(ui::theme().border_subtle.r,
                                                              ui::theme().border_subtle.g,
                                                              ui::theme().border_subtle.b,
                                                              0.50f)),
                        1.0f);
        }

        // ── Inspector tab bar ──
        {
            const auto& colors  = ui::theme();
            float       avail_w = ImGui::GetContentRegionAvail().x;

            struct TabDef
            {
                const char* label;
                ui::Icon    icon;
                Section     section;
            };
            TabDef tabs[] = {
                {"Figure", ui::Icon::ChartLine, Section::Figure},
                {"Series", ui::Icon::Palette, Section::Series},
                {"Axes", ui::Icon::Axes, Section::Axes},
                {"Data", ui::Icon::Database, Section::DataEditor},
            };
            constexpr int tab_count = 4;
            float         tab_w     = std::floor(avail_w / static_cast<float>(tab_count));
            float         tab_h     = 28.0f;

            ImGui::PushFont(font_heading_);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::tokens::RADIUS_SM);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 0.0f));

            for (int i = 0; i < tab_count; ++i)
            {
                if (i > 0)
                    ImGui::SameLine();

                bool is_active = (active_section_ == tabs[i].section);

                // Tab button colors
                if (is_active)
                {
                    ImGui::PushStyleColor(
                        ImGuiCol_Button,
                        ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 0.18f));
                    ImGui::PushStyleColor(
                        ImGuiCol_ButtonHovered,
                        ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 0.28f));
                    ImGui::PushStyleColor(
                        ImGuiCol_ButtonActive,
                        ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 0.35f));
                    ImGui::PushStyleColor(
                        ImGuiCol_Text,
                        ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f));
                }
                else
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                          ImVec4(colors.text_secondary.r,
                                                 colors.text_secondary.g,
                                                 colors.text_secondary.b,
                                                 0.12f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                          ImVec4(colors.text_secondary.r,
                                                 colors.text_secondary.g,
                                                 colors.text_secondary.b,
                                                 0.20f));
                    ImGui::PushStyleColor(ImGuiCol_Text,
                                          ImVec4(colors.text_secondary.r,
                                                 colors.text_secondary.g,
                                                 colors.text_secondary.b,
                                                 0.85f));
                }

                char btn_id[64];
                std::snprintf(btn_id, sizeof(btn_id), "%s##insp_tab_%d", tabs[i].label, i);
                if (ImGui::Button(btn_id, ImVec2(tab_w, tab_h)))
                {
                    active_section_ = tabs[i].section;
                    // Clear stale selection when switching sections
                    if (tabs[i].section != Section::Series)
                        selection_ctx_.clear();
                }

                // Draw active indicator bar under the tab
                if (is_active)
                {
                    ImVec2      item_min = ImGui::GetItemRectMin();
                    ImVec2      item_max = ImGui::GetItemRectMax();
                    ImDrawList* tdl      = ImGui::GetWindowDrawList();
                    tdl->AddRectFilled(
                        ImVec2(item_min.x + 4.0f, item_max.y - 2.0f),
                        ImVec2(item_max.x - 4.0f, item_max.y),
                        ImGui::ColorConvertFloat4ToU32(
                            ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 0.8f)),
                        1.0f);
                }

                ImGui::PopStyleColor(4);
            }

            ImGui::PopStyleVar(2);
            ImGui::PopFont();

            // Separator line under tabs
            ImGui::Dummy(ImVec2(0, 4.0f));
            {
                ImDrawList* tdl   = ImGui::GetWindowDrawList();
                ImVec2      wpos  = ImGui::GetWindowPos();
                float       sep_y = ImGui::GetCursorScreenPos().y;
                tdl->AddLine(ImVec2(wpos.x + 8.0f, sep_y),
                             ImVec2(wpos.x + bounds.w - 8.0f, sep_y),
                             ImGui::ColorConvertFloat4ToU32(ImVec4(colors.border_subtle.r,
                                                                   colors.border_subtle.g,
                                                                   colors.border_subtle.b,
                                                                   0.35f)),
                             1.0f);
            }
            ImGui::Dummy(ImVec2(0, 4.0f));
        }

        // Scrollable content area
        ImGui::BeginChild("##inspector_content",
                          ImVec2(0, 0),
                          false,
                          ImGuiWindowFlags_NoBackground);

        if (panel_open_)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, panel_anim_);

            // Clear stale selection when switching to a different figure/tab
            if (selection_ctx_.type == ui::SelectionType::Series
                && selection_ctx_.figure != &figure)
            {
                selection_ctx_.clear();
            }

            // Update selection context based on active nav rail section.
            // When the Series section is active and the user has drilled into
            // a specific series, preserve that selection so the properties
            // panel stays visible. Switching to any other section always
            // overrides the selection.
            if (active_section_ == Section::DataEditor)
            {
                // Data editor mode: draw tabular data view instead of inspector
                data_editor_.draw(figure);
            }
            else
            {
                switch (active_section_)
                {
                    case Section::Figure:
                        selection_ctx_.select_figure(&figure);
                        break;
                    case Section::Series:
                        // Only show browser if user hasn't selected a specific series
                        if (selection_ctx_.type != ui::SelectionType::Series)
                        {
                            selection_ctx_.select_series_browser(&figure);
                        }
                        break;
                    case Section::Axes:
                        // Only show browser if user hasn't selected a specific axes.
                        // When switching figures, try to preserve the axes index if valid.
                        if (figure.axes().empty())
                        {
                            // No axes in this figure, clear selection
                            selection_ctx_.clear();
                        }
                        else if (selection_ctx_.type != ui::SelectionType::Axes)
                        {
                            selection_ctx_.select_axes(&figure, figure.axes_mut()[0].get(), 0);
                        }
                        else if (selection_ctx_.figure != &figure)
                        {
                            // User has axes selected but switched to a different figure.
                            // Try to select the same axes index in the new figure.
                            int target_idx = selection_ctx_.axes_index;
                            if (target_idx >= 0
                                && target_idx < static_cast<int>(figure.axes().size()))
                            {
                                selection_ctx_.select_axes(&figure,
                                                           figure.axes_mut()[target_idx].get(),
                                                           target_idx);
                            }
                            else
                            {
                                // Index out of range, fall back to first axes
                                selection_ctx_.select_axes(&figure, figure.axes_mut()[0].get(), 0);
                            }
                        }
                        break;
                    case Section::DataEditor:
                        break;   // Handled above
                }

                inspector_.set_context(selection_ctx_);
                inspector_.draw(figure);

                // Read back context (inspector may change selection, e.g. clicking a series)
                selection_ctx_ = inspector_.context();
            }

            ImGui::PopStyleVar();
        }

        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

void ImGuiIntegration::draw_inspector_toggle()
{
    if (!layout_manager_)
        return;

    Rect canvas  = layout_manager_->canvas_rect();
    bool is_open = layout_manager_->is_inspector_visible();

    // Small pill-shaped chevron toggle on the right edge of the canvas
    constexpr float BTN_W        = 16.0f;
    constexpr float BTN_H        = 36.0f;
    constexpr float BTN_ROUNDING = 8.0f;
    float           btn_x        = canvas.x + canvas.w - BTN_W;
    if (is_open)
    {
        Rect insp = layout_manager_->inspector_rect();
        btn_x     = insp.x - BTN_W;
    }
    float btn_y = canvas.y + (canvas.h - BTN_H) * 0.5f;

    ImGui::SetNextWindowPos(ImVec2(btn_x, btn_y));
    ImGui::SetNextWindowSize(ImVec2(BTN_W, BTN_H));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground
                             | ImGuiWindowFlags_NoBringToFrontOnFocus
                             | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollbar
                             | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::Begin("##inspector_toggle", nullptr, flags))
    {
        ImGui::InvisibleButton("##insp_toggle_btn", ImVec2(BTN_W, BTN_H));
        bool hovered = ImGui::IsItemHovered();
        bool clicked = ImGui::IsItemClicked();

        if (clicked)
        {
            if (is_open)
            {
                layout_manager_->set_inspector_visible(false);
                panel_open_ = false;
            }
            else
            {
                layout_manager_->set_inspector_visible(true);
                panel_open_ = true;
            }
        }

        const auto& colors = ui::theme();
        ImDrawList* dl     = ImGui::GetWindowDrawList();

        // Only show background on hover for a subtle feel
        if (hovered)
        {
            ImU32 bg_col = IM_COL32(static_cast<uint8_t>(colors.bg_tertiary.r * 255),
                                    static_cast<uint8_t>(colors.bg_tertiary.g * 255),
                                    static_cast<uint8_t>(colors.bg_tertiary.b * 255),
                                    180);
            dl->AddRectFilled(ImVec2(btn_x, btn_y),
                              ImVec2(btn_x + BTN_W, btn_y + BTN_H),
                              bg_col,
                              BTN_ROUNDING);
        }

        // Chevron icon centered in the pill
        ImFont* icon_font_ptr = font_icon_;
        if (icon_font_ptr)
        {
            const char* chevron =
                is_open ? ui::icon_str(ui::Icon::ChevronRight) : ui::icon_str(ui::Icon::Back);

            ImU32 icon_col =
                hovered ? ImGui::ColorConvertFloat4ToU32(
                              ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f))
                        : ImGui::ColorConvertFloat4ToU32(ImVec4(colors.text_secondary.r,
                                                                colors.text_secondary.g,
                                                                colors.text_secondary.b,
                                                                0.45f));

            float  icon_sz = icon_font_ptr->LegacySize;
            ImVec2 isz     = icon_font_ptr->CalcTextSizeA(icon_sz, FLT_MAX, 0.0f, chevron);
            float  ix      = btn_x + (BTN_W - isz.x) * 0.5f;
            float  iy      = btn_y + (BTN_H - icon_sz) * 0.5f;
            dl->AddText(icon_font_ptr, icon_sz, ImVec2(ix, iy), icon_col, chevron);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void ImGuiIntegration::draw_status_bar()
{
    if (!layout_manager_)
        return;

    Rect bounds = layout_manager_->status_bar_rect();
    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y));
    ImGui::SetNextWindowSize(ImVec2(bounds.w, bounds.h));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings
                             | ImGuiWindowFlags_NoBringToFrontOnFocus
                             | ImGuiWindowFlags_NoFocusOnAppearing;

    // Use zero vertical padding — we'll manually center text inside the bar
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui::tokens::SPACE_3, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    // Status bar uses a blend between bg_primary and bg_secondary (darker than panels)
    float sb_blend = 0.55f;
    auto  sb_bg_r =
        ui::theme().bg_primary.r * sb_blend + ui::theme().bg_secondary.r * (1.0f - sb_blend);
    auto sb_bg_g =
        ui::theme().bg_primary.g * sb_blend + ui::theme().bg_secondary.g * (1.0f - sb_blend);
    auto sb_bg_b =
        ui::theme().bg_primary.b * sb_blend + ui::theme().bg_secondary.b * (1.0f - sb_blend);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(sb_bg_r, sb_bg_g, sb_bg_b, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

    if (ImGui::Begin("##statusbar", nullptr, flags))
    {
        // ── Floating surface depth: top-edge shadow + hairline border ──
        {
            ImDrawList* bar_dl = ImGui::GetWindowDrawList();
            ImVec2      wpos   = ImGui::GetWindowPos();
            ImVec2      wsz    = ImGui::GetWindowSize();

            // Multi-layer soft shadow above the bar
            float shadow_spread = ui::tokens::ELEVATION_1_SPREAD;
            for (int i = 0; i < 4; ++i)
            {
                float t     = static_cast<float>(i) / 4.0f;
                float alpha = 0.08f * (1.0f - t);
                float off   = shadow_spread * t;
                bar_dl->AddRectFilled(ImVec2(wpos.x, wpos.y - off - 1.0f),
                                      ImVec2(wpos.x + wsz.x, wpos.y),
                                      IM_COL32(0, 0, 0, static_cast<int>(alpha * 255)));
            }

            // Crisp hairline border at top edge
            bar_dl->AddLine(ImVec2(wpos.x, wpos.y),
                            ImVec2(wpos.x + wsz.x, wpos.y),
                            IM_COL32(static_cast<uint8_t>(ui::theme().border_subtle.r * 255),
                                     static_cast<uint8_t>(ui::theme().border_subtle.g * 255),
                                     static_cast<uint8_t>(ui::theme().border_subtle.b * 255),
                                     80),
                            1.0f);
        }

        ImGuiIO& io = ImGui::GetIO();
        ImGui::PushFont(font_heading_);

        // Vertically center all text in the status bar
        float bar_h    = bounds.h;
        float text_h   = ImGui::GetTextLineHeight();
        float y_offset = (bar_h - text_h) * 0.5f;
        ImGui::SetCursorPosY(y_offset);

        // Left: cursor data readout
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(ui::theme().text_secondary.r,
                                     ui::theme().text_secondary.g,
                                     ui::theme().text_secondary.b,
                                     ui::theme().text_secondary.a));
        char cursor_buf[64];
        if (cursor_data_valid_)
            std::snprintf(cursor_buf,
                          sizeof(cursor_buf),
                          "X: %.4f  Y: %.4f",
                          cursor_data_x_,
                          cursor_data_y_);
        else
            std::snprintf(cursor_buf, sizeof(cursor_buf), "X: —  Y: —");
        ImGui::TextUnformatted(cursor_buf);
        ImGui::PopStyleColor();

        // Center: mode indicator with pill background
        ImGui::SameLine(0.0f, ui::tokens::SPACE_6);
        {
            const char* mode_label = "Navigate";
            auto        mode_color = ui::theme().text_secondary;
            switch (interaction_mode_)
            {
                case ToolMode::Pan:
                    mode_label = "Pan";
                    mode_color = ui::theme().accent;
                    break;
                case ToolMode::BoxZoom:
                    mode_label = "Box Zoom";
                    mode_color = ui::theme().warning;
                    break;
                case ToolMode::Select:
                    mode_label = "Select";
                    mode_color = ui::theme().info;
                    break;
                case ToolMode::ROI:
                    mode_label = "ROI";
                    mode_color = ui::theme().info;
                    break;
                case ToolMode::Measure:
                    mode_label = "Measure";
                    mode_color = ui::theme().success;
                    break;
                default:
                    break;
            }

            // Draw pill background behind mode label
            ImVec2 text_sz  = ImGui::CalcTextSize(mode_label);
            ImVec2 cursor_p = ImGui::GetCursorScreenPos();
            float  pill_pad = 4.0f;
            ImVec2 pill_min = ImVec2(cursor_p.x - pill_pad, cursor_p.y - 1.0f);
            ImVec2 pill_max =
                ImVec2(cursor_p.x + text_sz.x + pill_pad, cursor_p.y + text_sz.y + 1.0f);
            ImU32 pill_bg = ImGui::ColorConvertFloat4ToU32(
                ImVec4(mode_color.r, mode_color.g, mode_color.b, 0.12f));
            ImGui::GetWindowDrawList()->AddRectFilled(pill_min,
                                                      pill_max,
                                                      pill_bg,
                                                      ui::tokens::RADIUS_SM);

            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(mode_color.r, mode_color.g, mode_color.b, mode_color.a));
            ImGui::TextUnformatted(mode_label);
            ImGui::PopStyleColor();
        }

        // Separator — subtle dot
        ImGui::SameLine(0.0f, ui::tokens::SPACE_3);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(ui::theme().text_tertiary.r,
                                     ui::theme().text_tertiary.g,
                                     ui::theme().text_tertiary.b,
                                     0.5f));
        ImGui::TextUnformatted("\xC2\xB7");
        ImGui::PopStyleColor();

        // Zoom level
        ImGui::SameLine(0.0f, ui::tokens::SPACE_3);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(ui::theme().text_secondary.r,
                                     ui::theme().text_secondary.g,
                                     ui::theme().text_secondary.b,
                                     ui::theme().text_secondary.a));
        char zoom_buf[48];
        {
            double zoom_pct = static_cast<double>(zoom_level_) * 100.0;
            if (zoom_pct < 10000.0)
                std::snprintf(zoom_buf, sizeof(zoom_buf), "Zoom: %.0f%%", zoom_pct);
            else if (zoom_pct < 1e7)
                std::snprintf(zoom_buf, sizeof(zoom_buf), "Zoom: %.1fK%%", zoom_pct / 1e3);
            else if (zoom_pct < 1e10)
                std::snprintf(zoom_buf, sizeof(zoom_buf), "Zoom: %.1fM%%", zoom_pct / 1e6);
            else if (zoom_pct < 1e13)
                std::snprintf(zoom_buf, sizeof(zoom_buf), "Zoom: %.1fG%%", zoom_pct / 1e9);
            else
                std::snprintf(zoom_buf, sizeof(zoom_buf), "Zoom: %.2e%%", zoom_pct);
        }
        {
            ImVec2 chip_pos = ImGui::GetCursorScreenPos();
            ImVec2 chip_sz  = ImGui::CalcTextSize(zoom_buf);
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(chip_pos.x - 5.0f, chip_pos.y - 1.0f),
                ImVec2(chip_pos.x + chip_sz.x + 5.0f, chip_pos.y + chip_sz.y + 1.0f),
                IM_COL32(static_cast<uint8_t>(ui::theme().bg_tertiary.r * 255),
                         static_cast<uint8_t>(ui::theme().bg_tertiary.g * 255),
                         static_cast<uint8_t>(ui::theme().bg_tertiary.b * 255),
                         72),
                ui::tokens::RADIUS_SM);
            ImGui::TextUnformatted(zoom_buf);
        }
        ImGui::PopStyleColor();

        // Right side: performance info — anchor to right edge of window
        float perf_width = 160.0f;
        float abs_x      = ImGui::GetWindowWidth() - perf_width - ui::tokens::SPACE_3;
        ImGui::SameLine(abs_x > 0.0f ? abs_x : 0.0f);

        // FPS with color coding
        float fps_val   = io.Framerate;
        auto  fps_color = ui::theme().success;
        if (fps_val < 20.0f)
            fps_color = ui::theme().error;
        else if (fps_val < 45.0f)
            fps_color = ui::theme().warning;

        char fps_buf[32];
        std::snprintf(fps_buf, sizeof(fps_buf), "%d fps", static_cast<int>(fps_val));
        {
            ImVec2 chip_pos = ImGui::GetCursorScreenPos();
            ImVec2 chip_sz  = ImGui::CalcTextSize(fps_buf);
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(chip_pos.x - 5.0f, chip_pos.y - 1.0f),
                ImVec2(chip_pos.x + chip_sz.x + 5.0f, chip_pos.y + chip_sz.y + 1.0f),
                IM_COL32(static_cast<uint8_t>(fps_color.r * 255),
                         static_cast<uint8_t>(fps_color.g * 255),
                         static_cast<uint8_t>(fps_color.b * 255),
                         34),
                ui::tokens::RADIUS_SM);
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(fps_color.r, fps_color.g, fps_color.b, 1.0f));
            ImGui::TextUnformatted(fps_buf);
            ImGui::PopStyleColor();
        }

        // GPU time
        ImGui::SameLine(0.0f, ui::tokens::SPACE_3);
        char gpu_buf[32];
        std::snprintf(gpu_buf, sizeof(gpu_buf), "GPU: %.1fms", gpu_time_ms_);
        {
            ImVec2 chip_pos = ImGui::GetCursorScreenPos();
            ImVec2 chip_sz  = ImGui::CalcTextSize(gpu_buf);
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(chip_pos.x - 5.0f, chip_pos.y - 1.0f),
                ImVec2(chip_pos.x + chip_sz.x + 5.0f, chip_pos.y + chip_sz.y + 1.0f),
                IM_COL32(static_cast<uint8_t>(ui::theme().bg_tertiary.r * 255),
                         static_cast<uint8_t>(ui::theme().bg_tertiary.g * 255),
                         static_cast<uint8_t>(ui::theme().bg_tertiary.b * 255),
                         58),
                ui::tokens::RADIUS_SM);
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(ui::theme().text_tertiary.r,
                                         ui::theme().text_tertiary.g,
                                         ui::theme().text_tertiary.b,
                                         ui::theme().text_tertiary.a));
            ImGui::TextUnformatted(gpu_buf);
            ImGui::PopStyleColor();
        }

        ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

void ImGuiIntegration::draw_split_view_splitters()
{
    if (!dock_system_)
        return;

    auto*  draw_list = ImGui::GetForegroundDrawList();
    auto&  theme     = ui::theme();
    ImVec2 mouse     = ImGui::GetMousePos();

    // ── Non-split drag-to-split overlay ──────────────────────────────────
    // When NOT split and a tab is being dock-dragged, show edge zone
    // highlights to suggest splitting (like VSCode).
    if (!dock_system_->is_split() && dock_system_->is_dragging())
    {
        auto target = dock_system_->current_drop_target();
        // Only show edge zones (Left/Right/Top/Bottom), not Center
        if (target.zone != DropZone::None && target.zone != DropZone::Center)
        {
            Rect  hr               = target.highlight_rect;
            ImU32 highlight_color  = IM_COL32(static_cast<int>(theme.accent.r * 255),
                                             static_cast<int>(theme.accent.g * 255),
                                             static_cast<int>(theme.accent.b * 255),
                                             40);
            ImU32 highlight_border = IM_COL32(static_cast<int>(theme.accent.r * 255),
                                              static_cast<int>(theme.accent.g * 255),
                                              static_cast<int>(theme.accent.b * 255),
                                              160);

            draw_list->AddRectFilled(ImVec2(hr.x, hr.y),
                                     ImVec2(hr.x + hr.w, hr.y + hr.h),
                                     highlight_color,
                                     4.0f);
            draw_list->AddRect(ImVec2(hr.x, hr.y),
                               ImVec2(hr.x + hr.w, hr.y + hr.h),
                               highlight_border,
                               4.0f,
                               0,
                               2.0f);

            // Draw a label indicating the split direction
            const char* label = nullptr;
            switch (target.zone)
            {
                case DropZone::Left:
                    label = "Split Left";
                    break;
                case DropZone::Right:
                    label = "Split Right";
                    break;
                case DropZone::Top:
                    label = "Split Up";
                    break;
                case DropZone::Bottom:
                    label = "Split Down";
                    break;
                default:
                    break;
            }
            if (label)
            {
                ImVec2 lsz = ImGui::CalcTextSize(label);
                float  lx  = hr.x + (hr.w - lsz.x) * 0.5f;
                float  ly  = hr.y + (hr.h - lsz.y) * 0.5f;
                draw_list->AddText(ImVec2(lx, ly),
                                   IM_COL32(static_cast<int>(theme.accent.r * 255),
                                            static_cast<int>(theme.accent.g * 255),
                                            static_cast<int>(theme.accent.b * 255),
                                            200),
                                   label);
            }
        }
        return;   // No splitters to draw in non-split mode
    }

    if (!dock_system_->is_split())
        return;

    // Handle pane activation on mouse click in canvas area
    // (skip if mouse is over a pane tab header — that's handled by draw_pane_tab_headers)
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().WantCaptureMouse
        && !pane_tab_hovered_)
    {
        dock_system_->activate_pane_at(mouse.x, mouse.y);
    }

    // Handle splitter interaction
    if (dock_system_->is_over_splitter(mouse.x, mouse.y))
    {
        auto dir = dock_system_->splitter_direction_at(mouse.x, mouse.y);
        ImGui::SetMouseCursor(dir == SplitDirection::Horizontal ? ImGuiMouseCursor_ResizeEW
                                                                : ImGuiMouseCursor_ResizeNS);

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            dock_system_->begin_splitter_drag(mouse.x, mouse.y);
        }
    }

    if (dock_system_->is_dragging_splitter())
    {
        auto* sp = dock_system_->split_view().dragging_splitter();
        if (sp)
        {
            float pos = (sp->split_direction() == SplitDirection::Horizontal) ? mouse.x : mouse.y;
            dock_system_->update_splitter_drag(pos);
            ImGui::SetMouseCursor(sp->split_direction() == SplitDirection::Horizontal
                                      ? ImGuiMouseCursor_ResizeEW
                                      : ImGuiMouseCursor_ResizeNS);
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            dock_system_->end_splitter_drag();
        }
    }

    // Draw splitter handles for all internal nodes
    auto pane_infos = dock_system_->get_pane_infos();

    // Walk the split tree to find internal nodes and draw their splitters
    std::function<void(SplitPane*)> draw_splitters = [&](SplitPane* node)
    {
        if (!node || node->is_leaf())
            return;

        Rect sr          = node->splitter_rect();
        bool is_dragging = dock_system_->is_dragging_splitter()
                           && dock_system_->split_view().dragging_splitter() == node;

        // Splitter background
        ImU32 splitter_color;
        if (is_dragging)
        {
            splitter_color = IM_COL32(static_cast<int>(theme.accent.r * 255),
                                      static_cast<int>(theme.accent.g * 255),
                                      static_cast<int>(theme.accent.b * 255),
                                      200);
        }
        else
        {
            splitter_color = IM_COL32(static_cast<int>(theme.border_default.r * 255),
                                      static_cast<int>(theme.border_default.g * 255),
                                      static_cast<int>(theme.border_default.b * 255),
                                      120);
        }

        draw_list->AddRectFilled(ImVec2(sr.x, sr.y),
                                 ImVec2(sr.x + sr.w, sr.y + sr.h),
                                 splitter_color);

        // Draw a subtle grip indicator in the center of the splitter
        float cx         = sr.x + sr.w * 0.5f;
        float cy         = sr.y + sr.h * 0.5f;
        ImU32 grip_color = IM_COL32(static_cast<int>(theme.text_tertiary.r * 255),
                                    static_cast<int>(theme.text_tertiary.g * 255),
                                    static_cast<int>(theme.text_tertiary.b * 255),
                                    150);

        if (node->split_direction() == SplitDirection::Horizontal)
        {
            // Vertical splitter — draw horizontal grip dots
            for (int i = -2; i <= 2; ++i)
            {
                draw_list->AddCircleFilled(ImVec2(cx, cy + i * 6.0f), 1.5f, grip_color);
            }
        }
        else
        {
            // Horizontal splitter — draw vertical grip dots
            for (int i = -2; i <= 2; ++i)
            {
                draw_list->AddCircleFilled(ImVec2(cx + i * 6.0f, cy), 1.5f, grip_color);
            }
        }

        // Recurse into children
        draw_splitters(node->first());
        draw_splitters(node->second());
    };

    draw_splitters(dock_system_->split_view().root());

    // Draw active pane border highlight
    for (const auto& info : pane_infos)
    {
        if (info.is_active && pane_infos.size() > 1)
        {
            ImU32 border_color = IM_COL32(static_cast<int>(theme.accent.r * 255),
                                          static_cast<int>(theme.accent.g * 255),
                                          static_cast<int>(theme.accent.b * 255),
                                          180);
            draw_list->AddRect(ImVec2(info.bounds.x, info.bounds.y),
                               ImVec2(info.bounds.x + info.bounds.w, info.bounds.y + info.bounds.h),
                               border_color,
                               0.0f,
                               0,
                               2.0f);
        }
    }

    // Draw drop zone highlight during drag-to-dock
    if (dock_system_->is_dragging())
    {
        auto target = dock_system_->current_drop_target();
        if (target.zone != DropZone::None)
        {
            Rect  hr               = target.highlight_rect;
            ImU32 highlight_color  = IM_COL32(static_cast<int>(theme.accent.r * 255),
                                             static_cast<int>(theme.accent.g * 255),
                                             static_cast<int>(theme.accent.b * 255),
                                             60);
            ImU32 highlight_border = IM_COL32(static_cast<int>(theme.accent.r * 255),
                                              static_cast<int>(theme.accent.g * 255),
                                              static_cast<int>(theme.accent.b * 255),
                                              180);

            draw_list->AddRectFilled(ImVec2(hr.x, hr.y),
                                     ImVec2(hr.x + hr.w, hr.y + hr.h),
                                     highlight_color);
            draw_list->AddRect(ImVec2(hr.x, hr.y),
                               ImVec2(hr.x + hr.w, hr.y + hr.h),
                               highlight_border,
                               0.0f,
                               0,
                               2.0f);
        }
    }
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
