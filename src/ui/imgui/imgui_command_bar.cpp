#ifdef SPECTRA_USE_IMGUI

    #include "imgui_integration_internal.hpp"

    #include "../../../third_party/tinyfiledialogs.h"

namespace spectra
{

// ─── Icon sidebar ───────────────────────────────────────────────────────────

// Helper: draw a clickable icon + label button for the Vision nav rail.
// Matches Vision.png: icon centered above a tiny label, subtle pill bg on active,
// very muted inactive state, generous vertical cell height.
static bool icon_label_button(const char* icon_codepoint,
                              const char* label,
                              bool        active,
                              ImFont*     icon_font,
                              ImFont*     label_font,
                              float       width)
{
    using namespace ui;

    const auto& colors = theme();

    // Vision.png metrics: tall cells, icon above label, centered
    float icon_sz  = icon_font ? icon_font->FontSize : 20.0f;
    float label_sz = label_font ? (label_font->FontSize * 0.92f) : 11.0f;   // ~11px label
    float icon_gap = 3.0f;    // gap between icon and label
    float cell_h   = 56.0f;   // generous cell height like Vision.png
    float pill_pad = 7.0f;    // horizontal inset for the highlight pill
    float pill_w   = width - pill_pad * 2.0f;

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    ImGuiStorage* storage        = ImGui::GetStateStorage();
    ImGuiID       hover_anim_id  = ImGui::GetID("hover_anim");
    ImGuiID       active_anim_id = ImGui::GetID("active_anim");
    ImGui::InvisibleButton("##btn", ImVec2(width, cell_h));
    bool  clicked = ImGui::IsItemClicked();
    bool  hovered = ImGui::IsItemHovered();
    float dt      = ImGui::GetIO().DeltaTime;
    float hover_t = storage->GetFloat(hover_anim_id, hovered ? 1.0f : 0.0f);
    float active_t = storage->GetFloat(active_anim_id, active ? 1.0f : 0.0f);
    hover_t += ((hovered ? 1.0f : 0.0f) - hover_t) * std::min(1.0f, dt * 16.0f);
    active_t += ((active ? 1.0f : 0.0f) - active_t) * std::min(1.0f, dt * 12.0f);
    storage->SetFloat(hover_anim_id, hover_t);
    storage->SetFloat(active_anim_id, active_t);
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();

    float  motion_t = std::max(hover_t, active_t);
    float  lift     = active_t * 1.5f + hover_t * 0.75f;
    ImVec2 pill_min = ImVec2(cursor.x + pill_pad, cursor.y + 3.0f - lift);
    ImVec2 pill_max = ImVec2(cursor.x + pill_pad + pill_w, cursor.y + cell_h - 3.0f - lift);

    if (motion_t > 0.01f)
    {
        ui::Color glow_color =
            colors.accent_glow.lerp(colors.accent, 0.18f + active_t * 0.18f + hover_t * 0.08f);
        dl->AddRectFilled(ImVec2(pill_min.x - 1.5f, pill_min.y - 1.0f),
                          ImVec2(pill_max.x + 1.5f, pill_max.y + 1.5f),
                          ImGui::ColorConvertFloat4ToU32(
                              ImVec4(glow_color.r,
                                     glow_color.g,
                                     glow_color.b,
                                     (0.06f + hover_t * 0.06f + active_t * 0.10f)
                                         * colors.glow_intensity)),
                          tokens::RADIUS_MD + 1.0f);

        ui::Color pill_fill =
            colors.bg_secondary.lerp(colors.bg_tertiary, 0.62f).lerp(colors.accent,
                                                                      0.06f + active_t * 0.16f
                                                                          + hover_t * 0.10f);
        dl->AddRectFilled(
            pill_min,
            pill_max,
            ImGui::ColorConvertFloat4ToU32(
                ImVec4(pill_fill.r, pill_fill.g, pill_fill.b, 0.55f + active_t * 0.36f + hover_t * 0.18f)),
            tokens::RADIUS_MD);
        dl->AddRect(
            pill_min,
            pill_max,
            ImGui::ColorConvertFloat4ToU32(
                ImVec4(colors.border_subtle.r * (1.0f - active_t) + colors.border_strong.r * active_t,
                       colors.border_subtle.g * (1.0f - active_t) + colors.border_strong.g * active_t,
                       colors.border_subtle.b * (1.0f - active_t) + colors.border_strong.b * active_t,
                       0.28f + hover_t * 0.18f + active_t * 0.24f)),
            tokens::RADIUS_MD);
    }

    if (active_t > 0.01f)
    {
        float  bar_w   = 3.0f;
        ImVec2 bar_min = ImVec2(cursor.x, cursor.y + 10.0f - lift);
        ImVec2 bar_max = ImVec2(cursor.x + bar_w, cursor.y + cell_h - 10.0f - lift);
        if (colors.glow_intensity > 0.01f)
        {
            dl->AddRectFilled(ImVec2(bar_min.x - 1.0f, bar_min.y - 1.0f),
                              ImVec2(bar_max.x + 2.0f, bar_max.y + 1.0f),
                              ImGui::ColorConvertFloat4ToU32(
                                  ImVec4(colors.accent_glow.r,
                                         colors.accent_glow.g,
                                         colors.accent_glow.b,
                                         colors.accent_glow.a * (0.18f + active_t * 0.10f))),
                              2.0f);
        }
        dl->AddRectFilled(bar_min,
                          bar_max,
                          ImGui::ColorConvertFloat4ToU32(
                              ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 0.80f + active_t * 0.20f)),
                          1.0f);
    }

