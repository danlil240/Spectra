// data_provider.hpp — source-agnostic interface behind the Data sidebar.
//
// The Data sidebar lists every data source the app knows about (open log
// files, live topics, plugin data sources, loaded CSV files, ...) as one
// searchable tree:
//
//   provider ("Log files")  →  source ("flight.ulg")  →  [group]  →  field
//
// Each kind of source plugs in through a thin DataProvider adapter that lists
// its sources and fields and turns a field into a series on a plot.  The
// adapters wrap the existing panels (Log Browser, Topics, CSV loader), which
// stay available as the "details" windows.
//
// Everything here is headless (no ImGui), so providers and the sidebar model
// are unit-testable.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ui/theme/icons.hpp"

namespace spectra
{
class Axes;
}

namespace spectra::ui::data_sidebar
{

// One plottable signal.
struct DataField
{
    std::string group;     // optional sub-folder (e.g. the log topic); "" for none
    std::string name;      // label in the tree (e.g. "q[0]", "/imu/accel_x")
    std::string detail;    // secondary text (sample count, unit, rate, ...)
    uint64_t    key = 0;   // provider-defined, stable while the source exists
};

// A container of fields (a file, a publisher, a plugin source, ...).
struct DataSource
{
    enum class State
    {
        Static,    // a file or other source with no run state
        Stopped,   // a live source that can be started
        Running,
        Faulted,
    };

    std::string            name;
    std::string            detail;
    uint64_t               key   = 0;
    State                  state = State::Static;
    std::vector<DataField> fields;   // fields sharing a group are contiguous
};

// Where "Plot" puts a field: the axes to add a series to (file-backed
// providers) and the figure id / axes index (topic subscriptions).
// figure_id 0 asks the topic transport to create a figure.
struct PlotTarget
{
    Axes*    axes       = nullptr;
    uint64_t figure_id  = 0;
    uint32_t axes_index = 0;
};

struct PlotResult
{
    bool        ok = false;
    std::string message;   // shown in the sidebar's status line
};

// Drag-and-drop payload for a field row.  A provider whose fields already
// have a canvas drop target (log fields, topics) returns that payload; an
// empty type makes the sidebar use its generic DATA_FIELD_DRAG_TYPE.
struct DragPayload
{
    std::string                type;
    std::vector<unsigned char> data;

    bool empty() const { return type.empty(); }
};

class DataProvider
{
   public:
    DataProvider()                               = default;
    virtual ~DataProvider()                      = default;
    DataProvider(const DataProvider&)            = delete;
    DataProvider& operator=(const DataProvider&) = delete;

    // Stable id ("logs", "topics", "csv") and the provider row's title.
    virtual std::string_view id() const    = 0;
    virtual std::string_view title() const = 0;
    virtual Icon             icon() const { return Icon::Database; }

    // Append the current sources (called about once per frame while the
    // sidebar is open; providers may use it to request fresh data).
    virtual void list_sources(std::vector<DataSource>& out) = 0;

    // Add field `field_key` of source `source_key` to `target`.
    virtual PlotResult plot_field(uint64_t          source_key,
                                  uint64_t          field_key,
                                  const PlotTarget& target) = 0;

    // Native drag payload for a field (see DragPayload).
    virtual DragPayload drag_payload(uint64_t /*source_key*/, uint64_t /*field_key*/) const
    {
        return {};
    }

    // Start / stop a live source.  Returns false if unsupported.
    virtual bool set_source_running(uint64_t /*source_key*/, bool /*run*/) { return false; }

    // Close / forget a source (e.g. a log file).  Returns false if unsupported.
    virtual bool supports_close() const { return false; }
    virtual bool close_source(uint64_t /*source_key*/) { return false; }

    // Optional "open more" action shown on the provider row and in its empty
    // state (e.g. "Open Log..."), and the floating window with more detail.
    virtual std::string open_label() const { return {}; }
    virtual void        open() {}
    virtual std::string details_label() const { return {}; }
    virtual void        open_details() {}

    // Text shown under the provider when it has no sources.
    virtual std::string empty_hint() const { return {}; }
};

}   // namespace spectra::ui::data_sidebar
