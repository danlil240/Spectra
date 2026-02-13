#include "dock_node.hpp"
#include <iostream>
#include <algorithm>
#include <functional>

namespace plotix {

// ─── DockNode ─────────────────────────────────────────────────────────────

DockNode::DockNode(DockContentType content) 
    : content_(content) {
}

DockNode* DockNode::split(DockDirection direction, float ratio) {
    if (first_child_) {
        // Already split - return nullptr
        return nullptr;
    }
    
    // Clamp ratio to valid range
    ratio = std::clamp(ratio, 0.1f, 0.9f);
    
    // Create children
    auto first = std::make_unique<DockNode>(content_);
    auto second = std::make_unique<DockNode>();
    
    // Set up parent relationships
    first->parent_ = this;
    second->parent_ = this;
    
    // Configure split
    split_direction_ = direction;
    split_ratio_ = ratio;
    
    // Move children into place
    first_child_ = std::move(first);
    second_child_ = std::move(second);
    
    // This node becomes an internal node
    content_ = DockContentType::None;
    
    // Return the second child for convenience
    return second_child_.get();
}

void DockNode::set_content(DockContentType content) {
    if (is_leaf()) {
        content_ = content;
    }
}

void DockNode::compute_layout(const Rect& bounds) {
    bounds_ = bounds;
    
    if (is_leaf()) {
        return;  // Leaf nodes just store their bounds
    }
    
    compute_children_layout();
}

void DockNode::compute_children_layout() {
    if (!first_child_ || !second_child_) {
        return;
    }
    
    Rect first_bounds, second_bounds;
    
    if (split_direction_ == DockDirection::Horizontal) {
        // Left/Right split
        float first_width = bounds_.w * split_ratio_;
        float second_width = bounds_.w - first_width;
        
        first_bounds = Rect{
            bounds_.x,
            bounds_.y,
            first_width,
            bounds_.h
        };
        
        second_bounds = Rect{
            bounds_.x + first_width,
            bounds_.y,
            second_width,
            bounds_.h
        };
    } else {
        // Top/Bottom split
        float first_height = bounds_.h * split_ratio_;
        float second_height = bounds_.h - first_height;
        
        first_bounds = Rect{
            bounds_.x,
            bounds_.y,
            bounds_.w,
            first_height
        };
        
        second_bounds = Rect{
            bounds_.x,
            bounds_.y + first_height,
            bounds_.w,
            second_height
        };
    }
    
    first_child_->compute_layout(first_bounds);
    second_child_->compute_layout(second_bounds);
}

void DockNode::set_split_ratio(float ratio) {
    split_ratio_ = std::clamp(ratio, 0.1f, 0.9f);
    
    if (!is_leaf()) {
        compute_children_layout();
    }
}

void DockNode::debug_print(int depth) const {
    std::string indent(depth * 2, ' ');
    
    std::cout << indent << "Node: ";
    switch (content_) {
        case DockContentType::None: std::cout << "Split"; break;
        case DockContentType::Canvas: std::cout << "Canvas"; break;
        case DockContentType::Navigation: std::cout << "Navigation"; break;
        case DockContentType::Inspector: std::cout << "Inspector"; break;
        case DockContentType::Panel: std::cout << "Panel"; break;
        case DockContentType::TabBar: std::cout << "TabBar"; break;
    }
    
    if (!is_leaf()) {
        std::cout << " (" << (split_direction_ == DockDirection::Horizontal ? "H" : "V") 
                  << ", " << split_ratio_ << ")";
    }
    
    std::cout << " [" << bounds_.x << "," << bounds_.y << " " 
              << bounds_.w << "x" << bounds_.h << "]\n";
    
    if (first_child_) {
        first_child_->debug_print(depth + 1);
    }
    if (second_child_) {
        second_child_->debug_print(depth + 1);
    }
}

size_t DockNode::count_nodes() const {
    size_t count = 1;  // Count this node
    if (first_child_) {
        count += first_child_->count_nodes();
    }
    if (second_child_) {
        count += second_child_->count_nodes();
    }
    return count;
}

DockNode* DockNode::get_root() {
    DockNode* node = this;
    while (node->parent_) {
        node = node->parent_;
    }
    return node;
}

// ─── DockManager ───────────────────────────────────────────────────────────

DockManager::DockManager() {
    initialize_default_layout();
}

void DockManager::initialize_default_layout() {
    // Create root node covering entire window
    root_ = std::make_unique<DockNode>();
    
    // Default layout: Navigation | Canvas | Inspector
    // First split: Navigation | (Canvas | Inspector)
    auto nav_split = root_->split(DockDirection::Horizontal, 0.15f);  // 15% for navigation
    if (!nav_split) return;
    nav_split->set_content(DockContentType::Navigation);
    
    // Second split: Canvas | Inspector
    DockNode* center = root_->get_first_child();
    if (!center) return;
    auto canvas_split = center->split(DockDirection::Horizontal, 0.8f);  // 80% of remaining for canvas
    if (!canvas_split) return;
    DockNode* canvas_node = center->get_first_child();
    if (canvas_node) {
        canvas_node->set_content(DockContentType::Canvas);
    }
    canvas_split->set_content(DockContentType::Inspector);
}

void DockManager::update(float window_width, float window_height) {
    window_width_ = window_width;
    window_height_ = window_height;
    
    if (root_) {
        Rect full_bounds{0.0f, 0.0f, window_width, window_height};
        root_->compute_layout(full_bounds);
    }
}

Rect DockManager::get_content_bounds(DockContentType content) const {
    DockNode* node = find_content_node(content);
    if (node) {
        return node->get_bounds();
    }
    return Rect{0.0f, 0.0f, 0.0f, 0.0f};  // Not found
}

DockNode* DockManager::dock_left(DockContentType content, float ratio) {
    if (!root_) {
        return nullptr;
    }
    
    // Split root horizontally from the left
    auto new_node = root_->split(DockDirection::Horizontal, ratio);
    if (new_node) {
        new_node->set_content(content);
        return new_node;
    }
    return nullptr;
}

DockNode* DockManager::dock_right(DockContentType content, float ratio) {
    if (!root_) {
        return nullptr;
    }
    
    // Split root horizontally from the right
    auto new_node = root_->split(DockDirection::Horizontal, 1.0f - ratio);
    if (new_node) {
        new_node->set_content(content);
        return new_node;
    }
    return nullptr;
}

DockNode* DockManager::dock_top(DockContentType content, float ratio) {
    if (!root_) {
        return nullptr;
    }
    
    // Split root vertically from the top
    auto new_node = root_->split(DockDirection::Vertical, ratio);
    if (new_node) {
        new_node->set_content(content);
        return new_node;
    }
    return nullptr;
}

DockNode* DockManager::dock_bottom(DockContentType content, float ratio) {
    if (!root_) {
        return nullptr;
    }
    
    // Split root vertically from the bottom
    auto new_node = root_->split(DockDirection::Vertical, 1.0f - ratio);
    if (new_node) {
        new_node->set_content(content);
        return new_node;
    }
    return nullptr;
}

DockNode* DockManager::find_content_node(DockContentType content) const {
    if (!root_) {
        return nullptr;
    }
    
    // Simple recursive search
    std::function<DockNode*(DockNode*)> search = [&](DockNode* node) -> DockNode* {
        if (!node) {
            return nullptr;
        }
        
        if (node->get_content() == content) {
            return node;
        }
        
        // Search children
        DockNode* found = search(node->get_first_child());
        if (found) {
            return found;
        }
        
        return search(node->get_second_child());
    };
    
    return search(root_.get());
}

std::string DockManager::serialize_state() const {
    // TODO: Implement state serialization for workspace save/load
    return "{}";
}

bool DockManager::deserialize_state(const std::string& state) {
    // TODO: Implement state deserialization
    (void)state;
    return false;
}

} // namespace plotix
