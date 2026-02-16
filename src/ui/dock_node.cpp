#include "dock_node.hpp"

#include <algorithm>
#include <functional>
#include <iostream>

namespace spectra
{

// ─── DockNode ─────────────────────────────────────────────────────────────

DockNode::DockNode(DockContentType content) : content_(content) {}

DockNode* DockNode::split(DockDirection direction, float ratio)
{
    if (first_child_)
    {
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

void DockNode::set_content(DockContentType content)
{
    if (is_leaf())
    {
        content_ = content;
    }
}

void DockNode::compute_layout(const Rect& bounds)
{
    bounds_ = bounds;

    if (is_leaf())
    {
        return;  // Leaf nodes just store their bounds
    }

    compute_children_layout();
}

void DockNode::compute_children_layout()
{
    if (!first_child_ || !second_child_)
    {
        return;
    }

    Rect first_bounds, second_bounds;

    if (split_direction_ == DockDirection::Horizontal)
    {
        // Left/Right split
        float first_width = bounds_.w * split_ratio_;
        float second_width = bounds_.w - first_width;

        first_bounds = Rect{bounds_.x, bounds_.y, first_width, bounds_.h};

        second_bounds = Rect{bounds_.x + first_width, bounds_.y, second_width, bounds_.h};
    }
    else
    {
        // Top/Bottom split
        float first_height = bounds_.h * split_ratio_;
        float second_height = bounds_.h - first_height;

        first_bounds = Rect{bounds_.x, bounds_.y, bounds_.w, first_height};

        second_bounds = Rect{bounds_.x, bounds_.y + first_height, bounds_.w, second_height};
    }

    first_child_->compute_layout(first_bounds);
    second_child_->compute_layout(second_bounds);
}

void DockNode::set_split_ratio(float ratio)
{
    split_ratio_ = std::clamp(ratio, 0.1f, 0.9f);

    if (!is_leaf())
    {
        compute_children_layout();
    }
}

void DockNode::debug_print(int depth) const
{
    std::string indent(depth * 2, ' ');

    std::cout << indent << "Node: ";
    switch (content_)
    {
        case DockContentType::None:
            std::cout << "Split";
            break;
        case DockContentType::Canvas:
            std::cout << "Canvas";
            break;
        case DockContentType::Navigation:
            std::cout << "Navigation";
            break;
        case DockContentType::Inspector:
            std::cout << "Inspector";
            break;
        case DockContentType::Panel:
            std::cout << "Panel";
            break;
        case DockContentType::TabBar:
            std::cout << "TabBar";
            break;
    }

    if (!is_leaf())
    {
        std::cout << " (" << (split_direction_ == DockDirection::Horizontal ? "H" : "V") << ", "
                  << split_ratio_ << ")";
    }

    std::cout << " [" << bounds_.x << "," << bounds_.y << " " << bounds_.w << "x" << bounds_.h
              << "]\n";

    if (first_child_)
    {
        first_child_->debug_print(depth + 1);
    }
    if (second_child_)
    {
        second_child_->debug_print(depth + 1);
    }
}

size_t DockNode::count_nodes() const
{
    size_t count = 1;  // Count this node
    if (first_child_)
    {
        count += first_child_->count_nodes();
    }
    if (second_child_)
    {
        count += second_child_->count_nodes();
    }
    return count;
}

DockNode* DockNode::get_root()
{
    DockNode* node = this;
    while (node->parent_)
    {
        node = node->parent_;
    }
    return node;
}

// ─── DockManager ───────────────────────────────────────────────────────────

DockManager::DockManager()
{
    initialize_default_layout();
}

void DockManager::initialize_default_layout()
{
    // Create root node covering entire window
    root_ = std::make_unique<DockNode>();

    // Default layout: Navigation | Canvas | Inspector
    // First split: Navigation | (Canvas | Inspector)
    auto nav_split = root_->split(DockDirection::Horizontal, 0.15f);  // 15% for navigation
    if (!nav_split)
        return;
    nav_split->set_content(DockContentType::Navigation);

    // Second split: Canvas | Inspector
    DockNode* center = root_->get_first_child();
    if (!center)
        return;
    auto canvas_split =
        center->split(DockDirection::Horizontal, 0.8f);  // 80% of remaining for canvas
    if (!canvas_split)
        return;
    DockNode* canvas_node = center->get_first_child();
    if (canvas_node)
    {
        canvas_node->set_content(DockContentType::Canvas);
    }
    canvas_split->set_content(DockContentType::Inspector);
}

void DockManager::update(float window_width, float window_height)
{
    window_width_ = window_width;
    window_height_ = window_height;

    if (root_)
    {
        Rect full_bounds{0.0f, 0.0f, window_width, window_height};
        root_->compute_layout(full_bounds);
    }
}

Rect DockManager::get_content_bounds(DockContentType content) const
{
    DockNode* node = find_content_node(content);
    if (node)
    {
        return node->get_bounds();
    }
    return Rect{0.0f, 0.0f, 0.0f, 0.0f};  // Not found
}

DockNode* DockManager::dock_left(DockContentType content, float ratio)
{
    if (!root_)
    {
        return nullptr;
    }

    // Split root horizontally from the left
    auto new_node = root_->split(DockDirection::Horizontal, ratio);
    if (new_node)
    {
        new_node->set_content(content);
        return new_node;
    }
    return nullptr;
}

DockNode* DockManager::dock_right(DockContentType content, float ratio)
{
    if (!root_)
    {
        return nullptr;
    }

    // Split root horizontally from the right
    auto new_node = root_->split(DockDirection::Horizontal, 1.0f - ratio);
    if (new_node)
    {
        new_node->set_content(content);
        return new_node;
    }
    return nullptr;
}

DockNode* DockManager::dock_top(DockContentType content, float ratio)
{
    if (!root_)
    {
        return nullptr;
    }

    // Split root vertically from the top
    auto new_node = root_->split(DockDirection::Vertical, ratio);
    if (new_node)
    {
        new_node->set_content(content);
        return new_node;
    }
    return nullptr;
}

DockNode* DockManager::dock_bottom(DockContentType content, float ratio)
{
    if (!root_)
    {
        return nullptr;
    }

    // Split root vertically from the bottom
    auto new_node = root_->split(DockDirection::Vertical, 1.0f - ratio);
    if (new_node)
    {
        new_node->set_content(content);
        return new_node;
    }
    return nullptr;
}

DockNode* DockManager::find_content_node(DockContentType content) const
{
    if (!root_)
    {
        return nullptr;
    }

    // Simple recursive search
    std::function<DockNode*(DockNode*)> search = [&](DockNode* node) -> DockNode*
    {
        if (!node)
        {
            return nullptr;
        }

        if (node->get_content() == content)
        {
            return node;
        }

        // Search children
        DockNode* found = search(node->get_first_child());
        if (found)
        {
            return found;
        }

        return search(node->get_second_child());
    };

    return search(root_.get());
}

static std::string serialize_node(const DockNode* node)
{
    if (!node)
        return "null";
    std::string s = "{";
    s += "\"leaf\":" + std::string(node->is_leaf() ? "true" : "false");
    if (node->is_leaf())
    {
        int ct = static_cast<int>(node->get_content());
        s += ",\"content\":" + std::to_string(ct);
    }
    else
    {
        s += ",\"dir\":\""
             + std::string(node->get_split_direction() == DockDirection::Horizontal ? "h" : "v")
             + "\"";
        s += ",\"ratio\":" + std::to_string(node->get_split_ratio());
        s += ",\"first\":" + serialize_node(node->get_first_child());
        s += ",\"second\":" + serialize_node(node->get_second_child());
    }
    s += "}";
    return s;
}

std::string DockManager::serialize_state() const
{
    std::string s = "{";
    s += "\"width\":" + std::to_string(window_width_);
    s += ",\"height\":" + std::to_string(window_height_);
    if (root_)
    {
        s += ",\"root\":" + serialize_node(root_.get());
    }
    s += "}";
    return s;
}

static std::unique_ptr<DockNode> deserialize_node(const std::string& data)
{
    if (data.empty() || data == "null" || data[0] != '{')
        return nullptr;

    auto find_val = [&](const std::string& key) -> std::string
    {
        std::string search = "\"" + key + "\":";
        auto pos = data.find(search);
        if (pos == std::string::npos)
            return "";
        pos += search.size();
        while (pos < data.size() && data[pos] == ' ')
            ++pos;
        if (pos >= data.size())
            return "";
        if (data[pos] == '"')
        {
            auto end = data.find('"', pos + 1);
            if (end == std::string::npos)
                return "";
            return data.substr(pos + 1, end - pos - 1);
        }
        else if (data[pos] == '{')
        {
            int depth = 0;
            size_t start = pos;
            for (size_t i = pos; i < data.size(); ++i)
            {
                if (data[i] == '{')
                    ++depth;
                else if (data[i] == '}')
                {
                    --depth;
                    if (depth == 0)
                        return data.substr(start, i - start + 1);
                }
            }
            return "";
        }
        else
        {
            auto end = data.find_first_of(",}", pos);
            if (end == std::string::npos)
                return "";
            return data.substr(pos, end - pos);
        }
    };

    bool is_leaf = (find_val("leaf") == "true");

    if (is_leaf)
    {
        int ct = 0;
        std::string ct_str = find_val("content");
        if (!ct_str.empty())
            ct = std::stoi(ct_str);
        auto node = std::make_unique<DockNode>(static_cast<DockContentType>(ct));
        return node;
    }

    // Internal node: create a leaf, then split it
    auto node = std::make_unique<DockNode>(DockContentType::None);

    std::string dir_str = find_val("dir");
    DockDirection dir = (dir_str == "v") ? DockDirection::Vertical : DockDirection::Horizontal;

    float ratio = 0.5f;
    std::string ratio_str = find_val("ratio");
    if (!ratio_str.empty())
        ratio = std::stof(ratio_str);

    auto* second = node->split(dir, ratio);
    if (!second)
        return node;

    // Recursively deserialize children
    std::string first_str = find_val("first");
    std::string second_str = find_val("second");

    auto first_child = deserialize_node(first_str);
    auto second_child = deserialize_node(second_str);

    // Set content on children if they are leaves
    if (first_child && first_child->is_leaf() && node->get_first_child())
    {
        node->get_first_child()->set_content(first_child->get_content());
    }
    if (second_child && second_child->is_leaf() && node->get_second_child())
    {
        node->get_second_child()->set_content(second_child->get_content());
    }

    return node;
}

bool DockManager::deserialize_state(const std::string& state)
{
    if (state.empty() || state[0] != '{')
        return false;

    auto find_val = [&](const std::string& key) -> std::string
    {
        std::string search = "\"" + key + "\":";
        auto pos = state.find(search);
        if (pos == std::string::npos)
            return "";
        pos += search.size();
        while (pos < state.size() && state[pos] == ' ')
            ++pos;
        if (pos >= state.size())
            return "";
        if (state[pos] == '"')
        {
            auto end = state.find('"', pos + 1);
            if (end == std::string::npos)
                return "";
            return state.substr(pos + 1, end - pos - 1);
        }
        else if (state[pos] == '{')
        {
            int depth = 0;
            size_t start = pos;
            for (size_t i = pos; i < state.size(); ++i)
            {
                if (state[i] == '{')
                    ++depth;
                else if (state[i] == '}')
                {
                    --depth;
                    if (depth == 0)
                        return state.substr(start, i - start + 1);
                }
            }
            return "";
        }
        else
        {
            auto end = state.find_first_of(",}", pos);
            if (end == std::string::npos)
                return "";
            return state.substr(pos, end - pos);
        }
    };

    std::string w_str = find_val("width");
    std::string h_str = find_val("height");
    if (!w_str.empty())
        window_width_ = std::stof(w_str);
    if (!h_str.empty())
        window_height_ = std::stof(h_str);

    std::string root_str = find_val("root");
    if (!root_str.empty())
    {
        auto new_root = deserialize_node(root_str);
        if (new_root)
        {
            root_ = std::move(new_root);
            update(window_width_, window_height_);
            return true;
        }
    }

    return false;
}

}  // namespace spectra
