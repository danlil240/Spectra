#include "tab_bar.hpp"
#include "theme.hpp"
#include "design_tokens.hpp"

#ifdef PLOTIX_USE_IMGUI
#include <imgui.h>
#include <imgui_internal.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>

namespace plotix {

TabBar::TabBar() {
    // Start with one default tab
    add_tab("Figure 1", false);  // Can't close the first tab
}

size_t TabBar::add_tab(const std::string& title, bool can_close) {
    tabs_.emplace_back(title, can_close);
    size_t new_index = tabs_.size() - 1;
    
    // Auto-activate the new tab
    set_active_tab(new_index);
    
    return new_index;
}

void TabBar::remove_tab(size_t index) {
    if (index >= tabs_.size()) {
        return;
    }
    
    if (!tabs_[index].can_close) {
        return;  // Can't close this tab
    }
    
    // Notify callback before removal
    if (on_tab_close_) {
        on_tab_close_(index);
    }
    
    tabs_.erase(tabs_.begin() + index);
    
    // Adjust active tab if necessary
    if (active_tab_ >= tabs_.size()) {
        active_tab_ = tabs_.size() > 0 ? tabs_.size() - 1 : 0;
    } else if (active_tab_ > index) {
        active_tab_--;
    }
    
    // Reset interaction state
    hovered_tab_ = SIZE_MAX;
    hovered_close_ = SIZE_MAX;
    is_dragging_ = false;
}

void TabBar::set_tab_title(size_t index, const std::string& title) {
    if (index < tabs_.size()) {
        tabs_[index].title = title;
    }
}

const std::string& TabBar::get_tab_title(size_t index) const {
    static const std::string empty = "";
    return (index < tabs_.size()) ? tabs_[index].title : empty;
}

void TabBar::set_active_tab(size_t index) {
    if (index < tabs_.size() && index != active_tab_) {
        active_tab_ = index;
        
        // Notify callback
        if (on_tab_change_) {
            on_tab_change_(active_tab_);
        }
    }
}

void TabBar::draw(const Rect& bounds) {
#ifdef PLOTIX_USE_IMGUI
    if (tabs_.empty()) {
        return;
    }
    
    // Set up drawing context
    ImGui::SetCursorScreenPos(ImVec2(bounds.x, bounds.y));
    ImGui::PushClipRect(ImVec2(bounds.x, bounds.y), 
                       ImVec2(bounds.x + bounds.w, bounds.y + bounds.h), 
                       true);
    
    // Handle input interactions
    handle_input(bounds);
    
    // Draw the tabs
    draw_tabs(bounds);
    
    // Draw add button if we have room
    if (tabs_.size() < 20) {  // Reasonable limit
        draw_add_button(bounds);
    }
    
    // Draw scroll buttons if needed
    if (needs_scroll_buttons(bounds)) {
        draw_scroll_buttons(bounds);
    }
    
    // Draw context menu (must be outside clip rect)
    ImGui::PopClipRect();
    draw_context_menu();
#endif
}

void TabBar::set_tab_modified(size_t index, bool modified) {
    if (index < tabs_.size()) {
        tabs_[index].is_modified = modified;
    }
}

bool TabBar::is_tab_modified(size_t index) const {
    if (index < tabs_.size()) {
        return tabs_[index].is_modified;
    }
    return false;
}

bool TabBar::is_tab_hovered(size_t index) const {
    return hovered_tab_ == index;
}

bool TabBar::is_close_button_hovered(size_t index) const {
    return hovered_close_ == index;
}

void TabBar::handle_input(const Rect& bounds) {
#ifdef PLOTIX_USE_IMGUI
    ImVec2 mouse_pos = ImGui::GetMousePos();
    bool mouse_in_bounds = (mouse_pos.x >= bounds.x && mouse_pos.x < bounds.x + bounds.w &&
                           mouse_pos.y >= bounds.y && mouse_pos.y < bounds.y + bounds.h);
    
    if (!mouse_in_bounds) {
        hovered_tab_ = SIZE_MAX;
        hovered_close_ = SIZE_MAX;
        return;
    }
    
    // Compute tab layouts
    auto layouts = compute_tab_layouts(bounds);
    
    // Check hover state
    hovered_tab_ = get_tab_at_position(mouse_pos, layouts);
    hovered_close_ = get_close_button_at_position(mouse_pos, layouts);
    
    // Handle mouse clicks
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (hovered_close_ < tabs_.size()) {
            // Close button clicked
            remove_tab(hovered_close_);
        } else if (hovered_tab_ < tabs_.size()) {
            // Tab clicked
            set_active_tab(hovered_tab_);
            start_drag(hovered_tab_, mouse_pos.x);
        }
    }
    
