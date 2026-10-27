#include "overlay_registry.hpp"

#include <algorithm>

#include "spectra/logger.hpp"
#include "ui/workspace/plugin_guard.hpp"

namespace spectra
{

void OverlayRegistry::register_overlay(const std::string& name,
                                       DrawCallback       callback,
                                       const void*        owner)
{
    std::lock_guard lock(mutex_);

    // Replace if already registered with same name
    for (auto& entry : overlays_)
    {
        if (entry.name == name)
        {
            entry.callback = std::move(callback);
            entry.owner    = owner;
            entry.enabled  = true;
            return;
        }
    }

    Entry entry;
    entry.name     = name;
    entry.callback = std::move(callback);
    entry.owner    = owner;
    overlays_.push_back(std::move(entry));
}

size_t OverlayRegistry::unregister_owner(const void* owner)
{
    if (!owner)
        return 0;
    std::lock_guard lock(mutex_);
    return std::erase_if(overlays_, [owner](const Entry& e) { return e.owner == owner; });
}

void OverlayRegistry::set_owner_enabled(const void* owner, bool enabled)
{
    if (!owner)
        return;
    std::lock_guard lock(mutex_);
    for (auto& entry : overlays_)
    {
        if (entry.owner == owner)
            entry.enabled = enabled;
    }
}

size_t OverlayRegistry::count_owned(const void* owner) const
{
    if (!owner)
        return 0;
    std::lock_guard lock(mutex_);
    return static_cast<size_t>(std::count_if(overlays_.begin(),
                                             overlays_.end(),
                                             [owner](const Entry& e) { return e.owner == owner; }));
}

void OverlayRegistry::unregister_overlay(const std::string& name)
{
    std::lock_guard lock(mutex_);

    overlays_.erase(std::remove_if(overlays_.begin(),
                                   overlays_.end(),
                                   [&](const Entry& e) { return e.name == name; }),
                    overlays_.end());
}

void OverlayRegistry::draw_all(const OverlayDrawContext& ctx) const
{
    std::lock_guard lock(mutex_);

    for (auto& entry : overlays_)
    {
        if (entry.faulted || !entry.enabled)
            continue;

        auto result = plugin_guard_invoke(entry.name.c_str(), [&]() { entry.callback(ctx); });
        if (result != PluginCallResult::Success)
        {
            SPECTRA_LOG_ERROR("plugin",
                              "Overlay '{}' faulted — skipping future invocations",
                              entry.name);
            entry.faulted = true;
        }
    }
}

std::vector<std::string> OverlayRegistry::overlay_names() const
{
    std::lock_guard          lock(mutex_);
    std::vector<std::string> names;
    names.reserve(overlays_.size());
    for (const auto& entry : overlays_)
    {
        names.push_back(entry.name);
    }
    return names;
}

size_t OverlayRegistry::count() const
{
    std::lock_guard lock(mutex_);
    return overlays_.size();
}

}   // namespace spectra
