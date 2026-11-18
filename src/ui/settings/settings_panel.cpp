#include "settings_panel.hpp"

#ifdef SPECTRA_USE_IMGUI

    #include <algorithm>
    #include <cctype>
    #include <cfloat>
    #include <cmath>
    #include <string>
    #include <unordered_map>

    #include <imgui.h>

    #include "../../../third_party/tinyfiledialogs.h"

    #include "core/config_paths.hpp"
    #include "settings_layout.hpp"
    #include "settings_search.hpp"
    #include "settings_store.hpp"
    #include "ui/dialog_env_guard.hpp"
    #include "ui/export_dialog.hpp"
    #include "ui/native_dialog_policy.hpp"
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

    // Search box above the tabs: with a query, the matching sections of every
    // tab are listed instead of the tabs.  Tiny work areas skip it so the body
    // keeps its room.
    if (lim.max_size.y < k_settings_search_min_height * scale)
        search_.clear();
    else
    {
        char buf[128] = {};
        std::copy_n(search_.begin(), std::min(search_.size(), sizeof(buf) - 1), buf);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputTextWithHint("##settings_search",
                                     "Search settings (theme, palette, shortcut...)",
                                     buf,
                                     sizeof(buf)))
        {
            search_ = buf;
        }
        ImGui::Spacing();
    }
    const bool searching = settings_query_active(search_);

    // A programmatic switch (select_tab()) shows its body this frame; the tab
    // bar itself may only report the new selection on the next frame.
    const bool forced_tab = pending_tab_ >= 0 && pending_tab_ < 3;
    if (forced_tab)
    {
        active_tab_ = pending_tab_;
        search_.clear();
    }

    if (!searching && ImGui::BeginTabBar("##settings_tabs"))
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

    // Body slot: the active tab, or the search results (slot 3).
    const int   body_slot  = searching ? 3 : active_tab_;
    const bool  has_footer = !searching && (active_tab_ == 0 || active_tab_ == 1);
    const float footer_h =
        has_footer ? style.ItemSpacing.y * 2.0f + 1.0f + ImGui::GetFrameHeightWithSpacing() : 0.0f;
    // Title bar + search + tab bar (cursor), footer, bottom padding.
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
    const bool  known  = body_content_h_[body_slot] >= 0.0f;
    const float used_h = known ? std::clamp(body_content_h_[body_slot], 1.0f, body_max_h_) : 1.0f;
    // Explicit width: a fill-width child inside an auto-resizing window feeds
    // its own width back into the window and can latch at the maximum.
    const float body_w = std::max(lim.min_size.x - 2.0f * style.WindowPadding.x, 1.0f);
    static constexpr const char* k_body_ids[] = {"##settings_body_appearance",
                                                 "##settings_body_shortcuts",
                                                 "##settings_body_ui_defaults",
                                                 "##settings_body_search"};
    // An explicit content height makes ImGui decide the scrollbar against the
    // child's current size, not its 1px size from the measuring frame.
    if (known)
        ImGui::SetNextWindowContentSize(ImVec2(0.0f, body_content_h_[body_slot]));
    if (ImGui::BeginChild(k_body_ids[body_slot], ImVec2(body_w, used_h)))
    {
        body_origin_y_ = ImGui::GetCursorPosY();
        switch (body_slot)
        {
            case 0:
                draw_appearance_tab();
                break;
            case 1:
                draw_shortcuts_tab();
                break;
            case 2:
                draw_ui_defaults_tab();
                break;
            default:
                draw_search_results();
                break;
        }
        body_content_h_[body_slot] =
            std::max(ImGui::GetCursorPosY() - body_origin_y_ - style.ItemSpacing.y, 0.0f);
    }
    ImGui::EndChild();

    // ImGui may enforce a minimum child height, so compare with the real one.
    const float actual_h = ImGui::GetItemRectSize().y;
    const float need_h   = std::clamp(body_content_h_[body_slot], 1.0f, body_max_h_);
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

// ─── Sections and search ──────────────────────────────────────────────────────