    // Handle right-click context menu
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        if (hovered_tab_ < tabs_.size()) {
            context_menu_tab_ = hovered_tab_;
            context_menu_open_ = true;
            ImGui::OpenPopup("##tab_context_menu");
        }
    }
    
    // Handle mouse release (end drag)
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && is_dragging_) {
        end_drag();
    }
    
    // Handle mouse drag
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && is_dragging_) {
        update_drag(mouse_pos.x);
    }
#endif
}

static ImU32 to_imcol(const ui::Color& c, float alpha_override = -1.0f) {
    float a = alpha_override >= 0.0f ? alpha_override : c.a;
    return IM_COL32(uint8_t(c.r*255), uint8_t(c.g*255), uint8_t(c.b*255), uint8_t(a*255));
}

void TabBar::draw_tabs(const Rect& bounds) {
#ifdef PLOTIX_USE_IMGUI
    auto layouts = compute_tab_layouts(bounds);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const auto& colors = ui::theme();
    
    // Bottom border line across the full tab bar
    draw_list->AddLine(
        ImVec2(bounds.x, bounds.y + bounds.h - 1),
        ImVec2(bounds.x + bounds.w, bounds.y + bounds.h - 1),
        to_imcol(colors.border_subtle), 1.0f);
    
    for (size_t i = 0; i < layouts.size(); ++i) {
        const auto& layout = layouts[i];
        if (!layout.is_visible) {
            continue;
        }
        
        const auto& tab = tabs_[i];
        bool is_active = (i == active_tab_);
        bool is_hovered = (i == hovered_tab_);
        bool is_dragged = (is_dragging_ && i == dragged_tab_);
        
        // Tab background color from theme
        ImU32 bg_color;
        if (is_active) {
            bg_color = to_imcol(colors.bg_tertiary);
        } else if (is_hovered) {
            bg_color = to_imcol(colors.accent_subtle);
        } else {
            bg_color = to_imcol(colors.bg_secondary);
        }
        
        // Dragged tab gets elevated look
        if (is_dragged) {
            bg_color = to_imcol(colors.bg_elevated);
        }
        
        float inset = 1.0f;
        ImVec2 tl(layout.bounds.x + inset, layout.bounds.y + 4);
        ImVec2 br(layout.bounds.x + layout.bounds.w - inset, layout.bounds.y + layout.bounds.h);
        
        // Draw tab background with rounded top corners
        draw_list->AddRectFilled(tl, br, bg_color, ui::tokens::RADIUS_SM, ImDrawFlags_RoundCornersTop);
        
        // Active tab: accent underline instead of border
        if (is_active) {
            draw_list->AddLine(
                ImVec2(tl.x + 4, br.y - 1),
                ImVec2(br.x - 4, br.y - 1),
                to_imcol(colors.accent), 2.0f);
        }
        
        // Tab title
        ImVec2 text_size = ImGui::CalcTextSize(tab.title.c_str());
        ImVec2 text_pos(
            layout.bounds.x + TAB_PADDING,
            layout.bounds.y + (layout.bounds.h - text_size.y) * 0.5f
        );
        
        ImU32 text_color = is_active ? to_imcol(colors.text_primary) : to_imcol(colors.text_secondary);
        draw_list->AddText(text_pos, text_color, tab.title.c_str());
        
        // Close button (if enabled, only show on hover or active)
        if (tab.can_close && (is_active || is_hovered)) {
            bool close_hovered = (i == hovered_close_);
            ImU32 close_color = close_hovered
                ? to_imcol(colors.error)
                : to_imcol(colors.text_tertiary);
            
            ImVec2 close_center(
                layout.close_bounds.x + layout.close_bounds.w * 0.5f,
                layout.close_bounds.y + layout.close_bounds.h * 0.5f
            );
            
            // Close button hover background
            if (close_hovered) {
                draw_list->AddCircleFilled(close_center, CLOSE_BUTTON_SIZE * 0.5f,
                    to_imcol(colors.error, 0.15f));
            }
            
            // Draw 'X'
            float sz = CLOSE_BUTTON_SIZE * 0.3f;
            draw_list->AddLine(
                ImVec2(close_center.x - sz, close_center.y - sz),
                ImVec2(close_center.x + sz, close_center.y + sz),
                close_color, 1.5f);
            draw_list->AddLine(
                ImVec2(close_center.x - sz, close_center.y + sz),
                ImVec2(close_center.x + sz, close_center.y - sz),
                close_color, 1.5f);
        }
        
        // Modified indicator dot
        if (tab.is_modified) {
            ImVec2 dot_pos(layout.bounds.x + 8, layout.bounds.y + 10);
            draw_list->AddCircleFilled(dot_pos, 3.0f, to_imcol(colors.warning));
        }
    }
#endif
}

