#pragma once

#include <chrono>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace spectra
{

enum class LogLevel : int
{
    Trace    = 0,
    Debug    = 1,
    Info     = 2,
    Warning  = 3,
    Error    = 4,
    Critical = 5
};

class Logger
{
   public:
    struct LogEntry
    {
        std::chrono::system_clock::time_point timestamp;
        LogLevel                              level;
        std::string                           category;
        std::string                           message;
        std::string                           file;
        int                                   line;
        std::string                           function;
    };

    using LogSink = std::function<void(const LogEntry&)>;

    static Logger& instance();

    void     set_level(LogLevel level);
    LogLevel get_level() const;

    void add_sink(LogSink sink);
    void clear_sinks();

    void log(LogLevel         level,
             std::string_view category,
             std::string_view message,
             std::string_view file     = "",
             int              line     = 0,
             std::string_view function = "");

    template <typename... Args>
    void log_formatted(LogLevel         level,
                       std::string_view category,
                       std::string_view format,
                       Args&&... args);

    bool is_enabled(LogLevel level) const;

   private:
    Logger()                         = default;
    ~Logger()                        = default;
    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;

    mutable std::mutex   mutex_;
    LogLevel             min_level_ = LogLevel::Info;
    std::vector<LogSink> sinks_;

    template <typename T>
    static std::string arg_to_string(T&& v)
    {
        using D = std::decay_t<T>;
        if constexpr (std::is_same_v<D, std::string>)
            return v;
        else if constexpr (std::is_same_v<D, std::string_view>)
            return std::string(v);
        else if constexpr (std::is_same_v<D, const char*> || std::is_same_v<D, char*>)
        {
            const char* p = v;
            return p ? std::string(p) : std::string("(null)");
        }
        else if constexpr (std::is_same_v<D, bool>)
            return v ? "true" : "false";
        else
            return std::to_string(v);
    }

    static std::string format_message(std::string_view format, auto&&... args)
    {
        std::string result(format);
        if constexpr (sizeof...(args) > 0)
        {
            auto replace_next = [&](auto&& arg)
            {
                auto pos = result.find("{}");
                if (pos != std::string::npos)
                    result.replace(pos, 2, arg_to_string(std::forward<decltype(arg)>(arg)));
            };
            (replace_next(std::forward<decltype(args)>(args)), ...);
        }
        return result;
    }

   public:
    static std::string level_to_string(LogLevel level);
    static std::string timestamp_to_string(const std::chrono::system_clock::time_point& tp);
};

// Template definitions must be in the header
template <typename... Args>
void Logger::log_formatted(LogLevel         level,
                           std::string_view category,
                           std::string_view format,
                           Args&&... args)
{
    if (!is_enabled(level))
    {
        return;
    }

    try
    {
        std::string formatted = format_message(format, std::forward<Args>(args)...);
        log(level, category, formatted);
    }
    catch (const std::exception& e)
    {
        log(LogLevel::Error, "logger", "Format error: {}", e.what());
    }
}

namespace sinks
{
Logger::LogSink console_sink();
Logger::LogSink file_sink(const std::string& filename);
Logger::LogSink null_sink();
}   // namespace sinks

#define SPECTRA_LOG_TRACE(category, ...)                                            \
    do                                                                              \
    {                                                                               \
        if (::spectra::Logger::instance().is_enabled(::spectra::LogLevel::Trace))   \
        {                                                                           \
            ::spectra::Logger::instance().log_formatted(::spectra::LogLevel::Trace, \
                                                        category,                   \
                                                        __VA_ARGS__);               \
        }                                                                           \
    } while (0)

#define SPECTRA_LOG_DEBUG(category, ...)                                            \
    do                                                                              \
    {                                                                               \
        if (::spectra::Logger::instance().is_enabled(::spectra::LogLevel::Debug))   \
        {                                                                           \
            ::spectra::Logger::instance().log_formatted(::spectra::LogLevel::Debug, \
                                                        category,                   \
                                                        __VA_ARGS__);               \
        }                                                                           \
    } while (0)

#define SPECTRA_LOG_INFO(category, ...)                                            \
    do                                                                             \
    {                                                                              \
        if (::spectra::Logger::instance().is_enabled(::spectra::LogLevel::Info))   \
        {                                                                          \
            ::spectra::Logger::instance().log_formatted(::spectra::LogLevel::Info, \
                                                        category,                  \
                                                        __VA_ARGS__);              \
        }                                                                          \
    } while (0)

#define SPECTRA_LOG_WARN(category, ...)                                               \
    do                                                                                \
    {                                                                                 \
        if (::spectra::Logger::instance().is_enabled(::spectra::LogLevel::Warning))   \
        {                                                                             \
            ::spectra::Logger::instance().log_formatted(::spectra::LogLevel::Warning, \
                                                        category,                     \
                                                        __VA_ARGS__);                 \
        }                                                                             \
    } while (0)

#define SPECTRA_LOG_ERROR(category, ...)                                            \
    do                                                                              \
    {                                                                               \
        if (::spectra::Logger::instance().is_enabled(::spectra::LogLevel::Error))   \
        {                                                                           \
            ::spectra::Logger::instance().log_formatted(::spectra::LogLevel::Error, \
                                                        category,                   \
                                                        __VA_ARGS__);               \
        }                                                                           \
    } while (0)

#define SPECTRA_LOG_CRITICAL(category, ...)                                            \
    do                                                                                 \
    {                                                                                  \
        if (::spectra::Logger::instance().is_enabled(::spectra::LogLevel::Critical))   \
        {                                                                              \
            ::spectra::Logger::instance().log_formatted(::spectra::LogLevel::Critical, \
                                                        category,                      \
                                                        __VA_ARGS__);                  \
        }                                                                              \
    } while (0)

#define SPECTRA_LOG_TRACE_HERE(category, ...) \
    SPECTRA_LOG_TRACE(category, __VA_ARGS__ " [{}:{}:{}]", __FILE__, __LINE__, __FUNCTION__)

#define SPECTRA_LOG_DEBUG_HERE(category, ...) \
    SPECTRA_LOG_DEBUG(category, __VA_ARGS__ " [{}:{}:{}]", __FILE__, __LINE__, __FUNCTION__)

#define SPECTRA_LOG_INFO_HERE(category, ...) \
    SPECTRA_LOG_INFO(category, __VA_ARGS__ " [{}:{}:{}]", __FILE__, __LINE__, __FUNCTION__)

#define SPECTRA_LOG_WARN_HERE(category, ...) \
    SPECTRA_LOG_WARN(category, __VA_ARGS__ " [{}:{}:{}]", __FILE__, __LINE__, __FUNCTION__)

#define SPECTRA_LOG_ERROR_HERE(category, ...) \
    SPECTRA_LOG_ERROR(category, __VA_ARGS__ " [{}:{}:{}]", __FILE__, __LINE__, __FUNCTION__)

#define SPECTRA_LOG_CRITICAL_HERE(category, ...) \
    SPECTRA_LOG_CRITICAL(category, __VA_ARGS__ " [{}:{}:{}]", __FILE__, __LINE__, __FUNCTION__)

}   // namespace spectra
