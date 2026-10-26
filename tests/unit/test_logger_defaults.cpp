// Tests for logger defaults: sink preservation, console level resolution,
// ANSI color policy, log file location and size-based rotation.

#include <gtest/gtest.h>

#include <spectra/logger.hpp>

#include "env_guard.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace fs = std::filesystem;
using namespace spectra;

namespace
{

using spectra::test::EnvGuard;

fs::path make_temp_dir(const std::string& tag)
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path   dir =
        fs::temp_directory_path() / ("spectra_logger_test_" + tag + "_" + std::to_string(stamp));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

}   // namespace

TEST(LoggerColor, DisabledWhenNotATty)
{
    EXPECT_FALSE(sinks::should_use_color(false, nullptr));
    EXPECT_FALSE(sinks::should_use_color(false, ""));
}

TEST(LoggerColor, EnabledOnTtyWithoutNoColor)
{
    EXPECT_TRUE(sinks::should_use_color(true, nullptr));
    // An empty NO_COLOR does not disable color (https://no-color.org).
    EXPECT_TRUE(sinks::should_use_color(true, ""));
}

TEST(LoggerColor, NoColorEnvDisables)
{
    EXPECT_FALSE(sinks::should_use_color(true, "1"));
    EXPECT_FALSE(sinks::should_use_color(true, "yes"));

    EnvGuard no_color("NO_COLOR");
    no_color.set("1");
    EXPECT_FALSE(sinks::console_colors_enabled());
}

TEST(LoggerLevel, LibraryDefaultIsWarning)
{
    EnvGuard lvl("SPECTRA_LOG_LEVEL");
    lvl.unset();
    EXPECT_EQ(default_console_log_level(), LogLevel::Warning);
    EXPECT_EQ(default_console_log_level(LogLevel::Info), LogLevel::Info);
}

TEST(LoggerLevel, EnvOverridesFallback)
{
    EnvGuard lvl("SPECTRA_LOG_LEVEL");
    lvl.set("debug");
    EXPECT_EQ(default_console_log_level(), LogLevel::Debug);
    EXPECT_EQ(default_console_log_level(LogLevel::Info), LogLevel::Debug);
    lvl.set("ERROR");
    EXPECT_EQ(default_console_log_level(), LogLevel::Error);
    lvl.set("bogus");
    EXPECT_EQ(default_console_log_level(), LogLevel::Warning);
}

TEST(LoggerSinks, AddRemoveById)
{
    auto&      logger = Logger::instance();
    const auto before = logger.sink_count();
    auto       id     = logger.add_sink(sinks::null_sink());
    EXPECT_EQ(logger.sink_count(), before + 1);
    EXPECT_TRUE(logger.remove_sink(id));
    EXPECT_FALSE(logger.remove_sink(id));
    EXPECT_EQ(logger.sink_count(), before);
}

TEST(LoggerSinks, SetupDualLoggingKeepsUserSinks)
{
    EnvGuard file_env("SPECTRA_LOG_FILE");
    file_env.set("0");

    auto& logger = Logger::instance();
    logger.clear_sinks();
    logger.set_level(LogLevel::Trace);

    auto count   = std::make_shared<std::atomic<int>>(0);
    auto user_id = logger.add_sink([count](const Logger::LogEntry&) { ++*count; });

    setup_dual_logging(LogLevel::Critical, LogLevel::Critical);
    // user sink + console sink (file logging disabled)
    EXPECT_EQ(logger.sink_count(), 2u);
    // Global level is not raised above the user's Trace level.
    EXPECT_EQ(logger.get_level(), LogLevel::Trace);

    // Calling again replaces only Spectra's own sinks.
    setup_dual_logging(LogLevel::Critical, LogLevel::Critical);
    EXPECT_EQ(logger.sink_count(), 2u);

    // (setup_dual_logging's own debug line also reached the user sink.)
    const int before = count->load();
    logger.log(LogLevel::Debug, "test", "hello");
    EXPECT_EQ(count->load(), before + 1);

    EXPECT_TRUE(logger.remove_sink(user_id));
    logger.clear_sinks();
}

TEST(LoggerSinks, EnsureDefaultLoggingIsNoOpAfterSetup)
{
    EnvGuard file_env("SPECTRA_LOG_FILE");
    file_env.set("0");

    auto& logger = Logger::instance();
    logger.clear_sinks();
    setup_dual_logging(LogLevel::Critical, LogLevel::Critical);
    const auto n = logger.sink_count();
    ensure_default_logging();
    EXPECT_EQ(logger.sink_count(), n);
    logger.clear_sinks();
}

TEST(LoggerFile, DisabledByEnv)
{
    EnvGuard file_env("SPECTRA_LOG_FILE");
    for (const char* v : {"0", "off", "false", "OFF", "none"})
    {
        file_env.set(v);
        EXPECT_TRUE(default_log_file_path().empty()) << v;
    }
}

TEST(LoggerFile, PathOverride)
{
    EnvGuard file_env("SPECTRA_LOG_FILE");
    auto     dir    = make_temp_dir("override");
    auto     target = dir / "sub" / "custom.log";
    file_env.set(target.string());
    EXPECT_EQ(default_log_file_path(), target.string());
    EXPECT_TRUE(fs::is_directory(target.parent_path()));
    fs::remove_all(dir);
}

#ifndef _WIN32
TEST(LoggerFile, UsesXdgStateHome)
{
    EnvGuard file_env("SPECTRA_LOG_FILE");
    EnvGuard xdg("XDG_STATE_HOME");
    file_env.unset();
    auto dir = make_temp_dir("xdg");
    xdg.set(dir.string());

    const std::string path = default_log_file_path();
    EXPECT_EQ(path, (dir / "spectra" / "spectra.log").string());
    EXPECT_TRUE(fs::is_directory(dir / "spectra"));
    auto perms = fs::status(dir / "spectra").permissions();
    EXPECT_EQ(perms & (fs::perms::group_all | fs::perms::others_all), fs::perms::none);
    fs::remove_all(dir);
}
#endif

TEST(LoggerFile, NeverUsesSharedTmpLog)
{
    EnvGuard file_env("SPECTRA_LOG_FILE");
    file_env.unset();
    const std::string path = default_log_file_path();
    EXPECT_EQ(path.find("spectra_app.log"), std::string::npos);
}

TEST(LoggerFile, RotatesAtSizeLimit)
{
    auto dir  = make_temp_dir("rotate");
    auto path = (dir / "rot.log").string();

    Logger::LogEntry entry{};
    entry.timestamp = std::chrono::system_clock::now();
    entry.level     = LogLevel::Info;
    entry.category  = "test";
    entry.message   = std::string(100, 'x');
    entry.line      = 0;

    {
        auto sink = sinks::rotating_file_sink(path, 1000);
        for (int i = 0; i < 25; ++i)
            sink(entry);
    }

    ASSERT_TRUE(fs::exists(path));
    ASSERT_TRUE(fs::exists(path + ".1"));
    EXPECT_LE(fs::file_size(path), 1000u);
    EXPECT_LE(fs::file_size(path + ".1"), 1000u);
    EXPECT_FALSE(fs::exists(path + ".2"));

    // Re-opening an oversized file rotates it immediately.
    {
        std::ofstream big(path, std::ios::trunc);
        big << std::string(2000, 'y');
    }
    {
        auto sink = sinks::rotating_file_sink(path, 1000);
        sink(entry);
    }
    EXPECT_EQ(fs::file_size(path + ".1"), 2000u);
    EXPECT_LT(fs::file_size(path), 1000u);

    fs::remove_all(dir);
}
