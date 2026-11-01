#include "log_reader_registry.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

#include "mcap_log_reader.hpp"
#include "ulog_log_reader.hpp"

namespace spectra::logs
{

std::string lowercase_extension(const std::string& path)
{
    std::string ext = std::filesystem::path(path).extension().string();
    if (!ext.empty() && ext.front() == '.')
        ext.erase(ext.begin());
    std::transform(ext.begin(),
                   ext.end(),
                   ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

void LogReaderRegistry::register_format(LogFormatInfo info, Factory factory, MagicProbe probe)
{
    for (auto& ext : info.extensions)
    {
        if (!ext.empty() && ext.front() == '.')
            ext.erase(ext.begin());
        std::transform(ext.begin(),
                       ext.end(),
                       ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& e : formats_)
    {
        if (e.info.name == info.name)
        {
            e = Entry{std::move(info), std::move(factory), std::move(probe)};
            return;
        }
    }
    formats_.push_back(Entry{std::move(info), std::move(factory), std::move(probe)});
}

bool LogReaderRegistry::unregister_format(std::string_view name)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto                        it = std::find_if(formats_.begin(),
                           formats_.end(),
                           [&](const Entry& e) { return e.info.name == name; });
    if (it == formats_.end())
        return false;
    formats_.erase(it);
    return true;
}

std::vector<LogReaderRegistry::Entry> LogReaderRegistry::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return formats_;
}

std::vector<LogFormatInfo> LogReaderRegistry::available_formats() const
{
    std::vector<LogFormatInfo> out;
    for (auto& e : snapshot())
        out.push_back(e.info);
    return out;
}

size_t LogReaderRegistry::count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return formats_.size();
}

std::vector<std::string> LogReaderRegistry::extensions() const
{
    std::vector<std::string> out;
    for (auto& e : snapshot())
    {
        for (auto& ext : e.info.extensions)
        {
            if (std::find(out.begin(), out.end(), ext) == out.end())
                out.push_back(ext);
        }
    }
    return out;
}

std::string LogReaderRegistry::detect(std::string_view         extension,
                                      std::span<const uint8_t> head) const
{
    const auto entries = snapshot();
    for (auto& e : entries)
    {
        if (e.probe && !head.empty() && e.probe(head))
            return e.info.name;
    }
    for (auto& e : entries)
    {
        for (auto& ext : e.info.extensions)
        {
            if (ext == extension)
                return e.info.name;
        }
    }
    return {};
}

std::string LogReaderRegistry::detect(const std::string& path) const
{
    uint8_t buf[MAGIC_BYTES] = {};
    size_t  n                = 0;
    {
        std::ifstream in(path, std::ios::binary);
        if (in)
        {
            in.read(reinterpret_cast<char*>(buf), static_cast<std::streamsize>(sizeof(buf)));
            n = static_cast<size_t>(in.gcount());
        }
    }
    return detect(lowercase_extension(path), std::span<const uint8_t>(buf, n));
}

std::unique_ptr<LogReader> LogReaderRegistry::create(std::string_view format_name) const
{
    for (auto& e : snapshot())
    {
        if (e.info.name == format_name)
            return e.factory ? e.factory() : nullptr;
    }
    return nullptr;
}

std::unique_ptr<LogReader> LogReaderRegistry::open(const std::string& path,
                                                   std::string*       error) const
{
    auto set_error = [error](std::string msg)
    {
        if (error)
            *error = std::move(msg);
    };

    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec))
    {
        set_error("Not a file: " + path);
        return nullptr;
    }

    const std::string format = detect(path);
    if (format.empty())
    {
        set_error("Unsupported log format: " + path);
        return nullptr;
    }

    auto reader = create(format);
    if (!reader)
    {
        set_error("No reader for format " + format);
        return nullptr;
    }
    if (!reader->open(path))
    {
        set_error(format + ": " + reader->last_error());
        return nullptr;
    }
    return reader;
}

void register_builtin_log_readers(LogReaderRegistry& registry)
{
    registry.register_format(
        {"ULog", {"ulg"}},
        []() -> std::unique_ptr<LogReader> { return std::make_unique<ULogLogReader>(); },
        [](std::span<const uint8_t> head) { return ULogLogReader::has_magic(head); });

#ifdef SPECTRA_HAS_MCAP
    registry.register_format(
        {"MCAP", {"mcap"}},
        []() -> std::unique_ptr<LogReader> { return std::make_unique<McapLogReader>(); },
        [](std::span<const uint8_t> head) { return McapLogReader::has_magic(head); });
#endif
}

}   // namespace spectra::logs
