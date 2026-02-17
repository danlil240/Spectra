#include "window_manager.hpp"

#include <spectra/logger.hpp>

#include "../render/vulkan/vk_backend.hpp"
#include "../render/vulkan/window_context.hpp"

#ifdef SPECTRA_USE_GLFW
    #define GLFW_INCLUDE_NONE
    #define GLFW_INCLUDE_VULKAN
    #include <GLFW/glfw3.h>
#endif

#include <algorithm>

namespace spectra
{

WindowManager::~WindowManager()
{
    shutdown();
}

void WindowManager::init(VulkanBackend* backend)
{
    backend_ = backend;
}

WindowContext* WindowManager::adopt_primary_window(void* glfw_window)
{
    if (!backend_)
    {
        SPECTRA_LOG_ERROR("window_manager", "adopt_primary_window: not initialized");
        return nullptr;
    }

    // Wrap the backend's existing primary_window_ into our managed list.
    // We do NOT create a new WindowContext — we create a unique_ptr that
    // points to a new WindowContext whose Vulkan resources are already
    // set up by the normal App init path (create_surface + create_swapchain).
    auto& primary = backend_->primary_window();
    primary.id = next_window_id_++;
    primary.glfw_window = glfw_window;
    primary.is_focused = true;

    // NOTE: We intentionally do NOT install GLFW callbacks on the primary window.
    // GlfwAdapter owns the primary window's glfwSetWindowUserPointer (set to
    // GlfwAdapter*) and its input/resize/close callbacks.  Overwriting the user
    // pointer would cause GlfwAdapter callbacks to cast a WindowManager* as a
    // GlfwAdapter*, resulting in a segfault.  Primary window events are already
    // handled by GlfwAdapter + app.cpp; WindowManager only installs callbacks
    // on secondary windows it creates via create_window().

    // We don't own primary_window_ memory (it lives in VulkanBackend),
    // so we store a non-owning entry.  Use a unique_ptr with a no-op deleter
    // would be complex; instead, store the pointer directly in active_ptrs_.
    // The primary window is special — it's always at index 0.
    rebuild_active_list();

    SPECTRA_LOG_INFO("window_manager",
                     "Adopted primary window (id=" + std::to_string(primary.id) + ")");
    return &primary;
}

WindowContext* WindowManager::create_window(uint32_t width, uint32_t height,
                                            const std::string& title)
{
    if (!backend_)
    {
        SPECTRA_LOG_ERROR("window_manager", "create_window: not initialized");
        return nullptr;
    }

    if (backend_->is_headless())
    {
        SPECTRA_LOG_WARN("window_manager", "create_window: cannot create OS windows in headless mode");
        return nullptr;
    }

#ifdef SPECTRA_USE_GLFW
    // Create GLFW window (shared context not needed — Vulkan doesn't use GL contexts)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    GLFWwindow* glfw_win = glfwCreateWindow(
        static_cast<int>(width), static_cast<int>(height), title.c_str(), nullptr, nullptr);
    if (!glfw_win)
    {
        SPECTRA_LOG_ERROR("window_manager", "create_window: glfwCreateWindow failed");
        return nullptr;
    }

    // Create WindowContext
    auto wctx = std::make_unique<WindowContext>();
    wctx->id = next_window_id_++;
    wctx->glfw_window = glfw_win;

    // Initialize Vulkan resources (surface, swapchain, cmd buffers, sync)
    if (!backend_->init_window_context(*wctx, width, height))
    {
        SPECTRA_LOG_ERROR("window_manager",
                          "create_window: Vulkan resource init failed for window "
                              + std::to_string(wctx->id));
        glfwDestroyWindow(glfw_win);
        return nullptr;
    }

    // Set GLFW callbacks for this window
    glfwSetWindowUserPointer(glfw_win, this);
    glfwSetFramebufferSizeCallback(glfw_win, glfw_framebuffer_size_callback);
    glfwSetWindowCloseCallback(glfw_win, glfw_window_close_callback);
    glfwSetWindowFocusCallback(glfw_win, glfw_window_focus_callback);

    WindowContext* ptr = wctx.get();
    windows_.push_back(std::move(wctx));
    rebuild_active_list();

    SPECTRA_LOG_INFO("window_manager",
                     "Created window " + std::to_string(ptr->id) + ": "
                         + std::to_string(width) + "x" + std::to_string(height)
                         + " \"" + title + "\"");
    return ptr;
#else
    (void)width;
    (void)height;
    (void)title;
    SPECTRA_LOG_ERROR("window_manager", "create_window: GLFW not available");
    return nullptr;
#endif
}

void WindowManager::request_close(uint32_t window_id)
{
    pending_close_ids_.push_back(window_id);
}

