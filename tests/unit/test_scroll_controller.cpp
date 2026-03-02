// test_scroll_controller.cpp — Unit tests for ScrollController (no ROS2, no ImGui).
//
// ScrollController is pure C++20 with a Spectra LineSeries/Axes dependency only.
// All tests are headless: no Vulkan, no GLFW, no ImGui context required.

#include <cmath>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <spectra/axes.hpp>
#include <spectra/series.hpp>

#include "scroll_controller.hpp"

using namespace spectra::adapters::ros2;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Build a LineSeries with n evenly spaced timestamps starting at t0 (seconds).
static spectra::LineSeries make_series(double t0, double dt, int n, float y_val = 1.0f)
{
    spectra::LineSeries s;
    for (int i = 0; i < n; ++i)
        s.append(static_cast<float>(t0 + i * dt), y_val);
    return s;
}

// ---------------------------------------------------------------------------
// Suite: Construction
// ---------------------------------------------------------------------------

TEST(ScrollController, DefaultConstruction)
{
    ScrollController sc;
    EXPECT_DOUBLE_EQ(sc.window_s(), ScrollController::DEFAULT_WINDOW_S);
    EXPECT_FALSE(sc.is_paused());
    EXPECT_DOUBLE_EQ(sc.now(), 0.0);
}

TEST(ScrollController, DefaultWindowIs30s)
{
    EXPECT_DOUBLE_EQ(ScrollController::DEFAULT_WINDOW_S, 30.0);
}

TEST(ScrollController, ConstantsRange)
{
    EXPECT_LT(ScrollController::MIN_WINDOW_S, ScrollController::MAX_WINDOW_S);
    EXPECT_LE(ScrollController::MIN_WINDOW_S, ScrollController::DEFAULT_WINDOW_S);
    EXPECT_LE(ScrollController::DEFAULT_WINDOW_S, ScrollController::MAX_WINDOW_S);
}

// ---------------------------------------------------------------------------
// Suite: Window configuration
// ---------------------------------------------------------------------------

TEST(ScrollController, SetWindowBasic)
{
    ScrollController sc;
    sc.set_window_s(60.0);
    EXPECT_DOUBLE_EQ(sc.window_s(), 60.0);
}

TEST(ScrollController, SetWindowClampsToMin)
{
    ScrollController sc;
    sc.set_window_s(0.0);
    EXPECT_DOUBLE_EQ(sc.window_s(), ScrollController::MIN_WINDOW_S);
}

TEST(ScrollController, SetWindowClampsToMax)
{
    ScrollController sc;
    sc.set_window_s(1e9);
    EXPECT_DOUBLE_EQ(sc.window_s(), ScrollController::MAX_WINDOW_S);
}

TEST(ScrollController, SetWindowExactMin)
{
    ScrollController sc;
    sc.set_window_s(ScrollController::MIN_WINDOW_S);
    EXPECT_DOUBLE_EQ(sc.window_s(), ScrollController::MIN_WINDOW_S);
}

TEST(ScrollController, SetWindowExactMax)
{
    ScrollController sc;
    sc.set_window_s(ScrollController::MAX_WINDOW_S);
    EXPECT_DOUBLE_EQ(sc.window_s(), ScrollController::MAX_WINDOW_S);
}

// ---------------------------------------------------------------------------
// Suite: Pause / resume
// ---------------------------------------------------------------------------

TEST(ScrollController, InitiallyFollowing)
{
    ScrollController sc;
    EXPECT_FALSE(sc.is_paused());
}

TEST(ScrollController, PauseSetsFlag)
{
    ScrollController sc;
    sc.pause();
    EXPECT_TRUE(sc.is_paused());
}

TEST(ScrollController, ResumeClears)
{
    ScrollController sc;
    sc.pause();
    sc.resume();
    EXPECT_FALSE(sc.is_paused());
}

TEST(ScrollController, TogglePaused)
{
    ScrollController sc;
    EXPECT_FALSE(sc.is_paused());
    sc.toggle_paused();
    EXPECT_TRUE(sc.is_paused());
    sc.toggle_paused();
    EXPECT_FALSE(sc.is_paused());
}

TEST(ScrollController, DoubleResumeIdempotent)
{
    ScrollController sc;
    sc.resume();
    sc.resume();
    EXPECT_FALSE(sc.is_paused());
}

TEST(ScrollController, DoublePauseIdempotent)
{
    ScrollController sc;
    sc.pause();
    sc.pause();
    EXPECT_TRUE(sc.is_paused());
}

