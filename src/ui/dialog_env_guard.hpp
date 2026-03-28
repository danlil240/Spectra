#pragma once

#include <cstdlib>
#include <string>
#include <vector>

namespace spectra
{

/// RAII guard that temporarily restores the pre-snap environment before spawning
/// a native file dialog.  Snap-packaged VS Code overrides GTK_PATH, GIO_MODULE_DIR,
/// and other variables to point into the snap tree, which causes spawned GTK
/// helpers (zenity, kdialog) to load an incompatible libpthread from snap core20
/// and crash.  VS Code stores original values with a _VSCODE_SNAP_ORIG suffix;
/// this guard restores those originals and reverts on destruction.
struct DialogEnvGuard
{
    // Variables that snap VS Code overrides and that cause spawned GTK helpers
    // to load incompatible snap libraries.  GDK_BACKEND is intentionally excluded —
    // it controls display routing, not library loading, and unsetting it breaks
    // zenity's ability to connect to the X server.
    static constexpr const char* snap_vars[] = {
        "GIO_MODULE_DIR",
        "GSETTINGS_SCHEMA_DIR",
        "GTK_EXE_PREFIX",
        "GTK_IM_MODULE_FILE",
        "GTK_PATH",
        "LOCPATH",
    };

    struct SavedVar
    {
        std::string name;
        std::string value;
        bool        had_value;
    };

    std::vector<SavedVar> saved_;

    DialogEnvGuard()
    {
        for (const char* var : snap_vars)
        {
            std::string orig_key = std::string(var) + "_VSCODE_SNAP_ORIG";
            const char* orig_val = std::getenv(orig_key.c_str());

            // Only act if the _VSCODE_SNAP_ORIG key exists (i.e. we're in snap VS Code).
            if (!orig_val)
                continue;

            // Save current value for restoration.
            const char* cur = std::getenv(var);
            saved_.push_back({var, cur ? cur : "", cur != nullptr});

            // Restore the original.  Empty original means the variable was unset
            // before snap modified it.
            if (orig_val[0] != '\0')
                setenv(var, orig_val, 1);
            else
                unsetenv(var);
        }
    }

    ~DialogEnvGuard()
    {
        for (auto& s : saved_)
        {
            if (s.had_value)
                setenv(s.name.c_str(), s.value.c_str(), 1);
            else
                unsetenv(s.name.c_str());
        }
    }

    DialogEnvGuard(const DialogEnvGuard&)            = delete;
    DialogEnvGuard& operator=(const DialogEnvGuard&) = delete;
};

}   // namespace spectra