    ui::Color icon_color =
        colors.text_secondary.lerp(colors.text_primary, hover_t * 0.78f).lerp(colors.accent_hover,
                                                                               active_t * 0.88f);
    ui::Color text_color =
        colors.text_secondary.lerp(colors.text_primary, hover_t * 0.60f).lerp(colors.accent_hover,
                                                                               active_t * 0.80f);
    ImU32 icon_col = ImGui::ColorConvertFloat4ToU32(
        ImVec4(icon_color.r, icon_color.g, icon_color.b, 0.90f + hover_t * 0.08f + active_t * 0.10f));
    ImU32 text_col = ImGui::ColorConvertFloat4ToU32(
        ImVec4(text_color.r, text_color.g, text_color.b, 0.82f + hover_t * 0.10f + active_t * 0.08f));

    float icon_draw_sz  = icon_sz * (1.0f + hover_t * 0.04f + active_t * 0.05f);
    float label_draw_sz = label_sz * (1.0f + hover_t * 0.02f + active_t * 0.03f);
    float content_h     = icon_draw_sz + icon_gap + label_draw_sz;
    float y_start       = cursor.y + (cell_h - content_h) * 0.5f - lift * 0.35f;

    if (icon_font)
    {
        ImVec2 isz = icon_font->CalcTextSizeA(icon_draw_sz, FLT_MAX, 0.0f, icon_codepoint);
        float  ix  = cursor.x + (width - isz.x) * 0.5f;
        dl->AddText(icon_font, icon_draw_sz, ImVec2(ix, y_start), icon_col, icon_codepoint);
    }

    if (label_font)
    {
        ImVec2 lsz = label_font->CalcTextSizeA(label_draw_sz, FLT_MAX, 0.0f, label);
        float  lx  = cursor.x + (width - lsz.x) * 0.5f;
        float  ly  = y_start + icon_draw_sz + icon_gap;
        dl->AddText(label_font, label_draw_sz, ImVec2(lx, ly), text_col, label);
    }

    return clicked;
}

// ─── Legacy Methods (To be removed after migration) ───────────────────────────

// These methods are kept temporarily for compatibility but will be removed
// once Agent C implements the proper inspector system

void ImGuiIntegration::draw_menubar()
{
    // Legacy method - replaced by draw_command_bar()
    draw_command_bar();
}

void ImGuiIntegration::draw_icon_bar()
{
    // Legacy method - replaced by draw_nav_rail()
    draw_nav_rail();
}

void ImGuiIntegration::draw_panel(Figure& figure)
{
    // Legacy method - replaced by draw_inspector()
    draw_inspector(figure);
}

// ─── Legacy Panel Drawing Methods (To be removed after Agent C migration) ───