void TabBar::draw_add_button(const Rect& bounds) {
#ifdef PLOTIX_USE_IMGUI
    const auto& colors = ui::theme();
    auto layouts = compute_tab_layouts(bounds);
    
    // Position add button after the last tab
    float last_tab_end = bounds.x;
    if (!layouts.empty()) {
        auto& last = layouts.back();
        last_tab_end = last.bounds.x + last.bounds.w;
    }
    
    float btn_x = last_tab_end + 4.0f;
    float btn_y = bounds.y + 4.0f;
    float btn_w = ADD_BUTTON_WIDTH - 8.0f;
    float btn_h = bounds.h - 8.0f;
    
    // Don't draw if it would overflow
    if (btn_x + btn_w > bounds.x + bounds.w - 4.0f) return;
    
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 mouse_pos = ImGui::GetMousePos();
    bool hovered = (mouse_pos.x >= btn_x && mouse_pos.x < btn_x + btn_w &&
                    mouse_pos.y >= btn_y && mouse_pos.y < btn_y + btn_h);
    
    ImU32 bg_color = hovered ? to_imcol(colors.accent_subtle) : to_imcol(colors.bg_secondary, 0.0f);
    draw_list->AddRectFilled(
        ImVec2(btn_x, btn_y),
        ImVec2(btn_x + btn_w, btn_y + btn_h),
        bg_color, ui::tokens::RADIUS_SM);
    
    // Plus sign
    ImVec2 center(btn_x + btn_w * 0.5f, btn_y + btn_h * 0.5f);
    ImU32 plus_color = hovered ? to_imcol(colors.accent) : to_imcol(colors.text_tertiary);
    float sz = 6.0f;
    draw_list->AddLine(ImVec2(center.x - sz, center.y), ImVec2(center.x + sz, center.y), plus_color, 1.5f);
    draw_list->AddLine(ImVec2(center.x, center.y - sz), ImVec2(center.x, center.y + sz), plus_color, 1.5f);
    
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (on_tab_add_) {
            on_tab_add_();
        }
    }
#endif
}

std::vector<TabBar::TabLayout> TabBar::compute_tab_layouts(const Rect& bounds) const {
    std::vector<TabLayout> layouts;
    layouts.reserve(tabs_.size());
    
    float current_x = bounds.x + scroll_offset_;
    float available_width = bounds.w;
    
    for (size_t i = 0; i < tabs_.size(); ++i) {
        TabLayout layout;
        
        // Calculate tab width based on title
        ImVec2 text_size = ImGui::CalcTextSize(tabs_[i].title.c_str());
        float tab_width = std::clamp(
            text_size.x + TAB_PADDING * 2 + (tabs_[i].can_close ? CLOSE_BUTTON_SIZE : 0),
            TAB_MIN_WIDTH, TAB_MAX_WIDTH);
        
        layout.bounds = Rect{
            current_x,
            bounds.y,
            tab_width,
            TAB_HEIGHT
        };
        
        // Close button bounds (right side of tab)
        if (tabs_[i].can_close) {
            layout.close_bounds = Rect{
                current_x + tab_width - CLOSE_BUTTON_SIZE - 4,
                bounds.y + (TAB_HEIGHT - CLOSE_BUTTON_SIZE) * 0.5f,
                CLOSE_BUTTON_SIZE,
                CLOSE_BUTTON_SIZE
            };
        } else {
            layout.close_bounds = Rect{0, 0, 0, 0};
        }
        
        // Check visibility
        layout.is_visible = (current_x + tab_width > bounds.x) && (current_x < bounds.x + available_width);
        layout.is_clipped = (current_x < bounds.x) || (current_x + tab_width > bounds.x + available_width);
        
        layouts.push_back(layout);
        current_x += tab_width;
    }
    
    return layouts;
}

size_t TabBar::get_tab_at_position(const ImVec2& pos, const std::vector<TabLayout>& layouts) const {
    for (size_t i = 0; i < layouts.size(); ++i) {
        const auto& layout = layouts[i];
        if (!layout.is_visible) {
            continue;
        }
        
        if (pos.x >= layout.bounds.x && pos.x < layout.bounds.x + layout.bounds.w &&
            pos.y >= layout.bounds.y && pos.y < layout.bounds.y + layout.bounds.h) {
            return i;
        }
    }
    return SIZE_MAX;
}

