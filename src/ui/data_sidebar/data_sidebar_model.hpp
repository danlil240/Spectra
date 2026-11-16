// data_sidebar_model.hpp — headless state of the Data sidebar.
//
// Holds the registered DataProviders, a snapshot of their sources, the search
// query, expansion and selection state, and flattens all of it into the list
// of visible rows the sidebar draws.  Keyboard navigation (up / down / left /
// right / enter) and the "plot field" action live here so they can be tested
// without ImGui.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ui/data_sidebar/data_provider.hpp"

namespace spectra::ui::data_sidebar
{

// Generic drag payload for fields whose provider has no native drop target.
inline constexpr const char* DATA_FIELD_DRAG_TYPE = "SPECTRA_DATA_FIELD";

struct DataFieldDragPayload
{
    uint32_t provider_index = 0;
    uint32_t reserved       = 0;
    uint64_t source_key     = 0;
    uint64_t field_key      = 0;
};

// Case-insensitive match of a search query against a field: every
// whitespace-separated term must occur in "source/group/name".  An empty query
// matches everything.
bool field_matches_query(std::string_view source_name,
                         const DataField& field,
                         std::string_view query);

enum class RowKind
{
    Provider,
    Source,
    Group,
    Field,
    Empty,   // placeholder under a provider without sources (shows its hint)
};

struct DataRow
{
    RowKind     kind     = RowKind::Field;
    int         depth    = 0;
    size_t      provider = 0;
    size_t      source   = 0;   // index into sources(provider)
    size_t      field    = 0;   // Field: index into fields; Group: first field
    std::string id;             // stable identity (expansion / selection)
    std::string label;
    std::string detail;
    size_t      count    = 0;   // containers: (matching) fields beneath
    bool        expanded = false;
};

class DataSidebarModel
{
   public:
    using TargetFn = std::function<PlotTarget()>;

    DataSidebarModel()                                   = default;
    ~DataSidebarModel()                                  = default;
    DataSidebarModel(const DataSidebarModel&)            = delete;
    DataSidebarModel& operator=(const DataSidebarModel&) = delete;

    // ─── Providers ───────────────────────────────────────────────────────

    DataProvider* add_provider(std::unique_ptr<DataProvider> provider);
    size_t        provider_count() const { return providers_.size(); }
    DataProvider* provider(size_t index) const;
    DataProvider* find_provider(std::string_view id) const;

    // Where Plot / double-click / Enter put a field (the active plot).
    void set_target_provider(TargetFn fn) { target_fn_ = std::move(fn); }

    // Re-list every provider's sources and rebuild the rows.
    void                           refresh();
    const std::vector<DataSource>& sources(size_t provider) const;

    // ─── Search ──────────────────────────────────────────────────────────

    void               set_query(std::string query);
    const std::string& query() const { return query_; }
    bool               filtering() const;
    // Fields matching the query (all fields when not filtering).
    size_t match_count() const { return match_count_; }
    size_t field_count() const { return field_count_; }
    size_t source_count() const { return source_count_; }

    // ─── Rows ────────────────────────────────────────────────────────────

    const std::vector<DataRow>& rows() const { return rows_; }
    // Containers default to expanded (providers, sources) or collapsed
    // (groups).  While filtering every container is shown expanded.
    void set_expanded(const std::string& row_id, bool expanded);
    void toggle_expanded(size_t row);

    // ─── Selection / keyboard ────────────────────────────────────────────

    // Index into rows(), -1 for none.
    int  selected() const { return selected_; }
    void select(size_t row);
    void clear_selection();
    // Move by `delta` rows (clamped), skipping placeholder rows.
    void move_selection(int delta);
    // Right: expand, or step into the first child.  Left: collapse, or step
    // to the parent.
    void expand_selected();
    void collapse_selected();
    // Enter: plot a field, toggle a container.
    PlotResult activate_selected();

    // ─── Actions ─────────────────────────────────────────────────────────

    // Plot a field row into the target from the TargetFn (or `target`).
    PlotResult plot_row(size_t row);
    PlotResult plot_row(size_t row, const PlotTarget& target);
    PlotResult plot_field(size_t            provider,
                          uint64_t          source_key,
                          uint64_t          field_key,
                          const PlotTarget& target);
    // Plot a dropped generic payload into `target`.
    PlotResult plot_payload(const DataFieldDragPayload& payload, const PlotTarget& target);

    // Payload for dragging a field row: the provider's native one, else a
    // DATA_FIELD_DRAG_TYPE DataFieldDragPayload.  Empty for other rows.
    DragPayload drag_payload(size_t row) const;

    PlotTarget current_target() const { return target_fn_ ? target_fn_() : PlotTarget{}; }

    // Last action's message.
    const std::string& status() const { return status_; }
    bool               status_is_error() const { return status_error_; }
    void               set_status(std::string message, bool error);

   private:
    void rebuild_rows();
    bool is_expanded(const std::string& id, bool default_expanded) const;
    void select_first_field();

    std::vector<std::unique_ptr<DataProvider>> providers_;
    std::vector<std::vector<DataSource>>       sources_;
    TargetFn                                   target_fn_;

    std::string                           query_;
    std::unordered_map<std::string, bool> expanded_;
    std::vector<DataRow>                  rows_;
    int                                   selected_ = -1;
    std::string                           selected_id_;

    size_t match_count_  = 0;
    size_t field_count_  = 0;
    size_t source_count_ = 0;

    std::string status_;
    bool        status_error_ = false;
};

}   // namespace spectra::ui::data_sidebar
