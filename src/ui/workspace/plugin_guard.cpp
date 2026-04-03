#include "plugin_guard.hpp"

#include <exception>
#include <mutex>

#include "spectra/logger.hpp"

#ifndef _WIN32
    #include <csetjmp>
    #include <csignal>
#endif

namespace spectra
{

namespace
{

#ifndef _WIN32

// ─── Thread-local signal context ─────────────────────────────────────────────
// Each thread maintains its own jmp_buf so that guarded calls on the UI thread
// and the render thread are independent.

thread_local sigjmp_buf            t_jmp_buf;
thread_local volatile sig_atomic_t t_in_guard      = 0;
thread_local volatile int          t_caught_signal = 0;

// Previous signal handlers, saved once at first use.
struct OldHandlers
{
    struct sigaction segv
    {
    };
    struct sigaction bus
    {
    };
    struct sigaction fpe
    {
    };
};

OldHandlers    s_old_handlers;
std::once_flag s_init_flag;

const char* signal_name(int sig)
{
    switch (sig)
    {
        case SIGSEGV:
            return "SIGSEGV";
        case SIGBUS:
            return "SIGBUS";
        case SIGFPE:
            return "SIGFPE";
        default:
            return "unknown";
    }
}

void crash_signal_handler(int sig)
{
    if (t_in_guard)
    {
        t_caught_signal = sig;
        siglongjmp(t_jmp_buf, 1);
    }

    // Not in a guarded context — chain to previous handler.
    const struct sigaction* old = nullptr;
    switch (sig)
    {
        case SIGSEGV:
            old = &s_old_handlers.segv;
            break;
        case SIGBUS:
            old = &s_old_handlers.bus;
            break;
        case SIGFPE:
            old = &s_old_handlers.fpe;
            break;
    }
    if (old && old->sa_handler != SIG_DFL && old->sa_handler != SIG_IGN)
    {
        old->sa_handler(sig);
        return;
    }

    // No previous handler — re-raise with default disposition.
    signal(sig, SIG_DFL);
    raise(sig);
}

void install_signal_handlers()
{
    struct sigaction sa
    {
    };
    sa.sa_handler = crash_signal_handler;
    sa.sa_flags   = 0;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGSEGV, &sa, &s_old_handlers.segv);
    sigaction(SIGBUS, &sa, &s_old_handlers.bus);
    sigaction(SIGFPE, &sa, &s_old_handlers.fpe);
}

#endif   // _WIN32

}   // namespace

PluginCallResult plugin_guard_invoke(const char* context_name, void (*fn)(void*), void* arg)
{
    const char* name = context_name ? context_name : "<unknown>";

#ifndef _WIN32
    // Install signal handlers once (process-wide).  The thread-local
    // t_in_guard flag ensures only the calling thread will longjmp.
    std::call_once(s_init_flag, install_signal_handlers);

    t_in_guard      = 1;
    t_caught_signal = 0;

    if (sigsetjmp(t_jmp_buf, 1) != 0)
    {
        // Arrived here via siglongjmp from the signal handler.
        t_in_guard = 0;
        int sig    = t_caught_signal;
        SPECTRA_LOG_ERROR("plugin",
                          "Plugin callback '{}' caught fatal signal {} — disabling",
                          name,
                          signal_name(sig));
        return PluginCallResult::Signal;
    }

    // Normal path — call the plugin callback inside a try/catch.
    try
    {
        fn(arg);
        t_in_guard = 0;
        return PluginCallResult::Success;
    }
    catch (const std::exception& e)
    {
        t_in_guard = 0;
        SPECTRA_LOG_ERROR("plugin",
                          "Plugin callback '{}' threw exception: {} — disabling",
                          name,
                          e.what());
        return PluginCallResult::Exception;
    }
    catch (...)
    {
        t_in_guard = 0;
        SPECTRA_LOG_ERROR("plugin",
                          "Plugin callback '{}' threw unknown exception — disabling",
                          name);
        return PluginCallResult::Exception;
    }

#else
    // Windows: exception-only isolation (no SEH in this implementation).
    try
    {
        fn(arg);
        return PluginCallResult::Success;
    }
    catch (const std::exception& e)
    {
        SPECTRA_LOG_ERROR("plugin",
                          "Plugin callback '{}' threw exception: {} — disabling",
                          name,
                          e.what());
        return PluginCallResult::Exception;
    }
    catch (...)
    {
        SPECTRA_LOG_ERROR("plugin",
                          "Plugin callback '{}' threw unknown exception — disabling",
                          name);
        return PluginCallResult::Exception;
    }
#endif
}

}   // namespace spectra
