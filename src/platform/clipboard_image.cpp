#include "clipboard_image.hpp"

#include <spectra/logger.hpp>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

#ifndef _WIN32
    #include <csignal>
    #include <sys/wait.h>
    #include <unistd.h>
#endif

namespace spectra::platform
{

namespace
{

#ifndef _WIN32
// Ignore SIGPIPE while writing to a clipboard helper pipe.  When xclip/wl-copy is
// missing the shell child exits immediately, the pipe breaks, and fwrite() would
// otherwise terminate the process (fuzz repro: file.copy_to_clipboard SIGSEGV).
struct SigpipeGuard
{
    void (*prev_)(int) = ::signal(SIGPIPE, SIG_IGN);
    ~SigpipeGuard() { ::signal(SIGPIPE, prev_); }
};
#endif

}   // namespace

bool copy_image_to_clipboard(const uint8_t* png_bytes, size_t size)
{
#if defined(__linux__) || defined(__unix__)
    if (png_bytes == nullptr || size == 0)
    {
        SPECTRA_LOG_WARN("clipboard", "copy_image_to_clipboard: empty PNG payload");
        return false;
    }

    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    const char* x11     = std::getenv("DISPLAY");

    if ((!wayland || wayland[0] == '\0') && (!x11 || x11[0] == '\0'))
    {
        SPECTRA_LOG_WARN("clipboard", "copy_image_to_clipboard: no WAYLAND_DISPLAY or DISPLAY set");
        return false;
    }

    const bool  use_wl_copy = wayland && wayland[0] != '\0';
    const char* tool_name   = use_wl_copy ? "wl-copy" : "xclip";

    SigpipeGuard sigpipe_guard;

    int pipefd[2];
    if (::pipe(pipefd) != 0)
    {
        SPECTRA_LOG_WARN("clipboard", "copy_image_to_clipboard: pipe() failed");
        return false;
    }

    const pid_t pid = ::fork();
    if (pid < 0)
    {
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        SPECTRA_LOG_WARN("clipboard", "copy_image_to_clipboard: fork() failed");
        return false;
    }

    if (pid == 0)
    {
        ::close(pipefd[1]);
        if (::dup2(pipefd[0], STDIN_FILENO) < 0)
            ::_exit(127);
        ::close(pipefd[0]);

        // Clipboard helpers must not inherit app sockets (e.g. MCP on 8765).
        // FD_CLOEXEC does not apply across fork(), only exec().
        for (int fd = 3; fd < 256; ++fd)
            ::close(fd);

        if (use_wl_copy)
        {
            ::execlp("wl-copy", "wl-copy", "--type", "image/png", static_cast<char*>(nullptr));
        }
        else
        {
            ::execlp("xclip",
                     "xclip",
                     "-selection",
                     "clipboard",
                     "-t",
                     "image/png",
                     static_cast<char*>(nullptr));
        }
        ::_exit(127);
    }

    ::close(pipefd[0]);

    size_t      total_written = 0;
    const char* cursor        = reinterpret_cast<const char*>(png_bytes);
    while (total_written < size)
    {
        const ssize_t n =
            ::write(pipefd[1], cursor + total_written, size - total_written);
        if (n > 0)
        {
            total_written += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        break;
    }
    ::close(pipefd[1]);

    int   status    = 0;
    pid_t wait_pid  = ::waitpid(pid, &status, 0);
    const bool child_ok = wait_pid == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;

    if (total_written != size)
    {
        SPECTRA_LOG_WARN("clipboard",
                         "copy_image_to_clipboard: incomplete write to {}",
                         tool_name);
        return false;
    }

    if (!child_ok)
    {
        SPECTRA_LOG_WARN("clipboard",
                         "copy_image_to_clipboard: {} failed (is it installed?)",
                         tool_name);
        return false;
    }

    return true;
#elif defined(__APPLE__)
    // macOS: pbcopy only handles text, so stage the PNG in a private temp file
    // and let AppleScript place it on the pasteboard as PNG data.
    if (png_bytes == nullptr || size == 0)
    {
        SPECTRA_LOG_WARN("clipboard", "copy_image_to_clipboard: empty PNG payload");
        return false;
    }

    std::error_code ec;
    auto            tmp_dir = std::filesystem::temp_directory_path(ec);
    if (ec)
    {
        SPECTRA_LOG_WARN("clipboard", "copy_image_to_clipboard: no temp directory");
        return false;
    }
    std::string tmpl = (tmp_dir / "spectra-clip-XXXXXX").string();
    int         fd   = ::mkstemp(tmpl.data());
    if (fd < 0)
    {
        SPECTRA_LOG_WARN("clipboard", "copy_image_to_clipboard: mkstemp() failed");
        return false;
    }
    size_t      total_written = 0;
    const char* cursor        = reinterpret_cast<const char*>(png_bytes);
    while (total_written < size)
    {
        const ssize_t n = ::write(fd, cursor + total_written, size - total_written);
        if (n > 0)
            total_written += static_cast<size_t>(n);
        else if (!(n < 0 && errno == EINTR))
            break;
    }
    ::close(fd);
    if (total_written != size)
    {
        ::unlink(tmpl.c_str());
        SPECTRA_LOG_WARN("clipboard", "copy_image_to_clipboard: failed to write temp file");
        return false;
    }

    const std::string script =
        "set the clipboard to (read (POSIX file \"" + tmpl + "\") as \xC2\xABclass PNGf\xC2\xBB)";

    const pid_t pid = ::fork();
    if (pid == 0)
    {
        for (int cfd = 3; cfd < 256; ++cfd)
            ::close(cfd);
        ::execlp("osascript", "osascript", "-e", script.c_str(), static_cast<char*>(nullptr));
        ::_exit(127);
    }
    bool ok = false;
    if (pid > 0)
    {
        int status = 0;
        ok = ::waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    ::unlink(tmpl.c_str());
    if (!ok)
        SPECTRA_LOG_WARN("clipboard", "copy_image_to_clipboard: osascript failed");
    return ok;
#else
    (void)png_bytes;
    (void)size;
    SPECTRA_LOG_WARN("clipboard", "copy_image_to_clipboard: not supported on this platform");
    return false;
#endif
}

}   // namespace spectra::platform