size_t TabBar::get_close_button_at_position(const ImVec2& pos, const std::vector<TabLayout>& layouts) const {
    for (size_t i = 0; i < layouts.size(); ++i) {
        const auto& layout = layouts[i];
        if (!layout.is_visible || !tabs_[i].can_close) {
            continue;
        }
        
        if (pos.x >= layout.close_bounds.x && pos.x < layout.close_bounds.x + layout.close_bounds.w &&
            pos.y >= layout.close_bounds.y && pos.y < layout.close_bounds.y + layout.close_bounds.h) {
            return i;
        }
    }
    return SIZE_MAX;
}

void TabBar::start_drag(size_t tab_index, float mouse_x) {
    is_dragging_ = true;
    dragged_tab_ = tab_index;
    drag_offset_x_ = mouse_x;
}

void TabBar::update_drag(float mouse_x) {
#ifdef PLOTIX_USE_IMGUI
    if (dragged_tab_ >= tabs_.size()) return;
    
    float delta = mouse_x - drag_offset_x_;
    if (std::abs(delta) < 5.0f) return;  // Dead zone
    
    // Check if we should swap with adjacent tab
    if (delta > 30.0f && dragged_tab_ + 1 < tabs_.size()) {
        std::swap(tabs_[dragged_tab_], tabs_[dragged_tab_ + 1]);
        if (active_tab_ == dragged_tab_) active_tab_++;
        else if (active_tab_ == dragged_tab_ + 1) active_tab_--;
        if (on_tab_reorder_) on_tab_reorder_(dragged_tab_, dragged_tab_ + 1);
        dragged_tab_++;
        drag_offset_x_ = mouse_x;
    } else if (delta < -30.0f && dragged_tab_ > 0) {
        std::swap(tabs_[dragged_tab_], tabs_[dragged_tab_ - 1]);
        if (active_tab_ == dragged_tab_) active_tab_--;
        else if (active_tab_ == dragged_tab_ - 1) active_tab_++;
        if (on_tab_reorder_) on_tab_reorder_(dragged_tab_, dragged_tab_ - 1);
        dragged_tab_--;
        drag_offset_x_ = mouse_x;
    }
#else
    (void)mouse_x;
#endif
}

void TabBar::end_drag() {
    is_dragging_ = false;
    dragged_tab_ = SIZE_MAX;
}

bool TabBar::needs_scroll_buttons(const Rect& bounds) const {
    // Check if total tab width exceeds available space
    float total_width = 0.0f;
    for (const auto& tab : tabs_) {
        ImVec2 text_size = ImGui::CalcTextSize(tab.title.c_str());
        float tab_width = std::clamp(
            text_size.x + TAB_PADDING * 2 + (tab.can_close ? CLOSE_BUTTON_SIZE : 0),
            TAB_MIN_WIDTH, TAB_MAX_WIDTH);
        total_width += tab_width;
    }
    
    return total_width > bounds.w;
}

void TabBar::draw_scroll_buttons(const Rect& bounds) {
#ifdef PLOTIX_USE_IMGUI
    const auto& colors = ui::theme();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    float btn_w = 20.0f;
    float btn_h = bounds.h - 4.0f;
    ImVec2 mouse_pos = ImGui::GetMousePos();
    
    // Left scroll button
    if (scroll_offset_ < 0.0f) {
        float lx = bounds.x;
        float ly = bounds.y + 2.0f;
        bool lhov = (mouse_pos.x >= lx && mouse_pos.x < lx + btn_w &&
                     mouse_pos.y >= ly && mouse_pos.y < ly + btn_h);
        draw_list->AddRectFilled(ImVec2(lx, ly), ImVec2(lx + btn_w, ly + btn_h),
            lhov ? to_imcol(colors.accent_subtle) : to_imcol(colors.bg_elevated),
            ui::tokens::RADIUS_SM);
        ImVec2 arrow_center(lx + btn_w * 0.5f, ly + btn_h * 0.5f);
        draw_list->AddTriangleFilled(
            ImVec2(arrow_center.x + 4, arrow_center.y - 5),
            ImVec2(arrow_center.x + 4, arrow_center.y + 5),
            ImVec2(arrow_center.x - 4, arrow_center.y),
            to_imcol(lhov ? colors.accent : colors.text_secondary));
        if (lhov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            scroll_offset_ = std::min(scroll_offset_ + 100.0f, 0.0f);
        }
    }
    
    // Right scroll button
    {
        float rx = bounds.x + bounds.w - btn_w;
        float ry = bounds.y + 2.0f;
        bool rhov = (mouse_pos.x >= rx && mouse_pos.x < rx + btn_w &&
                     mouse_pos.y >= ry && mouse_pos.y < ry + btn_h);
        draw_list->AddRectFilled(ImVec2(rx, ry), ImVec2(rx + btn_w, ry + btn_h),
            rhov ? to_imcol(colors.accent_subtle) : to_imcol(colors.bg_elevated),
            ui::tokens::RADIUS_SM);
        ImVec2 arrow_center(rx + btn_w * 0.5f, ry + btn_h * 0.5f);
        draw_list->AddTriangleFilled(
            ImVec2(arrow_center.x - 4, arrow_center.y - 5),
            ImVec2(arrow_center.x - 4, arrow_center.y + 5),
            ImVec2(arrow_center.x + 4, arrow_center.y),
            to_imcol(rhov ? colors.accent : colors.text_secondary));
        if (rhov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            scroll_offset_ -= 100.0f;
        }
    }
#else
    (void)bounds;
#endif
}

