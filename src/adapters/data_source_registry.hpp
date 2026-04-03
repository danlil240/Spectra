#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "adapters/adapter_interface.hpp"

namespace spectra
{

// Registry for runtime data-source adapters (plugin or built-in).
// Thread-safe for registration; poll / build_ui must be called from the UI thread.
class DataSourceRegistry
{
   public:
    DataSourceRegistry()  = default;
    ~DataSourceRegistry() = default;

    DataSourceRegistry(const DataSourceRegistry&)            = delete;
    DataSourceRegistry& operator=(const DataSourceRegistry&) = delete;

    // Register an adapter.  Ownership is transferred to the registry.
    void register_source(std::unique_ptr<DataSourceAdapter> adapter);

    // Remove a data source by name.
    void unregister_source(const std::string& name);

    // Enumerate registered source names.
    std::vector<std::string> source_names() const;

    // Number of registered sources.
    size_t count() const;

    // Access an adapter by name (nullptr if not found).
    // The returned pointer is valid until unregister_source() is called.
    DataSourceAdapter* find(const std::string& name) const;

    // Poll all running sources and collect their data points.
    std::vector<DataPoint> poll_all();

   private:
    struct Entry
    {
        std::string                        name;
        std::unique_ptr<DataSourceAdapter> adapter;
    };

    mutable std::mutex mutex_;
    std::vector<Entry> sources_;
};

}   // namespace spectra
