// test_plugin_diagnostics.cpp — Unit tests for PluginDiagnostics and lifecycle tracking.

#include <gtest/gtest.h>

#include <cstdlib>
#include <stdexcept>

#include "ui/workspace/plugin_guard.hpp"

using namespace spectra;

// UBSan intercepts null-pointer dereferences before our signal handler can act,
// so crash-recovery tests must be skipped under UBSan.
static bool running_under_ubsan()
{
#if defined(__has_feature)
    #if __has_feature(undefined_behavior_sanitizer)
    return true;
    #endif
#endif
#if defined(__SANITIZE_UNDEFINED__)
    return true;
#endif
    return std::getenv("UBSAN_OPTIONS") != nullptr;
}

// ─── PluginDiagnostics struct ────────────────────────────────────────────────

TEST(PluginDiagnostics, DefaultInitialization)
{
    PluginDiagnostics diag;
    EXPECT_EQ(diag.init_time_us, 0u);
    EXPECT_EQ(diag.call_count, 0u);
    EXPECT_EQ(diag.fault_count, 0u);
    EXPECT_EQ(diag.last_fault_time, 0u);
    EXPECT_TRUE(diag.last_fault_reason.empty());
    EXPECT_FALSE(diag.quarantined);
}

// ─── call_count tracking ────────────────────────────────────────────────────

TEST(PluginDiagnostics, CallCountIncrementedOnSuccess)
{
    PluginDiagnostics diag;
    plugin_guard_invoke("test", []() {}, &diag);
    EXPECT_EQ(diag.call_count, 1u);
    plugin_guard_invoke("test", []() {}, &diag);
    EXPECT_EQ(diag.call_count, 2u);
}

TEST(PluginDiagnostics, CallCountIncrementedOnException)
{
    PluginDiagnostics diag;
    plugin_guard_invoke("test", []() { throw std::runtime_error("boom"); }, &diag);
    EXPECT_EQ(diag.call_count, 1u);
    EXPECT_EQ(diag.fault_count, 1u);
}

TEST(PluginDiagnostics, NullDiagnosticsPointerIsIgnored)
{
    // Should not crash when diag is nullptr
    auto result = plugin_guard_invoke("test", []() {}, nullptr);
    EXPECT_EQ(result, PluginCallResult::Success);
}

// ─── fault_count and fault info ─────────────────────────────────────────────

TEST(PluginDiagnostics, FaultCountIncrementedOnException)
{
    PluginDiagnostics diag;
    plugin_guard_invoke("test", []() { throw std::runtime_error("err"); }, &diag);
    EXPECT_EQ(diag.fault_count, 1u);
}

TEST(PluginDiagnostics, LastFaultReasonPopulatedOnException)
{
    PluginDiagnostics diag;
    plugin_guard_invoke("test", []() { throw std::runtime_error("specific error"); }, &diag);
    EXPECT_FALSE(diag.last_fault_reason.empty());
    EXPECT_NE(diag.last_fault_reason.find("specific error"), std::string::npos);
}

TEST(PluginDiagnostics, LastFaultReasonPopulatedOnUnknownException)
{
    PluginDiagnostics diag;
    plugin_guard_invoke("test", []() { throw 42; }, &diag);
    EXPECT_FALSE(diag.last_fault_reason.empty());
}

TEST(PluginDiagnostics, LastFaultTimeSetAfterException)
{
    PluginDiagnostics diag;
    plugin_guard_invoke("test", []() { throw std::runtime_error("err"); }, &diag);
    EXPECT_GT(diag.last_fault_time, 0u);
}

#ifndef _WIN32
TEST(PluginDiagnostics, FaultCountIncrementedOnSignal)
{
    if (running_under_ubsan())
        GTEST_SKIP() << "Skipped under UBSan (intentional null deref)";

    PluginDiagnostics diag;
    plugin_guard_invoke(
        "test",
        []()
        {
            volatile int* p = nullptr;
            *p              = 1;   // SIGSEGV
        },
        &diag);
    EXPECT_EQ(diag.fault_count, 1u);
    EXPECT_GT(diag.last_fault_time, 0u);
    EXPECT_FALSE(diag.last_fault_reason.empty());
}
#endif

// ─── Quarantine logic ────────────────────────────────────────────────────────

TEST(PluginDiagnostics, QuarantineAfterThresholdFaults)
{
    PluginDiagnostics diag;
    for (int i = 0; i < PLUGIN_QUARANTINE_THRESHOLD; i++)
    {
        EXPECT_FALSE(diag.quarantined) << "Should not be quarantined before threshold";
        plugin_guard_invoke("test", []() { throw std::runtime_error("err"); }, &diag);
    }
    EXPECT_TRUE(diag.quarantined);
}

TEST(PluginDiagnostics, QuarantinedPluginCallReturnsQuarantined)
{
    PluginDiagnostics diag;
    diag.quarantined = true;

    auto result = plugin_guard_invoke("test", []() {}, &diag);
    EXPECT_EQ(result, PluginCallResult::Quarantined);
}

TEST(PluginDiagnostics, QuarantinedPluginCallDoesNotIncrementCallCount)
{
    PluginDiagnostics diag;
    diag.quarantined = true;

    plugin_guard_invoke("test", []() {}, &diag);
    EXPECT_EQ(diag.call_count, 0u);
}

TEST(PluginDiagnostics, QuarantinedPluginCallbackIsNotInvoked)
{
    PluginDiagnostics diag;
    diag.quarantined = true;

    bool called = false;
    plugin_guard_invoke("test", [&]() { called = true; }, &diag);
    EXPECT_FALSE(called);
}

TEST(PluginDiagnostics, BelowThresholdNoQuarantine)
{
    PluginDiagnostics diag;
    for (int i = 0; i < PLUGIN_QUARANTINE_THRESHOLD - 1; i++)
        plugin_guard_invoke("test", []() { throw std::runtime_error("err"); }, &diag);
    EXPECT_FALSE(diag.quarantined);
}

TEST(PluginDiagnostics, SuccessfulCallsDoNotIncrementFaultCount)
{
    PluginDiagnostics diag;
    for (int i = 0; i < 5; i++)
        plugin_guard_invoke("test", []() {}, &diag);
    EXPECT_EQ(diag.fault_count, 0u);
    EXPECT_EQ(diag.call_count, 5u);
    EXPECT_FALSE(diag.quarantined);
}
