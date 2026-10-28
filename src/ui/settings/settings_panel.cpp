#include "settings_panel.hpp"

#ifdef SPECTRA_USE_IMGUI

    #include <algorithm>
    #include <cctype>
    #include <cfloat>
    #include <cmath>
    #include <string>
    #include <unordered_map>

    #include <imgui.h>

    #include "settings_layout.hpp"
    #include "settings_store.hpp"
    #include "ui/commands/shortcut_config.hpp"
    #include "ui/commands/shortcut_manager.hpp"
    #include "ui/imgui/widgets.hpp"
    #include "ui/theme/theme.hpp"

namespace spectra::ui::settings
{

namespace
{

std::string build_capture_shortcut(ImGuiKey key, bool ctrl, bool shift, bool alt)
{
    std::string s;
    if (ctrl)
        s += "Ctrl+";
    if (shift)
        s += "Shift+";
    if (alt)
        s += "Alt+";

    if (key >= ImGuiKey_A && key <= ImGuiKey_Z)
    {
        s += static_cast<char>('A' + (key - ImGuiKey_A));
        return s;
    }
    if (key >= ImGuiKey_0 && key <= ImGuiKey_9)
    {
        s += static_cast<char>('0' + (key - ImGuiKey_0));
        return s;
    }
    if (key >= ImGuiKey_F1 && key <= ImGuiKey_F12)
    {
        s += "F" + std::to_string(key - ImGuiKey_F1 + 1);
        return s;
    }
    if (key == ImGuiKey_Space)
    {
        s += "Space";
        return s;
    }
    if (key == ImGuiKey_Enter)
    {
        s += "Enter";
        return s;
    }
    if (key == ImGuiKey_Tab)
    {
        s += "Tab";
        return s;
    }
    if (key == ImGuiKey_Backspace)
    {
        s += "Backspace";
        return s;
    }
    if (key == ImGuiKey_Delete)
    {
        s += "Delete";
        return s;
    }
    if (key == ImGuiKey_Insert)
    {
        s += "Insert";
        return s;
    }
    if (key == ImGuiKey_LeftArrow)
    {
        s += "Left";
        return s;
    }
    if (key == ImGuiKey_RightArrow)
    {
        s += "Right";
        return s;
    }
    if (key == ImGuiKey_UpArrow)
    {
        s += "Up";
        return s;
    }
    if (key == ImGuiKey_DownArrow)
    {
        s += "Down";
        return s;
    }
    if (key == ImGuiKey_PageUp)
    {
        s += "PageUp";
        return s;
    }
    if (key == ImGuiKey_PageDown)
    {
        s += "PageDown";
        return s;
    }
    if (key == ImGuiKey_Home)
    {
        s += "Home";
        return s;
    }
    if (key == ImGuiKey_End)
    {
        s += "End";
        return s;
    }

    return {};
}

static const ImGuiKey k_capturable_keys[] = {
    ImGuiKey_A,       ImGuiKey_B,         ImGuiKey_C,         ImGuiKey_D,
    ImGuiKey_E,       ImGuiKey_F,         ImGuiKey_G,         ImGuiKey_H,
    ImGuiKey_I,       ImGuiKey_J,         ImGuiKey_K,         ImGuiKey_L,
    ImGuiKey_M,       ImGuiKey_N,         ImGuiKey_O,         ImGuiKey_P,
    ImGuiKey_Q,       ImGuiKey_R,         ImGuiKey_S,         ImGuiKey_T,
    ImGuiKey_U,       ImGuiKey_V,         ImGuiKey_W,         ImGuiKey_X,
    ImGuiKey_Y,       ImGuiKey_Z,         ImGuiKey_0,         ImGuiKey_1,
    ImGuiKey_2,       ImGuiKey_3,         ImGuiKey_4,         ImGuiKey_5,
    ImGuiKey_6,       ImGuiKey_7,         ImGuiKey_8,         ImGuiKey_9,
    ImGuiKey_F1,      ImGuiKey_F2,        ImGuiKey_F3,        ImGuiKey_F4,
    ImGuiKey_F5,      ImGuiKey_F6,        ImGuiKey_F7,        ImGuiKey_F8,
    ImGuiKey_F9,      ImGuiKey_F10,       ImGuiKey_F11,       ImGuiKey_F12,
    ImGuiKey_Space,   ImGuiKey_Enter,     ImGuiKey_Tab,       ImGuiKey_Backspace,
    ImGuiKey_Delete,  ImGuiKey_Insert,    ImGuiKey_LeftArrow, ImGuiKey_RightArrow,
    ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_PageUp,    ImGuiKey_PageDown,
    ImGuiKey_Home,    ImGuiKey_End,
};

}   // namespace

// ─── SettingsPanel::draw ─────────────────────────────────────────────────────

void SettingsPanel::draw()
{
    // Settings is a modal: ImGui dims everything behind it with a scrim
    // (ImGuiCol_ModalWindowDimBg) and keeps input away from the plot underneath.
    // visible_ stays the source of truth; the popup follows it.
    static constexpr const char* k_popup_id = "Settings##spectra";
    if (visible_ && !ImGui::IsPopupOpen(k_popup_id))
    {
        ImGui::OpenPopup(k_popup_id);
        recenter_frames_ = 3;   // hidden auto-fit frame + first visible frames
    }
    if (!ImGui::IsPopupOpen(k_popup_id))
        return;

    // Size to content, but never past the viewport work area (audit Q04): the
    // tab body scrolls inside a child region and the footer stays pinned.
    const ImGuiViewport*       vp    = ImGui::GetMainViewport();
    const ImGuiStyle&          style = ImGui::GetStyle();
    const float                scale = style.FontScaleMain * style.FontScaleDpi;
    const SettingsVec2         work_pos{vp->WorkPos.x, vp->WorkPos.y};
    const SettingsVec2         work_size{vp->WorkSize.x, vp->WorkSize.y};
    const SettingsWindowLimits lim = compute_settings_window_limits(work_size, scale);

    // Position before Begin (moving the window mid-frame misplaces its
    // contents).  Centre with a pivot, so it uses the size Begin computes,
    // while the window settles after opening or when the work area changes;
    // otherwise only pull last frame's rect back inside the work area.
    if (work_size.x != last_work_size_.x || work_size.y != last_work_size_.y)
        recenter_frames_ = std::max(recenter_frames_, 2);
    last_work_size_ = work_size;
    if (recenter_frames_ > 0)
    {
        --recenter_frames_;
        ImGui::SetNextWindowPos(vp->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    }
    else
    {
        const SettingsVec2 clamped =
            clamp_settings_window_pos(last_pos_, last_size_, work_pos, work_size, lim.margin);
        if (clamped.x != last_pos_.x || clamped.y != last_pos_.y)
            ImGui::SetNextWindowPos(ImVec2(clamped.x, clamped.y), ImGuiCond_Always);
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(lim.min_size.x, lim.min_size.y),
                                        ImVec2(lim.max_size.x, lim.max_size.y));

    // Opaque background: the glass panel transparency would let the plot show
    // through behind the dialog's text.  (Modals draw with ImGuiCol_PopupBg.)
    const auto& colors = ui::theme();
    ImGui::PushStyleColor(
        ImGuiCol_PopupBg,
        ImVec4(colors.bg_secondary.r, colors.bg_secondary.g, colors.bg_secondary.b, 1.0f));

    const bool open =
        ImGui::BeginPopupModal(k_popup_id,
                               &visible_,
                               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleColor();
    if (!open)
        return;
    if (!visible_)
    {
        // Hidden programmatically (command, automation) while open.
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    {
        const ImVec2 pos  = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        last_pos_         = {pos.x, pos.y};
        last_size_        = {size.x, size.y};
    }

    // A programmatic switch (select_tab()) shows its body this frame; the tab
    // bar itself may only report the new selection on the next frame.
    const bool forced_tab = pending_tab_ >= 0 && pending_tab_ < 3;
    if (forced_tab)
        active_tab_ = pending_tab_;

    if (ImGui::BeginTabBar("##settings_tabs"))
    {
        // Apply any programmatic tab switch (e.g. from automation select_tab()).
        // ImGuiTabItemFlags_SetSelected fires TabBarQueueFocus on the matching tab.
        const auto tab_flag = [&](int idx) -> ImGuiTabItemFlags
        {
            if (pending_tab_ == idx)
            {
                pending_tab_ = -1;
                return ImGuiTabItemFlags_SetSelected;
            }
            return ImGuiTabItemFlags_None;
        };

        // Tab items only pick the active tab; the body is drawn below in a
        // scrolling child so the footer can stay outside it.
        static constexpr const char* k_tab_names[] = {"Appearance", "Shortcuts", "UI Defaults"};
        for (int i = 0; i < 3; ++i)
        {
            if (ImGui::BeginTabItem(k_tab_names[i], nullptr, tab_flag(i)))
            {
                if (!forced_tab)
                    active_tab_ = i;
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    const bool  has_footer = active_tab_ == 0 || active_tab_ == 1;
    const float footer_h =
        has_footer ? style.ItemSpacing.y * 2.0f + 1.0f + ImGui::GetFrameHeightWithSpacing() : 0.0f;
    // Title bar + tab bar (cursor), footer, bottom padding.
    const float chrome_h = ImGui::GetCursorPosY() + footer_h + style.WindowPadding.y;
    body_max_h_ =
        compute_settings_body_max_height(lim.max_size.y, chrome_h, ImGui::GetFrameHeight());

    // The body is a fixed-height scrolling child sized to its natural content
    // height measured on the previous frame it was shown.  The first time a
    // tab is shown that height is unknown: its child starts at 1px and, once
    // the content has been measured, the parent's layout is extended to the
    // right height.  The window's first frame is hidden, so the auto-sizing
    // window is at its final size on the first visible frame.  Each tab has
    // its own child, so scrollbar state never carries over between tabs.
    const bool  known  = body_content_h_[active_tab_] >= 0.0f;
    const float used_h = known ? std::clamp(body_content_h_[active_tab_], 1.0f, body_max_h_) : 1.0f;
    // Explicit width: a fill-width child inside an auto-resizing window feeds
    // its own width back into the window and can latch at the maximum.
    const float body_w = std::max(lim.min_size.x - 2.0f * style.WindowPadding.x, 1.0f);
    static constexpr const char* k_body_ids[] = {"##settings_body_appearance",
                                                 "##settings_body_shortcuts",
                                                 "##settings_body_ui_defaults"};
    // An explicit content height makes ImGui decide the scrollbar against the
    // child's current size, not its 1px size from the measuring frame.
    if (known)
        ImGui::SetNextWindowContentSize(ImVec2(0.0f, body_content_h_[active_tab_]));
    if (ImGui::BeginChild(k_body_ids[active_tab_], ImVec2(body_w, used_h)))
    {
        body_origin_y_ = ImGui::GetCursorPosY();
        switch (active_tab_)
        {
            case 0:
                draw_appearance_tab();
                break;
            case 1:
                draw_shortcuts_tab();
                break;
            default:
                draw_ui_defaults_tab();
                break;
        }
        body_content_h_[active_tab_] =
            std::max(ImGui::GetCursorPosY() - body_origin_y_ - style.ItemSpacing.y, 0.0f);
    }
    ImGui::EndChild();

    // ImGui may enforce a minimum child height, so compare with the real one.
    const float actual_h = ImGui::GetItemRectSize().y;
    const float need_h   = std::clamp(body_content_h_[active_tab_], 1.0f, body_max_h_);
    if (need_h > actual_h)
    {
        if (has_footer)
        {
            // The footer's first item extends the layout from here.
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (need_h - actual_h));
        }
        else
        {
            // Zero-size item at the body's would-be bottom edge (the cursor
            // already includes the item spacing after the child).
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (need_h - actual_h)
                                 - style.ItemSpacing.y);
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
        }
    }

    if (has_footer)
    {
        ImGui::Separator();
        ImGui::Spacing();
        if (active_tab_ == 0)
            draw_appearance_footer();
        else
            draw_shortcuts_footer();
    }

    ImGui::EndPopup();
}

// ─── Appearance tab ───────────────────────────────────────────────────────────

void SettingsPanel::draw_appearance_tab()
{
    if (!store_ || !theme_mgr_)
        return;

    auto& d = store_->data_mut();

    static const char* k_themes[]   = {"night", "dark", "light", "high_contrast"};
    static const char* k_palettes[] = {"default",
                                       "colorblind",
                                       "tol_bright",
                                       "tol_muted",
                                       "ibm",
                                       "wong",
                                       "viridis",
                                       "monochrome"};

    ImGui::Spacing();

    const float label_w = std::max({ImGui::CalcTextSize("Theme").x,
                                    ImGui::CalcTextSize("Data Palette").x,
                                    ImGui::CalcTextSize("UI Scale").x})
                          + ImGui::GetStyle().ItemInnerSpacing.x * 2.0f;

    // Contract: the combos show and select the ACTIVE theme / palette.  A
    // selection here also saves it as the startup default; commands and
    // undo/redo change only the active one, so a hint reveals the difference.
    const AppearanceState state = appearance_state(*theme_mgr_, d);

    // Secondary "Startup default: X  [Make default]" row under a combo.
    auto startup_hint = [&](const char* id, const std::string& default_label) -> bool
    {
        const auto& colors = theme_mgr_->colors();
        ImGui::PushID(id);
        ImGui::SetCursorPosX(label_w);   // align with the combo above
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(colors.text_secondary.r,
                                     colors.text_secondary.g,
                                     colors.text_secondary.b,
                                     colors.text_secondary.a));
        ImGui::Text("Startup default: %s", default_label.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        const bool clicked = ImGui::SmallButton("Make default");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Start Spectra with the current selection");
        ImGui::PopID();
        return clicked;
    };

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Theme");
    ImGui::SameLine(label_w);
    ImGui::SetNextItemWidth(200.0f);
    const std::string current_theme_label = theme_mgr_->display_name(state.active_theme);
    if (ImGui::BeginCombo("##theme", current_theme_label.c_str()))
    {
        for (const char* t : k_themes)
        {
            bool              selected = (state.active_theme == t);
            const std::string label    = theme_mgr_->display_name(t) + "##" + t;
            if (ImGui::Selectable(label.c_str(), selected) && select_theme(*theme_mgr_, *store_, t))
            {
                theme_mgr_->reset_glass_defaults();
                theme_mgr_->save_current_as_default();
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (!state.theme_matches_startup()
        && startup_hint("theme_default", theme_mgr_->display_name(state.startup_theme)))
    {
        make_active_theme_default(*theme_mgr_, *store_);
    }

    ImGui::Spacing();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Data Palette");
    ImGui::SameLine(label_w);
    ImGui::SetNextItemWidth(200.0f);
    // Swatches show each palette as the plot draws it on the current canvas
    // (displayed_data_palette: canvas-adjusted, ink slots in the theme
    // foreground), not the raw palette definition.
    auto draw_swatches = [&](const ui::DataPalette& pal, float size)
    {
        ImDrawList*  dl    = ImGui::GetWindowDrawList();
        const ImVec2 at    = ImGui::GetCursorScreenPos();
        const size_t count = std::min<size_t>(pal.colors.size(), 10);
        const float  gap   = std::max(1.0f, size * 0.2f);
        for (size_t i = 0; i < count; ++i)
        {
            const auto&  pc = pal.colors[i];
            const ImVec2 a(at.x + static_cast<float>(i) * (size + gap), at.y);
            dl->AddRectFilled(a,
                              ImVec2(a.x + size, a.y + size),
                              ImGui::ColorConvertFloat4ToU32(ImVec4(pc.r, pc.g, pc.b, 1.0f)),
                              2.0f);
        }
        ImGui::Dummy(ImVec2(count > 0 ? count * (size + gap) - gap : 0.0f, size));
    };

    if (ImGui::BeginCombo("##palette", state.active_data_palette.c_str()))
    {
        const float sw = ImGui::GetTextLineHeight() * 0.8f;
        for (const char* p : k_palettes)
        {
            const bool cvd_safe = theme_mgr_->get_data_palette(p).colorblind_safe;
            const bool selected = (state.active_data_palette == p);
            // Append CVD badge so colorblind users can identify safe choices without
            // relying on prior knowledge of palette names (WCAG 1.3.3 / 1.4.1).
            std::string label = std::string(p) + (cvd_safe ? "  (CVD-safe)" : "  (not CVD-safe)");
            if (ImGui::Selectable(label.c_str(), selected))
                select_data_palette(*theme_mgr_, *store_, p);
            if (selected)
                ImGui::SetItemDefaultFocus();
            ImGui::Indent(ImGui::GetStyle().ItemSpacing.x);
            draw_swatches(theme_mgr_->displayed_data_palette(p), sw);
            ImGui::Unindent(ImGui::GetStyle().ItemSpacing.x);
        }
        ImGui::EndCombo();
    }
    ImGui::SetCursorPosX(label_w);   // align with the combo above
    draw_swatches(theme_mgr_->displayed_data_palette(), ImGui::GetTextLineHeight());
    if (!state.data_palette_matches_startup()
        && startup_hint("palette_default", state.startup_data_palette))
    {
        make_active_data_palette_default(*theme_mgr_, *store_);
    }

    ImGui::Spacing();

    // App UI scale: applied when chosen (not while dragging) and saved.
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("UI Scale");
    ImGui::SameLine(label_w);
    ImGui::SetNextItemWidth(200.0f);
    {
        static constexpr int k_scales[] = {75, 90, 100, 110, 125, 150, 175, 200};
        const int         current = static_cast<int>(std::lround(theme_mgr_->ui_scale() * 100.0f));
        const std::string preview = std::to_string(current) + "%";
        if (ImGui::BeginCombo("##ui_scale", preview.c_str()))
        {
            for (int pct : k_scales)
            {
                const std::string item     = std::to_string(pct) + "%";
                const bool        selected = pct == current;
                if (ImGui::Selectable(item.c_str(), selected))
                {
                    const float scale = static_cast<float>(pct) / 100.0f;
                    theme_mgr_->set_ui_scale(scale);
                    d.ui_scale = theme_mgr_->ui_scale();
                    store_->notify_change();
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextUnformatted("Glass / Transparency");
    ImGui::Spacing();

    ui::ThemeGlassSettings glass = theme_mgr_->glass();
    bool                   glass_changed = false;

    auto glass_slider = [&](const char* label, float* value)
    {
        ImGui::PushID(label);
        ImGui::TextUnformatted(label);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderFloat("##v", value, 0.0f, 1.0f, "%.2f"))
            glass_changed = true;
        ImGui::PopID();
    };

    glass_slider("Master Glass Intensity", &glass.master_intensity);
    glass_slider("Panel Transparency", &glass.panel_alpha);
    glass_slider("Plot Background Transparency", &glass.plot_alpha);
    glass_slider("Toolbar Transparency", &glass.toolbar_alpha);
    glass_slider("Glow Strength", &glass.glow_strength);

    if (widgets::checkbox_field("Blur Enabled (experimental)", glass.blur_enabled))
        glass_changed = true;

    if (glass_changed)
    {
        theme_mgr_->set_glass_settings(glass, true);
        theme_mgr_->save_current_as_default();
        store_->notify_change();
    }

    if (ImGui::Button("Reset Theme Defaults"))
    {
        theme_mgr_->reset_glass_defaults();
        theme_mgr_->save_current_as_default();
        store_->notify_change();
    }
}

void SettingsPanel::draw_appearance_footer()
{
    if (!store_ || !theme_mgr_)
        return;

    auto& d = store_->data_mut();
    if (ImGui::Button("Reset to Defaults"))
    {
        d.default_theme        = "night";
        d.default_data_palette = "default";
        theme_mgr_->set_theme("night");
        theme_mgr_->set_data_palette("default");
        d.ui_scale = 1.0f;
        theme_mgr_->set_ui_scale(1.0f);
        theme_mgr_->reset_glass_defaults();
        theme_mgr_->save_current_as_default();
        store_->notify_change();
    }
}

// ─── Shortcuts tab ────────────────────────────────────────────────────────────

void SettingsPanel::draw_shortcuts_tab()
{
    if (!shortcut_cfg_)
        return;

    auto* mgr = shortcut_cfg_->shortcut_manager();
    if (!mgr)
        return;

    ImGui::Spacing();
    ImGui::SetNextItemWidth(300.0f);
    ImGui::InputTextWithHint("Filter##sc_filter",
                             "type to filter by name...",
                             filter_buf_,
                             sizeof(filter_buf_));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("Filter shortcuts by command name.\n"
                          "Tab moves between rows; use the filter to jump to a command.");
    }
    ImGui::Spacing();

    const auto& colors   = ui::theme();
    auto        bindings = mgr->all_bindings();

    // Count shortcut strings to detect conflicts.
    std::unordered_map<std::string, int> shortcut_count;
    for (const auto& b : bindings)
    {
        if (b.shortcut.valid())
            shortcut_count[b.shortcut.to_string()]++;
    }

    std::string filter(filter_buf_);
    for (char& c : filter)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    constexpr ImGuiTableFlags kTableFlags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg;

    // Fill the body's height budget; the table scrolls its own rows, so the
    // pinned footer below the body stays reachable.
    const float table_h = std::max(body_max_h_ - (ImGui::GetCursorPosY() - body_origin_y_),
                                   ImGui::GetFrameHeightWithSpacing() * 4.0f);
    ImVec2      table_size(0.0f, table_h);
    if (ImGui::BeginTable("##shortcuts", 3, kTableFlags, table_size))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Command", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, 160.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        ImGui::TableHeadersRow();

        auto& io = ImGui::GetIO();

        for (const auto& b : bindings)
        {
            if (!filter.empty())
            {
                std::string lower_id = b.command_id;
                for (char& c : lower_id)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (lower_id.find(filter) == std::string::npos)
                    continue;
            }

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(b.command_id.c_str());

            ImGui::TableSetColumnIndex(1);

            bool is_capturing = capturing_shortcut_ && capturing_command_id_ == b.command_id;
            if (is_capturing)
            {
                ImGui::TextColored(
                    ImVec4(colors.warning.r, colors.warning.g, colors.warning.b, 1.0f),
                    "Press key...  (Esc = cancel)");

                // Detect any non-modifier key press.
                for (ImGuiKey key : k_capturable_keys)
                {
                    if (!ImGui::IsKeyPressed(key, false))
                        continue;

                    std::string new_sc =
                        build_capture_shortcut(key, io.KeyCtrl, io.KeyShift, io.KeyAlt);
                    if (!new_sc.empty())
                    {
                        shortcut_cfg_->set_override(b.command_id, new_sc);
                        shortcut_cfg_->apply_overrides();
                        shortcut_cfg_->save(ShortcutConfig::default_path());
                    }
                    capturing_shortcut_ = false;
                    break;
                }

                if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                    capturing_shortcut_ = false;
            }
            else
            {
                std::string sc_str   = b.shortcut.valid() ? b.shortcut.to_string() : "(none)";
                bool        conflict = b.shortcut.valid()
                                       && shortcut_count.contains(b.shortcut.to_string())
                                       && shortcut_count.at(b.shortcut.to_string()) > 1;

                if (conflict)
                {
                    // Use theme error token (not hardcoded) and prepend "[!]" so
                    // the conflict is communicated by shape as well as color (WCAG 1.4.1).
                    ImGui::PushStyleColor(
                        ImGuiCol_Text,
                        ImVec4(colors.error.r, colors.error.g, colors.error.b, 1.0f));
                    sc_str.insert(0, "[!] ");
                }
                ImGui::TextUnformatted(sc_str.c_str());
                if (conflict)
                    ImGui::PopStyleColor();
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::PushID(b.command_id.c_str());

            if (!capturing_shortcut_)
            {
                if (ImGui::SmallButton("Edit"))
                {
                    capturing_shortcut_   = true;
                    capturing_command_id_ = b.command_id;
                }
                if (shortcut_cfg_->has_override(b.command_id))
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X"))
                    {
                        shortcut_cfg_->remove_override(b.command_id);
                        shortcut_cfg_->apply_overrides();
                        shortcut_cfg_->save(ShortcutConfig::default_path());
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Remove custom shortcut override");
                }
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }
}

void SettingsPanel::draw_shortcuts_footer()
{
    if (!shortcut_cfg_)
        return;

    if (ImGui::Button("Reset All Shortcuts"))
    {
        shortcut_cfg_->reset_all();
        shortcut_cfg_->apply_overrides();
        shortcut_cfg_->save(ShortcutConfig::default_path());
    }
}

// ─── UI Defaults tab ──────────────────────────────────────────────────────────

void SettingsPanel::draw_ui_defaults_tab()
{
    if (!store_)
        return;

    auto& d = store_->data_mut();

    ImGui::Spacing();

    if (widgets::checkbox_field("Show Inspector by default", d.inspector_visible))
        store_->notify_change();

    ImGui::Spacing();

    if (widgets::checkbox_field("Show Navigation Rail by default", d.nav_rail_visible))
        store_->notify_change();

    ImGui::Spacing();

    if (widgets::checkbox_field("Show Timeline by default", d.timeline_visible))
        store_->notify_change();
}

}   // namespace spectra::ui::settings

#endif   // SPECTRA_USE_IMGUI
