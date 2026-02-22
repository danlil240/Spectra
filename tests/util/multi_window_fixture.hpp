#pragma once

// Multi-window test fixture for Spectra multi-window architecture validation.
// Provides scaffolding to create N headless windows and verify rendering,
// resize, and lifecycle behavior.
//
// NOTE: This is Day 0 scaffolding. The actual WindowContext, WindowManager,
// and FigureRegistry types do not exist yet — they will be created by
// Agents A, B, and C respectively. This fixture uses forward declarations
// and compile-time guards so it compiles NOW (against the current single-
// window codebase) and will be progressively enabled as each agent merges.

#include <chrono>
#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <spectra/app.hpp>
#include <spectra/figure.hpp>
#include <spectra/fwd.hpp>
#include <string>
#include <vector>

#include "render/backend.hpp"

namespace spectra::test
{

// ─── Phase Guards ────────────────────────────────────────────────────────────
// Define these in CMake or before including this header to enable phase-
// specific test code.  They are OFF by default so the scaffolding compiles
// against the current single-window codebase.
//
//   SPECTRA_HAS_WINDOW_CONTEXT   — Agent A merged (WindowContext exists)
//   SPECTRA_HAS_WINDOW_MANAGER   — Agent B merged (WindowManager exists)
//   SPECTRA_HAS_FIGURE_REGISTRY  — Agent C merged (FigureRegistry exists)
//   SPECTRA_HAS_TEAR_OFF         — Agent D merged (tab tear-off works)

// ─── Single-Window Headless Fixture ──────────────────────────────────────────
// Always available. Used for regression testing after each agent merge.

class SingleWindowFixture : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        AppConfig config;
        config.headless = true;
        app_ = std::make_unique<App>(config);
    }

    void TearDown() override { app_.reset(); }

    // Create a figure with a simple line plot for smoke testing
    Figure& create_simple_figure(uint32_t width = 640, uint32_t height = 480)
    {
        auto& fig = app_->figure({.width = width, .height = height});
        auto& ax = fig.subplot(1, 1, 1);
        std::vector<float> x = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f};
        std::vector<float> y = {0.0f, 1.0f, 0.5f, 1.5f, 1.0f};
        ax.line(x, y).label("test");
        ax.xlim(0.0f, 4.0f);
        ax.ylim(-0.5f, 2.0f);
        return fig;
    }

    // Render one frame and verify no crash
    bool render_one_frame()
    {
        app_->run();
        return true;  // If we get here, no crash
    }

    // Readback framebuffer pixels
    bool readback(Figure& fig, std::vector<uint8_t>& pixels)
    {
        uint32_t w = fig.width();
        uint32_t h = fig.height();
        pixels.resize(static_cast<size_t>(w) * h * 4);
        Backend* backend = app_->backend();
        if (!backend)
            return false;
        return backend->readback_framebuffer(pixels.data(), w, h);
    }

    // Check that pixel buffer is not all zeros (something was rendered)
    static bool has_non_zero_pixels(const std::vector<uint8_t>& pixels)
    {
        for (auto p : pixels)
        {
            if (p != 0)
                return true;
        }
        return false;
    }

    std::unique_ptr<App> app_;
};

// ─── Multi-Window Fixture (Phase 2+) ────────────────────────────────────────
// Enabled after Agent B merges WindowManager.
// Until then, this is a stub that creates N headless single-window Apps
// as a stand-in.

#ifdef SPECTRA_HAS_WINDOW_MANAGER

// Forward declarations — these types will exist after Agent A/B merge
// #include "render/vulkan/window_context.hpp"
// #include "ui/window_manager.hpp"

