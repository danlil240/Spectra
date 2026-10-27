#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <latch>
#include <optional>
#include <spectra/theme_api.hpp>
#include <thread>
#include <vector>

#include "ui/theme/theme.hpp"

using namespace spectra;

namespace
{
std::vector<ThemeSnapshot> g_calls;

void record_call(const ThemeSnapshot& snap)
{
    g_calls.push_back(snap);
}

void expect_rgba_eq(const float got[4], const ui::Color& want)
{
    EXPECT_FLOAT_EQ(got[0], want.r);
    EXPECT_FLOAT_EQ(got[1], want.g);
    EXPECT_FLOAT_EQ(got[2], want.b);
    EXPECT_FLOAT_EQ(got[3], want.a);
}

class ThemeChangeCallbackTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        ui::ThemeManager::set_current(&tm_);
        tm_.ensure_initialized();
        tm_.set_theme("night");
        g_calls.clear();
        register_theme_change_callback(&record_call);
    }

    void TearDown() override
    {
        register_theme_change_callback(nullptr);
        g_calls.clear();
        ui::ThemeManager::set_current(nullptr);
    }

    ui::ThemeManager tm_;
};
}   // namespace

TEST_F(ThemeChangeCallbackTest, SwitchNightToLightDeliversLightSnapshot)
{
    const ui::Color night_canvas = tm_.colors().bg_canvas;
    tm_.set_theme("light");

    ASSERT_EQ(g_calls.size(), 1u);
    const ThemeSnapshot&   snap  = g_calls[0];
    const ui::ThemeColors& light = tm_.colors();
    ASSERT_EQ(tm_.current_theme_name(), "light");
    EXPECT_NE(light.bg_canvas, night_canvas);
    expect_rgba_eq(snap.bg_canvas, light.bg_canvas);
    expect_rgba_eq(snap.bg_primary, light.bg_primary);
    expect_rgba_eq(snap.text_primary, light.text_primary);
    expect_rgba_eq(snap.accent, light.accent);
    expect_rgba_eq(snap.crosshair, light.crosshair);
    EXPECT_EQ(snap.version, tm_.theme_version());

    ThemeSnapshot polled{};
    EXPECT_EQ(get_theme_snapshot(polled), snap.version);
}

TEST_F(ThemeChangeCallbackTest, UnregisterStopsCalls)
{
    register_theme_change_callback(nullptr);
    tm_.set_theme("light");
    tm_.set_theme("night");
    EXPECT_TRUE(g_calls.empty());
}

TEST_F(ThemeChangeCallbackTest, ReapplyingSameThemeDoesNotRepeat)
{
    tm_.set_theme("night");   // already active
    EXPECT_TRUE(g_calls.empty());

    tm_.set_theme("light");
    tm_.set_theme("light");
    tm_.set_theme("light");
    EXPECT_EQ(g_calls.size(), 1u);

    tm_.set_theme("night");
    EXPECT_EQ(g_calls.size(), 2u);
}

TEST_F(ThemeChangeCallbackTest, UnknownThemeDoesNotFire)
{
    tm_.set_theme("does-not-exist");
    EXPECT_TRUE(g_calls.empty());
}

TEST_F(ThemeChangeCallbackTest, TransitionFiresOnceOnCompletion)
{
    tm_.transition_to("light", 0.2f);
    tm_.update(0.05f);
    tm_.update(0.05f);
    EXPECT_TRUE(g_calls.empty()) << "intermediate frames must not be reported";

    tm_.update(0.2f);   // completes
    ASSERT_EQ(g_calls.size(), 1u);
    EXPECT_EQ(tm_.current_theme_name(), "light");
    expect_rgba_eq(g_calls[0].bg_canvas, tm_.colors().bg_canvas);
}

TEST_F(ThemeChangeCallbackTest, PaletteAndGlassChangesDoNotFire)
{
    tm_.set_data_palette("colorblind");
    tm_.reset_glass_defaults();
    EXPECT_TRUE(g_calls.empty());
}

// ─── Unregister vs. concurrent dispatch ──────────────────────────────────────

