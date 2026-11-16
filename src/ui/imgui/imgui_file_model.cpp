#ifdef SPECTRA_USE_IMGUI

// imgui_file_model.cpp — toasts, the Save / Don't Save / Cancel prompt and the
// crash-recovery prompt (DocumentSession, ToastQueue).

    #include <algorithm>
    #include <optional>
    #include <string>
    #include <vector>

    #include "imgui_integration_internal.hpp"
    #include "ui/overlay/toast_queue.hpp"
    #include "ui/theme/design_tokens.hpp"
    #include "ui/theme/icons.hpp"
    #include "ui/theme/theme.hpp"
    #include "ui/theme/theme_imgui.hpp"
    #include "ui/ui_interaction_log.hpp"
    #include "ui/workspace/document_session.hpp"

namespace spectra
{

namespace
{

// Shared look of the file-model modals: same chrome as Theme Settings.
struct ModalStyle
{
    explicit ModalStyle(const ui::ThemeColors& c)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            ImVec2(ui::tokens::SPACE_6, ui::tokens::SPACE_5));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::tokens::RADIUS_LG);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::tokens::RADIUS_MD);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(ui::tokens::SPACE_4, ui::tokens::SPACE_2));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ui::tokens::SPACE_2, 6.0f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ui::to_imvec4(c.bg_elevated, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ui::to_imvec4(c.border_subtle, 0.3f));
        ImGui::PushStyleColor(ImGuiCol_TitleBg, ui::to_imvec4(c.bg_elevated, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ui::to_imvec4(c.bg_elevated, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(c.text_primary, 1.0f));
    }
    ~ModalStyle()
    {
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(6);
    }
    ModalStyle(const ModalStyle&)            = delete;
    ModalStyle& operator=(const ModalStyle&) = delete;
};

enum class ButtonRole
{
    Primary,
    Secondary,
    Danger,
};

bool modal_button(const ui::ThemeColors& c, const char* label, ButtonRole role, float width)
{
    ImVec4 bg;
    ImVec4 hover;
    ImVec4 text;
    switch (role)
    {
        case ButtonRole::Primary:
            bg    = ui::to_imvec4(c.accent, 1.0f);
            hover = ui::to_imvec4(c.accent_hover, 1.0f);
            text  = ui::to_imvec4(c.text_inverse, 1.0f);
            break;
        case ButtonRole::Danger:
            bg    = ui::to_imvec4(c.bg_tertiary, 0.8f);
            hover = ui::to_imvec4(c.error, 0.25f);
            text  = ui::to_imvec4(c.error, 1.0f);
            break;
        case ButtonRole::Secondary:
        default:
            bg    = ui::to_imvec4(c.bg_tertiary, 0.8f);
            hover = ui::to_imvec4(c.accent_subtle, 0.6f);
            text  = ui::to_imvec4(c.text_primary, 1.0f);
            break;
    }
    ImGui::PushStyleColor(ImGuiCol_Button, bg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, hover);
    ImGui::PushStyleColor(ImGuiCol_Text, text);
    const bool pressed = ImGui::Button(label, ImVec2(width, 0.0f));
    ImGui::PopStyleColor(4);
    return pressed;
}

const ui::Color& toast_color(const ui::ThemeColors& c, ui::ToastKind kind)
{
    switch (kind)
    {
        case ui::ToastKind::Success:
            return c.success;
        case ui::ToastKind::Warning:
            return c.warning;
        case ui::ToastKind::Error:
            return c.error;
        case ui::ToastKind::Info:
        default:
            return c.info;
    }
}

ui::Icon toast_icon(ui::ToastKind kind)
{
    switch (kind)
    {
        case ui::ToastKind::Success:
            return ui::Icon::Check;
        case ui::ToastKind::Warning:
            return ui::Icon::Warning;
        case ui::ToastKind::Error:
            return ui::Icon::Error;
        case ui::ToastKind::Info:
        default:
            return ui::Icon::Info;
    }
}

}   // namespace