const SettingsPanel::Section* SettingsPanel::sections(size_t& count)
{
    static const Section k_sections[] = {
        {0,
         "Theme",
         "appearance dark light night high contrast colour color mode",
         &SettingsPanel::draw_theme_section},
        {0,
         "Data Palette",
         "palette colors colours series cycle colorblind cvd safe",
         &SettingsPanel::draw_palette_section},
        {0,
         "Color Vision Preview",
         "colour vision deficiency cvd colorblind color blind protanopia deuteranopia tritanopia "
         "achromatopsia simulate accessibility",
         &SettingsPanel::draw_cvd_section},
        {0,
         "UI Scale",
         "zoom size dpi font text bigger smaller",
         &SettingsPanel::draw_scale_section},
        {0,
         "Theme & Palette Files",
         "import export user custom theme palette file folder json load reload",
         &SettingsPanel::draw_files_section},
        {0,
         "Glass / Transparency",
         "glass transparency opacity alpha blur glow panel toolbar plot background",
         &SettingsPanel::draw_glass_section},
        {1,
         "Keyboard Shortcuts",
         "shortcut key keys binding keyboard hotkey",
         &SettingsPanel::draw_shortcuts_tab},
        {2,
         "Startup Panels",
         "inspector navigation rail timeline default startup show panel",
         &SettingsPanel::draw_ui_defaults_tab},
    };
    count = sizeof(k_sections) / sizeof(k_sections[0]);
    return k_sections;
}

void SettingsPanel::draw_section_heading(const char* title)
{
    // SemiBold at FONT_XL: headings read above the 16px body labels.
    const auto& c = ui::theme();
    if (font_section_)
        ImGui::PushFont(font_section_);
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImVec4(c.text_primary.r, c.text_primary.g, c.text_primary.b, 1.0f));
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    if (font_section_)
        ImGui::PopFont();
    ImGui::Spacing();
}

bool SettingsPanel::shortcuts_match(const std::string& lowered_query) const
{
    if (!shortcut_cfg_ || !shortcut_cfg_->shortcut_manager())
        return false;
    for (const auto& b : shortcut_cfg_->shortcut_manager()->all_bindings())
        if (settings_lower(b.command_id).find(lowered_query) != std::string::npos)
            return true;
    return false;
}

void SettingsPanel::draw_search_results()
{
    static constexpr const char* k_tab_names[] = {"Appearance", "Shortcuts", "UI Defaults"};
    const auto&                  c             = ui::theme();
    const std::string            lowered       = settings_lower(search_);

    size_t         count = 0;
    const Section* all   = sections(count);
    bool           any   = false;
    for (size_t i = 0; i < count; ++i)
    {
        const Section& sec = all[i];
        // The shortcuts table also matches by command id (filtered below).
        const bool match = settings_query_matches(search_, sec.title, sec.keywords)
                           || (sec.tab == 1 && shortcuts_match(lowered));
        if (!match)
            continue;
        if (any)
        {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }
        any = true;
        ImGui::PushID(static_cast<int>(i));
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImVec4(c.text_tertiary.r, c.text_tertiary.g, c.text_tertiary.b, 1.0f));
        ImGui::Text("%s", k_tab_names[sec.tab]);
        ImGui::PopStyleColor();
        // Sections that start with their own heading draw it themselves.
        if (sec.tab != 0)
            draw_section_heading(sec.title);
        (this->*sec.draw)();
        ImGui::PopID();
    }
    if (!any)
    {
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, 1.0f));
        ImGui::Text("No settings match \"%s\".", search_.c_str());
        ImGui::PopStyleColor();
    }
}

// ─── Appearance tab ───────────────────────────────────────────────────────────

void SettingsPanel::draw_appearance_tab()
{
    if (!store_ || !theme_mgr_)
        return;
    size_t         count = 0;
    const Section* all   = sections(count);
    bool           first = true;
    for (size_t i = 0; i < count; ++i)
    {
        if (all[i].tab != 0)
            continue;
        if (!first)
        {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }
        first = false;
        (this->*all[i].draw)();
    }
}

