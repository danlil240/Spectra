#pragma once

#include <cstdlib>
#include <string>
#include <vector>

namespace spectra
{

// Portable wrappers for setenv/unsetenv (POSIX-only; MSVC uses _putenv_s).
namespace detail
{
#ifdef _WIN32
inline void set_env(const char* name, const char* value)
{
    _putenv_s(name, value);
}
inline void unset_env(const char* name)
{
    _putenv_s(name, "");
}
#else
inline void set_env(const char* name, const char* value)
{
    setenv(name, value, 1);
}
inline void unset_env(const char* name)
{
    unsetenv(name);
}
#endif
}   // namespace detail

/// RAII guard that temporarily restores the pre-snap environment before spawning
/// a native file dialog.  Snap-packaged VS Code overrides GTK_PATH, GIO_MODULE_DIR,
/// and other variables to point into the snap tree, which causes spawned GTK
/// helpers (zenity, kdialog) to load an incompatible libpthread from snap core20
/// and crash.  VS Code stores original values with a _VSCODE_SNAP_ORIG suffix;
/// this guard restores those originals and reverts on destruction.
struct DialogEnvGuard
{
    // Variables that must be unset before spawning native GTK dialog helpers
    // (zenity, kdialog, etc.).
    //
    // GTK module-path vars: when snap-packaged zenity loads a host-system GIO or
    // GTK module it gets an ABI mismatch (snap core20 vs Ubuntu 22.04+ glibc).
    //
    // LD_LIBRARY_PATH: if the caller's shell has a ROS or other workspace sourced,
    // LD_LIBRARY_PATH will contain host library paths.  Snap zenity uses its own
    // ld interpreter and may pick up the host libc.so.6 (glibc 2.35) through those
    // paths instead of snap core20's libc.so.6 (glibc 2.31).  snap core20's
    // libpthread.so.0 then can't resolve __libc_pthread_init from GLIBC_PRIVATE
    // and crashes immediately.  Unsetting LD_LIBRARY_PATH for the dialog spawn is
    // safe: spawned dialog tools don't need workspace-specific libraries, and the
    // variable is restored after the dialog returns.
    static constexpr const char* snap_vars[] = {
        "GIO_MODULE_DIR",
        "GSETTINGS_SCHEMA_DIR",
        "GTK_EXE_PREFIX",
        "GTK_IM_MODULE_FILE",
        "GTK_PATH",
        "LD_LIBRARY_PATH",
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
            const char* cur = std::getenv(var);

            // Save and unconditionally unset.  These variables control where GTK
            // loads its modules (GIO modules, input-method modules, etc.).  When
            // zenity or kdialog is snap-packaged it runs against snap core20's
            // runtime, and any host-system GTK/GIO module it loads will reference
            // glibc symbols that don't exist in core20's libpthread, producing a
            // symbol-lookup error and an instant crash.  Unsetting the module-path
            // variables makes snap-zenity fall back to its own bundled modules,
            // which are ABI-compatible with core20.
            //
            // Additionally honour the _VSCODE_SNAP_ORIG convention: if VS Code
            // snap saved a non-empty original value, prefer restoring to that.
            std::string orig_key = std::string(var) + "_VSCODE_SNAP_ORIG";
            const char* orig_val = std::getenv(orig_key.c_str());

            SavedVar sv;
            sv.name      = var;
            sv.value     = cur ? cur : "";
            sv.had_value = (cur != nullptr);
            // Override the restore-to value if the snap marker has a non-empty original.
            if (orig_val && orig_val[0] != '\0')
            {
                sv.value     = orig_val;
                sv.had_value = true;
            }
            saved_.push_back(std::move(sv));

            detail::unset_env(var);
        }
    }

    ~DialogEnvGuard()
    {
        for (auto& s : saved_)
        {
            if (s.had_value)
                detail::set_env(s.name.c_str(), s.value.c_str());
            else
                detail::unset_env(s.name.c_str());
        }
    }

    DialogEnvGuard(const DialogEnvGuard&)            = delete;
    DialogEnvGuard& operator=(const DialogEnvGuard&) = delete;
};

}   // namespace spectra