// ---------------------------------------------------------------------------
// Suite: set_now / view bounds
// ---------------------------------------------------------------------------

TEST(ScrollController, SetNow)
{
    ScrollController sc;
    sc.set_now(1000.0);
    EXPECT_DOUBLE_EQ(sc.now(), 1000.0);
}

TEST(ScrollController, ViewBoundsAfterTick)
{
    ScrollController sc;
    sc.set_window_s(10.0);
    sc.set_now(100.0);
    sc.tick(nullptr, nullptr);   // no axes/series — just update bounds
    EXPECT_DOUBLE_EQ(sc.view_min(), 90.0);
    EXPECT_DOUBLE_EQ(sc.view_max(), 100.0);
}

TEST(ScrollController, ViewBoundsNotUpdatedWhenNullAxes)
{
    ScrollController sc;
    sc.set_window_s(10.0);
    sc.set_now(100.0);
    sc.tick(nullptr, nullptr);
    // view_min_ / view_max_ are computed even when axes is nullptr
    // (the xlim call is skipped, but internal state updates).
    EXPECT_DOUBLE_EQ(sc.view_min(), 90.0);
    EXPECT_DOUBLE_EQ(sc.view_max(), 100.0);
}

TEST(ScrollController, ViewBoundsNotUpdatedWhenPaused)
{
    ScrollController sc;
    sc.set_window_s(10.0);
    sc.set_now(100.0);
    sc.pause();
    sc.tick(nullptr, nullptr);
    // When paused, xlim is skipped so view_min_/view_max_ remain at defaults.
    EXPECT_DOUBLE_EQ(sc.view_min(), 0.0);   // unchanged from init
    EXPECT_DOUBLE_EQ(sc.view_max(), 0.0);
}

// ---------------------------------------------------------------------------
// Suite: Pruning
// ---------------------------------------------------------------------------

TEST(ScrollController, NoPruneWhenDataInWindow)
{
    // 10 samples spanning [990, 999], window = 30s, now = 1000
    spectra::LineSeries series = make_series(990.0, 1.0, 10);
    ASSERT_EQ(series.point_count(), 10u);

    ScrollController sc;
    sc.set_window_s(30.0);
    sc.set_now(1000.0);
    sc.tick(&series, nullptr);

    // prune_before = 1000 - 2*30 = 940; all 10 samples are >= 940 → no prune
    EXPECT_EQ(series.point_count(), 10u);
    EXPECT_EQ(sc.last_pruned_count(), 0u);
}

TEST(ScrollController, PrunesOldData)
{
    // 20 samples at t = 900..919, window = 10s, now = 1000
    // prune_before = 1000 - 2*10 = 980 → all 20 samples pruned
    spectra::LineSeries series = make_series(900.0, 1.0, 20);
    ASSERT_EQ(series.point_count(), 20u);

    ScrollController sc;
    sc.set_window_s(10.0);
    sc.set_now(1000.0);
    sc.tick(&series, nullptr);

    EXPECT_EQ(series.point_count(), 0u);
    EXPECT_EQ(sc.last_pruned_count(), 20u);
}

TEST(ScrollController, PrunesOnlyOldData)
{
    // 10 samples at t = [995..1004], window = 10s, now = 1010
    // prune_before = 1010 - 20 = 990 → samples at 995..1004 survive
    spectra::LineSeries series = make_series(985.0, 1.0, 20);  // t=[985..1004]
    ASSERT_EQ(series.point_count(), 20u);

    ScrollController sc;
    sc.set_window_s(10.0);
    sc.set_now(1010.0);
    // prune_before = 1010 - 20 = 990
    // samples at t=985..989 pruned (5), t=990..1004 kept (15)
    sc.tick(&series, nullptr);

    EXPECT_EQ(series.point_count(), 15u);
    EXPECT_EQ(sc.last_pruned_count(), 5u);
}

TEST(ScrollController, PrunePreservesOrder)
{
    // Build series [0..99], prune_before = 50
    spectra::LineSeries series = make_series(0.0, 1.0, 100);

    ScrollController sc;
    sc.set_window_s(25.0);   // 2*25 = 50s prune horizon
    sc.set_now(100.0);
    sc.tick(&series, nullptr);

    // Expect remaining: t=[50..99] => 50 samples
    EXPECT_EQ(series.point_count(), 50u);
    if (series.point_count() > 0)
    {
        EXPECT_FLOAT_EQ(series.x_data()[0], 50.0f);
        EXPECT_FLOAT_EQ(series.x_data().back(), 99.0f);
    }
}