namespace
{

// Palette swatches as a strip of rounded squares at `at` (draw list only).
void draw_swatches_at(ImDrawList*                   dl,
                      ImVec2                        at,
                      const std::vector<ui::Color>& colors,
                      float                         size,
                      size_t                        max_count = 10)
{
    const size_t count = std::min(colors.size(), max_count);
    const float  gap   = std::max(1.0f, size * 0.2f);
    for (size_t i = 0; i < count; ++i)
    {
        const auto&  pc = colors[i];
        const ImVec2 a(at.x + static_cast<float>(i) * (size + gap), at.y);
        dl->AddRectFilled(a,
                          ImVec2(a.x + size, a.y + size),
                          ImGui::ColorConvertFloat4ToU32(ImVec4(pc.r, pc.g, pc.b, 1.0f)),
                          2.0f);
    }
}

ImU32 to_u32(const ui::Color& c, float alpha_mul = 1.0f)
{
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, c.a * alpha_mul));
}

// Miniature of a theme: chrome rail, plot canvas with grid, axes and three
// curves in the theme's own data palette.  Drawn from the theme's tokens.
void draw_theme_preview(ImDrawList* dl, const ui::Theme& t, ImVec2 p0, ImVec2 p1)
{
    const ui::ThemeColors& c = t.colors;
    const float            r = ui::tokens::RADIUS_SM;
    dl->AddRectFilled(p0, p1, to_u32(c.bg_primary.with_alpha(1.0f)), r);
    const float w = p1.x - p0.x;
    const float h = p1.y - p0.y;

    // Rail with an active (accent) item.
    const float rail_w = w * 0.12f;
    dl->AddRectFilled(p0,
                      ImVec2(p0.x + rail_w, p1.y),
                      to_u32(c.bg_secondary.with_alpha(1.0f)),
                      r,
                      ImDrawFlags_RoundCornersLeft);
    const float dot = rail_w * 0.45f;
    for (int i = 0; i < 3; ++i)
    {
        const ImVec2 d(p0.x + (rail_w - dot) * 0.5f, p0.y + h * 0.14f + i * dot * 1.9f);
        dl->AddRectFilled(
            d,
            ImVec2(d.x + dot, d.y + dot),
            i == 0 ? to_u32(c.accent.with_alpha(1.0f)) : to_u32(c.text_tertiary.with_alpha(0.6f)),
            1.5f);
    }

    // Canvas.
    const ImVec2 c0(p0.x + rail_w + w * 0.05f, p0.y + h * 0.10f);
    const ImVec2 c1(p1.x - w * 0.05f, p1.y - h * 0.10f);
    dl->AddRectFilled(c0, c1, to_u32(c.bg_canvas.with_alpha(1.0f)), 2.0f);
    const float cw = c1.x - c0.x;
    const float ch = c1.y - c0.y;
    for (int i = 1; i < 4; ++i)
    {
        const float y = c0.y + ch * static_cast<float>(i) / 4.0f;
        dl->AddLine(ImVec2(c0.x, y), ImVec2(c1.x, y), to_u32(c.grid_major), 1.0f);
    }
    dl->AddLine(ImVec2(c0.x + 1.0f, c0.y), ImVec2(c0.x + 1.0f, c1.y), to_u32(c.axis_line), 1.0f);
    dl->AddLine(ImVec2(c0.x, c1.y - 1.0f), ImVec2(c1.x, c1.y - 1.0f), to_u32(c.axis_line), 1.0f);

    // Three curves in the theme's palette.
    constexpr int N = 24;
    for (int k = 0; k < 3; ++k)
    {
        const ui::Color col =
            t.data_palette.colors.empty() ? c.accent : t.data_palette[static_cast<size_t>(k)];
        ImVec2 pts[N];
        for (int i = 0; i < N; ++i)
        {
            const float u = static_cast<float>(i) / static_cast<float>(N - 1);
            const float v =
                0.5f + 0.32f * std::sin(u * 6.2832f * 1.1f + k * 1.9f) * (1.0f - 0.2f * k);
            pts[i] = ImVec2(c0.x + 2.0f + u * (cw - 4.0f), c0.y + ch * (1.0f - v));
        }
        dl->AddPolyline(pts, N, to_u32(col.with_alpha(1.0f)), ImDrawFlags_None, 1.6f);
    }
    dl->AddRect(p0, p1, to_u32(c.border_default), r);
}

}   // namespace