class MultiWindowFixture : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        // Will be implemented when WindowManager exists:
        // app_ = std::make_unique<App>(AppConfig{.headless = true, .socket_path = ""});
        // window_mgr_ will be extracted from app_ or created alongside it
    }

    void TearDown() override
    {
        // Destroy all windows, wait for GPU idle
    }

    // Create N windows, each with its own swapchain
    // Returns window IDs
    // std::vector<uint32_t> create_windows(size_t count, uint32_t w = 640, uint32_t h = 480);

    // Render one frame across all windows
    // bool render_all_windows();

    // Resize a specific window
    // bool resize_window(uint32_t window_id, uint32_t w, uint32_t h);

    // Close a specific window
    // bool close_window(uint32_t window_id);

    // Get number of active windows
    // size_t active_window_count() const;

    // std::unique_ptr<App> app_;
};

#else

// Stub: creates N independent headless Apps to simulate multi-window
// This lets us write test structure now and swap in real multi-window later.
class MultiWindowFixture : public ::testing::Test
{
   protected:
    void SetUp() override {}

    void TearDown() override { apps_.clear(); }

    // Simulate N windows with independent headless Apps
    void create_windows(size_t count, uint32_t w = 640, uint32_t h = 480)
    {
        apps_.clear();
        for (size_t i = 0; i < count; ++i)
        {
            auto app = std::make_unique<App>(AppConfig{.headless = true, .socket_path = ""});
            auto& fig = app->figure({.width = w, .height = h});
            auto& ax = fig.subplot(1, 1, 1);
            std::vector<float> x = {0.0f, 1.0f, 2.0f, 3.0f};
            std::vector<float> y;
            y.resize(x.size());
            for (size_t j = 0; j < x.size(); ++j)
                y[j] = static_cast<float>(i) + x[j] * 0.5f;
            ax.line(x, y).label("window_" + std::to_string(i));
            ax.xlim(0.0f, 3.0f);
            ax.ylim(-1.0f, 5.0f);
            apps_.push_back(std::move(app));
        }
    }

    // Render one frame on all "windows"
    bool render_all_windows()
    {
        for (auto& app : apps_)
        {
            app->run();
        }
        return true;
    }

    // Readback from a specific "window"
    bool readback_window(size_t index, std::vector<uint8_t>& pixels)
    {
        if (index >= apps_.size())
            return false;
        auto* backend = apps_[index]->backend();
        if (!backend)
            return false;
        uint32_t w = backend->swapchain_width();
        uint32_t h = backend->swapchain_height();
        pixels.resize(static_cast<size_t>(w) * h * 4);
        return backend->readback_framebuffer(pixels.data(), w, h);
    }

    size_t active_window_count() const { return apps_.size(); }

    std::vector<std::unique_ptr<App>> apps_;
};

#endif  // SPECTRA_HAS_WINDOW_MANAGER

// ─── Figure Registry Fixture (Phase 3+) ─────────────────────────────────────
// Enabled after Agent C merges FigureRegistry.

#ifdef SPECTRA_HAS_FIGURE_REGISTRY

// Will be implemented when FigureRegistry exists
// class FigureRegistryFixture : public ::testing::Test { ... };

#endif

// ─── Timing Utility ──────────────────────────────────────────────────────────
// Measures wall-clock time for a callable. Useful for frame time assertions.

template <typename Func>
double measure_ms(Func&& fn)
{
    auto start = std::chrono::high_resolution_clock::now();
    fn();
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// ─── Stress Test Helpers ─────────────────────────────────────────────────────

// Run a callable N times and return (min, max, avg) durations in ms
struct TimingStats
{
    double min_ms = 0.0;
    double max_ms = 0.0;
    double avg_ms = 0.0;
    size_t iterations = 0;
};

template <typename Func>
TimingStats run_stress(size_t iterations, Func&& fn)
{
    TimingStats stats;
    stats.iterations = iterations;
    stats.min_ms = 1e9;
    stats.max_ms = 0.0;
    double total = 0.0;

    for (size_t i = 0; i < iterations; ++i)
    {
        double ms = measure_ms(fn);
        if (ms < stats.min_ms)
            stats.min_ms = ms;
        if (ms > stats.max_ms)
            stats.max_ms = ms;
        total += ms;
    }

    stats.avg_ms = (iterations > 0) ? (total / static_cast<double>(iterations)) : 0.0;
    return stats;
}

}  // namespace spectra::test