// Helper for drawing dropdown menus — modern 2026 style with:
//   • auto-close on mouse leave
//   • hover-switch between adjacent menus
//   • popup anchored to button's bottom-left corner
void ImGuiIntegration::draw_menubar_menu(const char* label, const std::vector<MenuItem>& items)
{
    const auto& colors = ui::theme();
    bool        menu_is_open = open_menu_label_ == label;

    ImGui::PushFont(font_menubar_);
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(colors.text_primary.r,
               colors.text_primary.g,
               colors.text_primary.b,
               menu_is_open ? 1.0f : 0.88f));
    ImGui::PushStyleColor(
        ImGuiCol_Button,
        menu_is_open ? ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 0.95f)
                     : ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(
        ImGuiCol_ButtonHovered,
        ImVec4(colors.accent_subtle.r, colors.accent_subtle.g, colors.accent_subtle.b, 0.78f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(colors.accent_muted.r,
                                 colors.accent_muted.g,
                                 colors.accent_muted.b,
                                 0.95f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(ui::tokens::SPACE_4, ui::tokens::SPACE_2));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::tokens::RADIUS_MD);

    // Remember button rect for popup positioning and auto-close
    ImVec2 btn_pos     = ImGui::GetCursorScreenPos();
    bool   clicked     = ImGui::Button(label);
    ImVec2 btn_size    = ImGui::GetItemRectSize();
    ImVec2 btn_max     = ImVec2(btn_pos.x + btn_size.x, btn_pos.y + btn_size.y);
    bool   btn_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);

    // Click opens this menu
    if (clicked)
    {
        SPECTRA_LOG_DEBUG("menu", "Click open: " + std::string(label));
        ImGui::OpenPopup(label);
        open_menu_label_ = label;
    }

    // Hover-switch: if another menu is open and user hovers this button, switch
    if (btn_hovered && !open_menu_label_.empty() && open_menu_label_ != label)
    {
        SPECTRA_LOG_DEBUG("menu",
                          "Hover switch: " + std::string(open_menu_label_) + " -> " + label);
        ImGui::OpenPopup(label);
        open_menu_label_ = label;
    }

    // Anchor popup at button's bottom-left corner (not at mouse position)
    ImGui::SetNextWindowPos(ImVec2(btn_pos.x, btn_max.y + 2.0f));

    // Modern popup styling
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(ui::tokens::SPACE_2, ui::tokens::SPACE_2));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, ui::tokens::RADIUS_LG);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(ui::tokens::SPACE_2, ui::tokens::SPACE_1));
    ImGui::PushStyleColor(
        ImGuiCol_PopupBg,
        ImVec4(colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b, 0.97f));
    ImGui::PushStyleColor(
        ImGuiCol_Border,
        ImVec4(colors.border_subtle.r, colors.border_subtle.g, colors.border_subtle.b, 0.4f));

    if (ImGui::BeginPopup(label))
    {
        // Track that this menu is the open one
        open_menu_label_ = label;

        // ── Auto-close: dismiss when mouse moves away from button + popup ──
        ImVec2 mouse      = ImGui::GetIO().MousePos;
        ImVec2 popup_pos  = ImGui::GetWindowPos();
        ImVec2 popup_size = ImGui::GetWindowSize();
        float  margin     = 20.0f;

        // Combined rect of button + popup + margin
        float combined_min_x = std::min(btn_pos.x, popup_pos.x) - margin;
        float combined_min_y = std::min(btn_pos.y, popup_pos.y) - margin;
        float combined_max_x = std::max(btn_max.x, popup_pos.x + popup_size.x) + margin;
        float combined_max_y = std::max(btn_max.y, popup_pos.y + popup_size.y) + margin;

        bool mouse_in_zone = (mouse.x >= combined_min_x && mouse.x <= combined_max_x
                              && mouse.y >= combined_min_y && mouse.y <= combined_max_y);

        if (!mouse_in_zone && !ImGui::IsAnyItemActive())
        {
            SPECTRA_LOG_DEBUG("menu", "Auto-close: " + std::string(label));
            ImGui::CloseCurrentPopup();
            open_menu_label_.clear();
        }

        // Multi-layer soft shadow behind popup (elevation 3)
        ImDrawList* bg_dl         = ImGui::GetBackgroundDrawList();
        float       popup_shadow  = ui::tokens::ELEVATION_3_SPREAD;
        for (int si = 1; si <= 4; ++si)
        {
            float st    = static_cast<float>(si) / 4.0f;
            float soff  = popup_shadow * st;
            float salph = 0.12f * (1.0f - st * 0.7f);
            bg_dl->AddRectFilled(
                ImVec2(popup_pos.x + soff * 0.3f, popup_pos.y + soff * 0.5f),
                ImVec2(popup_pos.x + popup_size.x + soff * 0.5f,
                       popup_pos.y + popup_size.y + soff),
                IM_COL32(0, 0, 0, static_cast<int>(salph * 255)),
                ui::tokens::RADIUS_LG + soff * 0.5f);
        }

        for (const auto& item : items)
        {
            if (item.label.empty())
            {
                ImGui::Dummy(ImVec2(0, 2));
                ImGui::PushStyleColor(ImGuiCol_Separator,
                                      ImVec4(colors.border_subtle.r,
                                             colors.border_subtle.g,
                                             colors.border_subtle.b,
                                             0.3f));
                ImGui::Separator();
                ImGui::PopStyleColor();
                ImGui::Dummy(ImVec2(0, 2));
            }
            else if (!item.callback)
            {
                // Null callback + non-empty label → disabled / grayed-out text item.
                // text_tertiary is the theme field for placeholders / disabled text.
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImVec4(colors.text_tertiary.r,
                                             colors.text_tertiary.g,
                                             colors.text_tertiary.b,
                                             colors.text_tertiary.a));
                float item_h = ImGui::GetTextLineHeight() + 10.0f;
                ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
                ImGui::Selectable(item.label.c_str(),
                                  false,
                                  ImGuiSelectableFlags_Disabled,
                                  ImVec2(0, item_h));
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImVec4(colors.text_primary.r,
                                             colors.text_primary.g,
                                             colors.text_primary.b,
                                             colors.text_primary.a));
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                                      ImVec4(colors.accent_subtle.r,
                                             colors.accent_subtle.g,
                                             colors.accent_subtle.b,
                                             0.5f));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive,
                                      ImVec4(colors.accent_muted.r,
                                             colors.accent_muted.g,
                                             colors.accent_muted.b,
                                             0.7f));
                ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));

                float item_h = ImGui::GetTextLineHeight() + 10.0f;
                if (ImGui::Selectable(item.label.c_str(),
                                      false,
                                      ImGuiSelectableFlags_None,
                                      ImVec2(0, item_h)))
                {
                    item.callback();
                    open_menu_label_.clear();
                }

                ImGui::PopStyleVar();
                ImGui::PopStyleColor(4);
            }
        }

        ImGui::EndPopup();
    }
    else
    {
        // Popup closed (e.g. by clicking outside) — clear tracking if this was the open one
        if (open_menu_label_ == label)
        {
            open_menu_label_.clear();
        }
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    ImGui::PopFont();
}

// Helper for drawing toolbar buttons with modern hover styling and themed tooltips
void ImGuiIntegration::draw_toolbar_button(const char*           icon,
                                           std::function<void()> callback,
                                           const char*           tooltip,
                                           bool                  is_active)
{
    const auto& colors = ui::theme();
    // Use per-instance font_icon_ (not the IconFont singleton) so that
    // secondary windows use their own atlas font, avoiding TexID mismatch.
    ImGui::PushFont(font_icon_);

    if (is_active)
    {
        // Subtle accent pill — consistent with nav rail icon_button
        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 0.15f));
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f));
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(colors.text_primary.r,
                                     colors.text_primary.g,
                                     colors.text_primary.b,
                                     0.80f));   // Bright to match nav rail
    }
    ImGui::PushStyleColor(
        ImGuiCol_ButtonHovered,
        ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 0.7f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 0.25f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(ui::tokens::SPACE_2, ui::tokens::SPACE_2));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::tokens::RADIUS_MD);

    if (ImGui::Button(icon))
    {
        if (callback)
            callback();
    }

    // Store tooltip for deferred rendering at the end of build_ui
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && tooltip)
    {
        deferred_tooltip_ = tooltip;
    }

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    ImGui::PopFont();
}

// ─── Layout-Based Drawing Methods ─────────────────────────────────────────────

