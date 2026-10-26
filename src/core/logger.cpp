#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <spectra/logger.hpp>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
    #include <io.h>
    #define SPECTRA_ISATTY _isatty
    #define SPECTRA_FILENO _fileno
#else
    #include <sys/stat.h>
    #include <unistd.h>
    #define SPECTRA_ISATTY ::isatty
    #define SPECTRA_FILENO ::fileno
#endif

namespace spectra
{

namespace
{
// Parse a log-level string (case-insensitive).  Returns std::nullopt on unknown.
std::optional<LogLevel> parse_level(const char* s)
{
    if (!s || !*s)
        return std::nullopt;
    std::string v;
    for (const char* p = s; *p; ++p)
        v += static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
    if (v == "trace")
        return LogLevel::Trace;
    if (v == "debug")
        return LogLevel::Debug;
    if (v == "info")
        return LogLevel::Info;
    if (v == "warn" || v == "warning")
        return LogLevel::Warning;
    if (v == "error")
        return LogLevel::Error;
    if (v == "critical" || v == "crit" || v == "fatal")
        return LogLevel::Critical;
    if (v == "off" || v == "none" || v == "silent")
        return LogLevel::Critical;
    return std::nullopt;
}

std::string lower(const char* s)
{
    std::string v;
    for (const char* p = s; p && *p; ++p)
        v += static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
    return v;
}

// Sinks owned by setup_dual_logging(); replaced (not accumulated) on each call.
struct DefaultSinkState
{
    std::mutex     mutex;
    bool           configured = false;
    Logger::SinkId console_id = 0;
    Logger::SinkId file_id    = 0;
};

DefaultSinkState& default_sink_state()
{
    static DefaultSinkState s;
    return s;
}

// Create `dir` (and parents) restricting the leaf to the current user.
bool ensure_private_dir(const std::filesystem::path& dir)
{
    std::error_code ec;
    if (std::filesystem::is_directory(dir, ec))
        return true;
    std::filesystem::create_directories(dir, ec);
    if (ec)
        return false;
#ifndef _WIN32
    ::chmod(dir.c_str(), 0700);
#endif
    return std::filesystem::is_directory(dir, ec);
}

}   // namespace

LogLevel default_console_log_level(LogLevel fallback)
{
    if (const char* env = std::getenv("SPECTRA_LOG_LEVEL"))
    {
        if (auto lvl = parse_level(env))
            return *lvl;
    }
    return fallback;
}

LogLevel default_file_log_level()
{
    if (const char* env = std::getenv("SPECTRA_LOG_FILE_LEVEL"))
    {
        if (auto lvl = parse_level(env))
            return *lvl;
    }
#ifdef NDEBUG
    return LogLevel::Info;
#else
    return LogLevel::Trace;
#endif
}

std::string default_log_file_path()
{
    namespace fs = std::filesystem;

    if (const char* env = std::getenv("SPECTRA_LOG_FILE"); env && *env)
    {
        const std::string v = lower(env);
        if (v == "0" || v == "off" || v == "false" || v == "no" || v == "none")
            return {};
        fs::path p(env);
        if (p.has_parent_path())
        {
            std::error_code ec;
            fs::create_directories(p.parent_path(), ec);
        }
        return p.string();
    }

    std::vector<fs::path> candidates;
#ifdef _WIN32
    if (const char* la = std::getenv("LOCALAPPDATA"); la && *la)
        candidates.push_back(fs::path(la) / "spectra");
#else
    if (const char* xdg = std::getenv("XDG_STATE_HOME"); xdg && *xdg && xdg[0] == '/')
        candidates.push_back(fs::path(xdg) / "spectra");
    if (const char* home = std::getenv("HOME"); home && *home)
        candidates.push_back(fs::path(home) / ".local" / "state" / "spectra");
#endif
    {
        std::error_code ec;
        fs::path        tmp = fs::temp_directory_path(ec);
        if (!ec)
        {
#ifdef _WIN32
            candidates.push_back(tmp / "spectra");
#else
            candidates.push_back(tmp / ("spectra-" + std::to_string(::getuid())));
#endif
        }
    }

    for (const auto& dir : candidates)
    {
        if (ensure_private_dir(dir))
            return (dir / "spectra.log").string();
    }
    return {};
}

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

Logger::SinkId Logger::add_sink(LogSink sink)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const SinkId                id = next_sink_id_++;
    sinks_.emplace_back(id, std::move(sink));
    return id;
}

bool Logger::remove_sink(SinkId id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto                        it = std::find_if(sinks_.begin(),
                           sinks_.end(),
                           [id](const auto& entry) { return entry.first == id; });
    if (it == sinks_.end())
        return false;
    sinks_.erase(it);
    return true;
}

void Logger::clear_sinks()
{
    std::lock_guard<std::mutex> lock(mutex_);
    sinks_.clear();
}

