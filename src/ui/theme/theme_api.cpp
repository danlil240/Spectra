#include <atomic>
#include <mutex>
#include <spectra/theme_api.hpp>

#include "ui/theme/theme.hpp"

namespace spectra
{

namespace
{
// The public registration API carries no handle, so the registration is
// process-wide.  s_callback_mutex guards s_theme_callback and is held for the
// whole dispatch (load + call), so register_theme_change_callback() from another
// thread waits for an in-flight call to return; once it returns, no call to the
// old function can start.  Recursive so a callback may unregister/re-register or
// switch theme (nested dispatch) on its own thread without deadlocking.
std::recursive_mutex s_callback_mutex;
ThemeChangeCallback  s_theme_callback = nullptr;

std::atomic<void (*)()> s_dispatch_hook_for_testing{nullptr};

void fill_rgba(float out[4], const ui::Color& c)
{
    out[0] = c.r;
    out[1] = c.g;
    out[2] = c.b;
    out[3] = c.a;
}
}   // namespace

namespace detail
{
void invoke_theme_change_callback(const ThemeSnapshot& snap)
{
    std::lock_guard<std::recursive_mutex> lock(s_callback_mutex);
    ThemeChangeCallback                   cb = s_theme_callback;
    if (auto hook = s_dispatch_hook_for_testing.load(std::memory_order_acquire))
        hook();
    if (cb)
        cb(snap);
}

void set_theme_callback_dispatch_hook_for_testing(void (*hook)())
{
    s_dispatch_hook_for_testing.store(hook, std::memory_order_release);
}
}   // namespace detail

namespace ui
{
void ThemeManager::fill_snapshot(ThemeSnapshot& out) const
{
    const auto& c = colors();
    out.version   = theme_version();

    fill_rgba(out.bg_canvas, c.bg_canvas);
    fill_rgba(out.bg_primary, c.bg_primary);
    fill_rgba(out.bg_secondary, c.bg_secondary);
    fill_rgba(out.bg_elevated, c.bg_elevated);

    fill_rgba(out.text_primary, c.text_primary);
    fill_rgba(out.text_secondary, c.text_secondary);

    fill_rgba(out.border_default, c.border_default);
    fill_rgba(out.border_subtle, c.border_subtle);

    fill_rgba(out.accent, c.accent);
    fill_rgba(out.accent_hover, c.accent_hover);

    fill_rgba(out.success, c.success);
    fill_rgba(out.warning, c.warning);
    fill_rgba(out.error, c.error);

    fill_rgba(out.grid_major, c.grid_major);
    fill_rgba(out.grid_minor, c.grid_minor);
    fill_rgba(out.axis_line, c.axis_line);
    fill_rgba(out.crosshair, c.crosshair);
}
}   // namespace ui

uint32_t get_theme_snapshot(ThemeSnapshot& out)
{
    ui::ThemeManager::instance().fill_snapshot(out);
    return out.version;
}

void register_theme_change_callback(ThemeChangeCallback callback)
{
    std::lock_guard<std::recursive_mutex> lock(s_callback_mutex);
    s_theme_callback = callback;
}

}   // namespace spectra