void SettingsPanel::draw_theme_section()
{
    if (!store_ || !theme_mgr_)
        return;
    const auto&           colors = ui::theme();
    const AppearanceState state  = appearance_state(*theme_mgr_, store_->data_mut());

    draw_section_heading("Theme");

    // Card grid: one live preview per registered theme (built-ins first,
    // then user themes), so themes are chosen by look, not by name.
    const std::vector<std::string> themes = theme_mgr_->available_themes();
    const ImGuiStyle&              style  = ImGui::GetStyle();
    const float                    avail  = ImGui::GetContentRegionAvail().x;
    const int                      columns =
        std::clamp(static_cast<int>(avail / (118.0f * ImGui::GetFontSize() / 16.0f)), 2, 4);
    const float gap = style.ItemSpacing.x;
    const float card_w =
        (avail - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns);
    const float prev_h = card_w * 0.58f;
    const float card_h = prev_h + ImGui::GetTextLineHeightWithSpacing() + ui::tokens::SPACE_2;

    for (size_t i = 0; i < themes.size(); ++i)
    {
        const std::string& id = themes[i];
        const ui::Theme*   t  = theme_mgr_->find_theme(id);
        if (!t)
            continue;
        if (i % static_cast<size_t>(columns) != 0)
            ImGui::SameLine(0.0f, gap);

        ImGui::PushID(id.c_str());
        const ImVec2 p0      = ImGui::GetCursorScreenPos();
        const bool   clicked = ImGui::InvisibleButton("##theme_card", ImVec2(card_w, card_h));
        const bool   hovered = ImGui::IsItemHovered();
        const bool   active  = state.active_theme == id;
        if (clicked && select_theme(*theme_mgr_, *store_, id))
        {
            theme_mgr_->reset_glass_defaults();
            remember_glass(*theme_mgr_, *store_);
        }
        if (hovered)
        {
            ImGui::SetTooltip("%s theme%s",
                              theme_mgr_->display_name(id).c_str(),
                              theme_mgr_->is_user_theme(id) ? " (user file)" : "");
        }

        ImDrawList*  dl = ImGui::GetWindowDrawList();
        const ImVec2 p1(p0.x + card_w, p0.y + card_h);
        dl->AddRectFilled(p0,
                          p1,
                          active    ? to_u32(colors.accent_muted)
                          : hovered ? to_u32(colors.hover_highlight)
                                    : to_u32(colors.bg_tertiary, 0.5f),
                          ui::tokens::RADIUS_MD);
        const float pad = ui::tokens::SPACE_2;
        draw_theme_preview(dl,
                           *t,
                           ImVec2(p0.x + pad, p0.y + pad),
                           ImVec2(p1.x - pad, p0.y + pad + prev_h - pad));
        std::string label = theme_mgr_->display_name(id);
        if (theme_mgr_->is_user_theme(id))
            label += "  \xC2\xB7 user";
        const ImVec2 text_pos(p0.x + pad, p0.y + prev_h + ui::tokens::SPACE_1);
        // Selection is the accent border: accent text fails 4.5:1 on Light.
        dl->AddText(text_pos, to_u32(colors.text_primary), label.c_str());
        if (active || hovered)
        {
            dl->AddRect(p0,
                        p1,
                        active ? to_u32(colors.accent) : to_u32(colors.border_strong),
                        ui::tokens::RADIUS_MD,
                        ImDrawFlags_None,
                        active ? 2.0f : 1.0f);
        }
        ImGui::PopID();
    }

    if (!state.theme_matches_startup())
    {
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(colors.text_secondary.r,
                                     colors.text_secondary.g,
                                     colors.text_secondary.b,
                                     1.0f));
        ImGui::Text("Startup default: %s", theme_mgr_->display_name(state.startup_theme).c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton("Make default##theme"))
            make_active_theme_default(*theme_mgr_, *store_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Start Spectra with the current theme");
    }
}

