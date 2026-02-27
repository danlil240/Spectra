#pragma once

// ─── Spectra Embedding API ──────────────────────────────────────────────────
//
// Render Spectra plots into a host application's GUI (Qt, GTK, game engines)
// without Spectra creating any OS windows.
//
// Two rendering modes:
//   1. CPU readback  — render_to_buffer() writes RGBA pixels to caller's buffer
//   2. Vulkan interop — render_to_image() renders directly into host's VkImage
//
// Usage (CPU readback — works with QImage, GdkPixbuf, any CPU-composited UI):
//
//   spectra::EmbedSurface surface({800, 600});
//   auto& fig = surface.figure();
//   auto& ax  = fig.subplot(1, 1, 1);
//   ax.line(x, y);
//
//   std::vector<uint8_t> pixels(800 * 600 * 4);
//   surface.render_to_buffer(pixels.data());
//   // ... blit pixels into your UI widget ...
//
// Usage (Vulkan interop — zero-copy for QVulkanWindow, game engines):
//
//   spectra::EmbedSurface surface({800, 600, 1, true});
//   auto& fig = surface.figure();
//   ...
//   spectra::VulkanInteropInfo interop;
//   interop.target_image      = my_vk_image;
//   interop.format            = VK_FORMAT_R8G8B8A8_UNORM;
//   interop.ready_semaphore   = my_ready_sem;
//   interop.finished_semaphore = my_done_sem;
//   surface.render_to_image(interop);
//
// Input forwarding (host translates its events → Spectra input):
//
//   surface.inject_mouse_move(x, y);
//   surface.inject_mouse_button(0, true, 0);  // left press
//   surface.inject_scroll(0, 1.0);
//
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <functional>
#include <memory>
#include <spectra/figure.hpp>
#include <spectra/fwd.hpp>

namespace spectra
{

// ─── Configuration ──────────────────────────────────────────────────────────

struct EmbedConfig
{
    uint32_t width  = 800;
    uint32_t height = 600;
    uint32_t msaa   = 1;   // 1 = no MSAA, 4 = 4x MSAA

    // When true, render_to_image() is available for zero-copy Vulkan interop.
    // When false (default), only render_to_buffer() is available.
    bool enable_vulkan_interop = false;

    // DPI scale factor (1.0 = 96 DPI, 2.0 = Retina/HiDPI).
    // Affects text size and tick length.
    float dpi_scale = 1.0f;

    // Background color alpha. Set to 0.0f for transparent background
    // (useful for compositing over host content).
    float background_alpha = 1.0f;
};

// Vulkan interop target: host provides these so Spectra renders directly
// into the host's VkImage.  Both semaphores are optional (VK_NULL_HANDLE
// to skip synchronization when the host manages barriers manually).
struct VulkanInteropInfo
{
    uint64_t target_image       = 0;   // VkImage (as uint64_t to avoid vulkan.h in public header)
    uint32_t format             = 37;  // VK_FORMAT_R8G8B8A8_UNORM
    uint64_t ready_semaphore    = 0;   // Host signals when image is available for writing
    uint64_t finished_semaphore = 0;   // Spectra signals when render is complete
    uint32_t width              = 0;   // Target image width  (0 = use surface width)
    uint32_t height             = 0;   // Target image height (0 = use surface height)
};

// ─── Callbacks ──────────────────────────────────────────────────────────────

// Cursor shape that the host should display.
enum class CursorShape
{
    Arrow,
    Crosshair,
    Hand,
    ResizeH,
    ResizeV,
    ResizeAll,
};

using RedrawCallback       = std::function<void()>;
using CursorChangeCallback = std::function<void(CursorShape)>;
using TooltipCallback      = std::function<void(const std::string& text, float x, float y)>;

// ─── Mouse button constants (match GLFW) ────────────────────────────────────

namespace embed
{
inline constexpr int MOUSE_BUTTON_LEFT   = 0;
inline constexpr int MOUSE_BUTTON_RIGHT  = 1;
inline constexpr int MOUSE_BUTTON_MIDDLE = 2;

inline constexpr int MOD_SHIFT   = 0x0001;
inline constexpr int MOD_CONTROL = 0x0002;
inline constexpr int MOD_ALT     = 0x0004;
inline constexpr int MOD_SUPER   = 0x0008;

// Key constants (match GLFW key codes)
inline constexpr int KEY_ESCAPE    = 256;
inline constexpr int KEY_ENTER     = 257;
inline constexpr int KEY_TAB       = 258;
inline constexpr int KEY_BACKSPACE = 259;
inline constexpr int KEY_DELETE    = 261;
inline constexpr int KEY_RIGHT     = 262;
inline constexpr int KEY_LEFT      = 263;
inline constexpr int KEY_DOWN      = 264;
inline constexpr int KEY_UP        = 265;
inline constexpr int KEY_HOME      = 268;
inline constexpr int KEY_END       = 269;

// Letter keys (A-Z = 65-90)
inline constexpr int KEY_A = 65;
inline constexpr int KEY_C = 67;
inline constexpr int KEY_G = 71;
inline constexpr int KEY_Q = 81;
inline constexpr int KEY_R = 82;
inline constexpr int KEY_S = 83;
inline constexpr int KEY_Z = 90;

// Number keys (0-9 = 48-57)
inline constexpr int KEY_0 = 48;
inline constexpr int KEY_9 = 57;

// Space
inline constexpr int KEY_SPACE = 32;

// Action constants
inline constexpr int ACTION_RELEASE = 0;
inline constexpr int ACTION_PRESS   = 1;
inline constexpr int ACTION_REPEAT  = 2;
}   // namespace embed

// ─── EmbedSurface ───────────────────────────────────────────────────────────

class EmbedSurface
{
   public:
    explicit EmbedSurface(const EmbedConfig& config = {});
    ~EmbedSurface();