void ImGuiIntegration::draw_command_bar()
{
    if (!layout_manager_)
    {
        SPECTRA_LOG_WARN("ui", "draw_command_bar called but layout_manager_ is null");
        return;
    }

    SPECTRA_LOG_TRACE("ui", "Drawing command bar");

    Rect bounds = layout_manager_->command_bar_rect();
    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y));
    ImGui::SetNextWindowSize(ImVec2(bounds.w, bounds.h));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings
                             | ImGuiWindowFlags_NoBringToFrontOnFocus
                             | ImGuiWindowFlags_NoFocusOnAppearing;

    // Top bar: slightly darker than panels to push visual focus to canvas
    float bar_blend = 0.74f;   // Blend toward bg_primary
    auto  bar_bg_r =
        ui::theme().bg_primary.r * bar_blend + ui::theme().bg_secondary.r * (1.0f - bar_blend);
    auto bar_bg_g =
        ui::theme().bg_primary.g * bar_blend + ui::theme().bg_secondary.g * (1.0f - bar_blend);
    auto bar_bg_b =
        ui::theme().bg_primary.b * bar_blend + ui::theme().bg_secondary.b * (1.0f - bar_blend);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(ui::tokens::SPACE_5, ui::tokens::SPACE_2));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ui::tokens::SPACE_3, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(bar_bg_r, bar_bg_g, bar_bg_b, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border,
                          ImVec4(ui::theme().border_subtle.r,
                                 ui::theme().border_subtle.g,
                                 ui::theme().border_subtle.b,
                                 0.3f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (ImGui::Begin("##commandbar", nullptr, flags))
    {
        SPECTRA_LOG_TRACE("ui", "Command bar window began successfully");

        // ── Floating surface depth: bottom shadow + hairline border ──
        {
            ImDrawList* bar_dl = ImGui::GetWindowDrawList();
            ImVec2      wpos   = ImGui::GetWindowPos();
            ImVec2      wsz    = ImGui::GetWindowSize();
            float       bottom = wpos.y + wsz.y;

            // Multi-layer soft shadow below the bar
            float shadow_spread = ui::tokens::ELEVATION_2_SPREAD;
            for (int i = 0; i < 4; ++i)
            {
                float t     = static_cast<float>(i) / 4.0f;
                float alpha = 0.10f * (1.0f - t);
                float off   = shadow_spread * t;
                bar_dl->AddRectFilled(
                    ImVec2(wpos.x, bottom),
                    ImVec2(wpos.x + wsz.x, bottom + off + 1.0f),
                    IM_COL32(0, 0, 0, static_cast<int>(alpha * 255)));
            }

            // Crisp hairline border at bottom edge
            bar_dl->AddLine(ImVec2(wpos.x, std::floor(bottom) - 1.0f),
                            ImVec2(wpos.x + wsz.x, std::floor(bottom) - 1.0f),
                            IM_COL32(static_cast<uint8_t>(ui::theme().border_subtle.r * 255),
                                     static_cast<uint8_t>(ui::theme().border_subtle.g * 255),
                                     static_cast<uint8_t>(ui::theme().border_subtle.b * 255),
                                     80),
                            1.0f);
        }

        // ── App title/brand on the left — textured S mark + clean wordmark ──
        {
            ImDrawList* dl      = ImGui::GetWindowDrawList();
            float       bar_h   = ImGui::GetWindowSize().y;
            ImVec2      cursor  = ImGui::GetCursorScreenPos();
            float       cy      = cursor.y + (bar_h - ImGui::GetCursorPosY() * 2.0f) * 0.5f;
            float       logo_sz = 28.0f;
            float       lx      = cursor.x + 1.0f;
            float       ly      = cy - logo_sz * 0.5f;
            float       text_x  = lx + logo_sz + 8.0f;

            if (corner_logo_texture_id_)
            {
                dl->AddImage(imgui_texture_id_from_u64(corner_logo_texture_id_),
                             ImVec2(lx, ly),
                             ImVec2(lx + logo_sz, ly + logo_sz));
            }

            ImGui::PushFont(font_title_);
            const char* letters = "SPECTRA";
            float       font_sz = font_title_->FontSize * 0.92f;
            float       text_y  = cy - font_sz * 0.5f;
            float       spacing = 2.6f;

            float total_w = 0.0f;
            for (const char* p = letters; *p; ++p)
            {
                char ch[2] = {*p, 0};
                total_w += ImGui::CalcTextSize(ch).x + (*p ? spacing : 0.0f);
            }
            total_w -= spacing;   // no trailing space

            {
                float gx  = text_x;
                int   idx = 0;
                int   len = static_cast<int>(strlen(letters));
                for (const char* p = letters; *p; ++p, ++idx)
                {
                    char    ch[2] = {*p, 0};
                    float   cw    = ImGui::CalcTextSize(ch).x;
                    float   t     = (len > 1) ? static_cast<float>(idx) / (len - 1) : 0.0f;
                    float   mix   = 0.25f + 0.35f * t;
                    uint8_t cr    = static_cast<uint8_t>(
                        (ui::theme().text_primary.r * (1.0f - mix) + ui::theme().accent.r * mix)
                        * 255);
                    uint8_t cg = static_cast<uint8_t>(
                        (ui::theme().text_primary.g * (1.0f - mix) + ui::theme().accent.g * mix)
                        * 255);
                    uint8_t cb = static_cast<uint8_t>(
                        (ui::theme().text_primary.b * (1.0f - mix) + ui::theme().accent.b * mix)
                        * 255);
                    ImU32 col = IM_COL32(cr, cg, cb, 245);
                    dl->AddText(font_title_, font_sz, ImVec2(gx, text_y), col, ch);
                    gx += cw + spacing;
                }
            }

            // Advance ImGui cursor past the entire brand block
            float brand_w = (text_x - cursor.x) + total_w + 4.0f;
            ImGui::Dummy(ImVec2(brand_w, font_sz));
            ImGui::PopFont();
        }

        ImGui::SameLine();

        draw_toolbar_button(
            ui::icon_str(ui::Icon::Home),
            [this]()
            {
                SPECTRA_LOG_DEBUG("ui_button", "Home button clicked - setting reset_view flag");
                reset_view_ = true;
                SPECTRA_LOG_DEBUG("ui_button", "Reset view flag set successfully");
            },
            "Reset View (Home)");

        ImGui::SameLine();

        // File menu
        draw_menubar_menu("File",
                          {MenuItem("New Figure",
                                    [this]()
                                    {
                                        if (command_registry_)
                                            command_registry_->execute("figure.new");
                                    }),
                           MenuItem("", nullptr),   // Separator
                           MenuItem("Export PNG",
                                    [this]()
                                    {
                                        if (command_registry_)
                                            command_registry_->execute("file.export_png");
                                    }),
                           MenuItem("Export SVG",
                                    [this]()
                                    {
                                        if (command_registry_)
                                            command_registry_->execute("file.export_svg");
                                    }),
                           MenuItem("Save Workspace",
                                    [this]()
                                    {
                                        if (command_registry_)
                                            command_registry_->execute("file.save_workspace");
                                    }),
                           MenuItem("Load Workspace",
                                    [this]()
                                    {
                                        if (command_registry_)
                                            command_registry_->execute("file.load_workspace");
                                    }),
                           MenuItem("", nullptr),   // Separator
                           MenuItem("Save Figure...",
                                    [this]()
                                    {
                                        if (command_registry_)
                                            command_registry_->execute("file.save_figure");
                                    }),
                           MenuItem("Load Figure...",
                                    [this]()
                                    {
                                        if (command_registry_)
                                            command_registry_->execute("file.load_figure");
                                    }),
                           MenuItem("", nullptr),   // Separator
                           MenuItem("Exit",
                                    [this]()
                                    {
                                        if (command_registry_)
                                            command_registry_->execute("app.cancel");
                                    })});

        ImGui::SameLine();

        // Data menu
        draw_menubar_menu(
            "Data",
            {MenuItem(
                "Load from CSV...",
                [this]()
                {
                    // Open native OS file dialog
                    char const* filters[3] = {"*.csv", "*.tsv", "*.txt"};
                    const char* home_env   = std::getenv("HOME");
                    std::string home_dir   = home_env ? std::string(home_env) + "/" : "/";
                    const char* home       = home_dir.c_str();
                    char const* result =
                        tinyfd_openFileDialog("Open CSV File", home, 3, filters, "CSV files", 0);
                    if (result)
                    {
                        csv_file_path_   = result;
                        csv_data_        = parse_csv(csv_file_path_);
                        csv_data_loaded_ = csv_data_.error.empty();
                        csv_error_       = csv_data_.error;
                        csv_col_x_       = 0;
                        csv_col_y_       = (csv_data_.num_cols > 1) ? 1 : 0;
                        csv_col_z_       = -1;
                        if (csv_data_loaded_)
                            csv_dialog_open_ = true;
                    }
                })});

        ImGui::SameLine();

        // View menu
        draw_menubar_menu(
            "View",
            {MenuItem("Toggle Inspector",
                      [this]()
                      {
                          if (command_registry_)
                              command_registry_->execute("panel.toggle_inspector");
                          else
                          {
                              bool new_vis = !layout_manager_->is_inspector_visible();
                              layout_manager_->set_inspector_visible(new_vis);
                              panel_open_ = new_vis;
                          }
                      }),
             MenuItem("Toggle Navigation Rail", [this]() { show_nav_rail_ = !show_nav_rail_; }),
             MenuItem("Toggle 2D/3D View",
                      [this]()
                      {
                          if (command_registry_)
                              command_registry_->execute("view.toggle_3d");
                      }),
             MenuItem("Zoom to Fit",
                      [this]()
                      {
                          if (command_registry_)
                              command_registry_->execute("view.autofit");
                      }),
             MenuItem("Reset View",
                      [this]()
                      {
                          if (command_registry_)
                              command_registry_->execute("view.reset");
                      }),
             MenuItem("Toggle Grid",
                      [this]()
                      {
                          if (command_registry_)
                              command_registry_->execute("view.toggle_grid");
                      }),
             MenuItem("Toggle Legend",
                      [this]()
                      {
                          if (command_registry_)
                              command_registry_->execute("view.toggle_legend");
                      }),
             MenuItem("Remove All Data Tips",
                      [this]()
                      {
                          if (data_interaction_)
                              data_interaction_->clear_markers();
                      }),
             MenuItem("", nullptr),   // Separator
             MenuItem("Toggle Timeline",
                      [this]()
                      {
                          if (command_registry_)
                              command_registry_->execute("panel.toggle_timeline");
                      }),
             MenuItem("Toggle Curve Editor",
                      [this]()
                      {
                          if (command_registry_)
                              command_registry_->execute("panel.toggle_curve_editor");
                      }),
             MenuItem("Toggle Parameters",
                      [this]()
                      {
                          if (knob_manager_ && !knob_manager_->empty())
                              knob_manager_->set_visible(!knob_manager_->is_visible());
                      }),
             MenuItem("Toggle Data Editor",
                      [this]()
                      {
                          if (active_section_ == Section::DataEditor && panel_open_)
                          {
                              panel_open_ = false;
                              layout_manager_->set_inspector_visible(false);
                          }
                          else
                          {
                              active_section_ = Section::DataEditor;
                              panel_open_     = true;
                              layout_manager_->set_inspector_visible(true);
                          }
                      })});

        ImGui::SameLine();

        // Axes menu — link/unlink axes across subplots (2D and 3D)
        {
            std::vector<MenuItem> axes_items;

            // Helper lambdas to collect 2D and 3D axes from the current figure
            auto has_enough_axes = [this]() -> bool
            {
                if (!axis_link_mgr_ || !current_figure_)
                    return false;
                return current_figure_->all_axes().size() >= 2;
            };

            axes_items.emplace_back(
                "Link X Axes",
                [this, has_enough_axes]()
                {
                    if (!has_enough_axes())
                        return;
                    // Link 2D axes on X
                    if (current_figure_->axes().size() >= 2)
                    {
                        auto gid = axis_link_mgr_->create_group("X Link", LinkAxis::X);
                        for (auto& ax : current_figure_->axes_mut())
                        {
                            if (ax)
                                axis_link_mgr_->add_to_group(gid, ax.get());
                        }
                    }
                    // Link 3D axes (xlim/ylim/zlim all propagate together)
                    {
                        std::vector<Axes3D*> axes3d_list;
                        for (auto& ab : current_figure_->all_axes_mut())
                        {
                            if (auto* a3 = dynamic_cast<Axes3D*>(ab.get()))
                                axes3d_list.push_back(a3);
                        }
                        for (size_t i = 1; i < axes3d_list.size(); ++i)
                            axis_link_mgr_->link_3d(axes3d_list[0], axes3d_list[i]);
                    }
                    SPECTRA_LOG_INFO("axes_link", "Linked all axes on X");
                });
            axes_items.emplace_back(
                "Link Y Axes",
                [this, has_enough_axes]()
                {
                    if (!has_enough_axes())
                        return;
                    if (current_figure_->axes().size() >= 2)
                    {
                        auto gid = axis_link_mgr_->create_group("Y Link", LinkAxis::Y);
                        for (auto& ax : current_figure_->axes_mut())
                        {
                            if (ax)
                                axis_link_mgr_->add_to_group(gid, ax.get());
                        }
                    }
                    // 3D axes link all limits together
                    {
                        std::vector<Axes3D*> axes3d_list;
                        for (auto& ab : current_figure_->all_axes_mut())
                        {
                            if (auto* a3 = dynamic_cast<Axes3D*>(ab.get()))
                                axes3d_list.push_back(a3);
                        }
                        for (size_t i = 1; i < axes3d_list.size(); ++i)
                            axis_link_mgr_->link_3d(axes3d_list[0], axes3d_list[i]);
                    }
                    SPECTRA_LOG_INFO("axes_link", "Linked all axes on Y");
                });
            axes_items.emplace_back(
                "Link Z Axes",
                [this, has_enough_axes]()
                {
                    if (!has_enough_axes())
                        return;
                    // Z-axis linking is only meaningful for 3D axes
                    std::vector<Axes3D*> axes3d_list;
                    for (auto& ab : current_figure_->all_axes_mut())
                    {
                        if (auto* a3 = dynamic_cast<Axes3D*>(ab.get()))
                            axes3d_list.push_back(a3);
                    }
                    for (size_t i = 1; i < axes3d_list.size(); ++i)
                        axis_link_mgr_->link_3d(axes3d_list[0], axes3d_list[i], LinkAxis::Z);
                    SPECTRA_LOG_INFO("axes_link", "Linked all 3D axes on Z");
                });
            axes_items.emplace_back(
                "Link All Axes",
                [this, has_enough_axes]()
                {
                    if (!has_enough_axes())
                        return;
                    // Link 2D axes on X+Y
                    if (current_figure_->axes().size() >= 2)
                    {
                        auto gid = axis_link_mgr_->create_group("XY Link", LinkAxis::Both);
                        for (auto& ax : current_figure_->axes_mut())
                        {
                            if (ax)
                                axis_link_mgr_->add_to_group(gid, ax.get());
                        }
                    }
                    // Link 3D axes (xlim/ylim/zlim)
                    {
                        std::vector<Axes3D*> axes3d_list;
                        for (auto& ab : current_figure_->all_axes_mut())
                        {
                            if (auto* a3 = dynamic_cast<Axes3D*>(ab.get()))
                                axes3d_list.push_back(a3);
                        }
                        for (size_t i = 1; i < axes3d_list.size(); ++i)
                            axis_link_mgr_->link_3d(axes3d_list[0], axes3d_list[i], LinkAxis::All);
                    }
                    SPECTRA_LOG_INFO("axes_link", "Linked all axes on X+Y+Z");
                });
            axes_items.emplace_back("", nullptr);   // separator
            axes_items.emplace_back("Unlink All",
                                    [this]()
                                    {
                                        if (!axis_link_mgr_)
                                            return;
                                        // Unlink 2D groups
                                        std::vector<LinkGroupId> ids;
                                        for (auto& [id, group] : axis_link_mgr_->groups())
                                        {
                                            ids.push_back(id);
                                        }
                                        for (auto id : ids)
                                        {
                                            axis_link_mgr_->remove_group(id);
                                        }
                                        // Unlink 3D axes
                                        if (current_figure_)
                                        {
                                            for (auto& ab : current_figure_->all_axes_mut())
                                            {
                                                if (auto* a3 = dynamic_cast<Axes3D*>(ab.get()))
                                                    axis_link_mgr_->remove_from_all_3d(a3);
                                            }
                                        }
                                        axis_link_mgr_->clear_shared_cursor();
                                        SPECTRA_LOG_INFO("axes_link", "Unlinked all axes");
                                    });

            draw_menubar_menu("Axes", axes_items);
        }

        ImGui::SameLine();

        // Transforms menu — apply data transforms to series
        {
            std::vector<MenuItem> xform_items;
            auto&                 registry = TransformRegistry::instance();
            auto                  names    = registry.available_transforms();

            // Built-in transforms
            for (const auto& name : names)
            {
                xform_items.emplace_back(
                    name,
                    [this, name]()
                    {
                        if (!current_figure_)
                            return;
                        DataTransform xform;
                        if (!TransformRegistry::instance().get_transform(name, xform))
                            return;

                        // Apply to all visible series in all axes
                        for (auto& ax : current_figure_->axes_mut())
                        {
                            if (!ax)
                                continue;
                            for (auto& series_ptr : ax->series_mut())
                            {
                                if (!series_ptr || !series_ptr->visible())
                                    continue;

                                if (auto* ls = dynamic_cast<LineSeries*>(series_ptr.get()))
                                {
                                    std::vector<float> rx, ry;
                                    xform.apply_y(ls->x_data(), ls->y_data(), rx, ry);
                                    ls->set_x(rx).set_y(ry);
                                }
                                else if (auto* sc = dynamic_cast<ScatterSeries*>(series_ptr.get()))
                                {
                                    std::vector<float> rx, ry;
                                    xform.apply_y(sc->x_data(), sc->y_data(), rx, ry);
                                    sc->set_x(rx).set_y(ry);
                                }
                            }
                            ax->auto_fit();
                        }
                        SPECTRA_LOG_INFO("transform", "Applied transform: " + name);
                    });
            }

            // Separator and custom formula option
            xform_items.emplace_back("", nullptr);   // separator
            xform_items.emplace_back(
                "Custom Formula...",
                [this]()
                {
                    custom_transform_dialog_.set_fonts(font_body_, font_heading_, font_title_);
                    custom_transform_dialog_.open(current_figure_);
                });

            draw_menubar_menu("Transforms", xform_items);
        }

        ImGui::SameLine();

        // Tools menu
        draw_menubar_menu(
            "Tools",
            {MenuItem("Screenshot (PNG)",
                      [this]()
                      {
                          if (command_registry_)
                              command_registry_->execute("file.export_png");
                      }),
             MenuItem("Undo",
                      [this]()
                      {
                          if (command_registry_)
                              command_registry_->execute("edit.undo");
                      }),
             MenuItem("Redo",
                      [this]()
                      {
                          if (command_registry_)
                              command_registry_->execute("edit.redo");
                      }),
             MenuItem("", nullptr),   // Separator
             MenuItem("Theme Settings", [this]() { show_theme_settings_ = !show_theme_settings_; }),
             MenuItem("Command Palette",
                      [this]()
                      {
                          if (command_registry_)
                              command_registry_->execute("app.command_palette");
                      }),
             MenuItem("", nullptr),   // Separator
    #ifdef SPECTRA_USE_ROS2
             MenuItem("ROS2 Adapter",
                      [this]()
                      {
                          if (command_registry_)
                              command_registry_->execute("tools.ros2_adapter");
                      })
    #else
             // Grayed-out placeholder when compiled without ROS2 support.
             // draw_menubar_menu skips items with a null callback; we render
             // a disabled text label instead via a zero-callback sentinel that
             // is handled specially in draw_menubar_menu.
             MenuItem("\xEF\xA0\xAD ROS2 Adapter (not available)", nullptr)
    #endif
            });

        // Push status info to the right
        ImGui::SameLine(0.0f, ImGui::GetContentRegionAvail().x - 220.0f);

        // Status info
        ImGuiIO& io = ImGui::GetIO();
        ImGui::PushFont(font_heading_);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(ui::theme().text_secondary.r,
                                     ui::theme().text_secondary.g,
                                     ui::theme().text_secondary.b,
                                     ui::theme().text_secondary.a));

        char status[128];
        std::snprintf(status,
                      sizeof(status),
                      "Display: %dx%d | FPS: %.0f | GPU",
                      static_cast<int>(io.DisplaySize.x),
                      static_cast<int>(io.DisplaySize.y),
                      io.Framerate);
        ImVec2 status_pos = ImGui::GetCursorScreenPos();
        ImVec2 status_sz  = ImGui::CalcTextSize(status);
        ImVec2 chip_min(status_pos.x - ui::tokens::SPACE_2, status_pos.y - 3.0f);
        ImVec2 chip_max(status_pos.x + status_sz.x + ui::tokens::SPACE_2,
                        status_pos.y + status_sz.y + 3.0f);
        ImGui::GetWindowDrawList()->AddRectFilled(
            chip_min,
            chip_max,
            ImGui::ColorConvertFloat4ToU32(ImVec4(ui::theme().bg_tertiary.r,
                                                  ui::theme().bg_tertiary.g,
                                                  ui::theme().bg_tertiary.b,
                                                  0.92f)),
            ui::tokens::RADIUS_MD);
        ImGui::GetWindowDrawList()->AddRect(
            chip_min,
            chip_max,
            ImGui::ColorConvertFloat4ToU32(ImVec4(ui::theme().border_subtle.r,
                                                  ui::theme().border_subtle.g,
                                                  ui::theme().border_subtle.b,
                                                  0.55f)),
            ui::tokens::RADIUS_MD);
        ImGui::TextUnformatted(status);

        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);
}

