#pragma once
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <spectra/logger.hpp>

#ifndef _WIN32
    #include <fcntl.h>
    #include <sys/wait.h>
    #include <unistd.h>
#else
// Windows fallback: tinyfiledialogs
extern "C" char* tinyfd_saveFileDialog(char const*        aTitle,
                                       char const*        aDefaultPathAndFile,
                                       int                aNumOfFilterPatterns,
                                       char const* const* aFilterPatterns,
                                       char const*        aSingleFilterDescription);
#endif

namespace spectra
{

/// Opens a native save-file dialog.
/// Returns the chosen absolute path, or std::nullopt if the user cancelled.
/// Updates \p last_dir to the parent of the chosen path for next use.
///
/// On Linux: spawns zenity via fork/exec with a clean minimal environment
/// (only DISPLAY, HOME, PATH, GDK_BACKEND=x11).  This avoids the libpthread
/// symbol-lookup crash that snap-packaged zenity suffers when the caller's
/// process has a workspace-sourced LD_LIBRARY_PATH containing glibc 2.35+
/// libraries incompatible with snap core20 (glibc 2.31).
inline std::optional<std::string> ask_export_path(const char*        title,
                                                  const char*        default_name,
                                                  int                num_patterns,
                                                  const char* const* patterns,
                                                  const char*        description,
                                                  std::string&       last_dir)
{
    std::string base;
    if (last_dir.empty())
    {
        const char* home = std::getenv("HOME");
        base             = home ? std::string(home) + "/" : "/";
    }
    else
    {
        base = last_dir;
        if (!base.empty() && base.back() != '/')
            base += '/';
    }
    std::string default_path = base + default_name;

    SPECTRA_LOG_INFO("export", "Opening save dialog: title='{}' default='{}'", title, default_path);

#ifndef _WIN32
    // ── Save env vars needed in the child before any clearenv() ───────────
    auto getenv_str = [](const char* name) -> std::string
    {
        const char* v = std::getenv(name);
        return v ? v : "";
    };
    std::string env_display = getenv_str("DISPLAY");
    std::string env_wayland = getenv_str("WAYLAND_DISPLAY");
    std::string env_home    = getenv_str("HOME");
    std::string env_xdg_rt  = getenv_str("XDG_RUNTIME_DIR");
    std::string env_path    = getenv_str("PATH");
    if (env_path.empty())
        env_path = "/usr/bin:/usr/local/bin";

    // ── Build zenity argv ─────────────────────────────────────────────────
    std::vector<std::string> args;
    args.emplace_back("zenity");
    args.emplace_back("--file-selection");
    args.emplace_back("--save");
    args.emplace_back("--confirm-overwrite");
    args.emplace_back(std::string("--title=") + title);
    args.emplace_back(std::string("--filename=") + default_path);
    if (num_patterns > 0 && patterns != nullptr)
    {
        std::string filter = "--file-filter=";
        if (description != nullptr && description[0] != '\0')
        {
            filter += description;
            filter += " | ";
        }
        for (int i = 0; i < num_patterns; ++i)
        {
            if (i > 0)
                filter += ' ';
            filter += patterns[i];
        }
        args.push_back(std::move(filter));
        args.emplace_back("--file-filter=All files | *");
    }

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (auto& s : args)
        argv.push_back(const_cast<char*>(s.c_str()));
    argv.push_back(nullptr);

    // ── Pipe for zenity stdout ────────────────────────────────────────────
    int pipefd[2];
    if (::pipe(pipefd) != 0)
        return std::nullopt;

    pid_t pid = ::fork();
    if (pid < 0)
    {
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        return std::nullopt;
    }

    if (pid == 0)
    {
        // ── Child process ─────────────────────────────────────────────────
        ::close(pipefd[0]);
        ::dup2(pipefd[1], STDOUT_FILENO);
        ::close(pipefd[1]);

        // Suppress zenity warnings on stderr.
        int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0)
        {
            ::dup2(devnull, STDERR_FILENO);
            ::close(devnull);
        }

        // Clear ALL environment variables so no workspace LD_LIBRARY_PATH,
        // snap overrides, or ROS vars reach zenity's snap runtime and cause
        // a libpthread GLIBC_PRIVATE symbol-lookup crash.
        ::clearenv();

        // Restore only what zenity / GTK need.
        ::setenv("GDK_BACKEND", "x11", 1);   // force X11; avoids Wayland --attach mismatch
        if (!env_display.empty())
            ::setenv("DISPLAY", env_display.c_str(), 1);
        if (!env_wayland.empty())
            ::setenv("WAYLAND_DISPLAY", env_wayland.c_str(), 1);
        if (!env_home.empty())
            ::setenv("HOME", env_home.c_str(), 1);
        if (!env_xdg_rt.empty())
            ::setenv("XDG_RUNTIME_DIR", env_xdg_rt.c_str(), 1);
        ::setenv("PATH", env_path.c_str(), 1);

        ::execvp("zenity", argv.data());
        ::_exit(127);   // zenity not found
    }

    // ── Parent: read selected path from zenity stdout ─────────────────────
    ::close(pipefd[1]);
    char    buf[4096] = {};
    ssize_t n         = ::read(pipefd[0], buf, sizeof(buf) - 1);
    ::close(pipefd[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);

    if (n <= 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        SPECTRA_LOG_INFO("export",
                         "Save dialog cancelled or failed (exit {})",
                         WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        return std::nullopt;
    }

    std::string result(buf, static_cast<size_t>(n));
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
        result.pop_back();

    if (result.empty())
        return std::nullopt;

    SPECTRA_LOG_INFO("export", "Save dialog returned: '{}'", result);
    std::filesystem::path p(result);
    last_dir = p.parent_path().string();
    return result;

#else
    // ── Windows fallback: tinyfiledialogs ─────────────────────────────────
    const char* result =
        tinyfd_saveFileDialog(title, default_path.c_str(), num_patterns, patterns, description);
    if (!result)
    {
        SPECTRA_LOG_INFO("export", "Save dialog cancelled or failed");
        return std::nullopt;
    }
    SPECTRA_LOG_INFO("export", "Save dialog returned: '{}'", result);
    std::filesystem::path p(result);
    last_dir = p.parent_path().string();
    return std::string(result);
#endif
}

}   // namespace spectra