void SettingsPanel::draw_palette_section()
{
    if (!store_ || !theme_mgr_)
        return;
    const auto&           colors = ui::theme();
    const AppearanceState state  = appearance_state(*theme_mgr_, store_->data_mut());

    draw_section_heading("Data Palette");

    // One row per palette: swatches as the plot draws them on the current
    // canvas (displayed_data_palette), name, CVD badge.
    const float row_h   = ImGui::GetTextLineHeight() + 2.0f * ui::tokens::SPACE_2;
    const float sw      = ImGui::GetTextLineHeight() * 0.62f;
    const float strip_w = 10.0f * sw + 9.0f * std::max(1.0f, sw * 0.2f);
    for (const std::string& id : theme_mgr_->ordered_data_palettes())
    {
        ImGui::PushID(id.c_str());
        const bool   selected = state.active_data_palette == id;
        const ImVec2 p0       = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##palette_row",
                              selected,
                              ImGuiSelectableFlags_None,
                              ImVec2(0.0f, row_h)))
            select_data_palette(*theme_mgr_, *store_, id);
        const ui::DataPalette& raw =
            id == "default" ? theme_mgr_->current().data_palette : theme_mgr_->get_data_palette(id);
        if (ImGui::IsItemHovered() && !raw.description.empty())
            ImGui::SetTooltip("%s", raw.description.c_str());

        ImDrawList* dl = ImGui::GetWindowDrawList();
        draw_swatches_at(dl,
                         ImVec2(p0.x + ui::tokens::SPACE_2, p0.y + (row_h - sw) * 0.5f),
                         theme_mgr_->displayed_data_palette(id).colors,
                         sw);
        std::string label = theme_mgr_->data_palette_label(id);
        if (theme_mgr_->is_user_data_palette(id))
            label += "  \xC2\xB7 user";
        const float text_y = p0.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f;
        dl->AddText(ImVec2(p0.x + ui::tokens::SPACE_2 + strip_w + ui::tokens::SPACE_4, text_y),
                    to_u32(colors.text_primary),
                    label.c_str());
        // Shape + text, not colour alone (WCAG 1.4.1).
        const bool   cvd_safe = raw.colorblind_safe;
        const char*  badge    = cvd_safe ? "CVD-safe" : "";
        const ImVec2 bsz      = ImGui::CalcTextSize(badge);
        const float  right    = p0.x + ImGui::GetContentRegionAvail().x;
        if (cvd_safe)
            dl->AddText(ImVec2(right - bsz.x - ui::tokens::SPACE_2, text_y),
                        to_u32(colors.success),
                        badge);
        ImGui::PopID();
    }

    if (!state.data_palette_matches_startup())
    {
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(colors.text_secondary.r,
                                     colors.text_secondary.g,
                                     colors.text_secondary.b,
                                     1.0f));
        ImGui::Text("Startup default: %s",
                    theme_mgr_->data_palette_label(state.startup_data_palette).c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton("Make default##palette"))
            make_active_data_palette_default(*theme_mgr_, *store_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Start Spectra with the current palette");
    }
}