TEST(ScrollController, PruneNullSeriesNoOp)
{
    ScrollController sc;
    sc.set_window_s(10.0);
    sc.set_now(1000.0);
    // Must not crash with nullptr series.
    sc.tick(nullptr, nullptr);
    EXPECT_EQ(sc.last_pruned_count(), 0u);
}

TEST(ScrollController, PruneEmptySeriesNoOp)
{
    spectra::LineSeries series;   // empty
    ScrollController sc;
    sc.set_window_s(10.0);
    sc.set_now(1000.0);
    sc.tick(&series, nullptr);
    EXPECT_EQ(series.point_count(), 0u);
    EXPECT_EQ(sc.last_pruned_count(), 0u);
}

TEST(ScrollController, PruneStillHappensWhenPaused)
{
    // Pruning runs regardless of pause state.
    spectra::LineSeries series = make_series(900.0, 1.0, 20);  // all old

    ScrollController sc;
    sc.set_window_s(10.0);
    sc.set_now(1000.0);
    sc.pause();
    sc.tick(&series, nullptr);

    // prune_before = 980; all samples at 900..919 pruned
    EXPECT_EQ(series.point_count(), 0u);
}

// ---------------------------------------------------------------------------
// Suite: xlim applied to Axes
// ---------------------------------------------------------------------------

TEST(ScrollController, AppliesXlimToAxes)
{
    spectra::Axes axes;
    ScrollController sc;
    sc.set_window_s(30.0);
    sc.set_now(1000.0);
    sc.tick(nullptr, &axes);

    auto lim = axes.x_limits();
    EXPECT_NEAR(lim.min, 970.0, 1e-6);
    EXPECT_NEAR(lim.max, 1000.0, 1e-6);
}

TEST(ScrollController, SkipsXlimWhenPaused)
{
    spectra::Axes axes;
    // Set initial limits so we can detect no-change.
    axes.xlim(0.0, 100.0);

    ScrollController sc;
    sc.set_window_s(30.0);
    sc.set_now(1000.0);
    sc.pause();
    sc.tick(nullptr, &axes);

    // xlim must remain unchanged when paused.
    auto lim = axes.x_limits();
    EXPECT_NEAR(lim.min, 0.0, 1e-6);
    EXPECT_NEAR(lim.max, 100.0, 1e-6);
}

TEST(ScrollController, ResumeThenXlimUpdates)
{
    spectra::Axes axes;
    axes.xlim(0.0, 1.0);

    ScrollController sc;
    sc.set_window_s(10.0);
    sc.set_now(500.0);
    sc.pause();
    sc.tick(nullptr, &axes);   // paused — no update

    sc.resume();
    sc.tick(nullptr, &axes);   // following again

    auto lim = axes.x_limits();
    EXPECT_NEAR(lim.min, 490.0, 1e-6);
    EXPECT_NEAR(lim.max, 500.0, 1e-6);
}

// ---------------------------------------------------------------------------
// Suite: Memory indicator
// ---------------------------------------------------------------------------

TEST(ScrollController, MemoryBytesNullptr)
{
    EXPECT_EQ(ScrollController::memory_bytes(nullptr), 0u);
}

TEST(ScrollController, MemoryBytesEmptySeries)
{
    spectra::LineSeries s;
    EXPECT_EQ(ScrollController::memory_bytes(&s), 0u);
}

TEST(ScrollController, MemoryBytesNPoints)
{
    spectra::LineSeries s = make_series(0.0, 1.0, 100);
    // 100 x-values + 100 y-values, each float (4 bytes)
    EXPECT_EQ(ScrollController::memory_bytes(&s), 100u * 2u * sizeof(float));
}

TEST(ScrollController, MemoryBytesDecreasesAfterPrune)
{
    spectra::LineSeries series = make_series(900.0, 1.0, 100);
    const size_t before = ScrollController::memory_bytes(&series);

    ScrollController sc;
    sc.set_window_s(10.0);
    sc.set_now(1000.0);
    sc.tick(&series, nullptr);

    const size_t after = ScrollController::memory_bytes(&series);
    EXPECT_LT(after, before);
}

// ---------------------------------------------------------------------------
// Suite: Window label
// ---------------------------------------------------------------------------