void WindowManager::destroy_window(uint32_t window_id)
{
    // Don't destroy the primary window through this path — it's owned by VulkanBackend
    auto& primary = backend_->primary_window();
    if (primary.id == window_id)
    {
        primary.should_close = true;
        SPECTRA_LOG_INFO("window_manager",
                         "Primary window " + std::to_string(window_id)
                             + " marked for close (not destroyed here)");
        rebuild_active_list();
        return;
    }

    // Find and destroy secondary window
    auto it = std::find_if(windows_.begin(), windows_.end(),
                           [window_id](const auto& w) { return w->id == window_id; });
    if (it == windows_.end())
        return;

    auto& wctx = **it;

    // Destroy Vulkan resources
    backend_->destroy_window_context(wctx);

#ifdef SPECTRA_USE_GLFW
    // Destroy GLFW window
    if (wctx.glfw_window)
    {
        glfwDestroyWindow(static_cast<GLFWwindow*>(wctx.glfw_window));
        wctx.glfw_window = nullptr;
    }
#endif

    SPECTRA_LOG_INFO("window_manager",
                     "Destroyed window " + std::to_string(window_id));

    windows_.erase(it);
    rebuild_active_list();
}

void WindowManager::process_pending_closes()
{
    // Check GLFW should_close flags on all windows
#ifdef SPECTRA_USE_GLFW
    auto& primary = backend_->primary_window();
    if (primary.glfw_window && !primary.should_close)
    {
        if (glfwWindowShouldClose(static_cast<GLFWwindow*>(primary.glfw_window)))
        {
            primary.should_close = true;
        }
    }

    for (auto& wctx : windows_)
    {
        if (wctx->glfw_window && !wctx->should_close)
        {
            if (glfwWindowShouldClose(static_cast<GLFWwindow*>(wctx->glfw_window)))
            {
                wctx->should_close = true;
                pending_close_ids_.push_back(wctx->id);
            }
        }
    }
#endif

    // Process deferred close requests
    if (pending_close_ids_.empty())
        return;

    // Copy and clear to avoid re-entrancy issues
    auto ids = std::move(pending_close_ids_);
    pending_close_ids_.clear();

    for (uint32_t id : ids)
    {
        destroy_window(id);
    }
}

void WindowManager::poll_events()
{
#ifdef SPECTRA_USE_GLFW
    glfwPollEvents();
#endif
}

WindowContext* WindowManager::focused_window() const
{
    // Check primary window first
    auto& primary = backend_->primary_window();
    if (!primary.should_close && primary.is_focused)
        return &const_cast<WindowContext&>(primary);

    for (auto& wctx : windows_)
    {
        if (!wctx->should_close && wctx->is_focused)
            return wctx.get();
    }

    // Fallback: return primary if it's still open
    if (!primary.should_close)
        return &const_cast<WindowContext&>(primary);

    return nullptr;
}

bool WindowManager::any_window_open() const
{
    auto& primary = backend_->primary_window();
    if (!primary.should_close)
        return true;

    for (auto& wctx : windows_)
    {
        if (!wctx->should_close)
            return true;
    }
    return false;
}

WindowContext* WindowManager::find_window(uint32_t window_id) const
{
    auto& primary = backend_->primary_window();
    if (primary.id == window_id)
        return &const_cast<WindowContext&>(primary);

    for (auto& wctx : windows_)
    {
        if (wctx->id == window_id)
            return wctx.get();
    }
    return nullptr;
}

void WindowManager::shutdown()
{
    if (!backend_)
        return;

    // Destroy all secondary windows (reverse order)
    while (!windows_.empty())
    {
        auto& wctx = *windows_.back();
        backend_->destroy_window_context(wctx);

#ifdef SPECTRA_USE_GLFW
        if (wctx.glfw_window)
        {
            glfwDestroyWindow(static_cast<GLFWwindow*>(wctx.glfw_window));
            wctx.glfw_window = nullptr;
        }
#endif

        windows_.pop_back();
    }

    active_ptrs_.clear();
    pending_close_ids_.clear();

    SPECTRA_LOG_INFO("window_manager", "Shutdown complete");
}

// --- Private helpers ---

void WindowManager::rebuild_active_list()
{
    active_ptrs_.clear();

    if (backend_)
    {
        auto& primary = backend_->primary_window();
        if (!primary.should_close && primary.id != 0)
        {
            active_ptrs_.push_back(&primary);
        }
    }

    for (auto& wctx : windows_)
    {
        if (!wctx->should_close)
        {
            active_ptrs_.push_back(wctx.get());
        }
    }
}

// --- GLFW callback trampolines ---

#ifdef SPECTRA_USE_GLFW