void ImGuiIntegration::draw_nav_rail()
{
    if (!layout_manager_ || !show_nav_rail_)
        return;

    Rect bounds = layout_manager_->nav_rail_rect();
    if (bounds.w < 1.0f || bounds.h < 1.0f)
        return;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings
                             | ImGuiWindowFlags_NoBringToFrontOnFocus
                             | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollbar;

    float rail_w = bounds.w;

    // Vision.png style: full-height, bg_primary (darkest), no border, no rounding
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, ui::tokens::SPACE_3));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(
        ImGuiCol_WindowBg,
        ImVec4(ui::theme().bg_secondary.r,
               ui::theme().bg_secondary.g,
               ui::theme().bg_secondary.b,
               0.98f));

    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(rail_w, bounds.h), ImGuiCond_Always);

    if (ImGui::Begin("##navrail", nullptr, flags))
    {
        // Floating surface depth: soft shadow on right edge
        {
            ImDrawList* dl            = ImGui::GetWindowDrawList();
            float       right_edge    = bounds.x + rail_w;
            float       shadow_spread = ui::tokens::ELEVATION_1_SPREAD;
            for (int i = 0; i < 4; ++i)
            {
                float t     = static_cast<float>(i) / 4.0f;
                float alpha = 0.10f * (1.0f - t);
                float off   = shadow_spread * t;
                dl->AddRectFilled(
                    ImVec2(right_edge, bounds.y),
                    ImVec2(right_edge + off + 1.0f, bounds.y + bounds.h),
                    IM_COL32(0, 0, 0, static_cast<int>(alpha * 255)));
            }

            // Hairline border on right edge
            dl->AddLine(ImVec2(right_edge - 1.0f, bounds.y),
                        ImVec2(right_edge - 1.0f, bounds.y + bounds.h),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(ui::theme().border_subtle.r,
                                                              ui::theme().border_subtle.g,
                                                              ui::theme().border_subtle.b,
                                                              0.48f)),
                        1.0f);
        }

        ImFont* label_font = font_heading_;   // 12.5px — compact labels
        float   btn_w      = rail_w;

        // Separator: very subtle hairline, Vision.png style
        auto draw_separator = [&]()
        {
            ImGui::Dummy(ImVec2(0, 3.0f));
            float  sep_inset = 14.0f;
            ImVec2 p0 = ImVec2(ImGui::GetWindowPos().x + sep_inset, ImGui::GetCursorScreenPos().y);
            ImVec2 p1 = ImVec2(ImGui::GetWindowPos().x + rail_w - sep_inset, p0.y);
            ImGui::GetWindowDrawList()->AddLine(p0,
                                                p1,
                                                IM_COL32(ui::theme().border_subtle.r * 255,
                                                         ui::theme().border_subtle.g * 255,
                                                         ui::theme().border_subtle.b * 255,
                                                         25),
                                                1.0f);
            ImGui::Dummy(ImVec2(0, 3.0f));
        };

        // ── Tool mode helper ──
        auto tool_btn = [&](ui::Icon icon, const char* label, ToolMode mode)
        {
            bool is_active = (interaction_mode_ == mode);
            if (icon_label_button(ui::icon_str(icon),
                                  label,
                                  is_active,
                                  font_icon_,
                                  label_font,
                                  btn_w))
            {
                interaction_mode_ = mode;
            }
        };

        // ── Toggle helper (for panel/feature toggles) ──
        auto toggle_btn =
            [&](ui::Icon icon, const char* label, bool is_active, std::function<void()> on_click)
        {
            if (icon_label_button(ui::icon_str(icon),
                                  label,
                                  is_active,
                                  font_icon_,
                                  label_font,
                                  btn_w))
            {
                on_click();
            }
        };

        // ── Group 1: Navigation tools ──
        tool_btn(ui::Icon::MousePointer, "Select", ToolMode::Select);
        tool_btn(ui::Icon::Hand, "Pan", ToolMode::Pan);
        tool_btn(ui::Icon::ZoomIn, "Zoom", ToolMode::BoxZoom);

        draw_separator();

        // ── Group 2: Analysis tools ──
        tool_btn(ui::Icon::Ruler, "Measure", ToolMode::Measure);
        tool_btn(ui::Icon::Comment, "Annotate", ToolMode::Annotate);
        tool_btn(ui::Icon::VectorSquare, "ROI", ToolMode::ROI);

        draw_separator();

        // ── Group 3: Data tools ──
        toggle_btn(ui::Icon::MapPin,
                   "Markers",
                   data_interaction_ && !data_interaction_->markers().empty(),
                   [this]()
                   {
                       if (data_interaction_)
                           data_interaction_->clear_markers();
                   });
        toggle_btn(
            ui::Icon::MagicWand,
            "Transform",
            custom_transform_dialog_.is_open(),
            [this]()
            {
                if (!custom_transform_dialog_.is_open())
                {
                    custom_transform_dialog_.set_fonts(font_body_, font_heading_, font_title_);
                    custom_transform_dialog_.open(current_figure_);
                }
            });

        draw_separator();

        // ── Group 4: Panels ──
        toggle_btn(ui::Icon::Database,
                   "Data",
                   panel_open_ && active_section_ == Section::DataEditor,
                   [this]()
                   {
                       bool was_active = panel_open_ && active_section_ == Section::DataEditor;
                       if (was_active)
                       {
                           panel_open_ = false;
                           layout_manager_->set_inspector_visible(false);
                       }
                       else
                       {
                           active_section_ = Section::DataEditor;
                           panel_open_     = true;
                           layout_manager_->set_inspector_visible(true);
                       }
                   });
        toggle_btn(ui::Icon::Timeline,
                   "Timeline",
                   show_timeline_,
                   [this]() { show_timeline_ = !show_timeline_; });

        draw_separator();

        // ── Group 5: Utilities ──
        toggle_btn(ui::Icon::Code,
                   "Python",
                   false,
                   [this]()
                   {
                       if (command_registry_)
                           command_registry_->execute("python.toggle_console");
                   });
        toggle_btn(ui::Icon::Help,
                   "Help",
                   false,
                   [this]()
                   {
                       if (command_registry_)
                           command_registry_->execute("help.show");
                   });
    }
    ImGui::End();
    ImGui::PopStyleColor(1);
    ImGui::PopStyleVar(5);
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