void TabBar::draw_context_menu() {
#ifdef PLOTIX_USE_IMGUI
    if (ImGui::BeginPopup("##tab_context_menu")) {
        if (context_menu_tab_ < tabs_.size()) {
            const auto& tab = tabs_[context_menu_tab_];

            // Rename
            if (ImGui::MenuItem("Rename...")) {
                renaming_tab_ = true;
                rename_tab_index_ = context_menu_tab_;
                strncpy(rename_buffer_, tab.title.c_str(), sizeof(rename_buffer_) - 1);
                rename_buffer_[sizeof(rename_buffer_) - 1] = '\0';
            }

            // Duplicate
            if (ImGui::MenuItem("Duplicate")) {
                if (on_tab_duplicate_) {
                    on_tab_duplicate_(context_menu_tab_);
                }
            }

            ImGui::Separator();

            // Close
            if (tab.can_close && tabs_.size() > 1) {
                if (ImGui::MenuItem("Close")) {
                    remove_tab(context_menu_tab_);
                }
            }

            // Close Others
            if (tabs_.size() > 1) {
                if (ImGui::MenuItem("Close Others")) {
                    if (on_tab_close_all_except_) {
                        on_tab_close_all_except_(context_menu_tab_);
                    }
                }
            }

            // Close to the Right
            if (context_menu_tab_ + 1 < tabs_.size()) {
                if (ImGui::MenuItem("Close to the Right")) {
                    if (on_tab_close_to_right_) {
                        on_tab_close_to_right_(context_menu_tab_);
                    }
                }
            }
        }
        ImGui::EndPopup();
    } else {
        context_menu_open_ = false;
        context_menu_tab_ = SIZE_MAX;
    }

    // Rename popup
    if (renaming_tab_ && rename_tab_index_ < tabs_.size()) {
        ImGui::OpenPopup("##tab_rename_popup");
        renaming_tab_ = false;
    }
    if (ImGui::BeginPopup("##tab_rename_popup")) {
        ImGui::Text("Rename tab:");
        bool enter_pressed = ImGui::InputText("##rename_input", rename_buffer_,
            sizeof(rename_buffer_), ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere(-1);
        }
        if (enter_pressed || ImGui::Button("OK")) {
            std::string new_title(rename_buffer_);
            if (!new_title.empty() && rename_tab_index_ < tabs_.size()) {
                tabs_[rename_tab_index_].title = new_title;
                if (on_tab_rename_) {
                    on_tab_rename_(rename_tab_index_, new_title);
                }
            }
            rename_tab_index_ = SIZE_MAX;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            rename_tab_index_ = SIZE_MAX;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
#endif
}

void TabBar::scroll_to_tab(size_t index) {
#ifdef PLOTIX_USE_IMGUI
    if (index >= tabs_.size()) return;
    // Compute approximate position of the target tab
    float x = 0.0f;
    for (size_t i = 0; i < index; ++i) {
        ImVec2 ts = ImGui::CalcTextSize(tabs_[i].title.c_str());
        float tw = std::clamp(
            ts.x + TAB_PADDING * 2 + (tabs_[i].can_close ? CLOSE_BUTTON_SIZE : 0),
            TAB_MIN_WIDTH, TAB_MAX_WIDTH);
        x += tw;
    }
    // Adjust scroll so the tab is visible
    scroll_offset_ = -std::max(0.0f, x - 50.0f);
#else
    (void)index;
#endif
}

} // namespace plotix
