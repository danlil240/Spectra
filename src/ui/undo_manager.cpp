#include "undo_manager.hpp"

#include <algorithm>

namespace plotix {

// ─── Push ────────────────────────────────────────────────────────────────────

void UndoManager::push(UndoAction action) {
    std::lock_guard lock(mutex_);

    if (grouping_) {
        group_actions_.push_back(std::move(action));
        return;
    }

    // Clear redo stack on new action
    redo_stack_.clear();

    undo_stack_.push_back(std::move(action));

    // Enforce max stack size
    if (undo_stack_.size() > MAX_STACK_SIZE) {
        undo_stack_.erase(undo_stack_.begin());
    }
}

// ─── Undo / Redo ─────────────────────────────────────────────────────────────

bool UndoManager::undo() {
    std::function<void()> fn;
    {
        std::lock_guard lock(mutex_);
        if (undo_stack_.empty()) return false;

        auto action = std::move(undo_stack_.back());
        undo_stack_.pop_back();
        fn = action.undo_fn;
        redo_stack_.push_back(std::move(action));
    }
    // Execute outside lock
    if (fn) fn();
    return true;
}

bool UndoManager::redo() {
    std::function<void()> fn;
    {
        std::lock_guard lock(mutex_);
        if (redo_stack_.empty()) return false;

        auto action = std::move(redo_stack_.back());
        redo_stack_.pop_back();
        fn = action.redo_fn;
        undo_stack_.push_back(std::move(action));
    }
    // Execute outside lock
    if (fn) fn();
    return true;
}

// ─── Queries ─────────────────────────────────────────────────────────────────

bool UndoManager::can_undo() const {
    std::lock_guard lock(mutex_);
    return !undo_stack_.empty();
}

bool UndoManager::can_redo() const {
    std::lock_guard lock(mutex_);
    return !redo_stack_.empty();
}

std::string UndoManager::undo_description() const {
    std::lock_guard lock(mutex_);
    return undo_stack_.empty() ? "" : undo_stack_.back().description;
}

std::string UndoManager::redo_description() const {
    std::lock_guard lock(mutex_);
    return redo_stack_.empty() ? "" : redo_stack_.back().description;
}

size_t UndoManager::undo_count() const {
    std::lock_guard lock(mutex_);
    return undo_stack_.size();
}

size_t UndoManager::redo_count() const {
    std::lock_guard lock(mutex_);
    return redo_stack_.size();
}

void UndoManager::clear() {
    std::lock_guard lock(mutex_);
    undo_stack_.clear();
    redo_stack_.clear();
    grouping_ = false;
    group_actions_.clear();
}

// ─── Grouping ────────────────────────────────────────────────────────────────

void UndoManager::begin_group(const std::string& description) {
    std::lock_guard lock(mutex_);
    grouping_ = true;
    group_description_ = description;
    group_actions_.clear();
}

void UndoManager::end_group() {
    std::lock_guard lock(mutex_);
    if (!grouping_) return;
    grouping_ = false;

    if (group_actions_.empty()) return;

    // Combine all group actions into a single UndoAction
    auto actions = std::move(group_actions_);
    std::string desc = std::move(group_description_);

    UndoAction combined;
    combined.description = desc;
    combined.undo_fn = [actions]() {
        // Undo in reverse order
        for (auto it = actions.rbegin(); it != actions.rend(); ++it) {
            if (it->undo_fn) it->undo_fn();
        }
    };
    combined.redo_fn = [actions]() {
        // Redo in forward order
        for (const auto& a : actions) {
            if (a.redo_fn) a.redo_fn();
        }
    };

    // Clear redo stack on new action
    redo_stack_.clear();
    undo_stack_.push_back(std::move(combined));

    if (undo_stack_.size() > MAX_STACK_SIZE) {
        undo_stack_.erase(undo_stack_.begin());
    }
}

bool UndoManager::in_group() const {
    std::lock_guard lock(mutex_);
    return grouping_;
}

} // namespace plotix