void ImGuiIntegration::draw_file_model_overlays()
{
    if (document_)
    {
        document_->check_recovery();
        draw_recovery_prompt();
        draw_unsaved_changes_prompt();
    }
    draw_toasts();
}

// ─── Toasts ──────────────────────────────────────────────────────────────────

void ImGuiIntegration::draw_toasts()
{
    if (!toasts_)
        return;
    ImGuiIO& io = ImGui::GetIO();
    toasts_->update(io.DeltaTime);
    if (toasts_->empty())
        return;

    const auto& c        = theme_colors();
    const float width    = 340.0f;
    const float margin   = ui::tokens::SPACE_4;
    const float pad      = ui::tokens::SPACE_3;
    const float accent_w = 3.0f;
    const float icon_sz  = ImGui::GetTextLineHeight();
    float       bottom   = io.DisplaySize.y - margin;
    if (layout_manager_)
    {
        const Rect sb = layout_manager_->status_bar_rect();
        if (sb.h > 0.0f)
            bottom = sb.y - margin;
    }

    // Newest at the bottom, stacked upwards.
    struct Card
    {
        uint64_t id;
        float    h;
    };
    std::vector<Card> cards;
    const float       text_w = width - pad * 2.0f - accent_w - icon_sz - ui::tokens::SPACE_2;
    for (const auto& t : toasts_->toasts())
    {
        float h = pad * 2.0f + ImGui::GetTextLineHeight();
        if (!t.detail.empty())
            h += ImGui::CalcTextSize(t.detail.c_str(), nullptr, false, text_w).y + 2.0f;
        if (!t.action_label.empty())
            h += ImGui::GetFrameHeight() + ui::tokens::SPACE_1;
        cards.push_back({t.id, h});
    }
    float total_h = 0.0f;
    for (const auto& card : cards)
        total_h += card.h + ui::tokens::SPACE_2;

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - width - margin, bottom - total_h));
    ImGui::SetNextWindowSize(ImVec2(width, total_h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings
                                   | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav
                                   | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::Begin("##spectra_toasts", nullptr, flags))
    {
        // Docked panels are separate windows that get focus; keep the stack above them.
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        ImDrawList* dl       = ImGui::GetWindowDrawList();
        uint64_t    dismiss  = 0;
        uint64_t    activate = 0;
        float       y        = ImGui::GetWindowPos().y;
        const float x        = ImGui::GetWindowPos().x;
        size_t      i        = 0;
        for (const auto& t : toasts_->toasts())
        {
            const float h = cards[i++].h;
            // Fade in over 0.15 s; self-dismissing toasts fade out in their last 0.4 s.
            float alpha = std::clamp(t.age / ui::ToastQueue::FADE_IN, 0.0f, 1.0f);
            if (t.lifetime > 0.0f)
                alpha = std::min(alpha, std::clamp((t.lifetime - t.age) / 0.4f, 0.0f, 1.0f));

            const ImVec2     p0(x, y);
            const ImVec2     p1(x + width, y + h);
            const ui::Color& kc = toast_color(c, t.kind);

            ImGui::SetCursorScreenPos(p0);
            ImGui::PushID(static_cast<int>(t.id));
            ImGui::InvisibleButton("##toast", ImVec2(width, h));
            const bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked())
                dismiss = t.id;

            for (int s = 1; s <= 3; ++s)
            {
                const float off = static_cast<float>(s) * 2.0f;
                dl->AddRectFilled(ImVec2(p0.x + off * 0.3f, p0.y + off),
                                  ImVec2(p1.x + off * 0.3f, p1.y + off),
                                  IM_COL32(0, 0, 0, static_cast<int>(18 * alpha)),
                                  ui::tokens::RADIUS_MD + off);
            }
            dl->AddRectFilled(p0,
                              p1,
                              ui::to_imu32(hovered ? c.bg_tertiary : c.bg_elevated, 0.98f * alpha),
                              ui::tokens::RADIUS_MD);
            dl->AddRect(p0, p1, ui::to_imu32(c.border_subtle, 0.5f * alpha), ui::tokens::RADIUS_MD);
            dl->AddRectFilled(p0,
                              ImVec2(p0.x + accent_w, p1.y),
                              ui::to_imu32(kc, alpha),
                              ui::tokens::RADIUS_MD,
                              ImDrawFlags_RoundCornersLeft);

            const float tx = p0.x + accent_w + pad;
            float       ty = p0.y + pad;
            if (font_icon_)
            {
                dl->AddText(font_icon_,
                            icon_sz * 0.9f,
                            ImVec2(tx, ty + 1.0f),
                            ui::to_imu32(kc, alpha),
                            ui::icon_str(toast_icon(t.kind)));
            }
            const float text_x = tx + icon_sz + ui::tokens::SPACE_2;
            dl->AddText(ImVec2(text_x, ty), ui::to_imu32(c.text_primary, alpha), t.title.c_str());
            if (t.lifetime <= 0.0f && hovered)
            {
                const char* hint = "click to dismiss";
                const float hw   = ImGui::CalcTextSize(hint).x;
                dl->AddText(ImVec2(p1.x - pad - hw, ty),
                            ui::to_imu32(c.text_tertiary, alpha),
                            hint);
            }
            ty += ImGui::GetTextLineHeight() + 2.0f;
            if (!t.detail.empty())
            {
                dl->AddText(nullptr,
                            0.0f,
                            ImVec2(text_x, ty),
                            ui::to_imu32(c.text_secondary, alpha),
                            t.detail.c_str(),
                            nullptr,
                            text_w);
                ty += ImGui::CalcTextSize(t.detail.c_str(), nullptr, false, text_w).y;
            }
            if (!t.action_label.empty())
            {
                ImGui::SetCursorScreenPos(ImVec2(text_x, ty + ui::tokens::SPACE_1));
                if (modal_button(c, t.action_label.c_str(), ButtonRole::Secondary, 0.0f))
                    activate = t.id;
            }
            ImGui::PopID();
            y += h + ui::tokens::SPACE_2;
        }
        if (activate)
            toasts_->activate(activate);
        else if (dismiss)
            toasts_->dismiss(dismiss);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

// ─── Save / Don't Save / Cancel ──────────────────────────────────────────────

void ImGuiIntegration::draw_unsaved_changes_prompt()
{
    static constexpr const char* k_popup_id = "Unsaved Changes##spectra_doc";
    UnsavedChangesPrompt&        prompt     = document_->prompt();
    if (prompt.is_open() && !ImGui::IsPopupOpen(k_popup_id))
        ImGui::OpenPopup(k_popup_id);
    if (!ImGui::IsPopupOpen(k_popup_id))
        return;

    const auto& c  = theme_colors();
    ImGuiIO&    io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(420.0f, 0.0f), ImVec2(560.0f, 600.0f));

    ModalStyle             style(c);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse
                                   | ImGuiWindowFlags_AlwaysAutoResize
                                   | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::BeginPopupModal(k_popup_id, nullptr, flags))
        return;

    using Choice = UnsavedChangesPrompt::Choice;
    std::optional<Choice> choice;
    if (!prompt.is_open())
    {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    std::string headline;
    std::string body;
    switch (prompt.reason())
    {
        case UnsavedChangesPrompt::Reason::CloseFigure:
        {
            std::string what = prompt.subject();
            if (what.empty())
            {
                const FigureId id =
                    dock_system_ ? dock_system_->active_figure_index() : INVALID_FIGURE_ID;
                what = get_figure_title_ && id != INVALID_FIGURE_ID
                           ? "\"" + get_figure_title_(id) + "\""
                           : std::string("this figure");
            }
            headline = "Close " + what + " without saving?";
            body     = "It has changes that are not saved in " + document_->display_name()
                   + ". Save the workspace first, or close it and lose them.";
            break;
        }
        case UnsavedChangesPrompt::Reason::Open:
            headline = "Save changes to " + document_->display_name() + "?";
            body     = "Opening another workspace replaces this window.";
            break;
        case UnsavedChangesPrompt::Reason::Quit:
        default:
            headline = "Save changes to " + document_->display_name() + " before closing?";
            body     = "Your changes will be lost if you don't save them.";
            break;
    }

    if (font_icon_)
    {
        ImGui::PushFont(font_icon_);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(c.warning, 1.0f));
        ImGui::TextUnformatted(ui::icon_str(ui::Icon::Warning));
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::SameLine();
    }
    ImGui::PushFont(font_title_ ? font_title_ : ImGui::GetFont());
    ImGui::TextUnformatted(headline.c_str());
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(c.text_secondary, 1.0f));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 400.0f);
    ImGui::TextUnformatted(body.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, ui::tokens::SPACE_3));

    const float bw = 110.0f;
    if (modal_button(c, "Don't Save", ButtonRole::Danger, bw))
        choice = Choice::DontSave;
    ImGui::SameLine();
    const float right =
        ImGui::GetContentRegionAvail().x - (bw * 2.0f + ImGui::GetStyle().ItemSpacing.x);
    if (right > 0.0f)
    {
        ImGui::Dummy(ImVec2(right, 0.0f));
        ImGui::SameLine();
    }
    if (modal_button(c, "Cancel", ButtonRole::Secondary, bw))
        choice = Choice::Cancel;
    ImGui::SameLine();
    const char* save_label = document_->path().empty() ? "Save As..." : "Save";
    if (modal_button(c, save_label, ButtonRole::Primary, bw))
        choice = Choice::Save;
    ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(c.text_tertiary, 1.0f));
    ImGui::TextUnformatted("Enter: save    Esc: cancel");
    ImGui::PopStyleColor();

    if (!choice && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            choice = Choice::Cancel;
        else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)
                 || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
            choice = Choice::Save;
    }

    if (choice)
    {
        ui::log_ui_action("dialog",
                          *choice == Choice::Save       ? "Unsaved: Save"
                          : *choice == Choice::DontSave ? "Unsaved: Don't Save"
                                                        : "Unsaved: Cancel",
                          "ok");
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        // After EndPopup: Save may open a native dialog and closing may tear
        // the window down.
        document_->resolve_prompt(*choice);
        return;
    }
    ImGui::EndPopup();
}

