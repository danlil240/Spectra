#include "core/config_paths.hpp"

#include <cstdlib>

namespace spectra
{

namespace
{

bool non_empty(const char* s)
{
    return s != nullptr && s[0] != '\0';
}

bool xdg_usable(const char* xdg, bool windows)
{
    // XDG Base Directory spec: relative paths are invalid and must be ignored.
    return !windows && non_empty(xdg) && xdg[0] == '/';
}

ConfigDirEnv read_env()
{
    ConfigDirEnv env;
    env.spectra_config_dir = std::getenv("SPECTRA_CONFIG_DIR");
    env.xdg_config_home    = std::getenv("XDG_CONFIG_HOME");
    env.home               = std::getenv("HOME");
    env.userprofile        = std::getenv("USERPROFILE");
    return env;
}

constexpr bool k_windows =
#ifdef _WIN32
    true;
#else
    false;
#endif

}   // namespace

std::filesystem::path resolve_config_dir(const ConfigDirEnv& env, bool windows)
{
    if (non_empty(env.spectra_config_dir))
        return std::filesystem::path(env.spectra_config_dir);
    if (xdg_usable(env.xdg_config_home, windows))
        return std::filesystem::path(env.xdg_config_home) / "spectra";
    if (non_empty(env.home))
        return std::filesystem::path(env.home) / ".config" / "spectra";
    if (non_empty(env.userprofile))
        return std::filesystem::path(env.userprofile) / ".config" / "spectra";
    return {};
}

std::filesystem::path user_config_dir()
{
    return resolve_config_dir(read_env(), k_windows);
}

bool user_config_dir_overridden()
{
    const ConfigDirEnv env = read_env();
    return non_empty(env.spectra_config_dir) || xdg_usable(env.xdg_config_home, k_windows);
}

}   // namespace spectra
