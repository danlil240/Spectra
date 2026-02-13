#pragma once

#include <plotix/series.hpp>
#include <memory>
#include <string>

namespace plotix {

/**
 * DockNode - Tree-based docking system for Phase 2
 * 
 * This provides the foundation for resizable panels and split views.
 * Each node represents either a leaf (content) or an internal split node.
 */
enum class DockDirection {
    Horizontal,  // Left/Right split
    Vertical     // Top/Bottom split
};

enum class DockContentType {
    None,        // Empty space
    Canvas,      // Main plot canvas
    Navigation,  // Left navigation rail
    Inspector,   // Right property inspector
    Panel,       // Generic panel (future: data table, console, etc.)
    TabBar       // Figure tab bar
};

class DockNode {
public:
    DockNode(DockContentType content = DockContentType::None);
    ~DockNode() = default;

    // Disable copying (nodes manage unique children)
    DockNode(const DockNode&) = delete;
    DockNode& operator=(const DockNode&) = delete;

    // Tree structure operations
    DockNode* split(DockDirection direction, float ratio = 0.5f);
    void set_content(DockContentType content);
    DockContentType get_content() const { return content_; }
    
    // Layout queries
    void compute_layout(const Rect& bounds);
    Rect get_bounds() const { return bounds_; }
    
    // Tree navigation
    DockNode* get_parent() const { return parent_; }
    DockNode* get_first_child() const { return first_child_.get(); }
    DockNode* get_second_child() const { return second_child_.get(); }
    bool is_leaf() const { return !first_child_; }
    bool is_root() const { return !parent_; }
    
    // Split configuration
    DockDirection get_split_direction() const { return split_direction_; }
    float get_split_ratio() const { return split_ratio_; }
    void set_split_ratio(float ratio);
    
    // Debug utilities
    void debug_print(int depth = 0) const;
    size_t count_nodes() const;

private:
    // Node type
    DockContentType content_;
    
    // Tree structure
    DockNode* parent_ = nullptr;
    std::unique_ptr<DockNode> first_child_;
    std::unique_ptr<DockNode> second_child_;
    
    // Split configuration (for internal nodes)
    DockDirection split_direction_ = DockDirection::Horizontal;
    float split_ratio_ = 0.5f;  // Ratio for first child (0.0-1.0)
    
    // Computed layout
    Rect bounds_;
    
    // Internal helpers
    void compute_children_layout();
    DockNode* get_root();
};

/**
 * DockManager - High-level interface for docking operations
 * 
 * Manages the dock tree and provides convenience methods for
 * common docking operations.
 */
class DockManager {
public:
    DockManager();
    ~DockManager() = default;

    // Disable copying
    DockManager(const DockManager&) = delete;
    DockManager& operator=(const DockManager&) = delete;

    // Layout management
    void update(float window_width, float window_height);
    Rect get_content_bounds(DockContentType content) const;
    
    // Docking operations (for Phase 2)
    DockNode* dock_left(DockContentType content, float ratio = 0.2f);
    DockNode* dock_right(DockContentType content, float ratio = 0.2f);
    DockNode* dock_top(DockContentType content, float ratio = 0.2f);
    DockNode* dock_bottom(DockContentType content, float ratio = 0.2f);
    
    // Tree access
    DockNode* get_root() const { return root_.get(); }
    
    // State serialization (future)
    std::string serialize_state() const;
    bool deserialize_state(const std::string& state);

private:
    std::unique_ptr<DockNode> root_;
    float window_width_ = 1280.0f;
    float window_height_ = 720.0f;
    
    void initialize_default_layout();
    DockNode* find_content_node(DockContentType content) const;
};

} // namespace plotix