void WindowManager::glfw_framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    auto* mgr = static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (!mgr)
        return;

    // Find which WindowContext this GLFW window belongs to
    WindowContext* wctx = nullptr;
    auto& primary = mgr->backend_->primary_window();
    if (primary.glfw_window == window)
    {
        wctx = &primary;
    }
    else
    {
        for (auto& w : mgr->windows_)
        {
            if (w->glfw_window == window)
            {
                wctx = w.get();
                break;
            }
        }
    }

    if (!wctx || width <= 0 || height <= 0)
        return;

    wctx->needs_resize = true;
    wctx->pending_width = static_cast<uint32_t>(width);
    wctx->pending_height = static_cast<uint32_t>(height);
    wctx->resize_time = std::chrono::steady_clock::now();

    SPECTRA_LOG_DEBUG("window_manager",
                      "Window " + std::to_string(wctx->id) + " resize: "
                          + std::to_string(width) + "x" + std::to_string(height));
}

void WindowManager::glfw_window_close_callback(GLFWwindow* window)
{
    auto* mgr = static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (!mgr)
        return;

    auto& primary = mgr->backend_->primary_window();
    if (primary.glfw_window == window)
    {
        primary.should_close = true;
        return;
    }

    for (auto& w : mgr->windows_)
    {
        if (w->glfw_window == window)
        {
            w->should_close = true;
            mgr->pending_close_ids_.push_back(w->id);
            return;
        }
    }
}

void WindowManager::glfw_window_focus_callback(GLFWwindow* window, int focused)
{
    auto* mgr = static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (!mgr)
        return;

    auto& primary = mgr->backend_->primary_window();
    if (primary.glfw_window == window)
    {
        primary.is_focused = (focused != 0);
        return;
    }

    for (auto& w : mgr->windows_)
    {
        if (w->glfw_window == window)
        {
            w->is_focused = (focused != 0);
            return;
        }
    }
}

void WindowManager::set_window_position(WindowContext& wctx, int x, int y)
{
    if (wctx.glfw_window)
    {
        glfwSetWindowPos(static_cast<GLFWwindow*>(wctx.glfw_window), x, y);
    }
}

#else

// Stubs when GLFW is not available
void WindowManager::glfw_framebuffer_size_callback(GLFWwindow*, int, int) {}
void WindowManager::glfw_window_close_callback(GLFWwindow*) {}
void WindowManager::glfw_window_focus_callback(GLFWwindow*, int) {}

void WindowManager::set_window_position(WindowContext& /*wctx*/, int /*x*/, int /*y*/) {}

#endif

WindowContext* WindowManager::detach_figure(FigureId figure_id, uint32_t width, uint32_t height,
                                             const std::string& title, int screen_x, int screen_y)
{
    if (!backend_)
    {
        SPECTRA_LOG_ERROR("window_manager", "detach_figure: not initialized");
        return nullptr;
    }

    if (figure_id == INVALID_FIGURE_ID)
    {
        SPECTRA_LOG_ERROR("window_manager", "detach_figure: invalid figure id");
        return nullptr;
    }

    // Clamp dimensions to reasonable minimums
    uint32_t w = width > 0 ? width : 800;
    uint32_t h = height > 0 ? height : 600;

    auto* wctx = create_window(w, h, title);
    if (!wctx)
    {
        SPECTRA_LOG_ERROR("window_manager",
                          "detach_figure: failed to create window for figure "
                              + std::to_string(figure_id));
        return nullptr;
    }

    wctx->assigned_figure_index = figure_id;
    set_window_position(*wctx, screen_x, screen_y);

    SPECTRA_LOG_INFO("window_manager",
                     "Detached figure " + std::to_string(figure_id) + " to window "
                         + std::to_string(wctx->id) + " at (" + std::to_string(screen_x) + ", "
                         + std::to_string(screen_y) + ")");
    return wctx;
}

bool WindowManager::move_figure(FigureId figure_id, uint32_t from_window_id, uint32_t to_window_id)
{
    auto* from_wctx = find_window(from_window_id);
    auto* to_wctx = find_window(to_window_id);
    if (!from_wctx || !to_wctx)
    {
        SPECTRA_LOG_ERROR("window_manager",
                          "move_figure: invalid window id (from=" + std::to_string(from_window_id)
                              + " to=" + std::to_string(to_window_id) + ")");
        return false;
    }
    if (from_wctx == to_wctx)
    {
        return false;  // No-op: same window
    }

    // Verify the source window is actually rendering this figure
    if (from_wctx->assigned_figure_index != figure_id)
    {
        SPECTRA_LOG_WARN("window_manager",
                         "move_figure: source window " + std::to_string(from_window_id)
                             + " is not rendering figure " + std::to_string(figure_id));
        return false;
    }

    // Reassign: target window now renders this figure
    to_wctx->assigned_figure_index = figure_id;

    // Clear source window's assignment (mark as unassigned)
    from_wctx->assigned_figure_index = INVALID_FIGURE_ID;

    SPECTRA_LOG_INFO("window_manager",
                     "Moved figure " + std::to_string(figure_id) + " from window "
                         + std::to_string(from_window_id) + " to window "
                         + std::to_string(to_window_id));
    return true;
}

}  // namespace spectra
