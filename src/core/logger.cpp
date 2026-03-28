#include <cstdio>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <spectra/logger.hpp>
#include <sstream>
#include <string>

namespace spectra
{

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

void Logger::set_level(LogLevel level)
{
    std::lock_guard<std::mutex> lock(mutex_);
    min_level_ = level;
}

LogLevel Logger::get_level() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return min_level_;
}

void Logger::add_sink(LogSink sink)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sinks_.push_back(std::move(sink));
}

void Logger::clear_sinks()
{
    std::lock_guard<std::mutex> lock(mutex_);
    sinks_.clear();
}

void Logger::log(LogLevel         level,
                 std::string_view category,
                 std::string_view message,
                 std::string_view file,
                 int              line,
                 std::string_view function)
{
    if (!is_enabled(level))
    {
        return;
    }

    LogEntry entry{.timestamp = std::chrono::system_clock::now(),
                   .level     = level,
                   .category  = std::string(category),
                   .message   = std::string(message),
                   .file      = std::string(file),
                   .line      = line,
                   .function  = std::string(function)};

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& sink : sinks_)
    {
        sink(entry);
    }
}

bool Logger::is_enabled(LogLevel level) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return level >= min_level_;
}

std::string Logger::level_to_string(LogLevel level)
{
    switch (level)
    {
        case LogLevel::Trace:
            return "TRACE";
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warning:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Critical:
            return "CRITICAL";
        default:
            return "UNKNOWN";
    }
}

std::string Logger::timestamp_to_string(const std::chrono::system_clock::time_point& tp)
{
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

namespace sinks
{

static std::string format_log_entry(const Logger::LogEntry& entry)
{
    std::string result = Logger::timestamp_to_string(entry.timestamp) + " "
                         + Logger::level_to_string(entry.level) + " " + "[" + entry.category + "] "
                         + entry.message;
    if (!entry.file.empty())
    {
        result += " (" + entry.file + ":" + std::to_string(entry.line);
        if (!entry.function.empty())
            result += " in " + entry.function;
        result += ")";
    }
    return result;
}

Logger::LogSink console_sink()
{
    return [](const Logger::LogEntry& entry)
    {
        const char* color_code = "";
        const char* reset_code = "\033[0m";

        switch (entry.level)
        {
            case LogLevel::Trace:
                color_code = "\033[37m";
                break;
            case LogLevel::Debug:
                color_code = "\033[36m";
                break;
            case LogLevel::Info:
                color_code = "\033[32m";
                break;
            case LogLevel::Warning:
                color_code = "\033[33m";
                break;
            case LogLevel::Error:
                color_code = "\033[31m";
                break;
            case LogLevel::Critical:
                color_code = "\033[35m";
                break;
        }

        std::cout << color_code << format_log_entry(entry) << reset_code << std::endl;
    };
}

Logger::LogSink file_sink(const std::string& filename)
{
    auto file = std::make_shared<std::ofstream>(filename, std::ios::app);
    return [file](const Logger::LogEntry& entry)
    {
        if (file->is_open())
        {
            *file << format_log_entry(entry) << std::endl;
            file->flush();
        }
    };
}

Logger::LogSink null_sink()
{
    return [](const Logger::LogEntry&)
    {
        // Do nothing
    };
}

Logger::LogSink filtered_sink(LogLevel min_level, Logger::LogSink inner)
{
    return [min_level, inner = std::move(inner)](const Logger::LogEntry& entry)
    {
        if (entry.level >= min_level)
        {
            inner(entry);
        }
    };
}

}   // namespace sinks

void setup_dual_logging(LogLevel console_level, LogLevel file_level, const std::string& log_path)
{
    auto& logger = Logger::instance();

    // Global min level = lowest of the two sinks so all messages reach dispatch
    LogLevel global_min = std::min(console_level, file_level);
    logger.set_level(global_min);
    logger.clear_sinks();

    // Console sink with per-sink level filter
    logger.add_sink(sinks::filtered_sink(console_level, sinks::console_sink()));

    // File sink
    std::string path = log_path;
    if (path.empty())
    {
        try
        {
            path = (std::filesystem::temp_directory_path() / "spectra_app.log").string();
        }
        catch (...)
        {
            path = "/tmp/spectra_app.log";
        }
    }

    try
    {
        logger.add_sink(sinks::filtered_sink(file_level, sinks::file_sink(path)));
        SPECTRA_LOG_INFO("app",
                         "Dual logging active — console: {} file: {} path: {}",
                         Logger::level_to_string(console_level),
                         Logger::level_to_string(file_level),
                         path);
    }
    catch (const std::exception& e)
    {
        SPECTRA_LOG_WARN("app", "Failed to open log file: {}", std::string(e.what()));
    }
}

}   // namespace spectra
