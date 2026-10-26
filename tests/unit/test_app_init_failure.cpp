// App behaviour when the render backend cannot initialize.
//
// The Vulkan loader is pointed at a non-existent driver manifest before any
// Vulkan call, so instance creation fails deterministically on every machine
// (with or without a GPU).  This binary must not create a working App first.

#include <gtest/gtest.h>

#include <spectra/app.hpp>
#include <spectra/easy.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>

#include "env_guard.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <memory>
#include <string>

namespace fs = std::filesystem;

namespace
{

class NoVulkanDriverEnv : public ::testing::Environment
{
   public:
    void SetUp() override
    {
        const std::string bogus =
            (fs::temp_directory_path() / "spectra_no_such_icd_manifest.json").string();
        spectra::test::set_env("VK_ICD_FILENAMES", bogus.c_str());
        spectra::test::set_env("VK_DRIVER_FILES", bogus.c_str());
        spectra::test::unset_env("VK_ADD_DRIVER_FILES");
        spectra::test::unset_env("SPECTRA_SOCKET");
        spectra::test::set_env("SPECTRA_LOG_FILE", "0");
    }
};

[[maybe_unused]] const auto* const kEnv =
    ::testing::AddGlobalTestEnvironment(new NoVulkanDriverEnv);

struct ErrorCounter
{
    std::shared_ptr<std::atomic<int>> errors = std::make_shared<std::atomic<int>>(0);
    spectra::Logger::SinkId           id     = 0;

    ErrorCounter()
    {
        auto e = errors;
        id     = spectra::Logger::instance().add_sink(
            [e](const spectra::Logger::LogEntry& entry)
            {
                if (entry.level >= spectra::LogLevel::Error)
                    ++*e;
            });
    }
    ~ErrorCounter() { spectra::Logger::instance().remove_sink(id); }
};

}   // namespace

TEST(AppInitFailure, OkIsFalseAndRunReturnsWithError)
{
    ErrorCounter counter;

    spectra::App app({.headless = true});
    EXPECT_FALSE(app.ok());
    const int errors_after_ctor = counter.errors->load();
    EXPECT_GE(errors_after_ctor, 1) << "backend init failure must be logged as an error";

    auto& fig = app.figure({.width = 320, .height = 240});
    fig.subplot(1, 1, 1).line(std::vector<float>{0.f, 1.f}, std::vector<float>{0.f, 1.f});
    const auto out = fs::temp_directory_path() / "spectra_init_failure_test.png";
    fs::remove(out);
    EXPECT_TRUE(fig.save_png(out.string()));   // request is valid…

    const auto t0 = std::chrono::steady_clock::now();
    app.run();   // …but run() must bail out promptly instead of hanging/crashing
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    EXPECT_LT(elapsed, std::chrono::seconds(5));
    EXPECT_GT(counter.errors->load(), errors_after_ctor) << "run() must log why it did nothing";
    EXPECT_FALSE(fs::exists(out));
}

TEST(AppInitFailure, RunDoesNotInstallSignalHandlersByDefault)
{
    using Handler       = void (*)(int);
    Handler      custom = [](int) {};
    const auto   prev   = std::signal(SIGINT, custom);
    spectra::App app({.headless = true});
    app.run();
    EXPECT_EQ(std::signal(SIGINT, prev), custom);
}

TEST(AppInitFailure, StepAfterFailedInitRequestsExit)
{
    spectra::App app({.headless = true});
    ASSERT_FALSE(app.ok());
    app.init_runtime();
    EXPECT_TRUE(app.step().should_exit);
    app.shutdown_runtime();
}

TEST(AppInitFailure, EasyShowReturnsFalse)
{
    spectra::plot({0.f, 1.f, 2.f}, {0.f, 1.f, 4.f}, "c-o");
    EXPECT_FALSE(spectra::show());
}