void SettingsPanel::draw_cvd_section()
{
    if (!theme_mgr_)
        return;
    const auto& colors = ui::theme();
    draw_section_heading("Color Vision Preview");

    static constexpr ui::CVDType k_types[] = {ui::CVDType::None,
                                              ui::CVDType::Protanopia,
                                              ui::CVDType::Deuteranopia,
                                              ui::CVDType::Tritanopia,
                                              ui::CVDType::Achromatopsia};
    const ui::CVDType            current   = theme_mgr_->cvd_preview();
    ImGui::SetNextItemWidth(260.0f * ImGui::GetFontSize() / 16.0f);
    if (ImGui::BeginCombo(
            "##cvd_preview",
            current == ui::CVDType::None ? "Off (normal vision)" : ui::cvd_type_label(current)))
    {
        for (ui::CVDType t : k_types)
        {
            const bool sel = t == current;
            if (ImGui::Selectable(
                    t == ui::CVDType::None ? "Off (normal vision)" : ui::cvd_type_label(t),
                    sel))
                theme_mgr_->set_cvd_preview(t);
            if (sel)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(colors.text_secondary.r, colors.text_secondary.g, colors.text_secondary.b, 1.0f));
    ImGui::TextWrapped("Draws the plots as seen with that deficiency. View only: exports and "
                       "saved files keep the real colours.");
    ImGui::PopStyleColor();

    // The current palette as each deficiency sees it.
    const ui::DataPalette shown = theme_mgr_->displayed_data_palette();
    const float           sw    = ImGui::GetTextLineHeight() * 0.8f;
    const float           lab_w = ImGui::CalcTextSize("Deuteranopia").x + ui::tokens::SPACE_4;
    for (ui::CVDType t :
         {ui::CVDType::Protanopia, ui::CVDType::Deuteranopia, ui::CVDType::Tritanopia})
    {
        std::vector<ui::Color> sim;
        sim.reserve(shown.colors.size());
        for (const ui::Color& c : shown.colors)
            sim.push_back(ui::simulate_cvd(c, t));
        const char* name = ui::cvd_type_id(t);
        std::string cap(name);
        if (!cap.empty())
            cap[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(cap[0])));
        const ImVec2 row = ImGui::GetCursorScreenPos();
        ImGui::TextUnformatted(cap.c_str());
        draw_swatches_at(ImGui::GetWindowDrawList(),
                         ImVec2(row.x + lab_w, row.y + (ImGui::GetTextLineHeight() - sw) * 0.5f),
                         sim,
                         sw);
    }
}

