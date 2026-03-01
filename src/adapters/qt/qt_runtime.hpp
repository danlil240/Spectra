#pragma once

#include <vulkan/vulkan.h>

#include <chrono>
#include <cstdint>
#include <memory>

#include <QtGui/QVulkanInstance>

class QWindow;

namespace spectra
{
class VulkanBackend;
class Renderer;
class Figure;
struct WindowContext;
}   // namespace spectra

namespace spectra::adapters::qt
{

class QtSurfaceHost;

// Bootstrap runtime that owns Spectra's Vulkan backend + renderer and
// drives a frame loop for one or more QWindow canvases.
//
// Typical usage:
//   QtRuntime rt;
//   rt.init();                     // creates VkInstance, VkDevice, Renderer
//   rt.attach_window(qwindow, w, h);  // creates surface + swapchain
//   // in requestUpdate / timer:
//   rt.begin_frame();
//   rt.render_figure(fig);
//   rt.end_frame();
class QtRuntime
{
   public:
    QtRuntime();
    ~QtRuntime();

    // Non-copyable
    QtRuntime(const QtRuntime&)            = delete;
    QtRuntime& operator=(const QtRuntime&) = delete;

    // Initialize Vulkan instance, device, and renderer.
    // Must be called before attach_window().
    bool init();

    // Shut down all Vulkan resources.  Safe to call multiple times.
    void shutdown();

    bool is_initialized() const { return initialized_; }

    // Adopt an existing VkInstance (e.g. from host app).
    // Must be called BEFORE init() if the host already created a VkInstance.
    // If not called, init() creates its own instance.
    bool adopt_vulkan_instance(VkInstance instance);

    // Bind a QWindow as a Vulkan canvas.
    // Creates VkSurfaceKHR + swapchain + command buffers + sync objects.
    // The QWindow must have surfaceType() == QSurface::VulkanSurface.
    // Returns true on success.
    bool attach_window(QWindow* window, uint32_t width, uint32_t height);

    // Recreate swapchain after resize.
    bool resize(uint32_t width, uint32_t height);

    // Mark swapchain as needing recreation (deferred to next begin_frame).
    // Use this from resize/DPR-change events instead of calling resize() directly.
    void mark_swapchain_dirty();

    // Frame lifecycle — call from Qt's update/timer callback.
    bool begin_frame();
    void render_figure(Figure& figure);
    void end_frame();

    // Accessors
    QVulkanInstance* vulkan_instance() const;
    VulkanBackend*   backend() const { return backend_.get(); }
    Renderer*        renderer() const { return renderer_.get(); }
    WindowContext*   window_context() const { return window_ctx_.get(); }

   private:
    bool initialized_ = false;

    std::unique_ptr<QtSurfaceHost>    surface_host_;
    std::unique_ptr<QVulkanInstance>   vulkan_instance_;
    std::unique_ptr<VulkanBackend>    backend_;
    std::unique_ptr<Renderer>         renderer_;
    std::unique_ptr<WindowContext>    window_ctx_;

    QWindow* attached_window_ = nullptr;

    // Debounce: track last resize request time to coalesce rapid resizes.
    static constexpr std::chrono::milliseconds k_resize_debounce{50};
    std::chrono::steady_clock::time_point      last_resize_request_{};
    bool                                       resize_pending_ = false;

    // Instrumentation counters (debug logging)
    uint32_t swapchain_recreate_count_ = 0;
    uint32_t frame_skip_count_         = 0;
};

}   // namespace spectra::adapters::qt
