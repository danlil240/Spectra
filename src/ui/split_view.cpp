#include "split_view.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <sstream>

namespace plotix {

// ─── SplitPane ───────────────────────────────────────────────────────────────

static std::atomic<SplitPane::PaneId> s_next_pane_id{1};

SplitPane::PaneId SplitPane::next_id() {
    return s_next_pane_id.fetch_add(1, std::memory_order_relaxed);
}

SplitPane::SplitPane(size_t figure_index)
    : id_(next_id())
    , figure_index_(figure_index)
{
    figure_indices_.push_back(figure_index);
    active_local_ = 0;
}

void SplitPane::set_active_local_index(size_t local_idx) {
    if (local_idx < figure_indices_.size()) {
        active_local_ = local_idx;
        figure_index_ = figure_indices_[active_local_];
    }
}

void SplitPane::add_figure(size_t fig_idx) {
    if (!has_figure(fig_idx)) {
        figure_indices_.push_back(fig_idx);
        // Activate the newly added figure
        active_local_ = figure_indices_.size() - 1;
        figure_index_ = fig_idx;
    }
}

void SplitPane::remove_figure(size_t fig_idx) {
    auto it = std::find(figure_indices_.begin(), figure_indices_.end(), fig_idx);
    if (it == figure_indices_.end()) return;
    size_t removed_idx = static_cast<size_t>(it - figure_indices_.begin());
    figure_indices_.erase(it);
    if (figure_indices_.empty()) {
        figure_index_ = SIZE_MAX;
        active_local_ = 0;
        return;
    }
    if (active_local_ >= figure_indices_.size()) {
        active_local_ = figure_indices_.size() - 1;
    } else if (active_local_ > removed_idx) {
        active_local_--;
    }
    figure_index_ = figure_indices_[active_local_];
}

bool SplitPane::has_figure(size_t fig_idx) const {
    return std::find(figure_indices_.begin(), figure_indices_.end(), fig_idx)
           != figure_indices_.end();
}

void SplitPane::swap_contents(SplitPane& other) {
    std::swap(figure_index_, other.figure_index_);
    std::swap(figure_indices_, other.figure_indices_);
    std::swap(active_local_, other.active_local_);
}

Rect SplitPane::content_bounds() const {
    if (!is_leaf()) return bounds_;
    // Always reserve space for the tab header (unified tab bar)
    if (figure_indices_.size() > 0) {
        return Rect{bounds_.x, bounds_.y + PANE_TAB_HEIGHT,
                    bounds_.w, std::max(0.0f, bounds_.h - PANE_TAB_HEIGHT)};
    }
    return bounds_;
}

SplitPane* SplitPane::split(SplitDirection direction, size_t new_figure_index,
                            float ratio) {
    if (is_split()) {
        return nullptr;  // Already split
    }

    ratio = std::clamp(ratio, MIN_RATIO, MAX_RATIO);

    // Create two children: first gets ALL our figures, second gets the new figure
    auto first_child = std::make_unique<SplitPane>(figure_index_);
    // Transfer the full figure list (not just the primary figure_index_)
    first_child->figure_indices_ = figure_indices_;
    first_child->active_local_ = active_local_;
    first_child->figure_index_ = figure_index_;

    auto second_child = std::make_unique<SplitPane>(new_figure_index);

    first_child->parent_ = this;
    second_child->parent_ = this;

    split_direction_ = direction;
    split_ratio_ = ratio;

    first_ = std::move(first_child);
    second_ = std::move(second_child);

    // This node is now internal — clear leaf state
    figure_index_ = SIZE_MAX;
    figure_indices_.clear();
    active_local_ = 0;

    // Recompute layout if we have bounds
    if (bounds_.w > 0.0f && bounds_.h > 0.0f) {
        compute_layout(bounds_);
    }

    return second_.get();
}

bool SplitPane::unsplit(bool keep_first) {
    if (is_leaf()) {
        return false;
    }

    SplitPane* kept = keep_first ? first_.get() : second_.get();
    if (!kept) {
        return false;
    }

    if (kept->is_leaf()) {
        // Simple case: kept child is a leaf — absorb ALL its figures
        figure_index_ = kept->figure_index_;
        figure_indices_ = kept->figure_indices_;
        active_local_ = kept->active_local_;
        first_.reset();
        second_.reset();
    } else {
        // Kept child is an internal node — adopt its children
        split_direction_ = kept->split_direction_;
        split_ratio_ = kept->split_ratio_;
        figure_index_ = kept->figure_index_;
        figure_indices_ = kept->figure_indices_;
        active_local_ = kept->active_local_;

        // Move grandchildren up
        auto new_first = std::move(kept->first_);
        auto new_second = std::move(kept->second_);

        first_ = std::move(new_first);
        second_ = std::move(new_second);

        if (first_) first_->parent_ = this;
        if (second_) second_->parent_ = this;
    }

    // Recompute layout
    if (bounds_.w > 0.0f && bounds_.h > 0.0f) {
        compute_layout(bounds_);
    }

    return true;
}

void SplitPane::set_split_ratio(float ratio) {
    split_ratio_ = std::clamp(ratio, MIN_RATIO, MAX_RATIO);
}

void SplitPane::compute_layout(const Rect& bounds) {
    bounds_ = bounds;

    if (is_leaf()) {
        return;
    }

    float half_splitter = SPLITTER_WIDTH * 0.5f;

    if (split_direction_ == SplitDirection::Horizontal) {
        // Left | Right
        float split_x = bounds_.x + bounds_.w * split_ratio_;
        float first_w = split_x - bounds_.x - half_splitter;
        float second_x = split_x + half_splitter;
        float second_w = bounds_.x + bounds_.w - second_x;

        first_w = std::max(first_w, 0.0f);
        second_w = std::max(second_w, 0.0f);

        if (first_) {
            first_->compute_layout(Rect{bounds_.x, bounds_.y, first_w, bounds_.h});
        }
        if (second_) {
            second_->compute_layout(Rect{second_x, bounds_.y, second_w, bounds_.h});
        }
    } else {
        // Top / Bottom
        float split_y = bounds_.y + bounds_.h * split_ratio_;
        float first_h = split_y - bounds_.y - half_splitter;
        float second_y = split_y + half_splitter;
        float second_h = bounds_.y + bounds_.h - second_y;

        first_h = std::max(first_h, 0.0f);
        second_h = std::max(second_h, 0.0f);

        if (first_) {
            first_->compute_layout(Rect{bounds_.x, bounds_.y, bounds_.w, first_h});
        }
        if (second_) {
            second_->compute_layout(Rect{bounds_.x, second_y, bounds_.w, second_h});
        }
    }
}

Rect SplitPane::splitter_rect() const {
    if (is_leaf()) {
        return Rect{0, 0, 0, 0};
    }

    float half_splitter = SPLITTER_WIDTH * 0.5f;

    if (split_direction_ == SplitDirection::Horizontal) {
        float split_x = bounds_.x + bounds_.w * split_ratio_;
        return Rect{split_x - half_splitter, bounds_.y, SPLITTER_WIDTH, bounds_.h};
    } else {
        float split_y = bounds_.y + bounds_.h * split_ratio_;
        return Rect{bounds_.x, split_y - half_splitter, bounds_.w, SPLITTER_WIDTH};
    }
}

void SplitPane::collect_leaves(std::vector<SplitPane*>& out) {
    if (is_leaf()) {
        out.push_back(this);
        return;
    }
    if (first_) first_->collect_leaves(out);
    if (second_) second_->collect_leaves(out);
}

void SplitPane::collect_leaves(std::vector<const SplitPane*>& out) const {
    if (is_leaf()) {
        out.push_back(this);
        return;
    }
    if (first_) first_->collect_leaves(out);
    if (second_) second_->collect_leaves(out);
}

SplitPane* SplitPane::find_by_figure(size_t figure_index) {
    if (is_leaf() && has_figure(figure_index)) {
        return this;
    }
    if (first_) {
        if (auto* found = first_->find_by_figure(figure_index)) return found;
    }
    if (second_) {
        if (auto* found = second_->find_by_figure(figure_index)) return found;
    }
    return nullptr;
}

const SplitPane* SplitPane::find_by_figure(size_t figure_index) const {
    if (is_leaf() && has_figure(figure_index)) {
        return this;
    }
    if (first_) {
        if (auto* found = first_->find_by_figure(figure_index)) return found;
    }
    if (second_) {
        if (auto* found = second_->find_by_figure(figure_index)) return found;
    }
    return nullptr;
}

SplitPane* SplitPane::find_at_point(float x, float y) {
    if (is_leaf()) {
        if (x >= bounds_.x && x < bounds_.x + bounds_.w &&
            y >= bounds_.y && y < bounds_.y + bounds_.h) {
            return this;
        }
        return nullptr;
    }
    if (first_) {
        if (auto* found = first_->find_at_point(x, y)) return found;
    }
    if (second_) {
        if (auto* found = second_->find_at_point(x, y)) return found;
    }
    return nullptr;
}

SplitPane* SplitPane::find_by_id(PaneId target_id) {
    if (id_ == target_id) return this;
    if (first_) {
        if (auto* found = first_->find_by_id(target_id)) return found;
    }
    if (second_) {
        if (auto* found = second_->find_by_id(target_id)) return found;
    }
    return nullptr;
}

size_t SplitPane::count_nodes() const {
    size_t count = 1;
    if (first_) count += first_->count_nodes();
    if (second_) count += second_->count_nodes();
    return count;
}

size_t SplitPane::count_leaves() const {
    if (is_leaf()) return 1;
    size_t count = 0;
    if (first_) count += first_->count_leaves();
    if (second_) count += second_->count_leaves();
    return count;
}

std::string SplitPane::serialize() const {
    std::ostringstream ss;
    ss << "{";
    ss << "\"id\":" << id_;
    ss << ",\"leaf\":" << (is_leaf() ? "true" : "false");

    if (is_leaf()) {
        ss << ",\"figure\":" << figure_index_;
    } else {
        ss << ",\"dir\":\"" << (split_direction_ == SplitDirection::Horizontal ? "h" : "v") << "\"";
        ss << ",\"ratio\":" << split_ratio_;
        if (first_) ss << ",\"first\":" << first_->serialize();
        if (second_) ss << ",\"second\":" << second_->serialize();
    }
    ss << "}";
    return ss.str();
}

std::unique_ptr<SplitPane> SplitPane::deserialize(const std::string& data) {
    // Minimal JSON parser for our known format
    if (data.empty() || data[0] != '{') return nullptr;

    auto find_value = [&](const std::string& key) -> std::string {
        std::string search = "\"" + key + "\":";
        auto pos = data.find(search);
        if (pos == std::string::npos) return "";
        pos += search.size();
        // Skip whitespace
        while (pos < data.size() && data[pos] == ' ') ++pos;
        if (pos >= data.size()) return "";

        if (data[pos] == '"') {
            // String value
            auto end = data.find('"', pos + 1);
            if (end == std::string::npos) return "";
            return data.substr(pos + 1, end - pos - 1);
        } else if (data[pos] == '{') {
            // Object value — find matching brace
            int depth = 0;
            size_t start = pos;
            for (size_t i = pos; i < data.size(); ++i) {
                if (data[i] == '{') ++depth;
                else if (data[i] == '}') {
                    --depth;
                    if (depth == 0) return data.substr(start, i - start + 1);
                }
            }
            return "";
        } else {
            // Number or bool
            auto end = data.find_first_of(",}", pos);
            if (end == std::string::npos) return "";
            return data.substr(pos, end - pos);
        }
    };

    bool is_leaf = (find_value("leaf") == "true");

    if (is_leaf) {
        size_t fig_idx = 0;
        std::string fig_str = find_value("figure");
        if (!fig_str.empty()) {
            fig_idx = static_cast<size_t>(std::stoul(fig_str));
        }
        return std::make_unique<SplitPane>(fig_idx);
    }

    // Internal node
    std::string dir_str = find_value("dir");
    SplitDirection dir = (dir_str == "v") ? SplitDirection::Vertical : SplitDirection::Horizontal;

    float ratio = 0.5f;
    std::string ratio_str = find_value("ratio");
    if (!ratio_str.empty()) {
        ratio = std::stof(ratio_str);
    }

    std::string first_str = find_value("first");
    std::string second_str = find_value("second");

    auto first_child = deserialize(first_str);
    auto second_child = deserialize(second_str);

    if (!first_child || !second_child) return nullptr;

    // Build internal node: create a dummy pane, then manually set children
    auto node = std::make_unique<SplitPane>(SIZE_MAX);
    node->split_direction_ = dir;
    node->split_ratio_ = ratio;
    node->figure_index_ = SIZE_MAX;

    first_child->parent_ = node.get();
    second_child->parent_ = node.get();

    node->first_ = std::move(first_child);
    node->second_ = std::move(second_child);

    return node;
}

// ─── SplitViewManager ────────────────────────────────────────────────────────

SplitViewManager::SplitViewManager()
    : root_(std::make_unique<SplitPane>(0))
{
}

SplitPane* SplitViewManager::split_pane(size_t figure_index,
                                         SplitDirection direction,
                                         size_t new_figure_index,
                                         float ratio) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!root_) return nullptr;

    // Check max panes
    if (root_->count_leaves() >= MAX_PANES) return nullptr;

    auto* pane = root_->find_by_figure(figure_index);
    if (!pane) return nullptr;

    auto* new_pane = pane->split(direction, new_figure_index, ratio);
    if (new_pane) {
        recompute_layout();
        if (on_split_) on_split_(new_pane);
    }
    return new_pane;
}

