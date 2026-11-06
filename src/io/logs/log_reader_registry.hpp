#pragma once

// LogReaderRegistry — picks a LogReader for a file by its magic bytes or,
// failing that, its extension.  Extensible like ExportFormatRegistry: the app
// registers the built-in readers (register_builtin_log_readers) and plugins
// or hosts may add more.  Owned by the app runtime and passed by pointer.
//
// Thread-safe: registration and lookups may happen on any thread.  Factories
// and probes run outside the registry lock.

#include "log_reader.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace spectra::logs
{

struct LogFormatInfo
{
    std::string              name;         // e.g. "ULog"
    std::vector<std::string> extensions;   // lowercase, without dot, e.g. {"ulg"}
};

class LogReaderRegistry
{
   public:
    using Factory = std::function<std::unique_ptr<LogReader>()>;
    // Returns true if `head` (the first bytes of the file, at most
    // MAGIC_BYTES long, shorter for tiny files) identifies this format.
    using MagicProbe = std::function<bool(std::span<const uint8_t> head)>;

    static constexpr size_t MAGIC_BYTES = 16;

    LogReaderRegistry()  = default;
    ~LogReaderRegistry() = default;

    LogReaderRegistry(const LogReaderRegistry&)            = delete;
    LogReaderRegistry& operator=(const LogReaderRegistry&) = delete;

    // Register (or replace, by name) a format.  `probe` may be empty for
    // formats without a signature; those are matched by extension only.
    void register_format(LogFormatInfo info, Factory factory, MagicProbe probe = {});

    // Returns false if no format has this name.
    bool unregister_format(std::string_view name);

    std::vector<LogFormatInfo> available_formats() const;
    size_t                     count() const;

    // Every registered extension (lowercase, no dot), for file dialogs.
    std::vector<std::string> extensions() const;

    // Name of the format that would open the file: the first format whose
    // magic probe accepts the file's head, else the first whose extension
    // matches.  Empty if none.
    std::string detect(const std::string& path) const;
    std::string detect(std::string_view extension, std::span<const uint8_t> head) const;

    bool can_open(const std::string& path) const { return !detect(path).empty(); }

    // A new, unopened reader for the named format (nullptr if unknown).
    std::unique_ptr<LogReader> create(std::string_view format_name) const;

    // Detect the format and open the file.  Returns nullptr and sets *error
    // (when given) if no format matches or the reader fails to open it.
    std::unique_ptr<LogReader> open(const std::string& path, std::string* error = nullptr) const;

   private:
    struct Entry
    {
        LogFormatInfo info;
        Factory       factory;
        MagicProbe    probe;
    };

    std::vector<Entry> snapshot() const;

    mutable std::mutex mutex_;
    std::vector<Entry> formats_;
};

// Register the readers built into Spectra: ULog (.ulg) and, when built with
// SPECTRA_USE_MCAP, MCAP (.mcap).
void register_builtin_log_readers(LogReaderRegistry& registry);

// Lowercase extension of `path` without the dot ("" if none).
std::string lowercase_extension(const std::string& path);

}   // namespace spectra::logs
