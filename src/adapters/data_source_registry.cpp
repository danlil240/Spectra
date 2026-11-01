#include "adapters/data_source_registry.hpp"

#include "spectra/logger.hpp"
#include "ui/workspace/plugin_guard.hpp"

namespace spectra
{

void DataSourceRegistry::register_source(std::unique_ptr<DataSourceAdapter> adapter,
                                         const void*                        owner)
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
    entry.owner   = owner;
    sources_.push_back(std::move(entry));
}

size_t DataSourceRegistry::unregister_owner(const void* owner)
{
    if (!owner)
        return 0;
    std::lock_guard lock(mutex_);
    for (auto& e : sources_)
    {
        // Stop while the owner's code is still loaded.
        if (e.owner == owner && e.adapter && !e.faulted && e.adapter->is_running())
            e.adapter->stop();
    }
    return std::erase_if(sources_, [owner](const Entry& e) { return e.owner == owner; });
}

void DataSourceRegistry::set_owner_enabled(const void* owner, bool enabled)
{
    if (!owner)
        return;
    std::lock_guard lock(mutex_);
    for (auto& e : sources_)
    {
        if (e.owner != owner)
            continue;
        if (!enabled && e.enabled && e.adapter && !e.faulted && e.adapter->is_running())
            e.adapter->stop();
        e.enabled = enabled;
    }
}

size_t DataSourceRegistry::count_owned(const void* owner) const
{
    if (!owner)
        return 0;
    std::lock_guard lock(mutex_);
    size_t          n = 0;
    for (const auto& e : sources_)
    {
        if (e.owner == owner)
            ++n;
    }
    return n;
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
        if (e.enabled)
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
        if (e.name == name && e.enabled)
            return e.adapter.get();
    }
    return nullptr;
}

std::vector<DataPoint> DataSourceRegistry::poll_all()
{
    std::vector<DataPoint> result;
    for (auto& batch : poll_all_by_source())
    {
        result.insert(result.end(),
                      std::make_move_iterator(batch.points.begin()),
                      std::make_move_iterator(batch.points.end()));
    }
    return result;
}

std::vector<DataSourceRegistry::SourceBatch> DataSourceRegistry::poll_all_by_source()
{
    std::lock_guard          lock(mutex_);
    std::vector<SourceBatch> result;
    for (auto& e : sources_)
    {
        if (e.faulted || !e.enabled)
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
            if (!pts.empty())
                result.push_back(SourceBatch{e.name, std::move(pts)});
        }
    }
    return result;
}

std::vector<DataSourceRegistry::SourceStatus> DataSourceRegistry::status() const
{
    std::lock_guard           lock(mutex_);
    std::vector<SourceStatus> out;
    out.reserve(sources_.size());
    for (const auto& e : sources_)
    {
        if (!e.enabled)
            continue;
        SourceStatus st;
        st.name    = e.name;
        st.faulted = e.faulted;
        st.running = !e.faulted && e.adapter && e.adapter->is_running();
        out.push_back(std::move(st));
    }
    return out;
}

bool DataSourceRegistry::set_running(const std::string& name, bool run)
{
    std::lock_guard lock(mutex_);
    for (auto& e : sources_)
    {
        if (e.name != name || !e.enabled || e.faulted || !e.adapter)
            continue;
        auto guard_result = plugin_guard_invoke(e.name.c_str(),
                                                [&]()
                                                {
                                                    if (run)
                                                        e.adapter->start();
                                                    else
                                                        e.adapter->stop();
                                                });
        if (guard_result != PluginCallResult::Success)
        {
            SPECTRA_LOG_ERROR("plugin",
                              "Data source '{}' faulted during {} — disabling",
                              e.name,
                              run ? "start" : "stop");
            e.faulted = true;
        }
        return true;
    }
    return false;
}

}   // namespace spectra