SplitPane* SplitViewManager::split_active(SplitDirection direction,
                                           size_t new_figure_index,
                                           float ratio) {
    return split_pane(active_figure_index_, direction, new_figure_index, ratio);
}

bool SplitViewManager::close_pane(size_t figure_index) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!root_) return false;

    // Can't close the last pane
    if (root_->is_leaf()) return false;

    auto* pane = root_->find_by_figure(figure_index);
    if (!pane) return false;

    auto* parent = pane->parent();
    if (!parent) {
        // This is the root — can't close
        return false;
    }

    // Determine which child to keep
    bool keep_first = (parent->second() == pane);

    // Get the figure index of the kept pane (for active pane update)
    SplitPane* kept = keep_first ? parent->first() : parent->second();
    size_t kept_figure = SIZE_MAX;
    if (kept && kept->is_leaf()) {
        kept_figure = kept->figure_index();
    } else if (kept) {
        // Kept is an internal node — find its first leaf
        std::vector<SplitPane*> leaves;
        kept->collect_leaves(leaves);
        if (!leaves.empty()) {
            kept_figure = leaves[0]->figure_index();
        }
    }

    if (on_unsplit_) on_unsplit_(pane);

    parent->unsplit(keep_first);

    // Update active figure if the closed pane was active
    if (active_figure_index_ == figure_index && kept_figure != SIZE_MAX) {
        active_figure_index_ = kept_figure;
        if (on_active_changed_) on_active_changed_(active_figure_index_);
    }

    recompute_layout();
    return true;
}

