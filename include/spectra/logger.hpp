#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
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
    Critical = 5,
    Off      = 6   // Threshold only: disables all logging when used as a min level
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
    using SinkId  = std::size_t;

    static Logger& instance();

    void     set_level(LogLevel level);
    LogLevel get_level() const;

    // Register a sink.  Returns an id that can be passed to remove_sink().
    SinkId add_sink(LogSink sink);
    // Remove a single sink by id.  Returns false if the id is unknown.
    bool remove_sink(SinkId id);
    void clear_sinks();
    // Number of registered sinks.
    std::size_t sink_count() const;

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

    mutable std::mutex                      mutex_;
    LogLevel                                min_level_ = LogLevel::Info;
    std::vector<std::pair<SinkId, LogSink>> sinks_;
    SinkId                                  next_sink_id_ = 1;

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
            // Cursor past the last substitution so placeholder-like text inside
            // an argument ("{}") is never treated as a placeholder itself.
            size_t search_from  = 0;
            auto   replace_next = [&](auto&& arg)
            {
                auto pos = result.find("{}", search_from);
                if (pos != std::string::npos)
                {
                    std::string replacement = arg_to_string(std::forward<decltype(arg)>(arg));
                    result.replace(pos, 2, replacement);
                    search_from = pos + replacement.size();
                }
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
        log(LogLevel::Error, "logger", std::string("Format error: ") + e.what());
    }
}

namespace sinks
{
// Console sink writing to stderr.  The no-argument overload enables ANSI colors
// only when stderr is a terminal and NO_COLOR is not set (see
// console_colors_enabled()).
Logger::LogSink console_sink();
Logger::LogSink console_sink(bool use_color);
Logger::LogSink file_sink(const std::string& filename);
// Appends to `filename`; when the file grows beyond `max_bytes` it is renamed
// to `<filename>.1` (replacing any previous backup) and a fresh file is started.
Logger::LogSink rotating_file_sink(const std::string& filename, std::uintmax_t max_bytes);
Logger::LogSink null_sink();
Logger::LogSink filtered_sink(LogLevel min_level, Logger::LogSink inner);

// True when the default console sink should emit ANSI colors: stderr is a TTY
// and the NO_COLOR environment variable is unset or empty (https://no-color.org).
bool console_colors_enabled();
// Pure helper behind console_colors_enabled(), exposed for testing.
bool should_use_color(bool stream_is_tty, const char* no_color_env);
}   // namespace sinks

// Default size limit of the rotating log file written by setup_dual_logging().
inline constexpr std::uintmax_t kDefaultLogFileMaxBytes = 5u * 1024u * 1024u;

// Install Spectra's standard console + file sinks.
//
// Only the sinks previously installed by setup_dual_logging() are replaced;
// sinks added by the application with Logger::add_sink() are kept.  The global
// level is lowered (never raised) as needed so both sinks receive messages.
//
// `log_path` empty → default_log_file_path(); file logging is skipped when
// that resolves to an empty string (SPECTRA_LOG_FILE=0).
void setup_dual_logging(LogLevel           console_level = LogLevel::Info,
                        LogLevel           file_level    = LogLevel::Trace,
                        const std::string& log_path      = "");

// Call setup_dual_logging() with library defaults unless logging was already
// configured — either by an earlier setup_dual_logging() call or because the
// application registered its own sinks.  Used by App's constructor.
void ensure_default_logging();

// Default console log level.  SPECTRA_LOG_LEVEL={trace,debug,info,warning,
// error,critical,off} overrides it; otherwise `fallback` is returned.  The
// library default is Warning so that embedding Spectra stays quiet; the
// Spectra executables pass LogLevel::Info.
LogLevel default_console_log_level(LogLevel fallback = LogLevel::Warning);

// Resolve the default log file path:
//   SPECTRA_LOG_FILE=0|off|false|no|none → "" (file logging disabled)
//   SPECTRA_LOG_FILE=<path>               → <path>
//   otherwise $XDG_STATE_HOME/spectra/spectra.log, ~/.local/state/spectra/
//   spectra.log, or <temp dir>/spectra-<uid>/spectra.log as a last resort.
// Creates the parent directory (mode 0700 on POSIX) when needed.
std::string default_log_file_path();

// Default file log level resolved from build type and SPECTRA_LOG_FILE_LEVEL env.
// Release: Info, Debug: Trace.  Override with SPECTRA_LOG_FILE_LEVEL=...
LogLevel default_file_log_level();

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
