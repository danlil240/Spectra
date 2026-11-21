#include "ui/native_dialog_policy.hpp"

#include <spectra/logger.hpp>

#include <atomic>
#include <cstring>

namespace spectra
{

namespace
{

bool env_truthy(const char* name)
{
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0')
        return false;
    return value[0] != '0';
}

// Seeded from the environment on first use, so every program linking Spectra
// (examples, embedders, the QA agent), not only those that call
// init_native_dialog_policy(), honours SPECTRA_NO_NATIVE_DIALOGS.
std::atomic<bool>& enabled_flag()
{
    static std::atomic<bool> flag{!native_dialogs_disabled_by_env()};
    return flag;
}

}   // namespace

bool native_dialogs_disabled_by_env()
{
    return env_truthy("SPECTRA_NO_NATIVE_DIALOGS") || env_truthy("SPECTRA_AUTOMATION");
}

bool native_dialogs_enabled()
{
    return enabled_flag().load(std::memory_order_relaxed);
}

void set_native_dialogs_enabled(bool enabled)
{
    enabled_flag().store(enabled, std::memory_order_relaxed);
}

void init_native_dialog_policy(int& argc, char** argv)
{
    if (native_dialogs_disabled_by_env())
        set_native_dialogs_enabled(false);

    int write_idx = 1;
    for (int read_idx = 1; read_idx < argc; ++read_idx)
    {
        if (std::strcmp(argv[read_idx], "--no-native-dialogs") == 0)
        {
            set_native_dialogs_enabled(false);
            continue;
        }
        argv[write_idx++] = argv[read_idx];
    }
    argc            = write_idx;
    argv[write_idx] = nullptr;

    if (!native_dialogs_enabled())
    {
        SPECTRA_LOG_INFO("app",
                         "Native file dialogs disabled (automation mode — "
                         "SPECTRA_NO_NATIVE_DIALOGS / SPECTRA_AUTOMATION / --no-native-dialogs)");
    }
}

}   // namespace spectra
