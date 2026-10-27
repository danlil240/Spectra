#include "io/export_registry.hpp"

#include <algorithm>

#include "spectra/logger.hpp"
#include "ui/workspace/plugin_guard.hpp"

namespace spectra
{

void ExportFormatRegistry::register_format(const std::string& name,
                                           const std::string& extension,
                                           ExportCallback     callback,
                                           const void*        owner)
{
    {
        std::lock_guard lock(mutex_);

        // Check for duplicate
        for (const auto& entry : formats_)
        {
            if (entry.name == name)
            {
                SPECTRA_LOG_WARN("export_registry", "Export format '{}' already registered", name);
                return;
            }
        }

        Entry entry;
        entry.name      = name;
        entry.extension = extension;
        entry.callback  = std::move(callback);
        entry.owner     = owner;
        formats_.push_back(std::move(entry));
        SPECTRA_LOG_INFO("export_registry",
                         "Registered export format '{}'  (.{})",
                         name,
                         extension);
    }
    notify({name, extension}, ChangeKind::Registered);
}

void ExportFormatRegistry::unregister_format(const std::string& name)
{
    ExportFormatInfo removed;
    bool             found = false;
    {
        std::lock_guard lock(mutex_);

        // Names are unique (register_format rejects duplicates).
        auto it = std::find_if(formats_.begin(),
                               formats_.end(),
                               [&](const Entry& e) { return e.name == name; });
        if (it != formats_.end())
        {
            removed = {it->name, it->extension};
            found   = true;
            formats_.erase(it);
            SPECTRA_LOG_INFO("export_registry", "Unregistered export format '{}'", name);
        }
    }
    if (found)
        notify(removed, ChangeKind::Unregistered);
}

size_t ExportFormatRegistry::unregister_owner(const void* owner)
{
    if (!owner)
        return 0;
    std::vector<ExportFormatInfo> removed;
    {
        std::lock_guard lock(mutex_);
        for (const auto& e : formats_)
        {
            if (e.owner == owner)
                removed.push_back({e.name, e.extension});
        }
        std::erase_if(formats_, [owner](const Entry& e) { return e.owner == owner; });
    }
    for (const auto& info : removed)
        notify(info, ChangeKind::Unregistered);
    return removed.size();
}

void ExportFormatRegistry::set_owner_enabled(const void* owner, bool enabled)
{
    if (!owner)
        return;
    // Listeners see a disabled format as removed and a re-enabled one as added,
    // so UI built from them (e.g. File > Export commands) follows the toggle.
    std::vector<ExportFormatInfo> changed;
    {
        std::lock_guard lock(mutex_);
        for (auto& entry : formats_)
        {
            if (entry.owner == owner && entry.enabled != enabled)
            {
                entry.enabled = enabled;
                changed.push_back({entry.name, entry.extension});
            }
        }
    }
    for (const auto& info : changed)
        notify(info, enabled ? ChangeKind::Registered : ChangeKind::Unregistered);
}

size_t ExportFormatRegistry::count_owned(const void* owner) const
{
    if (!owner)
        return 0;
    std::lock_guard lock(mutex_);
    return static_cast<size_t>(std::count_if(formats_.begin(),
                                             formats_.end(),
                                             [owner](const Entry& e) { return e.owner == owner; }));
}

std::vector<ExportFormatInfo> ExportFormatRegistry::available_formats() const
{
    std::lock_guard               lock(mutex_);
    std::vector<ExportFormatInfo> result;
    result.reserve(formats_.size());
    for (const auto& entry : formats_)
    {
        if (entry.enabled)
            result.push_back({entry.name, entry.extension});
    }
    return result;
}

bool ExportFormatRegistry::export_figure(const std::string& format_name,
                                         const std::string& figure_json,
                                         const uint8_t*     rgba_pixels,
                                         uint32_t           pixel_width,
                                         uint32_t           pixel_height,
                                         const std::string& output_path) const
{
    ExportCallback cb;
    {
        std::lock_guard lock(mutex_);
        for (const auto& entry : formats_)
        {
            if (entry.name == format_name && !entry.faulted && entry.enabled)
            {
                cb = entry.callback;
                break;
            }
        }
    }

    if (!cb)
    {
        SPECTRA_LOG_WARN("export_registry", "Export format '{}' not found", format_name);
        return false;
    }

    ExportContext ctx{};
    ctx.figure_json     = figure_json.c_str();
    ctx.figure_json_len = figure_json.size();
    ctx.rgba_pixels     = rgba_pixels;
    ctx.pixel_width     = pixel_width;
    ctx.pixel_height    = pixel_height;
    ctx.output_path     = output_path.c_str();

    bool ok     = false;
    auto result = plugin_guard_invoke(format_name.c_str(), [&]() { ok = cb(ctx); });
    if (result != PluginCallResult::Success)
    {
        SPECTRA_LOG_ERROR("plugin",
                          "Export format '{}' faulted during export — disabling",
                          format_name);
        // Mark the entry as faulted so future exports skip it.
        std::lock_guard lock(mutex_);
        for (auto& entry : formats_)
        {
            if (entry.name == format_name)
            {
                entry.faulted = true;
                break;
            }
        }
        return false;
    }
    return ok;
}

size_t ExportFormatRegistry::count() const
{
    std::lock_guard lock(mutex_);
    return formats_.size();
}

ExportFormatRegistry::ListenerId ExportFormatRegistry::add_change_listener(ChangeListener listener)
{
    if (!listener)
        return 0;
    std::lock_guard  lock(listener_mutex_);
    const ListenerId id = next_listener_id_++;
    listeners_.emplace_back(id, std::move(listener));
    return id;
}

void ExportFormatRegistry::remove_change_listener(ListenerId id)
{
    std::lock_guard lock(listener_mutex_);
    std::erase_if(listeners_, [id](const auto& l) { return l.first == id; });
}

void ExportFormatRegistry::notify(const ExportFormatInfo& info, ChangeKind kind) const
{
    // Copy so listeners can add/remove listeners or touch the registry.
    std::vector<ChangeListener> snapshot;
    {
        std::lock_guard lock(listener_mutex_);
        snapshot.reserve(listeners_.size());
        for (const auto& [id, listener] : listeners_)
            snapshot.push_back(listener);
    }
    for (const auto& listener : snapshot)
        listener(info, kind);
}

}   // namespace spectra
