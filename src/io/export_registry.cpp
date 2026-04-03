#include "io/export_registry.hpp"

#include <algorithm>

#include "spectra/logger.hpp"

namespace spectra
{

void ExportFormatRegistry::register_format(const std::string& name,
                                           const std::string& extension,
                                           ExportCallback     callback)
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

    formats_.push_back({name, extension, std::move(callback)});
    SPECTRA_LOG_INFO("export_registry", "Registered export format '{}'  (.{})", name, extension);
}

void ExportFormatRegistry::unregister_format(const std::string& name)
{
    std::lock_guard lock(mutex_);

    auto it = std::remove_if(formats_.begin(),
                             formats_.end(),
                             [&](const Entry& e) { return e.name == name; });
    if (it != formats_.end())
    {
        formats_.erase(it, formats_.end());
        SPECTRA_LOG_INFO("export_registry", "Unregistered export format '{}'", name);
    }
}

std::vector<ExportFormatInfo> ExportFormatRegistry::available_formats() const
{
    std::lock_guard               lock(mutex_);
    std::vector<ExportFormatInfo> result;
    result.reserve(formats_.size());
    for (const auto& entry : formats_)
    {
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
            if (entry.name == format_name)
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

    return cb(ctx);
}

size_t ExportFormatRegistry::count() const
{
    std::lock_guard lock(mutex_);
    return formats_.size();
}

}   // namespace spectra