TEST(ScrollController, WindowLabelSeconds)
{
    ScrollController sc;
    sc.set_window_s(30.0);
    EXPECT_EQ(sc.window_label(), "30 s");
}

TEST(ScrollController, WindowLabelMinutes)
{
    ScrollController sc;
    sc.set_window_s(120.0);
    EXPECT_EQ(sc.window_label(), "2 min");
}

TEST(ScrollController, WindowLabelHours)
{
    ScrollController sc;
    sc.set_window_s(3600.0);
    EXPECT_EQ(sc.window_label(), "1 h");
}

TEST(ScrollController, WindowLabel1s)
{
    ScrollController sc;
    sc.set_window_s(1.0);
    EXPECT_EQ(sc.window_label(), "1 s");
}

// ---------------------------------------------------------------------------
// Suite: status_text
// ---------------------------------------------------------------------------

TEST(ScrollController, StatusTextFollowing)
{
    ScrollController sc;
    EXPECT_STREQ(sc.status_text(), "following");
}

TEST(ScrollController, StatusTextPaused)
{
    ScrollController sc;
    sc.pause();
    EXPECT_STREQ(sc.status_text(), "paused");
}

// ---------------------------------------------------------------------------
// Suite: PruneFactorConstant
// ---------------------------------------------------------------------------

TEST(ScrollController, PruneFactorIs2)
{
    EXPECT_DOUBLE_EQ(ScrollController::PRUNE_FACTOR, 2.0);
}

TEST(ScrollController, PruneHorizonIs2xWindow)
{
    // 10 samples: [0..9]. window=5, now=20 → prune_before = 20 - 10 = 10
    // All samples < 10 are pruned.
    spectra::LineSeries series = make_series(0.0, 1.0, 10);

    ScrollController sc;
    sc.set_window_s(5.0);
    sc.set_now(20.0);
    sc.tick(&series, nullptr);

    EXPECT_EQ(series.point_count(), 0u);
}

// ---------------------------------------------------------------------------
// Suite: last_pruned_count
// ---------------------------------------------------------------------------

TEST(ScrollController, LastPrunedCountZeroOnNoPrune)
{
    spectra::LineSeries series = make_series(995.0, 1.0, 5);

    ScrollController sc;
    sc.set_window_s(30.0);
    sc.set_now(1000.0);
    sc.tick(&series, nullptr);

    EXPECT_EQ(sc.last_pruned_count(), 0u);
}

TEST(ScrollController, LastPrunedCountUpdatedEachTick)
{
    // First tick: no prune. Second tick: prune some.
    spectra::LineSeries series = make_series(990.0, 1.0, 10);   // [990..999]

    ScrollController sc;
    sc.set_window_s(10.0);
    sc.set_now(1010.0);   // prune_before = 990 — samples at 990..989 pruned (none)
    sc.tick(&series, nullptr);
    EXPECT_EQ(sc.last_pruned_count(), 0u);

    sc.set_now(1015.0);   // prune_before = 995 — samples at 990..994 pruned (5)
    sc.tick(&series, nullptr);
    EXPECT_EQ(sc.last_pruned_count(), 5u);
}

// ---------------------------------------------------------------------------
// Suite: EdgeCases
// ---------------------------------------------------------------------------

TEST(ScrollController, SetNowZero)
{
    ScrollController sc;
    sc.set_now(0.0);
    EXPECT_DOUBLE_EQ(sc.now(), 0.0);
}

TEST(ScrollController, NegativeWindowClampedToMin)
{
    ScrollController sc;
    sc.set_window_s(-5.0);
    EXPECT_DOUBLE_EQ(sc.window_s(), ScrollController::MIN_WINDOW_S);
}

TEST(ScrollController, MultipleTicksSameNow)
{
    spectra::Axes axes;
    ScrollController sc;
    sc.set_window_s(10.0);
    sc.set_now(100.0);

    sc.tick(nullptr, &axes);
    auto lim1 = axes.x_limits();

    sc.tick(nullptr, &axes);
    auto lim2 = axes.x_limits();

    // Same now → same limits.
    EXPECT_NEAR(lim1.min, lim2.min, 1e-9);
    EXPECT_NEAR(lim1.max, lim2.max, 1e-9);
}

TEST(ScrollController, TickWithBothNullNoOp)
{
    ScrollController sc;
    sc.set_window_s(30.0);
    sc.set_now(100.0);
    // Must not crash.
    sc.tick(nullptr, nullptr);
    EXPECT_EQ(sc.last_pruned_count(), 0u);
}
