#pragma once

// Spectra's per-user configuration directory.
//
// Every per-user file Spectra reads or writes (settings.json, keybindings.json,
// workspace.spectra, autosave.spectra, default_theme.json, plugins/, PX4/ROS2
// recent files and sessions) lives in this one directory.  Resolution order:
//
//   1. $SPECTRA_CONFIG_DIR, if set and non-empty — used as the Spectra
//      directory itself (no "spectra" suffix).  Full isolation override for
//      tests, QA runs and side-by-side installs.
//   2. $XDG_CONFIG_HOME/spectra, if XDG_CONFIG_HOME is set and absolute
//      (non-Windows only, per the XDG Base Directory spec).
//   3. Platform default: $HOME/.config/spectra, falling back to
//      $USERPROFILE/.config/spectra (the historical location on every
//      platform, so existing files keep being found).
//
// When none of these can be determined the result is empty and callers keep
// their own relative fallback.  Log files are separate: they follow
// XDG_STATE_HOME (see logger.cpp), not this directory.

#include <filesystem>

namespace spectra
{

// Raw environment values the resolution reads.  nullptr == unset.
struct ConfigDirEnv
{
    const char* spectra_config_dir = nullptr;
    const char* xdg_config_home    = nullptr;
    const char* home               = nullptr;
    const char* userprofile        = nullptr;
};

// Pure resolution from explicit values (unit-testable, no environment access).
// `windows` selects the Windows rules (XDG_CONFIG_HOME ignored).
std::filesystem::path resolve_config_dir(const ConfigDirEnv& env, bool windows);

// Reads the process environment and resolves for the current platform.
std::filesystem::path user_config_dir();

// True when SPECTRA_CONFIG_DIR or (non-Windows) an absolute XDG_CONFIG_HOME
// redirects the config directory away from the platform default.
bool user_config_dir_overridden();

}   // namespace spectra
