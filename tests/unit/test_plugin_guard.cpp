// test_plugin_guard.cpp — Unit tests for plugin crash isolation (MR-7).

#include <gtest/gtest.h>

#include <stdexcept>

#include "ui/overlay/overlay_registry.hpp"
#include "ui/workspace/plugin_guard.hpp"

using namespace spectra;

// ─── Basic guard tests ──────────────────────────────────────────────────────

TEST(PluginGuard, SuccessfulCallback)
{
    int  value  = 0;
    auto result = plugin_guard_invoke("test", [&]() { value = 42; });
    EXPECT_EQ(result, PluginCallResult::Success);
    EXPECT_EQ(value, 42);
}

TEST(PluginGuard, CatchStdException)
{
    auto result =
        plugin_guard_invoke("throw_test", [&]() { throw std::runtime_error("test error"); });
    EXPECT_EQ(result, PluginCallResult::Exception);
}

TEST(PluginGuard, CatchUnknownException)
{
    auto result = plugin_guard_invoke("throw_int", [&]() { throw 42; });
    EXPECT_EQ(result, PluginCallResult::Exception);
}

TEST(PluginGuard, NullContextName)
{
    auto result = plugin_guard_invoke(nullptr, [&]() {});
    EXPECT_EQ(result, PluginCallResult::Success);
}

#ifndef _WIN32
TEST(PluginGuard, CatchSegfault)
{
    auto result = plugin_guard_invoke("segfault_test",
                                      [&]()
                                      {
                                          volatile int* p = nullptr;
                                          *p              = 42;   // SIGSEGV
                                      });
    EXPECT_EQ(result, PluginCallResult::Signal);
}
#endif

TEST(PluginGuard, MultipleSuccessfulCalls)
{
    int count = 0;
    for (int i = 0; i < 10; i++)
    {
        auto result = plugin_guard_invoke("loop_test", [&]() { count++; });
        EXPECT_EQ(result, PluginCallResult::Success);
    }
    EXPECT_EQ(count, 10);
}

TEST(PluginGuard, RecoveryAfterException)
{
    // First call: exception
    auto r1 = plugin_guard_invoke("fail", [&]() { throw std::runtime_error("boom"); });
    EXPECT_EQ(r1, PluginCallResult::Exception);

    // Second call: should succeed (guard resets properly)
    int  value = 0;
    auto r2    = plugin_guard_invoke("ok", [&]() { value = 99; });
    EXPECT_EQ(r2, PluginCallResult::Success);
    EXPECT_EQ(value, 99);
}

#ifndef _WIN32
TEST(PluginGuard, RecoveryAfterSignal)
{
    // First call: segfault
    auto r1 = plugin_guard_invoke("crash",
                                  [&]()
                                  {
                                      volatile int* p = nullptr;
                                      *p              = 1;
                                  });
    EXPECT_EQ(r1, PluginCallResult::Signal);

    // Second call: should succeed
    int  value = 0;
    auto r2    = plugin_guard_invoke("ok", [&]() { value = 77; });
    EXPECT_EQ(r2, PluginCallResult::Success);
    EXPECT_EQ(value, 77);
}
#endif

// ─── Overlay registry integration tests ──────────────────────────────────────

TEST(PluginGuard, OverlayFaultSkipsFutureInvocations)
{
    OverlayRegistry reg;

    int good_count = 0;

    reg.register_overlay("good_overlay", [&](const OverlayDrawContext&) { good_count++; });
    reg.register_overlay("bad_overlay",
                         [&](const OverlayDrawContext&)
                         { throw std::runtime_error("plugin bug"); });

    OverlayDrawContext ctx{};
    ctx.viewport_w = 800;
    ctx.viewport_h = 600;

    // First call: bad overlay faults, good overlay runs
    reg.draw_all(ctx);
    EXPECT_EQ(good_count, 1);

    // Second call: bad overlay is skipped (faulted), good overlay runs
    reg.draw_all(ctx);
    EXPECT_EQ(good_count, 2);

    // Third call: same — faulted overlay stays disabled
    reg.draw_all(ctx);
    EXPECT_EQ(good_count, 3);
}

TEST(PluginGuard, OverlayAllFaultedStaysStable)
{
    OverlayRegistry reg;

    reg.register_overlay("bad1",
                         [&](const OverlayDrawContext&) { throw std::runtime_error("fault1"); });
    reg.register_overlay("bad2",
                         [&](const OverlayDrawContext&) { throw std::runtime_error("fault2"); });

    OverlayDrawContext ctx{};

    // Both fault on first call
    reg.draw_all(ctx);

    // Subsequent calls are no-ops (all faulted) — no crash
    reg.draw_all(ctx);
    reg.draw_all(ctx);

    // Count should still be 2 (entries exist but are faulted)
    EXPECT_EQ(reg.count(), 2u);
}
