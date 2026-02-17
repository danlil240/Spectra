#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <spectra/fwd.hpp>

// Forward declarations
struct GLFWwindow;

namespace spectra
{

struct WindowContext;
class VulkanBackend;
class Renderer;

// Manages multiple OS windows, each with its own GLFW window, Vulkan surface,
// swapchain, command buffers, and sync objects.  Shared Vulkan resources
// (VkInstance, VkDevice, pipelines, descriptor pool, series GPU buffers)
// remain in VulkanBackend.
//
// Usage:
//   WindowManager wm;
//   wm.init(backend);
//   auto* primary = wm.adopt_primary_window(glfw_window, backend);
//   auto* secondary = wm.create_window(800, 600, "Window 2");
//   ...
//   // In render loop:
//   wm.poll_events();
//   wm.process_pending_closes();
//   for (auto* wctx : wm.windows()) { ... }
//   ...
//   wm.shutdown();
class WindowManager
{
   public:
    WindowManager() = default;
    ~WindowManager();

    WindowManager(const WindowManager&) = delete;
    WindowManager& operator=(const WindowManager&) = delete;

    // Initialize the window manager with a reference to the Vulkan backend
    // and figure registry.  Must be called before any other method.
    void init(VulkanBackend* backend, FigureRegistry* registry = nullptr,
              Renderer* renderer = nullptr);

    // Adopt the primary window that was already created by GlfwAdapter.
    // This wraps the existing primary_window_ in VulkanBackend into a
    // managed WindowContext.  Returns the context pointer.
    WindowContext* adopt_primary_window(void* glfw_window);

    // Create a new OS window with its own swapchain and Vulkan resources.
    // Returns nullptr on failure.
    WindowContext* create_window(uint32_t width, uint32_t height, const std::string& title);

    // Mark a window for destruction.  Actual cleanup happens in
    // process_pending_closes() to avoid mutating the window list during
    // iteration.
    void request_close(uint32_t window_id);

    // Destroy a window immediately.  Waits for GPU idle on that window's
    // resources before cleanup.  Do NOT call while iterating windows().
    void destroy_window(uint32_t window_id);

    // Process any windows that have been marked for close (either via
    // request_close or GLFW's should_close flag).
    void process_pending_closes();

    // Single glfwPollEvents() call for all windows.
    void poll_events();

    // Returns all currently active (non-closed) window contexts.
    const std::vector<WindowContext*>& windows() const { return active_ptrs_; }

    // Returns the window that currently has OS focus, or nullptr.
    WindowContext* focused_window() const;

    // Returns true if at least one window is still open.
    bool any_window_open() const;

    // Returns the window context for a given window ID, or nullptr.
    WindowContext* find_window(uint32_t window_id) const;

    // Set the screen position of a window (top-left corner).
    void set_window_position(WindowContext& wctx, int x, int y);

    // Move a figure from one window to another.
    // Reassigns the figure's FigureId to the target window.
    // Returns true on success, false if source or target window not found.
    bool move_figure(FigureId figure_id, uint32_t from_window_id, uint32_t to_window_id);

    // Detach a figure into a new OS window at the given screen position.
    // Creates a new window sized to the figure's dimensions and assigns the
    // figure to it.  Returns the new WindowContext, or nullptr on failure.
    // Will refuse to detach if it's the last figure (caller must check).
    WindowContext* detach_figure(FigureId figure_id, uint32_t width, uint32_t height,
                                const std::string& title, int screen_x, int screen_y);

    // Create a new OS window with full UI stack (ImGui, FigureManager, DockSystem,
    // InputHandler, etc.).  The window gets its own ImGui context and can render
    // independently.  figure_id is the initial figure to assign to the window.
    // Returns the new WindowContext, or nullptr on failure.
    WindowContext* create_window_with_ui(uint32_t width, uint32_t height,
                                         const std::string& title,
                                         FigureId initial_figure_id,
                                         int screen_x = 0, int screen_y = 0);

    // Returns the number of open windows.
    size_t window_count() const { return active_ptrs_.size(); }

    // Shutdown: destroy all windows and release resources.
    // After this, the WindowManager is in an uninitialized state.
    void shutdown();

   private:
    // Create Vulkan resources (surface, swapchain, cmd buffers, sync) for a
    // WindowContext that already has a valid glfw_window pointer.
    bool init_vulkan_resources(WindowContext& wctx, uint32_t width, uint32_t height);

    // Destroy Vulkan resources for a window context (surface, swapchain, etc).
    void destroy_vulkan_resources(WindowContext& wctx);

    // Rebuild the active_ptrs_ cache from windows_.
    void rebuild_active_list();

    // GLFW callback trampolines — route events to the correct WindowContext.
    static void glfw_framebuffer_size_callback(GLFWwindow* window, int width, int height);
    static void glfw_window_close_callback(GLFWwindow* window);
    static void glfw_window_focus_callback(GLFWwindow* window, int focused);

    // Full input callbacks for windows with UI (mouse, key, scroll, char, cursor enter)
    static void glfw_cursor_pos_callback(GLFWwindow* window, double x, double y);
    static void glfw_mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
    static void glfw_scroll_callback(GLFWwindow* window, double x_offset, double y_offset);
    static void glfw_key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void glfw_char_callback(GLFWwindow* window, unsigned int codepoint);
    static void glfw_cursor_enter_callback(GLFWwindow* window, int entered);

    // Helper: find the WindowContext for a GLFW window handle
    WindowContext* find_by_glfw_window(GLFWwindow* window) const;

    // Initialize the full UI subsystem bundle for a WindowContext.
    // Creates ImGuiIntegration, FigureManager, DockSystem, InputHandler, etc.
    bool init_window_ui(WindowContext& wctx, FigureId initial_figure_id);

    VulkanBackend* backend_ = nullptr;
    FigureRegistry* registry_ = nullptr;
    Renderer* renderer_ = nullptr;
    std::vector<std::unique_ptr<WindowContext>> windows_;
    std::vector<WindowContext*> active_ptrs_;  // cache of raw pointers for fast iteration
    uint32_t next_window_id_ = 1;

    // IDs of windows pending destruction (deferred to avoid mutation during iteration)
    std::vector<uint32_t> pending_close_ids_;
};

}  // namespace spectra