void SplitViewManager::unsplit_all() {
    std::lock_guard<std::mutex> lock(mutex_);

    size_t fig = active_figure_index_;
    root_ = std::make_unique<SplitPane>(fig);
    recompute_layout();
}

size_t SplitViewManager::active_figure_index() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_figure_index_;
}

void SplitViewManager::set_active_figure_index(size_t idx) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (idx != active_figure_index_) {
        active_figure_index_ = idx;
        if (on_active_changed_) on_active_changed_(idx);
    }
}

SplitPane* SplitViewManager::active_pane() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!root_) return nullptr;
    return root_->find_by_figure(active_figure_index_);
}

const SplitPane* SplitViewManager::active_pane() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!root_) return nullptr;
    return root_->find_by_figure(active_figure_index_);
}

void SplitViewManager::update_layout(const Rect& canvas_bounds) {
    std::lock_guard<std::mutex> lock(mutex_);
    canvas_bounds_ = canvas_bounds;
    if (root_) {
        root_->compute_layout(canvas_bounds);
    }
}

bool SplitViewManager::is_split() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return root_ && root_->is_split();
}

size_t SplitViewManager::pane_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return root_ ? root_->count_leaves() : 0;
}

std::vector<SplitPane*> SplitViewManager::all_panes() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SplitPane*> result;
    if (root_) root_->collect_leaves(result);
    return result;
}

