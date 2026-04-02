#ifdef SPECTRA_USE_IMGUI

    #include "annotation.hpp"

    #include <algorithm>
    #include <cmath>
    #include <cstdio>
    #include <cstring>
    #include <imgui.h>
    #include <spectra/axes.hpp>

    #include "ui/theme/theme.hpp"

namespace spectra
{

void AnnotationManager::set_fonts(ImFont* body, ImFont* heading)
{
    font_body_    = body;
    font_heading_ = heading;
}

size_t AnnotationManager::add(float data_x, float data_y, const Axes* axes)
{
    Annotation a;
    a.data_x           = data_x;
    a.data_y           = data_y;
    a.axes             = axes;
    a.text             = "";
    a.editing          = true;
    const auto& accent = ui::theme().accent;
    a.color            = Color(accent.r, accent.g, accent.b, accent.a);
    a.offset_x         = 0.0f;
    a.offset_y         = -40.0f;

    // Cancel any other active edits
    for (auto& ann : annotations_)
        ann.editing = false;

    annotations_.push_back(std::move(a));
    size_t idx = annotations_.size() - 1;

    // Prepare edit buffer
    std::memset(edit_buf_, 0, sizeof(edit_buf_));

    return idx;
}

void AnnotationManager::remove(size_t index)
{
    if (index < annotations_.size())
    {
        annotations_.erase(annotations_.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

void AnnotationManager::remove_for_axes(const Axes* axes)
{
    annotations_.erase(std::remove_if(annotations_.begin(),
                                      annotations_.end(),
                                      [axes](const Annotation& a) { return a.axes == axes; }),
                       annotations_.end());
}

void AnnotationManager::clear()
{
    annotations_.clear();
    drag_active_ = false;
}

void AnnotationManager::cancel_editing()
{
    for (size_t i = 0; i < annotations_.size();)
    {
        if (annotations_[i].editing)
        {
            // If the annotation has no text, remove it
            if (annotations_[i].text.empty())
            {
                annotations_.erase(annotations_.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            annotations_[i].editing = false;
        }
        ++i;
    }
}

bool AnnotationManager::is_editing() const
{
    for (const auto& a : annotations_)
    {
        if (a.editing)
            return true;
    }
    return false;
}

void AnnotationManager::data_to_screen(float       data_x,
                                       float       data_y,
                                       const Rect& viewport,
                                       float       xlim_min,
                                       float       xlim_max,
                                       float       ylim_min,
                                       float       ylim_max,
                                       float&      screen_x,
                                       float&      screen_y)
{
    float x_range = xlim_max - xlim_min;
    float y_range = ylim_max - ylim_min;
    if (x_range == 0.0f)
        x_range = 1.0f;
    if (y_range == 0.0f)
        y_range = 1.0f;

    float norm_x = (data_x - xlim_min) / x_range;
    float norm_y = (data_y - ylim_min) / y_range;

    screen_x = viewport.x + norm_x * viewport.w;
    screen_y = viewport.y + (1.0f - norm_y) * viewport.h;
}

void AnnotationManager::begin_drag(size_t index, float screen_x, float screen_y)
{
    if (index >= annotations_.size())
        return;
    drag_active_         = true;
    drag_index_          = index;
    drag_start_mouse_x_  = screen_x;
    drag_start_mouse_y_  = screen_y;
    drag_start_offset_x_ = annotations_[index].offset_x;
    drag_start_offset_y_ = annotations_[index].offset_y;
}

void AnnotationManager::update_drag(float screen_x, float screen_y)
{
    if (!drag_active_ || drag_index_ >= annotations_.size())
        return;
    annotations_[drag_index_].offset_x = drag_start_offset_x_ + (screen_x - drag_start_mouse_x_);
    annotations_[drag_index_].offset_y = drag_start_offset_y_ + (screen_y - drag_start_mouse_y_);
}

void AnnotationManager::end_drag()
{
    drag_active_ = false;
}

int AnnotationManager::hit_test(float       screen_x,
                                float       screen_y,
                                const Rect& viewport,
                                float       xlim_min,
                                float       xlim_max,
                                float       ylim_min,
                                float       ylim_max,
                                float       radius_px,
                                const Axes* filter_axes) const
{
    ImFont*     font  = font_body_ ? font_body_ : ImGui::GetFont();
    const float fs    = font->LegacySize;
    const float pad_x = 10.0f;
    const float pad_y = 6.0f;

    for (size_t i = 0; i < annotations_.size(); ++i)
    {
        const auto& a = annotations_[i];
        if (filter_axes && a.axes != filter_axes)
            continue;

        float sx = 0.0f;
        float sy = 0.0f;
        data_to_screen(a.data_x,
                       a.data_y,
                       viewport,
                       xlim_min,
                       xlim_max,
                       ylim_min,
                       ylim_max,
                       sx,
                       sy);

        // Annotation box position
        float box_cx = sx + a.offset_x;
        float box_cy = sy + a.offset_y;

        // Compute box size from text
        const char* display_text = a.text.empty() ? "Click to type..." : a.text.c_str();
        ImVec2      text_sz      = font->CalcTextSizeA(fs, 400.0f, 0.0f, display_text);
        float       box_w        = text_sz.x + pad_x * 2.0f;
        float       box_h        = text_sz.y + pad_y * 2.0f;

        float box_left  = box_cx - box_w * 0.5f;
        float box_right = box_cx + box_w * 0.5f;
        float box_top   = box_cy - box_h * 0.5f;
        float box_bot   = box_cy + box_h * 0.5f;

        // Hit test the box
        constexpr float hit_margin = 4.0f;
        if (screen_x >= box_left - hit_margin && screen_x <= box_right + hit_margin
            && screen_y >= box_top - hit_margin && screen_y <= box_bot + hit_margin)
        {
            return static_cast<int>(i);
        }

        // Also hit test the anchor dot
        float dx = screen_x - sx;
        float dy = screen_y - sy;
        if (dx * dx + dy * dy <= radius_px * radius_px)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void AnnotationManager::draw(const Rect& viewport,
                             float       xlim_min,
                             float       xlim_max,
                             float       ylim_min,
                             float       ylim_max,
                             float       opacity,
                             const Axes* filter_axes,
                             ImDrawList* dl)
{
    if (annotations_.empty())
        return;

    const auto& colors   = (theme_mgr_ ? *theme_mgr_ : ui::ThemeManager::instance()).colors();
    ImDrawList* fg       = dl ? dl : ImGui::GetForegroundDrawList();
    ImFont*     font     = font_body_ ? font_body_ : ImGui::GetFont();
    const float fs       = font->LegacySize;
    const float pad_x    = 10.0f;
    const float pad_y    = 6.0f;
    const float corner_r = 8.0f;
    const float dot_r    = 3.5f;

    for (size_t i = 0; i < annotations_.size(); ++i)
    {
        auto& a = annotations_[i];

        if (filter_axes && a.axes != filter_axes)
            continue;

        float sx = 0.0f;
        float sy = 0.0f;
        data_to_screen(a.data_x,
                       a.data_y,
                       viewport,
                       xlim_min,
                       xlim_max,
                       ylim_min,
                       ylim_max,
                       sx,
                       sy);

        // Skip if anchor is outside viewport
        if (sx < viewport.x || sx > viewport.x + viewport.w || sy < viewport.y
            || sy > viewport.y + viewport.h)
            continue;

        // Box position (anchor + offset)
        float box_cx = sx + a.offset_x;
        float box_cy = sy + a.offset_y;

        // ── Inline editing via ImGui InputText ──────────────────────────
        if (a.editing)
        {
            // Copy current text to buffer if first frame
            if (edit_buf_[0] == '\0' && !a.text.empty())
            {
                std::strncpy(edit_buf_, a.text.c_str(), sizeof(edit_buf_) - 1);
            }

            // Draw anchor dot
            ImU32 dot_col = ImGui::ColorConvertFloat4ToU32(
                ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, opacity));
            fg->AddCircleFilled(ImVec2(sx, sy), dot_r + 1.0f, dot_col);

            // Draw leader line from anchor to box
            ImU32 line_col = ImGui::ColorConvertFloat4ToU32(
                ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 0.5f * opacity));
            fg->AddLine(ImVec2(sx, sy), ImVec2(box_cx, box_cy), line_col, 1.0f);

            // Draw ImGui input at the box position
            ImGui::SetNextWindowPos(ImVec2(box_cx - 100.0f, box_cy - 14.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(220.0f, 0.0f), ImGuiCond_Always);

            char win_id[64];
            std::snprintf(win_id, sizeof(win_id), "##ann_edit_%zu", i);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 4.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, corner_r);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
            ImGui::PushStyleColor(
                ImGuiCol_WindowBg,
                ImVec4(colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_Border,
                                  ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 0.6f));

            ImGuiWindowFlags win_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                                         | ImGuiWindowFlags_NoSavedSettings
                                         | ImGuiWindowFlags_AlwaysAutoResize
                                         | ImGuiWindowFlags_NoFocusOnAppearing;

            if (ImGui::Begin(win_id, nullptr, win_flags))
            {
                ImGui::PushFont(font);
                ImGui::PushItemWidth(200.0f);

                char input_id[64];
                std::snprintf(input_id, sizeof(input_id), "##ann_input_%zu", i);

                // Auto-focus on first frame
                if (ImGui::IsWindowAppearing())
                    ImGui::SetKeyboardFocusHere();

                bool committed = ImGui::InputText(
                    input_id,
                    edit_buf_,
                    sizeof(edit_buf_),
                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

                // Commit on Enter
                if (committed)
                {
                    a.text    = edit_buf_;
                    a.editing = false;
                    // Remove empty annotations
                    if (a.text.empty())
                    {
                        annotations_.erase(annotations_.begin() + static_cast<std::ptrdiff_t>(i));
                        --i;
                    }
                    std::memset(edit_buf_, 0, sizeof(edit_buf_));
                }

                // Cancel on Escape or clicking away
                if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                {
                    a.editing = false;
                    if (a.text.empty())
                    {
                        annotations_.erase(annotations_.begin() + static_cast<std::ptrdiff_t>(i));
                        --i;
                    }
                    std::memset(edit_buf_, 0, sizeof(edit_buf_));
                }

                ImGui::PopItemWidth();
                ImGui::PopFont();
            }
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);
            continue;
        }

        // ── Non-editing: render the annotation box ──────────────────────
        const char* display_text = a.text.c_str();
        if (a.text.empty())
            continue;   // Don't render empty annotations

        ImVec2 text_sz  = font->CalcTextSizeA(fs, 400.0f, 0.0f, display_text);
        float  box_w    = text_sz.x + pad_x * 2.0f;
        float  box_h    = text_sz.y + pad_y * 2.0f;
        float  box_left = box_cx - box_w * 0.5f;
        float  box_top  = box_cy - box_h * 0.5f;

        // ── Anchor dot ──────────────────────────────────────────────────
        ImU32 dot_bg = ImGui::ColorConvertFloat4ToU32(
            ImVec4(colors.bg_primary.r, colors.bg_primary.g, colors.bg_primary.b, opacity));
        fg->AddCircleFilled(ImVec2(sx, sy), dot_r + 1.5f, dot_bg);

        ImU32 dot_col =
            ImGui::ColorConvertFloat4ToU32(ImVec4(a.color.r, a.color.g, a.color.b, opacity));
        fg->AddCircleFilled(ImVec2(sx, sy), dot_r, dot_col);

        // ── Leader line from anchor to box center ───────────────────────
        ImU32 line_col = ImGui::ColorConvertFloat4ToU32(
            ImVec4(a.color.r, a.color.g, a.color.b, 0.35f * opacity));
        fg->AddLine(ImVec2(sx, sy), ImVec2(box_cx, box_cy), line_col, 1.0f);

        // ── Shadow ──────────────────────────────────────────────────────
        ImU32 shadow_col = ImGui::ColorConvertFloat4ToU32(ImVec4(0, 0, 0, 0.12f * opacity));
        fg->AddRectFilled(ImVec2(box_left + 1.0f, box_top + 2.0f),
                          ImVec2(box_left + box_w + 1.0f, box_top + box_h + 2.0f),
                          shadow_col,
                          corner_r);

        // ── Box background ──────────────────────────────────────────────
        ImU32 bg_col = ImGui::ColorConvertFloat4ToU32(ImVec4(colors.tooltip_bg.r,
                                                             colors.tooltip_bg.g,
                                                             colors.tooltip_bg.b,
                                                             colors.tooltip_bg.a * opacity));
        fg->AddRectFilled(ImVec2(box_left, box_top),
                          ImVec2(box_left + box_w, box_top + box_h),
                          bg_col,
                          corner_r);

        // ── Box border ──────────────────────────────────────────────────
        ImU32 box_border =
            ImGui::ColorConvertFloat4ToU32(ImVec4(colors.tooltip_border.r,
                                                  colors.tooltip_border.g,
                                                  colors.tooltip_border.b,
                                                  colors.tooltip_border.a * opacity));
        fg->AddRect(ImVec2(box_left, box_top),
                    ImVec2(box_left + box_w, box_top + box_h),
                    box_border,
                    corner_r,
                    0,
                    0.5f);

        // ── Color accent bar on left edge ───────────────────────────────
        ImU32 accent_col = ImGui::ColorConvertFloat4ToU32(
            ImVec4(a.color.r, a.color.g, a.color.b, 0.85f * opacity));
        fg->AddRectFilled(ImVec2(box_left, box_top + corner_r),
                          ImVec2(box_left + 2.5f, box_top + box_h - corner_r),
                          accent_col);

        // ── Text ────────────────────────────────────────────────────────
        ImU32 text_col = ImGui::ColorConvertFloat4ToU32(
            ImVec4(colors.text_primary.r, colors.text_primary.g, colors.text_primary.b, opacity));
        fg->AddText(font, fs, ImVec2(box_left + pad_x, box_top + pad_y), text_col, display_text);
    }
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
