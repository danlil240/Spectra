#pragma once

#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace plotix {

// A single undoable action with forward (redo) and backward (undo) operations.
struct UndoAction {
    std::string description;       // Human-readable description, e.g. "Change line color"
    std::function<void()> undo_fn; // Restores previous state
    std::function<void()> redo_fn; // Re-applies the change
};

// Manages an undo/redo stack for property changes.
// Thread-safe: push/undo/redo may be called from any thread.
// Capped at MAX_STACK_SIZE entries to prevent unbounded memory growth.
class UndoManager {
public:
    static constexpr size_t MAX_STACK_SIZE = 100;

    UndoManager() = default;
    ~UndoManager() = default;

    UndoManager(const UndoManager&) = delete;
    UndoManager& operator=(const UndoManager&) = delete;

    // Push a new undoable action. Clears the redo stack.
    // If the stack exceeds MAX_STACK_SIZE, the oldest entry is removed.
    void push(UndoAction action);

    // Convenience: push with captured before/after values.
    // setter is called with `before` on undo and `after` on redo.
    template <typename T>
    void push_value(const std::string& description,
                    T before, T after,
                    std::function<void(const T&)> setter) {
        UndoAction action;
        action.description = description;
        action.undo_fn = [setter, before]() { setter(before); };
        action.redo_fn = [setter, after]() { setter(after); };
        push(std::move(action));
    }

    // Undo the last action. Returns false if nothing to undo.
    bool undo();

    // Redo the last undone action. Returns false if nothing to redo.
    bool redo();

    // Check if undo/redo are available.
    bool can_undo() const;
    bool can_redo() const;

    // Get description of the next undo/redo action.
    std::string undo_description() const;
    std::string redo_description() const;

    // Number of items in undo/redo stacks.
    size_t undo_count() const;
    size_t redo_count() const;

    // Clear all undo/redo history.
    void clear();

    // Begin/end a group: multiple pushes between begin_group/end_group
    // are treated as a single undoable action.
    void begin_group(const std::string& description);
    void end_group();
    bool in_group() const;

private:
    mutable std::mutex mutex_;
    std::vector<UndoAction> undo_stack_;
    std::vector<UndoAction> redo_stack_;

    // Group state
    bool grouping_ = false;
    std::string group_description_;
    std::vector<UndoAction> group_actions_;
};

} // namespace plotix
