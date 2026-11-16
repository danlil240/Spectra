#pragma once

// View history: the Back / Forward list of 2D view states of a figure.
//
// Every completed view change (drag pan, a wheel-zoom burst, box zoom,
// auto-fit / home, keyboard pan / zoom, axis-range edits in the Inspector)
// is one entry holding the limits of every axes it moved (the axes and its
// linked peers) before and after.  Back restores an entry's "before" state,
// Forward re-applies its "after" state, and a new change drops the entries
// ahead of the cursor, like a browser history.
//
// Relation to Ctrl+Z: InputHandler also pushes each new entry onto the undo
// stack, so Ctrl+Z / Ctrl+Y undo and redo view changes in order with other
// edits (one step per drag or wheel burst) and keep this cursor in step.
// Back / Forward only walk view states: they are navigation, not edits, and
// push nothing onto the undo stack.

#include <chrono>
#include <cstddef>
#include <memory>
#include <spectra/axes.hpp>
#include <spectra/fwd.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace spectra
{

// Limits of one 2D axes.
struct AxesView
{
    Axes*      axes = nullptr;
    AxisLimits x{};
    AxisLimits y{};
};

// Limits of a set of axes (no duplicates).
using ViewState = std::vector<AxesView>;

// The entry for @p ax, or nullptr.
const AxesView* find_axes_view(const ViewState& state, const Axes* ax);

// Set (or add) @p ax's limits in @p state.
void set_axes_view(ViewState& state, Axes* ax, AxisLimits x, AxisLimits y);

// True when both states hold the same axes with identical limits.
bool same_view(const ViewState& a, const ViewState& b);

// Set every axes of @p state that still belongs to @p fig to its limits
// (stale pointers of removed axes are skipped).  Returns the count applied.
size_t apply_view_state(Figure& fig, const ViewState& state);

struct ViewHistoryEntry
{
    std::string                           label;
    ViewState                             before;
    ViewState                             after;
    std::string                           coalesce_key;
    std::chrono::steady_clock::time_point updated{};
};

class ViewHistory
{
   public:
    using Clock                         = std::chrono::steady_clock;
    static constexpr size_t MAX_ENTRIES = 100;

    struct PushResult
    {
        std::shared_ptr<ViewHistoryEntry> entry;   // nullptr: nothing changed
        bool                              coalesced = false;
    };

    // Record a change from @p before to @p after.  A change that moves
    // nothing is dropped.  With a non-empty @p coalesce_key equal to the
    // newest entry's (and the cursor at the end), the change is merged into
    // that entry instead: its "before" stays, its "after" becomes @p after.
    // A positive @p coalesce_window limits merging to changes that follow
    // the previous one within that time (a wheel burst); zero merges for as
    // long as the key matches (one Inspector drag).
    PushResult push(std::string       label,
                    ViewState         before,
                    ViewState         after,
                    std::string       coalesce_key    = {},
                    Clock::duration   coalesce_window = Clock::duration::zero(),
                    Clock::time_point now             = Clock::now());

    bool can_back() const { return cursor_ > 0; }
    bool can_forward() const { return cursor_ < entries_.size(); }

    // Step back / forward; returns the state to apply, or nullptr at an end.
    const ViewState* back();
    const ViewState* forward();

    // Undo / redo of @p entry from the undo stack: returns the state to apply
    // and moves the cursor across the entry when it is the adjacent one.
    const ViewState& undo_entry(const std::shared_ptr<ViewHistoryEntry>& entry);
    const ViewState& redo_entry(const std::shared_ptr<ViewHistoryEntry>& entry);

    // Labels of the entries Back / Forward would cross ("" at an end).
    std::string back_label() const;
    std::string forward_label() const;

    // Stop the newest entry from absorbing further changes (something else
    // happened since it was recorded).
    void seal_last()
    {
        if (!entries_.empty())
            entries_.back()->coalesce_key.clear();
    }

    // Newest entry (nullptr when empty).
    std::shared_ptr<ViewHistoryEntry> last_entry() const
    {
        return entries_.empty() ? nullptr : entries_.back();
    }

    size_t size() const { return entries_.size(); }
    size_t cursor() const { return cursor_; }
    void   clear();

   private:
    std::vector<std::shared_ptr<ViewHistoryEntry>> entries_;
    size_t                                         cursor_ = 0;   // entries applied
};

// One ViewHistory per figure, owned by the window's InputHandler.
class ViewHistoryStore
{
   public:
    // History of @p fig, created on first use.
    std::shared_ptr<ViewHistory> history(const Figure* fig);
    // History of @p fig, or nullptr when it has none yet.
    ViewHistory* find(const Figure* fig) const;
    // Drop @p fig's history (the figure is being destroyed).
    void   forget(const Figure* fig);
    void   clear() { histories_.clear(); }
    size_t size() const { return histories_.size(); }

   private:
    std::unordered_map<const Figure*, std::shared_ptr<ViewHistory>> histories_;
};

}   // namespace spectra
