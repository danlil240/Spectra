#pragma once

#include <cstdint>
#include <string>
#include <type_traits>

namespace spectra
{

/// Lifecycle diagnostics for a loaded plugin.
struct PluginDiagnostics
{
    uint64_t    init_time_us    = 0;   ///< Time spent in spectra_plugin_init (microseconds).
    size_t      call_count      = 0;   ///< Total guarded callback invocations.
    size_t      fault_count     = 0;   ///< Number of caught exceptions / signals.
    uint64_t    last_fault_time = 0;   ///< Monotonic timestamp (ns) of last fault.
    std::string last_fault_reason;     ///< Human-readable fault description.
    bool        quarantined = false;   ///< Auto-disabled after repeated faults.
};

/// Number of faults before a plugin is automatically quarantined.
static constexpr int PLUGIN_QUARANTINE_THRESHOLD = 3;

/// Result of a guarded plugin callback invocation.
enum class PluginCallResult
{
    Success,       ///< Callback completed normally.
    Exception,     ///< C++ exception was caught.
    Signal,        ///< Fatal signal was caught (SIGSEGV, SIGBUS, SIGFPE).
    Quarantined,   ///< Plugin is quarantined — call was skipped.
};

/// Invoke a plugin callback with crash isolation, recording diagnostics.
///
/// Catches C++ exceptions (std::exception and unknown) and fatal signals
/// (SIGSEGV, SIGBUS, SIGFPE on POSIX).
///
/// @param context_name  Identifier for log messages (e.g. overlay or series type name).
/// @param fn            The plugin callback to invoke (C function pointer + void* arg).
/// @param arg           Opaque argument forwarded to @p fn.
/// @param diag          Optional diagnostics struct to update. May be nullptr.
/// @return              Success, or the type of failure that occurred.
///
/// Thread-safe: uses thread-local signal context.
///
/// WARNING: When a signal is caught, C++ destructors for stack objects between
/// the guard point and the faulting instruction are NOT executed.  Only use for
/// C ABI / plugin callbacks where this is acceptable.
PluginCallResult plugin_guard_invoke(const char* context_name,
                                     void (*fn)(void*),
                                     void*              arg,
                                     PluginDiagnostics* diag = nullptr);

/// Convenience overload for lambdas and functors.
template <typename Fn>
PluginCallResult plugin_guard_invoke(const char*        context_name,
                                     Fn&&               fn,
                                     PluginDiagnostics* diag = nullptr)
{
    using FnType = std::decay_t<Fn>;
    FnType fn_storage(static_cast<Fn&&>(fn));
    return plugin_guard_invoke(
        context_name,
        [](void* ptr) { (*static_cast<FnType*>(ptr))(); },
        &fn_storage,
        diag);
}

}   // namespace spectra