std::size_t Logger::sink_count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return sinks_.size();
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
    for (const auto& [id, sink] : sinks_)
    {
        (void)id;
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

    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t);
#else
    localtime_r(&time_t, &tm_buf);
#endif

    std::stringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
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

bool should_use_color(bool stream_is_tty, const char* no_color_env)
{
    if (no_color_env != nullptr && no_color_env[0] != '\0')
        return false;
    return stream_is_tty;
}

bool console_colors_enabled()
{
    return should_use_color(SPECTRA_ISATTY(SPECTRA_FILENO(stderr)) != 0, std::getenv("NO_COLOR"));
}

Logger::LogSink console_sink()
{
    return console_sink(console_colors_enabled());
}

Logger::LogSink console_sink(bool use_color)
{
    return [use_color](const Logger::LogEntry& entry)
    {
        if (!use_color)
        {
            std::cerr << format_log_entry(entry) << '\n';
            std::cerr.flush();
            return;
        }

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

        std::cerr << color_code << format_log_entry(entry) << reset_code << '\n';
        std::cerr.flush();
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

Logger::LogSink rotating_file_sink(const std::string& filename, std::uintmax_t max_bytes)
{
    struct State
    {
        std::string    path;
        std::uintmax_t max_bytes = 0;
        std::uintmax_t size      = 0;
        std::ofstream  out;

        void rotate()
        {
            out.close();
            std::error_code   ec;
            const std::string backup = path + ".1";
            std::filesystem::remove(backup, ec);
            std::filesystem::rename(path, backup, ec);
            out.open(path, std::ios::out | std::ios::trunc);
            size = 0;
        }

        void open()
        {
            std::error_code ec;
            size = std::filesystem::exists(path, ec) ? std::filesystem::file_size(path, ec) : 0;
            if (ec)
                size = 0;
            if (max_bytes > 0 && size >= max_bytes)
                rotate();
            else
                out.open(path, std::ios::out | std::ios::app);
        }
    };

    auto state       = std::make_shared<State>();
    state->path      = filename;
    state->max_bytes = max_bytes;
    state->open();

    return [state](const Logger::LogEntry& entry)
    {
        if (!state->out.is_open())
            return;
        const std::string line = format_log_entry(entry) + '\n';
        if (state->max_bytes > 0 && state->size + line.size() > state->max_bytes && state->size > 0)
        {
            state->rotate();
            if (!state->out.is_open())
                return;
        }
        state->out << line;
        state->out.flush();
        state->size += line.size();
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
    auto& state  = default_sink_state();

    std::string path = log_path.empty() ? default_log_file_path() : log_path;

    {
        std::lock_guard<std::mutex> lock(state.mutex);

        // Replace only the sinks this function installed earlier; keep any
        // sinks the application registered itself.
        if (state.console_id != 0)
            logger.remove_sink(state.console_id);
        if (state.file_id != 0)
            logger.remove_sink(state.file_id);
        state.console_id = 0;
        state.file_id    = 0;

        const bool has_user_sinks = logger.sink_count() > 0;

        // Global min level = lowest of the two sinks so all messages reach
        // dispatch.  When the application has its own sinks, never raise the
        // level above what it configured.
        LogLevel global_min = path.empty() ? console_level : std::min(console_level, file_level);
        if (has_user_sinks)
            global_min = std::min(global_min, logger.get_level());
        logger.set_level(global_min);

        state.console_id =
            logger.add_sink(sinks::filtered_sink(console_level, sinks::console_sink()));

        if (!path.empty() && std::ofstream(path, std::ios::out | std::ios::app).is_open())
        {
            try
            {
                state.file_id = logger.add_sink(
                    sinks::filtered_sink(file_level,
                                         sinks::rotating_file_sink(path, kDefaultLogFileMaxBytes)));
            }
            catch (const std::exception&)
            {
                state.file_id = 0;
            }
        }
        state.configured = true;
    }

    if (path.empty())
    {
        SPECTRA_LOG_DEBUG("app",
                          "Logging active — console: {} (file logging disabled)",
                          Logger::level_to_string(console_level));
    }
    else if (state.file_id == 0)
    {
        SPECTRA_LOG_WARN("app", "Failed to open log file: {}", path);
    }
    else
    {
        SPECTRA_LOG_DEBUG("app",
                          "Dual logging active — console: {} file: {} path: {}",
                          Logger::level_to_string(console_level),
                          Logger::level_to_string(file_level),
                          path);
    }
}

void ensure_default_logging()
{
    {
        auto&                       state = default_sink_state();
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.configured)
            return;
        // The application installed its own sinks: it owns logging output.
        if (Logger::instance().sink_count() > 0)
        {
            state.configured = true;
            return;
        }
    }
    setup_dual_logging(default_console_log_level(), default_file_log_level());
}

}   // namespace spectra