std::vector<const SplitPane*> SplitViewManager::all_panes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<const SplitPane*> result;
    if (root_) root_->collect_leaves(result);
    return result;
}

SplitPane* SplitViewManager::pane_at_point(float x, float y) {
    std::lock_guard<std::mutex> lock(mutex_);
    return root_ ? root_->find_at_point(x, y) : nullptr;
}

SplitPane* SplitViewManager::pane_for_figure(size_t figure_index) {
    std::lock_guard<std::mutex> lock(mutex_);
    return root_ ? root_->find_by_figure(figure_index) : nullptr;
}

bool SplitViewManager::is_figure_visible(size_t figure_index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return root_ && root_->find_by_figure(figure_index) != nullptr;
}

SplitPane* SplitViewManager::splitter_at_point(float x, float y) {
    std::lock_guard<std::mutex> lock(mutex_);
    return find_splitter_recursive(root_.get(), x, y);
}

SplitPane* SplitViewManager::find_splitter_recursive(SplitPane* node, float x, float y) {
    if (!node || node->is_leaf()) return nullptr;

    Rect sr = node->splitter_rect();
    if (x >= sr.x && x < sr.x + sr.w && y >= sr.y && y < sr.y + sr.h) {
        return node;
    }

    // Check children (they may have their own splitters)
    if (auto* found = find_splitter_recursive(node->first(), x, y)) return found;
    if (auto* found = find_splitter_recursive(node->second(), x, y)) return found;
    return nullptr;
}

