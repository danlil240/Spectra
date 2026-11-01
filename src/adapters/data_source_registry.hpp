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
    // `owner` is an opaque token (the plugin module that registered it, nullptr
    // for the host) used by unregister_owner() / set_owner_enabled().
    void register_source(std::unique_ptr<DataSourceAdapter> adapter, const void* owner = nullptr);

    // Remove a data source by name.
    void unregister_source(const std::string& name);

    // Stop and remove every source registered with `owner` (nullptr: none).
    // Returns the number removed.
    size_t unregister_owner(const void* owner);

    // Enable/disable every source registered with `owner`.  Disabling stops a
    // running source; disabled sources stay registered but are not listed by
    // source_names(), find() does not return them and poll_all() skips them.
    void set_owner_enabled(const void* owner, bool enabled);

    // Number of sources (enabled or not) registered with `owner`.
    size_t count_owned(const void* owner) const;

    // Enumerate enabled source names.
    std::vector<std::string> source_names() const;

    // Number of registered sources (enabled or not).
    size_t count() const;

    // Access an enabled adapter by name (nullptr if not found or disabled).
    // The returned pointer is valid until unregister_source() is called.
    DataSourceAdapter* find(const std::string& name) const;

    // Poll all enabled, running sources and collect their data points.
    std::vector<DataPoint> poll_all();

    // Points polled from one source.
    struct SourceBatch
    {
        std::string            source;
        std::vector<DataPoint> points;
    };

    // Like poll_all(), but keeps each source's points apart (sources that
    // returned nothing are omitted).
    std::vector<SourceBatch> poll_all_by_source();

    struct SourceStatus
    {
        std::string name;
        bool        running = false;
        bool        faulted = false;
    };

    // Enabled sources with their running / faulted state, in registration order.
    std::vector<SourceStatus> status() const;

    // Start (run = true) or stop an enabled, non-faulted source.  Returns
    // false when no such source exists.  A plugin that throws is marked
    // faulted, as in poll_all().
    bool set_running(const std::string& name, bool run);

   private:
    struct Entry
    {
        std::string                        name;
        std::unique_ptr<DataSourceAdapter> adapter;
        bool                               faulted = false;   // Set true on crash/exception
        const void*                        owner   = nullptr;
        bool                               enabled = true;
    };

    mutable std::mutex mutex_;
    std::vector<Entry> sources_;
};

}   // namespace spectra
