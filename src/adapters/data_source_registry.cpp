#include "adapters/data_source_registry.hpp"

#include "spectra/logger.hpp"
#include "ui/workspace/plugin_guard.hpp"

namespace spectra
{

void DataSourceRegistry::register_source(std::unique_ptr<DataSourceAdapter> adapter)
{
    if (!adapter)
        return;

    std::lock_guard lock(mutex_);

    std::string adapter_name = adapter->name();

    // Reject duplicate names.
    for (const auto& e : sources_)
    {
        if (e.name == adapter_name)
        {
            SPECTRA_LOG_WARN("plugin",
                             "Data source '{}' already registered — ignoring duplicate",
                             adapter_name);
            return;
        }
    }

    Entry entry;
    entry.name    = std::move(adapter_name);
    entry.adapter = std::move(adapter);
    sources_.push_back(std::move(entry));
}

void DataSourceRegistry::unregister_source(const std::string& name)
{
    std::lock_guard lock(mutex_);
    for (auto it = sources_.begin(); it != sources_.end(); ++it)
    {
        if (it->name == name)
        {
            // Stop the adapter before removing.
            if (it->adapter && it->adapter->is_running())
            {
                it->adapter->stop();
            }
            sources_.erase(it);
            return;
        }
    }
}

std::vector<std::string> DataSourceRegistry::source_names() const
{
    std::lock_guard          lock(mutex_);
    std::vector<std::string> names;
    names.reserve(sources_.size());
    for (const auto& e : sources_)
    {
        names.push_back(e.name);
    }
    return names;
}

size_t DataSourceRegistry::count() const
{
    std::lock_guard lock(mutex_);
    return sources_.size();
}

DataSourceAdapter* DataSourceRegistry::find(const std::string& name) const
{
    std::lock_guard lock(mutex_);
    for (const auto& e : sources_)
    {
        if (e.name == name)
            return e.adapter.get();
    }
    return nullptr;
}

std::vector<DataPoint> DataSourceRegistry::poll_all()
{
    std::lock_guard        lock(mutex_);
    std::vector<DataPoint> result;
    for (auto& e : sources_)
    {
        if (e.faulted)
            continue;

        if (e.adapter && e.adapter->is_running())
        {
            std::vector<DataPoint> pts;
            auto                   guard_result =
                plugin_guard_invoke(e.name.c_str(), [&]() { pts = e.adapter->poll(); });
            if (guard_result != PluginCallResult::Success)
            {
                SPECTRA_LOG_ERROR("plugin",
                                  "Data source '{}' faulted during poll — disabling",
                                  e.name);
                e.faulted = true;
                continue;
            }
            result.insert(result.end(),
                          std::make_move_iterator(pts.begin()),
                          std::make_move_iterator(pts.end()));
        }
    }
    return result;
}

}   // namespace spectra