void SplitViewManager::begin_splitter_drag(SplitPane* splitter_pane, float mouse_pos) {
    std::lock_guard<std::mutex> lock(mutex_);
    dragging_splitter_ = splitter_pane;
    drag_start_pos_ = mouse_pos;
    drag_start_ratio_ = splitter_pane ? splitter_pane->split_ratio() : 0.5f;
}

void SplitViewManager::update_splitter_drag(float mouse_pos) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!dragging_splitter_) return;

    Rect b = dragging_splitter_->bounds();
    float delta = mouse_pos - drag_start_pos_;

    float total_size;
    if (dragging_splitter_->split_direction() == SplitDirection::Horizontal) {
        total_size = b.w;
    } else {
        total_size = b.h;
    }

    if (total_size < 1.0f) return;

    float delta_ratio = delta / total_size;
    float new_ratio = drag_start_ratio_ + delta_ratio;

    // Enforce minimum pane sizes
    float min_ratio = SplitPane::MIN_PANE_SIZE / total_size;
    float max_ratio = 1.0f - min_ratio;
    new_ratio = std::clamp(new_ratio, std::max(SplitPane::MIN_RATIO, min_ratio),
                           std::min(SplitPane::MAX_RATIO, max_ratio));

    dragging_splitter_->set_split_ratio(new_ratio);
    recompute_layout();
}

void SplitViewManager::end_splitter_drag() {
    std::lock_guard<std::mutex> lock(mutex_);
    dragging_splitter_ = nullptr;
}

std::string SplitViewManager::serialize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream ss;
    ss << "{\"active\":" << active_figure_index_;
    if (root_) {
        ss << ",\"root\":" << root_->serialize();
    }
    ss << "}";
    return ss.str();
}

bool SplitViewManager::deserialize(const std::string& data) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (data.empty()) return false;

    // Find active figure index
    auto active_pos = data.find("\"active\":");
    if (active_pos != std::string::npos) {
        active_pos += 9;
        auto end = data.find_first_of(",}", active_pos);
        if (end != std::string::npos) {
            active_figure_index_ = static_cast<size_t>(
                std::stoul(data.substr(active_pos, end - active_pos)));
        }
    }

    // Find root object
    auto root_pos = data.find("\"root\":");
    if (root_pos != std::string::npos) {
        root_pos += 7;
        // Find matching brace
        int depth = 0;
        size_t start = root_pos;
        for (size_t i = root_pos; i < data.size(); ++i) {
            if (data[i] == '{') ++depth;
            else if (data[i] == '}') {
                --depth;
                if (depth == 0) {
                    auto root_data = data.substr(start, i - start + 1);
                    auto new_root = SplitPane::deserialize(root_data);
                    if (new_root) {
                        root_ = std::move(new_root);
                        recompute_layout();
                        return true;
                    }
                    return false;
                }
            }
        }
    }

    return false;
}

void SplitViewManager::recompute_layout() {
    if (root_ && canvas_bounds_.w > 0.0f && canvas_bounds_.h > 0.0f) {
        root_->compute_layout(canvas_bounds_);
    }
}

} // namespace plotix