    // Non-copyable
    EmbedSurface(const EmbedSurface&)            = delete;
    EmbedSurface& operator=(const EmbedSurface&) = delete;

    // Move-constructible
    EmbedSurface(EmbedSurface&&) noexcept;
    EmbedSurface& operator=(EmbedSurface&&) noexcept;

    // Returns true if the surface initialized successfully.
    bool is_valid() const;

    // ── Figure management ───────────────────────────────────────────────

    // Create a new figure on this surface.
    Figure& figure(const FigureConfig& cfg = {});

    // Get the currently active figure (nullptr if none).
    Figure* active_figure();
    const Figure* active_figure() const;

    // Set the active figure by pointer.
    void set_active_figure(Figure* fig);

    // Access the figure registry.
    FigureRegistry& figure_registry();

    // ── Rendering ───────────────────────────────────────────────────────

    // Resize the offscreen framebuffer. Call when the host widget changes size.
    // Returns false on failure (zero dimensions, Vulkan error).
    bool resize(uint32_t width, uint32_t height);

    // CPU readback mode: render one frame and write RGBA pixels into the
    // caller's buffer.  Buffer must be at least width() * height() * 4 bytes.
    // Returns false on render or readback failure.
    bool render_to_buffer(uint8_t* out_rgba);

    // Vulkan interop mode: render directly into host-provided VkImage.
    // Only available when EmbedConfig::enable_vulkan_interop is true.
    // Returns false on failure or if interop is not enabled.
    bool render_to_image(const VulkanInteropInfo& target);

    // ── Input forwarding ────────────────────────────────────────────────
    // Host application translates its native events and calls these.
    // Coordinates are in pixel space relative to the surface's top-left.
    // Button/key/mod constants are in the spectra::embed namespace above.

    void inject_mouse_move(float x, float y);
    void inject_mouse_button(int button, int action, int mods, float x, float y);
    void inject_scroll(float dx, float dy, float cursor_x, float cursor_y);
    void inject_key(int key, int action, int mods);
    void inject_char(unsigned int codepoint);

    // Advance internal animations by dt seconds.
    // Call once per host frame to keep pan inertia, zoom animations, etc. alive.
    void update(float dt);

    // ── Properties ──────────────────────────────────────────────────────

    uint32_t width() const;
    uint32_t height() const;
    float    dpi_scale() const;
    void     set_dpi_scale(float scale);
    float    background_alpha() const;
    void     set_background_alpha(float alpha);

    // ── Callbacks ───────────────────────────────────────────────────────

    // Called when Spectra's internal state changes and a repaint is needed.
    // Use this for on-demand rendering instead of constant polling.
    void set_redraw_callback(RedrawCallback cb);

    // Called when the cursor shape should change (e.g. crosshair during hover).
    void set_cursor_change_callback(CursorChangeCallback cb);

    // Called when a tooltip should be shown/hidden.
    void set_tooltip_callback(TooltipCallback cb);

    // ── Advanced: Vulkan device sharing ─────────────────────────────────
    // For advanced integrations where the host already has a Vulkan device.

    // Access Spectra's Vulkan internals (for device sharing, interop setup).
    Backend*  backend();
    Renderer* renderer();

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}   // namespace spectra