namespace
{
// Waits up to `timeout` for `flag` to become true.  Used to give a racing
// thread the chance to (incorrectly) finish; returns the final value.
bool wait_for_flag(const std::atomic<bool>& flag, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!flag.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    return flag.load();
}

std::atomic<bool>         g_unregister_returned{false};
std::atomic<bool>         g_old_started{false};
std::atomic<bool>         g_old_started_after_unregister{false};
std::optional<std::latch> g_loaded;
std::optional<std::latch> g_release;
std::atomic<bool>         g_hook_fired{false};

void old_callback(const ThemeSnapshot&)
{
    g_old_started_after_unregister = g_unregister_returned.load();
    g_old_started                  = true;
}

// Parks the switching thread after it has loaded the registered pointer but
// before it calls it.
void park_after_load()
{
    if (g_hook_fired.exchange(true))
        return;
    g_loaded->count_down();
    g_release->wait();
}

void blocking_callback(const ThemeSnapshot&)
{
    g_old_started = true;
    g_loaded->count_down();
    g_release->wait();
}

class ThemeCallbackRaceTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        ui::ThemeManager::set_current(&tm_);
        tm_.ensure_initialized();
        tm_.set_theme("night");
        g_unregister_returned          = false;
        g_old_started                  = false;
        g_old_started_after_unregister = false;
        g_hook_fired                   = false;
        g_loaded.emplace(1);
        g_release.emplace(1);
    }

    void TearDown() override
    {
        detail::set_theme_callback_dispatch_hook_for_testing(nullptr);
        register_theme_change_callback(nullptr);
        ui::ThemeManager::set_current(nullptr);
    }

    ui::ThemeManager tm_;
};
}   // namespace

// The switcher loads the old pointer, then another thread unregisters.  The
// unregister must not return before the old call has started and finished, so
// the callback never starts after unregister has returned.
TEST_F(ThemeCallbackRaceTest, CallbackNeverStartsAfterUnregisterReturns)
{
    register_theme_change_callback(&old_callback);
    detail::set_theme_callback_dispatch_hook_for_testing(&park_after_load);

    std::thread switcher([&] { tm_.set_theme("light"); });
    g_loaded->wait();   // switcher has loaded &old_callback and is parked

    std::thread unregisterer(
        []
        {
            register_theme_change_callback(nullptr);
            g_unregister_returned = true;
        });

    // Unregister must block while a dispatch is in progress.
    EXPECT_FALSE(wait_for_flag(g_unregister_returned, std::chrono::milliseconds(50)));

    g_release->count_down();
    switcher.join();
    unregisterer.join();

    EXPECT_TRUE(g_old_started);
    EXPECT_FALSE(g_old_started_after_unregister)
        << "old callback started after register_theme_change_callback(nullptr) returned";
    EXPECT_TRUE(g_unregister_returned);
}

// Stronger guarantee: unregister waits for a call that is already running.
TEST_F(ThemeCallbackRaceTest, UnregisterWaitsForInFlightCall)
{
    register_theme_change_callback(&blocking_callback);

    std::thread switcher([&] { tm_.set_theme("light"); });
    g_loaded->wait();   // callback is running and blocked

    std::thread unregisterer(
        []
        {
            register_theme_change_callback(nullptr);
            g_unregister_returned = true;
        });

    EXPECT_FALSE(wait_for_flag(g_unregister_returned, std::chrono::milliseconds(50)))
        << "unregister returned while the callback was still running";

    g_release->count_down();
    switcher.join();
    unregisterer.join();
    EXPECT_TRUE(g_unregister_returned);
}

// ─── Re-entrancy on the dispatching thread ───────────────────────────────────

namespace
{
int g_self_unregister_calls = 0;

void self_unregistering_callback(const ThemeSnapshot&)
{
    ++g_self_unregister_calls;
    register_theme_change_callback(nullptr);   // must not deadlock
}

ui::ThemeManager*        g_reentrant_tm = nullptr;
std::vector<std::string> g_reentrant_seen;

void theme_switching_callback(const ThemeSnapshot&)
{
    g_reentrant_seen.push_back(g_reentrant_tm->current_theme_name());
    if (g_reentrant_tm->current_theme_name() == "light")
        g_reentrant_tm->set_theme("night");   // nested dispatch, must not deadlock
}
}   // namespace

TEST_F(ThemeChangeCallbackTest, CallbackMayUnregisterItself)
{
    g_self_unregister_calls = 0;
    register_theme_change_callback(&self_unregistering_callback);
    tm_.set_theme("light");
    tm_.set_theme("night");
    EXPECT_EQ(g_self_unregister_calls, 1);
}

TEST_F(ThemeChangeCallbackTest, CallbackMaySwitchThemeReentrantly)
{
    g_reentrant_tm = &tm_;
    g_reentrant_seen.clear();
    register_theme_change_callback(&theme_switching_callback);
    tm_.set_theme("light");
    EXPECT_EQ(tm_.current_theme_name(), "night");
    ASSERT_EQ(g_reentrant_seen.size(), 2u);
    EXPECT_EQ(g_reentrant_seen[0], "light");
    EXPECT_EQ(g_reentrant_seen[1], "night");
    g_reentrant_tm = nullptr;
}
