#include "clipboard_image.hpp"

#include <spectra/logger.hpp>

#include <cstdio>
#include <cstdlib>

namespace spectra::platform
{

bool copy_image_to_clipboard(const uint8_t* png_bytes, size_t size)
{
#if defined(__linux__) || defined(__unix__)
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    const char* x11     = std::getenv("DISPLAY");

    FILE* pipe = nullptr;
    if (wayland && wayland[0] != '\0')
    {
        pipe = ::popen("wl-copy --type image/png", "w");
    }
    else if (x11 && x11[0] != '\0')
    {
        pipe = ::popen("xclip -selection clipboard -t image/png", "w");
    }
    else
    {
        SPECTRA_LOG_WARN("clipboard", "copy_image_to_clipboard: no WAYLAND_DISPLAY or DISPLAY set");
        return false;
    }

    if (!pipe)
    {
        SPECTRA_LOG_WARN("clipboard", "copy_image_to_clipboard: failed to open pipe");
        return false;
    }

    const size_t written   = std::fwrite(png_bytes, 1, size, pipe);
    const int    exit_code = ::pclose(pipe);

    if (written != size)
    {
        SPECTRA_LOG_WARN("clipboard", "copy_image_to_clipboard: incomplete write");
        return false;
    }

    return exit_code == 0;
#else
    (void)png_bytes;
    (void)size;
    SPECTRA_LOG_WARN("clipboard", "copy_image_to_clipboard: not supported on this platform");
    return false;
#endif
}

}   // namespace spectra::platform
