#include <cstdio>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <plotix/logger.hpp>
#include <sstream>
#include <string>

namespace plotix
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

void Logger::log(LogLevel level,
                 std::string_view category,
                 std::string_view message,
                 std::string_view file,
                 int line,
                 std::string_view function)
{
    if (!is_enabled(level))
    {
        return;
    }

    LogEntry entry{.timestamp = std::chrono::system_clock::now(),
                   .level = level,
                   .category = std::string(category),
                   .message = std::string(message),
                   .file = std::string(file),
                   .line = line,
                   .function = std::string(function)};

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

        std::cout << color_code << Logger::timestamp_to_string(entry.timestamp) << " "
                  << Logger::level_to_string(entry.level) << " "
                  << "[" << entry.category << "] " << entry.message;

        if (!entry.file.empty())
        {
            std::cout << " (" << entry.file << ":" << entry.line;
            if (!entry.function.empty())
            {
                std::cout << " in " << entry.function;
            }
            std::cout << ")";
        }

        std::cout << reset_code << std::endl;
    };
}

Logger::LogSink file_sink(const std::string& filename)
{
    auto file = std::make_shared<std::ofstream>(filename, std::ios::app);
    return [file](const Logger::LogEntry& entry)
    {
        if (file->is_open())
        {
            *file << Logger::timestamp_to_string(entry.timestamp) << " "
                  << Logger::level_to_string(entry.level) << " "
                  << "[" << entry.category << "] " << entry.message;

            if (!entry.file.empty())
            {
                *file << " (" << entry.file << ":" << entry.line;
                if (!entry.function.empty())
                {
                    *file << " in " << entry.function;
                }
                *file << ")";
            }

            *file << std::endl;
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

}  // namespace sinks

}  // namespace plotix
