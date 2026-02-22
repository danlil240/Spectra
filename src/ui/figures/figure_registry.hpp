#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <spectra/figure.hpp>
#include <spectra/fwd.hpp>
#include <unordered_map>
#include <vector>

namespace spectra
{

/**
 * FigureRegistry — Stable-ID figure ownership for multi-window support.
 *
 * Replaces positional indexing (vector<unique_ptr<Figure>>) with monotonic
 * uint64_t IDs that are never reused.  Figures can be looked up, iterated,
 * and moved between windows without invalidating pointers or GPU buffers.
 *
 * Thread-safe: all public methods lock an internal mutex.
 */
class FigureRegistry
{
   public:
    using IdType = uint64_t;

    FigureRegistry()  = default;
    ~FigureRegistry() = default;

    FigureRegistry(const FigureRegistry&)            = delete;
    FigureRegistry& operator=(const FigureRegistry&) = delete;

    // Register a figure and return its stable ID.
    // The registry takes ownership of the Figure.
    IdType register_figure(std::unique_ptr<Figure> fig);

    // Unregister and destroy a figure by ID.
    // No-op if the ID is invalid.
    void unregister_figure(IdType id);

    // Look up a figure by ID.  Returns nullptr if not found.
    Figure* get(IdType id) const;

    // Return all currently registered IDs (insertion order).
    std::vector<IdType> all_ids() const;

    // Number of registered figures.
    size_t count() const;

    // Check if an ID is registered.
    bool contains(IdType id) const;

    // Reverse lookup: find the ID for a given Figure pointer.
    // Returns 0 (INVALID_FIGURE_ID) if not found.
    IdType find_id(const Figure* fig) const;

    // Release ownership of a figure (removes from registry, returns the unique_ptr).
    // Returns nullptr if the ID is not found.
    std::unique_ptr<Figure> release(IdType id);

    // Clear all figures.
    void clear();

   private:
    mutable std::mutex                                  mutex_;
    std::unordered_map<IdType, std::unique_ptr<Figure>> figures_;
    std::vector<IdType> insertion_order_;   // preserves iteration order
    IdType              next_id_ = 1;
};

}   // namespace spectra
