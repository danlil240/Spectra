#pragma once

#include <string>
#include <vector>

namespace spectra
{

// A single data point produced by a data source adapter.
struct DataPoint
{
    std::string series_label;
    double      timestamp;
    float       value;
};

// Abstract interface for runtime data source adapters.
//
// Concrete implementations supply streaming data from external systems
// (e.g. ROS2, PX4, CSV replay).  Adapters registered via the plugin C ABI
// use a thin wrapper that delegates to C function pointers instead.
class DataSourceAdapter
{
   public:
    virtual ~DataSourceAdapter() = default;

    DataSourceAdapter()                                    = default;
    DataSourceAdapter(const DataSourceAdapter&)            = delete;
    DataSourceAdapter& operator=(const DataSourceAdapter&) = delete;

    // Human-readable adapter name (e.g. "ROS2 Topics").
    virtual const char* name() const = 0;

    // Start producing data.  Called once when the user activates the source.
    virtual void start() = 0;

    // Stop producing data.
    virtual void stop() = 0;

    // Whether the adapter is currently running.
    virtual bool is_running() const = 0;

    // Poll for new data points.  Called once per frame.
    // Returns an empty vector when there is nothing new.
    virtual std::vector<DataPoint> poll() = 0;

    // Optional ImGui panel drawn in the Data Sources window.
    // Default implementation does nothing.
    virtual void build_ui() {}
};

}   // namespace spectra