// ─── Crash recovery ──────────────────────────────────────────────────────────

void ImGuiIntegration::draw_recovery_prompt()
{
    static constexpr const char* k_popup_id = "Restore Unsaved Work##spectra_recovery";
    if (document_->recovery_pending() && !ImGui::IsPopupOpen(k_popup_id)
        && !document_->prompt().is_open())
        ImGui::OpenPopup(k_popup_id);
    if (!ImGui::IsPopupOpen(k_popup_id))
        return;

    const auto& c  = theme_colors();
    ImGuiIO&    io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    ModalStyle             style(c);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse
                                   | ImGuiWindowFlags_AlwaysAutoResize
                                   | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::BeginPopupModal(k_popup_id, nullptr, flags))
        return;
    if (!document_->recovery_pending())
    {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::PushFont(font_title_ ? font_title_ : ImGui::GetFont());
    ImGui::TextUnformatted("Restore unsaved work?");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 2));
    const size_t n = document_->recovery_files().size();
    ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(c.text_secondary, 1.0f));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 400.0f);
    ImGui::Text("Spectra did not close normally last time. %zu autosaved workspace%s "
                "can be restored into this window.",
                n,
                n == 1 ? "" : "s");
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, ui::tokens::SPACE_3));

    bool restore = false;
    bool discard = false;
    if (modal_button(c, "Discard", ButtonRole::Danger, 110.0f))
        discard = true;
    ImGui::SameLine();
    if (modal_button(c, "Restore", ButtonRole::Primary, 110.0f))
        restore = true;
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false))
        restore = true;

    if (restore || discard)
    {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        if (restore)
            document_->restore_recovery();
        else
            document_->discard_recovery();
        return;
    }
    ImGui::EndPopup();
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
