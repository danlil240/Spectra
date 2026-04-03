#pragma once

#include <type_traits>

namespace spectra
{

/// Result of a guarded plugin callback invocation.
enum class PluginCallResult
{
    Success,     ///< Callback completed normally.
    Exception,   ///< C++ exception was caught.
    Signal,      ///< Fatal signal was caught (SIGSEGV, SIGBUS, SIGFPE).
};

/// Invoke a plugin callback with crash isolation.
///
/// Catches C++ exceptions (std::exception and unknown) and fatal signals
/// (SIGSEGV, SIGBUS, SIGFPE on POSIX).
///
/// @param context_name  Identifier for log messages (e.g. overlay or series type name).
/// @param fn            The plugin callback to invoke (C function pointer + void* arg).
/// @param arg           Opaque argument forwarded to @p fn.
/// @return              Success, or the type of failure that occurred.
///
/// Thread-safe: uses thread-local signal context.
///
/// WARNING: When a signal is caught, C++ destructors for stack objects between
/// the guard point and the faulting instruction are NOT executed.  Only use for
/// C ABI / plugin callbacks where this is acceptable.
PluginCallResult plugin_guard_invoke(const char* context_name, void (*fn)(void*), void* arg);

/// Convenience overload for lambdas and functors.
template <typename Fn>
PluginCallResult plugin_guard_invoke(const char* context_name, Fn&& fn)
{
    using FnType = std::decay_t<Fn>;
    FnType fn_storage(static_cast<Fn&&>(fn));
    return plugin_guard_invoke(
        context_name,
        [](void* ptr) { (*static_cast<FnType*>(ptr))(); },
        &fn_storage);
}

}   // namespace spectra
