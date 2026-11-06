#pragma once

// layout.hpp — shareable layout files (File > Save Layout / Open Layout).
//
// A layout is a workspace v4 JSON file with "layout_only": true.  It keeps the
// figures, subplot grid, axes limits / labels, series styles and colours,
// transform presets and dock arrangement, and for each series a data-binding
// URI instead of its data.  Opening it builds empty figures and re-resolves
// every binding (binding_resolver.hpp), so a teammate can open the same
// dashboard against their own CSV files or live topics.

#include <cstddef>
#include <functional>
#include <optional>
#include <spectra/fwd.hpp>
#include <string>
#include <vector>

#include "binding_resolver.hpp"
#include "workspace.hpp"

namespace spectra
{

class FigureRegistry;
class Series;

// One series of an opened layout and what its binding resolved to.
struct LayoutBindingSlot
{
    FigureId                         figure_id  = INVALID_FIGURE_ID;
    uint32_t                         axes_index = 0;
    const Series*                    series     = nullptr;   // identity only: see find_series()
    std::string                      uri;                    // "" = no binding saved
    std::string                      label;                  // saved series label
    BindingResult                    result;
    std::vector<LayoutTransformStep> steps;   // the axes' saved transform steps
};

struct LayoutOpenResult
{
    std::vector<FigureId>          figure_ids;   // registered, in file order
    std::vector<LayoutBindingSlot> slots;        // every rebuilt series

    size_t count(BindingStatus status) const;
};

// Builds and registers one figure per layout figure (Workspace::build_layout_figure
// + Workspace::apply) and resolves every series binding through @p resolver.
// A plain workspace (layout_only == false) gets its figures without series.
// Unresolved series stay empty and get the unresolved_label() as legend label.
LayoutOpenResult instantiate_layout(const WorkspaceData&   data,
                                    FigureRegistry&        registry,
                                    const BindingResolver& resolver);

// Re-resolve one slot (e.g. after the user picked a replacement binding);
// updates slot.uri, slot.result, the series binding and its label.
// Returns false when the slot's series no longer exists.
bool rebind_slot(LayoutBindingSlot&     slot,
                 const std::string&     uri,
                 FigureRegistry&        registry,
                 const BindingResolver& resolver);

// Legend label of a series whose binding could not be resolved.
std::string unresolved_label(const std::string& label, const std::string& uri);

// The live series @p series of figure @p figure_id, or nullptr if the figure or
// series is gone.  Only pointer identity is compared, so a stale pointer is
// never dereferenced.
Series* find_series(const FigureRegistry& registry, FigureId figure_id, const Series* series);

// Rewrites the figure ids in a DockSystem state string ("figure" of each
// leaf pane and "active").  @p map returns the replacement id, or nullopt to
// keep it.  Layouts store figure positions (0, 1, ...) instead of this
// session's ids.
std::string remap_dock_figures(const std::string&                                      dock_state,
                               const std::function<std::optional<FigureId>(FigureId)>& map);

// Most-recently used layout files (File > Recent Layouts), one absolute path
// per line in <config dir>/recent_layouts.txt.
class RecentLayouts
{
   public:
    static constexpr size_t DEFAULT_MAX_ENTRIES = 10;

    // Empty path = in memory only.
    explicit RecentLayouts(std::string path = {}, size_t max_entries = DEFAULT_MAX_ENTRIES);

    static std::string default_path();

    bool load();
    bool save() const;
    // Moves @p file to the front (absolute, normalized) and saves.
    void push(const std::string& file);
    void remove(const std::string& file);
    void clear();

    const std::vector<std::string>& entries() const { return entries_; }
    bool                            empty() const { return entries_.empty(); }

   private:
    std::string              path_;
    size_t                   max_entries_;
    std::vector<std::string> entries_;
};

}   // namespace spectra