void SettingsPanel::draw_scale_section()
{
    if (!store_ || !theme_mgr_)
        return;
    auto& d = store_->data_mut();
    draw_section_heading("UI Scale");

    // App UI scale: applied when chosen (not while dragging) and saved.
    ImGui::SetNextItemWidth(200.0f * ImGui::GetFontSize() / 16.0f);
    static constexpr int k_scales[] = {75, 90, 100, 110, 125, 150, 175, 200};
    const int            current = static_cast<int>(std::lround(theme_mgr_->ui_scale() * 100.0f));
    const std::string    preview = std::to_string(current) + "%";
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

void SettingsPanel::draw_files_section()
{
    if (!theme_mgr_)
        return;
    const auto& colors = ui::theme();
    draw_section_heading("Theme & Palette Files");

    const std::filesystem::path cfg = user_config_dir();
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(colors.text_secondary.r, colors.text_secondary.g, colors.text_secondary.b, 1.0f));
    if (cfg.empty())
    {
        ImGui::TextWrapped("No configuration folder: user themes are unavailable.");
        ImGui::PopStyleColor();
        return;
    }
    ImGui::TextWrapped("User themes: %s\nUser palettes: %s",
                       ui::ThemeManager::user_themes_dir(cfg).string().c_str(),
                       ui::ThemeManager::user_palettes_dir(cfg).string().c_str());
    ImGui::PopStyleColor();

    const bool dialogs = native_dialogs_enabled();
    ImGui::BeginDisabled(!dialogs);
    if (ImGui::Button("Import..."))
    {
        DialogEnvGuard env_guard;
        char const*    filters[1] = {"*.json"};
        const char*    home       = std::getenv("HOME");
        std::string    start      = home ? std::string(home) + "/" : "/";
        const char*    path       = tinyfd_openFileDialog("Import Theme or Palette",
                                                 start.c_str(),
                                                 1,
                                                 filters,
                                                 "Spectra theme / palette (*.json)",
                                                 0);
        if (path)
        {
            const auto res      = theme_mgr_->import_user_file(path, cfg);
            files_status_error_ = !res.ok;
            files_status_       = res.ok ? std::string("Imported ")
                                         + (res.is_palette ? "palette '" : "theme '") + res.id + "'"
                                         : "Import failed: " + res.error;
        }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Copy a theme or palette .json into your user folder and add it");
    ImGui::SameLine();
    if (ImGui::Button("Export Theme..."))
    {
        DialogEnvGuard    env_guard;
        const std::string id      = theme_mgr_->current_theme_name();
        const std::string name    = id + ".json";
        char const*       pats[1] = {"*.json"};
        if (auto path = ask_export_path("Export Theme",
                                        name.c_str(),
                                        1,
                                        pats,
                                        "Spectra theme (*.json)",
                                        last_export_dir_))
        {
            const bool ok       = theme_mgr_->export_theme_file(id, *path);
            files_status_error_ = !ok;
            files_status_       = ok ? "Exported theme to " + *path : "Could not write " + *path;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Export Palette..."))
    {
        DialogEnvGuard    env_guard;
        const std::string id      = theme_mgr_->current_data_palette_name();
        const std::string name    = id + ".json";
        char const*       pats[1] = {"*.json"};
        if (auto path = ask_export_path("Export Palette",
                                        name.c_str(),
                                        1,
                                        pats,
                                        "Spectra palette (*.json)",
                                        last_export_dir_))
        {
            const bool ok       = theme_mgr_->export_data_palette_file(id, *path);
            files_status_error_ = !ok;
            files_status_       = ok ? "Exported palette to " + *path : "Could not write " + *path;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Reload"))
    {
        const auto res      = theme_mgr_->load_user_files(cfg);
        files_status_error_ = !res.errors.empty();
        files_status_       = "Loaded " + std::to_string(res.themes.size()) + " theme(s), "
                        + std::to_string(res.palettes.size()) + " palette(s)";
        const size_t removed = res.removed_themes.size() + res.removed_palettes.size();
        if (removed > 0)
            files_status_ += ", removed " + std::to_string(removed);
        if (!res.errors.empty())
            files_status_ +=
                "; skipped " + res.errors.front()
                + (res.errors.size() > 1 ? " (+" + std::to_string(res.errors.size() - 1) + " more)"
                                         : "");
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Re-read the user theme and palette folders");
    if (!dialogs)
    {
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImVec4(colors.text_tertiary.r, colors.text_tertiary.g, colors.text_tertiary.b, 1.0f));
        ImGui::TextWrapped("File dialogs are disabled; copy .json files into the folders above "
                           "and press Reload.");
        ImGui::PopStyleColor();
    }
    if (!files_status_.empty())
    {
        const ui::Color& sc = files_status_error_ ? colors.error : colors.success;
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(sc.r, sc.g, sc.b, 1.0f));
        ImGui::TextWrapped("%s", files_status_.c_str());
        ImGui::PopStyleColor();
    }
}

void SettingsPanel::draw_glass_section()
{
    if (!store_ || !theme_mgr_)
        return;
    draw_section_heading("Glass / Transparency");

    ui::ThemeGlassSettings glass         = theme_mgr_->glass();
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
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Real backdrop blur is not enabled in this build.\n"
                          "Glass styling uses layered translucency and glow instead.");
    }

    // Saved to settings.json for the active theme (restored at startup).
    if (glass_changed)
    {
        theme_mgr_->set_glass_settings(glass, true);
        remember_glass(*theme_mgr_, *store_);
    }

    if (ImGui::Button("Reset Theme Defaults"))
    {
        theme_mgr_->reset_glass_defaults();
        remember_glass(*theme_mgr_, *store_);
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
        d.glass.reset();
        d.glass_theme.clear();
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

    // In search results the Settings search box filters the table (unless the
    // query named the section itself, e.g. "shortcut").
    const bool searching = settings_query_active(search_);
    if (!searching)
    {
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
    }

    const auto& colors   = ui::theme();
    auto        bindings = mgr->all_bindings();

    // Count shortcut strings to detect conflicts.
    std::unordered_map<std::string, int> shortcut_count;
    for (const auto& b : bindings)
    {
        if (b.shortcut.valid())
            shortcut_count[b.shortcut.to_string()]++;
    }

    std::string filter = settings_lower(filter_buf_);
    if (searching)
    {
        filter = shortcuts_match(settings_lower(search_)) ? settings_lower(search_) : std::string();
    }

    constexpr ImGuiTableFlags kTableFlags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg;

    // Fill the body's height budget; the table scrolls its own rows, so the
    // pinned footer below the body stays reachable.
    const float table_h = searching
                              ? ImGui::GetFrameHeightWithSpacing() * 8.0f
                              : std::max(body_max_h_ - (ImGui::GetCursorPosY() - body_origin_y_),
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
