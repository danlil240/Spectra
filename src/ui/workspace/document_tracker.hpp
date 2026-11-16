#pragma once

// document_tracker.hpp — "has this window changed since it was saved?"
//
// A window's figures are fingerprinted (figure_fingerprint()) every frame.
// Changes the user makes are picked up by detect(); changes the program makes
// itself (on_frame animation callbacks, data streamed from a Python/IPC
// client, theme colours resolved while drawing) are folded into the saved
// baseline by absorb(), so a plot that only animates is never "modified".
// The frame loop brackets every program-driven region with detect() before
// and absorb() after it (see WindowRuntime::update()).

#include <cstdint>
#include <spectra/fwd.hpp>
#include <unordered_map>
#include <utility>
#include <vector>

namespace spectra
{

// Hash of what a save would store and the user can edit: subplot layout,
// legend, axes titles/labels/grid, and per series its identity, data
// generation (Series::change_generation()), label, visibility and style.
// View state (axis limits, camera) is not part of it.
uint64_t figure_fingerprint(const Figure& figure);

class DocumentTracker
{
   public:
    struct Snapshot
    {
        std::vector<std::pair<FigureId, uint64_t>> figures;         // tab order
        uint64_t                                   structure = 0;   // tab set, order, titles
    };

    // Compares @p now with the baseline; parts that differ become modified
    // (sticky until mark_saved()).  Returns true if anything newly changed.
    bool detect(const Snapshot& now);
    // Moves the baseline of every part that is not modified to @p now.
    void absorb(const Snapshot& now);
    // Everything in @p now is saved.
    void mark_saved(const Snapshot& now);
    // Marks the whole document modified (e.g. a change outside the fingerprint).
    void mark_modified() { forced_ = true; }

    bool is_modified() const;
    // True for every tracked figure after mark_modified().
    bool is_figure_modified(FigureId id) const;
    // Number of modified figures (structure changes not counted).
    size_t modified_figure_count() const;

   private:
    struct Entry
    {
        uint64_t baseline = 0;
        bool     modified = false;
    };
    std::unordered_map<FigureId, Entry> figures_;
    Entry                               structure_;
    bool                                initialized_ = false;
    bool                                forced_      = false;
};

}   // namespace spectra
