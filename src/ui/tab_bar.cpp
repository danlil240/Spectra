#include "tab_bar.hpp"

#ifdef PLOTIX_USE_IMGUI
#include <imgui.h>
#include <imgui_internal.h>
#endif

#include <algorithm>
#include <cmath>

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
    
    // Notify callback
    if (on_tab_add_) {
        on_tab_add_();
    }
    
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
    
    ImGui::PopClipRect();
#endif
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

void TabBar::draw_tabs(const Rect& bounds) {
#ifdef PLOTIX_USE_IMGUI
    auto layouts = compute_tab_layouts(bounds);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    
    for (size_t i = 0; i < layouts.size(); ++i) {
        const auto& layout = layouts[i];
        if (!layout.is_visible || layout.is_clipped) {
            continue;
        }
        
        const auto& tab = tabs_[i];
        
        // Tab background
        ImU32 bg_color;
        if (i == active_tab_) {
            bg_color = IM_COL32(45, 45, 48, 255);  // Active tab background
        } else if (i == hovered_tab_) {
            bg_color = IM_COL32(60, 60, 65, 255);  // Hovered tab background
        } else {
            bg_color = IM_COL32(35, 35, 38, 255);  // Normal tab background
        }
        
        // Draw tab background with rounded top corners
        draw_list->AddRectFilled(
            ImVec2(layout.bounds.x + 2, layout.bounds.y + 2),
            ImVec2(layout.bounds.x + layout.bounds.w - 2, layout.bounds.y + layout.bounds.h),
            bg_color, 4.0f, ImDrawFlags_RoundCornersTop);
        
        // Tab border
        ImU32 border_color = IM_COL32(80, 80, 85, 255);
        draw_list->AddRect(
            ImVec2(layout.bounds.x + 2, layout.bounds.y + 2),
            ImVec2(layout.bounds.x + layout.bounds.w - 2, layout.bounds.y + layout.bounds.h),
            border_color, 4.0f, ImDrawFlags_RoundCornersTop, 1.0f);
        
        // Tab title
        ImVec2 text_size = ImGui::CalcTextSize(tab.title.c_str());
        ImVec2 text_pos(
            layout.bounds.x + TAB_PADDING,
            layout.bounds.y + (layout.bounds.h - text_size.y) * 0.5f
        );
        
        draw_list->AddText(text_pos, IM_COL32(255, 255, 255, 255), tab.title.c_str());
        
        // Close button (if enabled)
        if (tab.can_close) {
            ImU32 close_color = (i == hovered_close_) ? 
                IM_COL32(255, 100, 100, 255) : IM_COL32(180, 180, 180, 255);
            
            ImVec2 close_center(
                layout.close_bounds.x + layout.close_bounds.w * 0.5f,
                layout.close_bounds.y + layout.close_bounds.h * 0.5f
            );
            
            // Draw 'X' for close button
            float size = CLOSE_BUTTON_SIZE * 0.4f;
            draw_list->AddLine(
                ImVec2(close_center.x - size, close_center.y - size),
                ImVec2(close_center.x + size, close_center.y + size),
                close_color, 2.0f);
            draw_list->AddLine(
                ImVec2(close_center.x - size, close_center.y + size),
                ImVec2(close_center.x + size, close_center.y - size),
                close_color, 2.0f);
        }
        
        // Modified indicator (future)
        if (tab.is_modified) {
            ImVec2 dot_pos(layout.bounds.x + 6, layout.bounds.y + layout.bounds.h - 6);
            draw_list->AddCircleFilled(dot_pos, 3.0f, IM_COL32(255, 200, 100, 255));
        }
    }
#endif
}

void TabBar::draw_add_button(const Rect& bounds) {
#ifdef PLOTIX_USE_IMGUI
    // Position add button at the right edge
    ImVec2 button_pos(bounds.x + bounds.w - ADD_BUTTON_WIDTH - 4, bounds.y + 4);
    ImVec2 button_size(ADD_BUTTON_WIDTH - 8, bounds.h - 8);
    
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    
    // Button background
    ImU32 bg_color = IM_COL32(60, 60, 65, 255);
    draw_list->AddRectFilled(
        button_pos,
        ImVec2(button_pos.x + button_size.x, button_pos.y + button_size.y),
        bg_color, 4.0f);
    
    // Plus sign
    ImVec2 center(button_pos.x + button_size.x * 0.5f, button_pos.y + button_size.y * 0.5f);
    ImU32 plus_color = IM_COL32(200, 200, 200, 255);
    float size = 8.0f;
    
    draw_list->AddLine(
        ImVec2(center.x - size, center.y),
        ImVec2(center.x + size, center.y),
        plus_color, 2.0f);
    draw_list->AddLine(
        ImVec2(center.x, center.y - size),
        ImVec2(center.x, center.y + size),
        plus_color, 2.0f);
    
    // Handle click
    ImVec2 mouse_pos = ImGui::GetMousePos();
    if (mouse_pos.x >= button_pos.x && mouse_pos.x < button_pos.x + button_size.x &&
        mouse_pos.y >= button_pos.y && mouse_pos.y < button_pos.y + button_size.y &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        add_tab("Figure " + std::to_string(tabs_.size() + 1));
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
    // TODO: Implement visual tab reordering during drag
    // This is a placeholder for the drag feedback
    (void)mouse_x;
}

void TabBar::end_drag() {
    // TODO: Implement actual tab reordering logic
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
    // TODO: Implement scroll buttons for tab overflow
    (void)bounds;
}

void TabBar::scroll_to_tab(size_t index) {
    // TODO: Implement auto-scroll to make tab visible
    (void)index;
}

} // namespace plotix
