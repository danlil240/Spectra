#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace spectra
{

// Describes a plugin-registered export format.
struct ExportFormatInfo
{
    std::string name;        // Human-readable format name (e.g. "CSV Data")
    std::string extension;   // File extension without dot (e.g. "csv")
};

// Context passed to export callbacks with everything needed to write the file.
struct ExportContext
{
    const char*    figure_json;       // JSON description of axes/series/labels/limits
    size_t         figure_json_len;   // Length of the JSON string
    const uint8_t* rgba_pixels;    // Raw RGBA pixel buffer (may be nullptr for data-only exports)
    uint32_t       pixel_width;    // Pixel buffer width
    uint32_t       pixel_height;   // Pixel buffer height
    const char*    output_path;    // Destination file path
};

// Registry for plugin-driven export formats.
// Thread-safe for registration; export_figure() may be called from any thread.
class ExportFormatRegistry
{
   public:
    using ExportCallback = std::function<bool(const ExportContext& ctx)>;

    ExportFormatRegistry()  = default;
    ~ExportFormatRegistry() = default;

    ExportFormatRegistry(const ExportFormatRegistry&)            = delete;
    ExportFormatRegistry& operator=(const ExportFormatRegistry&) = delete;

    // Register an export format with a unique name.  `owner` is an opaque token
    // (the plugin module that registered it, nullptr for the host) used by
    // unregister_owner() / set_owner_enabled().
    void register_format(const std::string& name,
                         const std::string& extension,
                         ExportCallback     callback,
                         const void*        owner = nullptr);

    // Remove a previously registered export format.
    void unregister_format(const std::string& name);

    // Remove every format registered with `owner` (nullptr: none).
    // Returns the number removed.
    size_t unregister_owner(const void* owner);

    // Enable/disable every format registered with `owner`.  Disabled formats
    // stay registered but are not listed by available_formats() and
    // export_figure() refuses them.
    void set_owner_enabled(const void* owner, bool enabled);

    // Number of formats (enabled or not) registered with `owner`.
    size_t count_owned(const void* owner) const;

    // List all enabled format names.
    std::vector<ExportFormatInfo> available_formats() const;

    // Export a figure using the named (enabled) format.
    // Returns true on success.
    bool export_figure(const std::string& format_name,
                       const std::string& figure_json,
                       const uint8_t*     rgba_pixels,
                       uint32_t           pixel_width,
                       uint32_t           pixel_height,
                       const std::string& output_path) const;

    // Number of registered formats.
    size_t count() const;

   private:
    struct Entry
    {
        std::string    name;
        std::string    extension;
        ExportCallback callback;
        bool           faulted = false;   // Set true on crash/exception
        const void*    owner   = nullptr;
        bool           enabled = true;
    };

    mutable std::mutex         mutex_;
    mutable std::vector<Entry> formats_;
};

}   // namespace spectra
