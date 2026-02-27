#include "window_manager.hpp"

#include <spectra/logger.hpp>

#include "render/renderer.hpp"
#include "render/vulkan/vk_backend.hpp"
#include "render/vulkan/window_context.hpp"
#include "ui/app/window_ui_context.hpp"

#ifdef SPECTRA_USE_GLFW
    #define GLFW_INCLUDE_NONE
    #define GLFW_INCLUDE_VULKAN
    #include <GLFW/glfw3.h>

    #include "glfw_utils.hpp"
#endif

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
    #include <imgui_impl_glfw.h>

    #include "ui/app/register_commands.hpp"
    #include "ui/figures/figure_manager.hpp"
    #include "ui/figures/figure_registry.hpp"
    #include "ui/imgui/imgui_integration.hpp"
    #include "ui/figures/tab_bar.hpp"
#endif

#include <algorithm>

namespace spectra
{

WindowManager::~WindowManager()
{
    shutdown();
}

void WindowManager::init(VulkanBackend* backend, FigureRegistry* registry, Renderer* renderer)
{
    backend_  = backend;
    registry_ = registry;
    renderer_ = renderer;
}

WindowContext* WindowManager::create_initial_window(void* glfw_window)
{
    if (!backend_)
    {
        SPECTRA_LOG_ERROR("window_manager", "create_initial_window: not initialized");
        return nullptr;
    }

    // Take ownership of the backend's initial WindowContext (already has
    // surface + swapchain initialized by the App init path).
    auto wctx = backend_->release_initial_window();
    if (!wctx)
    {
        SPECTRA_LOG_ERROR("window_manager", "create_initial_window: no initial window to take");
        return nullptr;
    }

    wctx->id          = next_window_id_++;
    wctx->glfw_window = glfw_window;
    wctx->is_focused  = true;

#ifdef SPECTRA_USE_GLFW
    // Set user pointer so WindowManager callbacks can find the manager.
    // Actual callbacks are installed later by install_input_callbacks(),
    // which must run AFTER ImGui init to avoid breaking ImGui's callback
    // chaining (ImGui saves "previous" callbacks during init).
    auto* glfw_win = static_cast<GLFWwindow*>(glfw_window);
    if (glfw_win)
    {
        glfwSetWindowUserPointer(glfw_win, this);
    }
#endif

    // Set active window so the backend can continue operating
    backend_->set_active_window(wctx.get());

    WindowContext* ptr = wctx.get();
    windows_.push_back(std::move(wctx));
    rebuild_active_list();

    SPECTRA_LOG_INFO("window_manager",
                     "Created initial window (id=" + std::to_string(ptr->id) + ")");
    return ptr;
}

WindowContext* WindowManager::create_window(uint32_t           width,
                                            uint32_t           height,
                                            const std::string& title)
{
    if (!backend_)
    {
        SPECTRA_LOG_ERROR("window_manager", "create_window: not initialized");
        return nullptr;
    }

    if (backend_->is_headless())
    {
        SPECTRA_LOG_WARN("window_manager",
                         "create_window: cannot create OS windows in headless mode");
        return nullptr;
    }

#ifdef SPECTRA_USE_GLFW
    // Create GLFW window (shared context not needed — Vulkan doesn't use GL contexts)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    set_wayland_app_id();
    GLFWwindow* glfw_win = glfwCreateWindow(static_cast<int>(width),
                                            static_cast<int>(height),
                                            title.c_str(),
                                            nullptr,
                                            nullptr);
    if (!glfw_win)
    {
        SPECTRA_LOG_ERROR("window_manager", "create_window: glfwCreateWindow failed");
        return nullptr;
    }

    // Create WindowContext
    auto wctx         = std::make_unique<WindowContext>();
    wctx->id          = next_window_id_++;
    wctx->glfw_window = glfw_win;

    // Initialize Vulkan resources (surface, swapchain, cmd buffers, sync)
    if (!backend_->init_window_context(*wctx, width, height))
    {
        SPECTRA_LOG_ERROR(
            "window_manager",
            "create_window: Vulkan resource init failed for window " + std::to_string(wctx->id));
        glfwDestroyWindow(glfw_win);
        return nullptr;
    }

    set_window_icon(glfw_win);

    // Set GLFW callbacks for this window
    glfwSetWindowUserPointer(glfw_win, this);
    glfwSetFramebufferSizeCallback(glfw_win, glfw_framebuffer_size_callback);
    glfwSetWindowCloseCallback(glfw_win, glfw_window_close_callback);
    glfwSetWindowFocusCallback(glfw_win, glfw_window_focus_callback);

    WindowContext* ptr = wctx.get();
    windows_.push_back(std::move(wctx));
    rebuild_active_list();

    SPECTRA_LOG_INFO("window_manager",
                     "Created window " + std::to_string(ptr->id) + ": " + std::to_string(width)
                         + "x" + std::to_string(height) + " \"" + title + "\"");
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
    // Find the window in our managed list
    auto it = std::find_if(windows_.begin(),
                           windows_.end(),
                           [window_id](const auto& w) { return w->id == window_id; });
    if (it == windows_.end())
        return;

    auto& wctx = **it;

    // Window close policy: destroy all figures owned by this window.
    // Closing a window (or its last tab) kills the figures — they do NOT
    // migrate to other windows.
    if (registry_ && !wctx.assigned_figures.empty())
    {
#ifdef SPECTRA_USE_IMGUI
        if (wctx.ui_ctx && wctx.ui_ctx->fig_mgr)
        {
            auto figs_copy = wctx.assigned_figures;
            for (FigureId fig_id : figs_copy)
                wctx.ui_ctx->fig_mgr->remove_figure(fig_id);
        }
#endif
        for (FigureId fig_id : wctx.assigned_figures)
        {
            registry_->unregister_figure(fig_id);
            SPECTRA_LOG_INFO("window_manager",
                             "Destroyed figure " + std::to_string(fig_id) + " (window "
                                 + std::to_string(window_id) + " closed)");
        }
        wctx.assigned_figures.clear();
    }

    // Wait for all GPU work to complete before destroying any resources.
    // Without this, ImGui shutdown frees descriptor sets / pipelines / buffers
    // that are still referenced by in-flight command buffers.
    if (backend_->device() != VK_NULL_HANDLE)
        vkDeviceWaitIdle(backend_->device());

    // Destroy UI context first (shuts down ImGui before Vulkan resources)
    if (wctx.ui_ctx)
    {
#ifdef SPECTRA_USE_IMGUI
        if (wctx.ui_ctx->imgui_ui)
        {
            auto* prev_active = backend_->active_window();
            backend_->set_active_window(&wctx);
            wctx.ui_ctx->imgui_ui->shutdown();
            // Restore previous active window, but NOT if it was the window
            // we are destroying — that would leave a dangling pointer after
            // windows_.erase(it) frees the WindowContext.
            if (prev_active != &wctx)
                backend_->set_active_window(prev_active);
            else
                backend_->set_active_window(nullptr);
        }
        // ImGuiIntegration::shutdown() already destroyed the ImGui context.
        // Null it out so destroy_window_context() doesn't double-shutdown.
        wctx.imgui_context = nullptr;
#endif
        wctx.ui_ctx.reset();
    }

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

    SPECTRA_LOG_INFO("window_manager", "Destroyed window " + std::to_string(window_id));

    windows_.erase(it);
    rebuild_active_list();
}

void WindowManager::process_pending_closes()
{
    // Check GLFW should_close flags on all windows
#ifdef SPECTRA_USE_GLFW
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
    for (auto& wctx : windows_)
    {
        if (!wctx->should_close && wctx->is_focused)
            return wctx.get();
    }

    // Fallback: return first open window
    for (auto& wctx : windows_)
    {
        if (!wctx->should_close)
            return wctx.get();
    }

    return nullptr;
}

bool WindowManager::any_window_open() const
{
    for (auto& wctx : windows_)
    {
        if (!wctx->should_close)
            return true;
    }
    return false;
}

WindowContext* WindowManager::find_window(uint32_t window_id) const
{
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

    // Wait for all GPU work to complete before destroying any window resources.
    if (backend_->device() != VK_NULL_HANDLE)
        vkDeviceWaitIdle(backend_->device());

    // Destroy all windows (reverse order)
    while (!windows_.empty())
    {
        auto& wctx = *windows_.back();

        // Destroy UI context first (shuts down ImGui before Vulkan resources)
        if (wctx.ui_ctx)
        {
#ifdef SPECTRA_USE_IMGUI
            if (wctx.ui_ctx->imgui_ui)
            {
                auto* prev_active = backend_->active_window();
                backend_->set_active_window(&wctx);
                wctx.ui_ctx->imgui_ui->shutdown();
                backend_->set_active_window(prev_active);
            }
            // ImGuiIntegration::shutdown() already destroyed the ImGui context.
            // Null it out so destroy_window_context() doesn't double-shutdown.
            wctx.imgui_context = nullptr;
#endif
            wctx.ui_ctx.reset();
        }

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

    // Null active_window_ before backend_->shutdown() runs (via destructor or
    // explicit call). All WindowContext objects owned by windows_ are now
    // destroyed — active_window_ is a dangling pointer. Clearing it prevents
    // VulkanBackend::shutdown() from accessing freed memory and double-freeing
    // semaphores/fences that destroy_window_context() already cleaned up.
    backend_->set_active_window(nullptr);

    // Mark as shut down so destructor and repeated calls are no-ops.
    backend_ = nullptr;

    SPECTRA_LOG_INFO("window_manager", "Shutdown complete");
}

// --- Private helpers ---

void WindowManager::rebuild_active_list()
{
    active_ptrs_.clear();

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

    WindowContext* wctx = mgr->find_by_glfw_window(window);
    if (!wctx || width <= 0 || height <= 0)
        return;

    wctx->needs_resize   = true;
    wctx->pending_width  = static_cast<uint32_t>(width);
    wctx->pending_height = static_cast<uint32_t>(height);
    wctx->resize_time    = std::chrono::steady_clock::now();

    SPECTRA_LOG_DEBUG("window_manager",
                      "Window " + std::to_string(wctx->id) + " resize: " + std::to_string(width)
                          + "x" + std::to_string(height));
}

void WindowManager::glfw_window_close_callback(GLFWwindow* window)
{
    auto* mgr = static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (!mgr)
        return;

    WindowContext* wctx = mgr->find_by_glfw_window(window);
    if (!wctx)
        return;

    wctx->should_close = true;
    mgr->pending_close_ids_.push_back(wctx->id);
}

void WindowManager::glfw_window_focus_callback(GLFWwindow* window, int focused)
{
    auto* mgr = static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (!mgr)
        return;

    WindowContext* wctx = mgr->find_by_glfw_window(window);
    if (!wctx)
        return;

    wctx->is_focused = (focused != 0);
    if (focused)
        wctx->z_order = mgr->next_z_order_++;
    #ifdef SPECTRA_USE_IMGUI
    // Forward focus event to ImGui for this window's context
    if (wctx->imgui_context && wctx->ui_ctx)
    {
        ImGuiContext* prev_ctx = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(wctx->imgui_context));
        ImGui_ImplGlfw_WindowFocusCallback(window, focused);
        if (prev_ctx)
            ImGui::SetCurrentContext(prev_ctx);
    }
    #endif
}

void WindowManager::install_input_callbacks(WindowContext& wctx)
{
    #ifdef SPECTRA_USE_GLFW
    auto* glfw_win = static_cast<GLFWwindow*>(wctx.glfw_window);
    if (glfw_win)
    {
        // Window management callbacks
        glfwSetFramebufferSizeCallback(glfw_win, glfw_framebuffer_size_callback);
        glfwSetWindowCloseCallback(glfw_win, glfw_window_close_callback);
        glfwSetWindowFocusCallback(glfw_win, glfw_window_focus_callback);
        // Input callbacks
        glfwSetCursorPosCallback(glfw_win, glfw_cursor_pos_callback);
        glfwSetMouseButtonCallback(glfw_win, glfw_mouse_button_callback);
        glfwSetScrollCallback(glfw_win, glfw_scroll_callback);
        glfwSetKeyCallback(glfw_win, glfw_key_callback);
        glfwSetCharCallback(glfw_win, glfw_char_callback);
        glfwSetCursorEnterCallback(glfw_win, glfw_cursor_enter_callback);
    }
    #else
    (void)wctx;
    #endif
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

WindowContext* WindowManager::detach_figure(FigureId           figure_id,
                                            uint32_t           width,
                                            uint32_t           height,
                                            const std::string& title,
                                            int                screen_x,
                                            int                screen_y)
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

    // If we have a registry, create a window with full UI; otherwise bare window
    WindowContext* wctx = nullptr;
    if (registry_)
    {
        wctx = create_window_with_ui(w, h, title, figure_id, screen_x, screen_y);
    }
    else
    {
        wctx = create_window(w, h, title);
        if (wctx)
        {
            wctx->assigned_figure_index = figure_id;
            wctx->assigned_figures      = {figure_id};
            wctx->active_figure_id      = figure_id;
            set_window_position(*wctx, screen_x, screen_y);
        }
    }

    if (!wctx)
    {
        SPECTRA_LOG_ERROR(
            "window_manager",
            "detach_figure: failed to create window for figure " + std::to_string(figure_id));
        return nullptr;
    }

    SPECTRA_LOG_INFO("window_manager",
                     "Detached figure " + std::to_string(figure_id) + " to window "
                         + std::to_string(wctx->id) + " at (" + std::to_string(screen_x) + ", "
                         + std::to_string(screen_y) + ")");
    return wctx;
}

// ── Tearoff preview window ──────────────────────────────────────────────────

void WindowManager::request_preview_window(uint32_t           width,
                                           uint32_t           height,
                                           int                screen_x,
                                           int                screen_y,
                                           const std::string& figure_title)
{
    pending_preview_create_ = PendingPreviewCreate{width, height, screen_x, screen_y, figure_title};
}

void WindowManager::request_destroy_preview()
{
    pending_preview_destroy_ = true;
    pending_preview_create_.reset();   // Cancel any pending create
}

void WindowManager::process_deferred_preview()
{
    if (pending_preview_destroy_)
    {
        pending_preview_destroy_ = false;
        // Also cancel any pending create — the drag ended before the
        // preview could appear, so don't flash it briefly.
        pending_preview_create_.reset();
        destroy_preview_window_impl();
        return;
    }
    if (pending_preview_create_)
    {
        auto req = std::move(*pending_preview_create_);
        pending_preview_create_.reset();
        create_preview_window_impl(req.width, req.height, req.screen_x, req.screen_y, req.title);
    }
}

bool WindowManager::has_preview_window() const
{
    return preview_window_id_ != 0 || pending_preview_create_.has_value();
}

WindowContext* WindowManager::create_preview_window_impl(uint32_t           width,
                                                         uint32_t           height,
                                                         int                screen_x,
                                                         int                screen_y,
                                                         const std::string& figure_title)
{
    // Destroy any existing preview window first
    destroy_preview_window_impl();

    if (!backend_ || backend_->is_headless())
        return nullptr;

#ifdef SPECTRA_USE_GLFW
    // Save and restore window hints after creating the preview window
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);

    // Set suppression window BEFORE creating the window.  The WM may
    // grab the pointer during glfwCreateWindow (synchronous X11 event
    // processing), which sends a real ButtonRelease to the source window.
    // We suppress that artifact for 200ms after creation.
    if (mouse_release_tracking_)
    {
        suppress_release_until_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    }

    GLFWwindow* glfw_win = glfwCreateWindow(static_cast<int>(width),
                                            static_cast<int>(height),
                                            figure_title.c_str(),
                                            nullptr,
                                            nullptr);

    // Reset hints to defaults for future windows
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    glfwWindowHint(GLFW_FLOATING, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);

    if (!glfw_win)
    {
        SPECTRA_LOG_ERROR("window_manager", "create_preview_window: glfwCreateWindow failed");
        return nullptr;
    }

    // Position the window at the cursor
    glfwSetWindowPos(glfw_win,
                     screen_x - static_cast<int>(width) / 2,
                     screen_y - static_cast<int>(height) / 3);

    auto wctx         = std::make_unique<WindowContext>();
    wctx->id          = next_window_id_++;
    wctx->glfw_window = glfw_win;
    wctx->is_preview  = true;
    wctx->title       = figure_title;

    // Initialize Vulkan resources
    if (!backend_->init_window_context(*wctx, width, height))
    {
        SPECTRA_LOG_ERROR("window_manager", "create_preview_window: Vulkan init failed");
        glfwDestroyWindow(glfw_win);
        return nullptr;
    }

    // Set GLFW callbacks (minimal — framebuffer resize, close, and mouse button).
    // Mouse button callback is needed so we catch ButtonRelease events if the
    // X11 implicit pointer grab transfers to this window during a tab drag.
    glfwSetWindowUserPointer(glfw_win, this);
    glfwSetFramebufferSizeCallback(glfw_win, glfw_framebuffer_size_callback);
    glfwSetWindowCloseCallback(glfw_win, glfw_window_close_callback);
    glfwSetMouseButtonCallback(glfw_win, glfw_mouse_button_callback);

    // Initialize ImGui for this preview window so we can render the card
    auto ui = std::make_unique<WindowUIContext>();

    // Minimal ImGui init — no FigureManager, no DockSystem, no input
    ui->imgui_ui = std::make_unique<ImGuiIntegration>();

    ImGuiContext* prev_imgui_ctx = ImGui::GetCurrentContext();
    auto*         prev_active    = backend_->active_window();
    backend_->set_active_window(wctx.get());

    if (!ui->imgui_ui->init(*backend_, glfw_win, /*install_callbacks=*/false))
    {
        SPECTRA_LOG_ERROR("window_manager", "create_preview_window: ImGui init failed");
        backend_->set_active_window(prev_active);
        ImGui::SetCurrentContext(prev_imgui_ctx);
        glfwDestroyWindow(glfw_win);
        return nullptr;
    }

    wctx->imgui_context = ImGui::GetCurrentContext();
    backend_->set_active_window(prev_active);
    ImGui::SetCurrentContext(prev_imgui_ctx);

    preview_window_id_ = wctx->id;

    WindowContext* ptr = wctx.get();
    windows_.push_back(std::move(wctx));
    rebuild_active_list();

    // Transfer UI context
    ptr->ui_ctx = std::move(ui);

    SPECTRA_LOG_DEBUG("window_manager",
                      "Created preview window " + std::to_string(ptr->id) + ": "
                          + std::to_string(width) + "x" + std::to_string(height));
    return ptr;
#else
    (void)width;
    (void)height;
    (void)screen_x;
    (void)screen_y;
    (void)figure_title;
    return nullptr;
#endif
}

void WindowManager::move_preview_window(int screen_x, int screen_y)
{
#ifdef SPECTRA_USE_GLFW
    auto* wctx = preview_window();
    if (!wctx || !wctx->glfw_window)
        return;

    auto* glfw_win = static_cast<GLFWwindow*>(wctx->glfw_window);
    int   w = 0, h = 0;
    glfwGetWindowSize(glfw_win, &w, &h);
    glfwSetWindowPos(glfw_win, screen_x - w / 2, screen_y - h / 3);
#else
    (void)screen_x;
    (void)screen_y;
#endif
}

void WindowManager::destroy_preview_window_impl()
{
    if (preview_window_id_ == 0)
        return;

    uint32_t id        = preview_window_id_;
    preview_window_id_ = 0;
    destroy_window(id);
}

WindowContext* WindowManager::preview_window() const
{
    if (preview_window_id_ == 0)
        return nullptr;
    return find_window(preview_window_id_);
}

bool WindowManager::is_mouse_button_held(int glfw_button) const
{
    // When callback-based tracking is active (during a tab drag), use
    // the tracked state.  Polling glfwGetMouseButton gives false RELEASE
    // on X11 after creating a new GLFW window because poll_events()
    // processes X11 events from the window creation in the same frame.
    // The callback only fires for real ButtonRelease X11 events.
    if (mouse_release_tracking_ && glfw_button == GLFW_MOUSE_BUTTON_LEFT)
        return !mouse_release_seen_;

#ifdef SPECTRA_USE_GLFW
    for (auto& wctx : windows_)
    {
        if (wctx->glfw_window && !wctx->should_close)
        {
            if (glfwGetMouseButton(static_cast<GLFWwindow*>(wctx->glfw_window), glfw_button)
                == GLFW_PRESS)
                return true;
        }
    }
#endif
    return false;
}

void WindowManager::begin_mouse_release_tracking()
{
    mouse_release_tracking_ = true;
    mouse_release_seen_     = false;
}

void WindowManager::end_mouse_release_tracking()
{
    mouse_release_tracking_ = false;
    mouse_release_seen_     = false;
}

bool WindowManager::get_global_cursor_pos(double& screen_x, double& screen_y) const
{
#ifdef SPECTRA_USE_GLFW
    // Try focused window first, then fall back to the first open window.
    WindowContext* wctx = nullptr;
    for (auto& w : windows_)
    {
        if (w->glfw_window && !w->should_close && w->is_focused)
        {
            wctx = w.get();
            break;
        }
    }
    if (!wctx)
    {
        for (auto& w : windows_)
        {
            if (w->glfw_window && !w->should_close)
            {
                wctx = w.get();
                break;
            }
        }
    }
    if (!wctx)
        return false;

    auto*  glfw_win = static_cast<GLFWwindow*>(wctx->glfw_window);
    double cx, cy;
    glfwGetCursorPos(glfw_win, &cx, &cy);
    int wx, wy;
    glfwGetWindowPos(glfw_win, &wx, &wy);
    screen_x = wx + cx;
    screen_y = wy + cy;
    return true;
#else
    (void)screen_x;
    (void)screen_y;
    return false;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────

bool WindowManager::move_figure(FigureId figure_id, uint32_t from_window_id, uint32_t to_window_id)
{
    auto* from_wctx = find_window(from_window_id);
    auto* to_wctx   = find_window(to_window_id);
    if (!from_wctx || !to_wctx)
    {
        SPECTRA_LOG_ERROR("window_manager",
                          "move_figure: invalid window id (from=" + std::to_string(from_window_id)
                              + " to=" + std::to_string(to_window_id) + ")");
        return false;
    }
    if (from_wctx == to_wctx)
    {
        return false;   // No-op: same window
    }

    // Verify the source window has this figure in its assigned_figures list
    auto& src_figs = from_wctx->assigned_figures;
    auto  src_it   = std::find(src_figs.begin(), src_figs.end(), figure_id);
    if (src_it == src_figs.end())
    {
        // Fallback: check legacy single-figure field
        if (from_wctx->assigned_figure_index != figure_id)
        {
            SPECTRA_LOG_WARN("window_manager",
                             "move_figure: source window " + std::to_string(from_window_id)
                                 + " does not have figure " + std::to_string(figure_id));
            return false;
        }
    }

    // Remove from source window's assigned_figures
    if (src_it != src_figs.end())
    {
        src_figs.erase(src_it);
    }

    // Update source's active figure if we just removed the active one
    if (from_wctx->active_figure_id == figure_id)
    {
        from_wctx->active_figure_id = src_figs.empty() ? INVALID_FIGURE_ID : src_figs.front();
    }

    // Update legacy single-figure field
    from_wctx->assigned_figure_index =
        src_figs.empty() ? INVALID_FIGURE_ID : from_wctx->active_figure_id;

    // Add to target window's assigned_figures (avoid duplicates)
    auto& dst_figs = to_wctx->assigned_figures;
    if (std::find(dst_figs.begin(), dst_figs.end(), figure_id) == dst_figs.end())
    {
        dst_figs.push_back(figure_id);
    }
    to_wctx->active_figure_id      = figure_id;
    to_wctx->assigned_figure_index = figure_id;

    // Sync per-window FigureManagers if they exist
#ifdef SPECTRA_USE_IMGUI
    if (from_wctx->ui_ctx && from_wctx->ui_ctx->fig_mgr)
    {
        // Remove from source FigureManager (preserves FigureState, does NOT unregister)
        FigureState transferred_state = from_wctx->ui_ctx->fig_mgr->remove_figure(figure_id);

        // Remove from source DockSystem split panes if active
        auto& src_dock = from_wctx->ui_ctx->dock_system;
        if (src_dock.is_split())
        {
            src_dock.split_view().close_pane(figure_id);
        }

        // Add to target FigureManager with the transferred state.
        // Save dock active figure BEFORE add_figure(), because
        // add_figure → switch_to → tab bar callback will change
        // active_figure_index to the new figure (not yet in any pane).
        if (to_wctx->ui_ctx && to_wctx->ui_ctx->fig_mgr)
        {
            auto&    dst_dock         = to_wctx->ui_ctx->dock_system;
            FigureId prev_dock_active = dst_dock.active_figure_index();

            to_wctx->ui_ctx->fig_mgr->add_figure(figure_id, std::move(transferred_state));

            // Place the figure in a split pane so it's visible
            if (dst_dock.is_split())
            {
                auto* target_pane = dst_dock.split_view().pane_for_figure(prev_dock_active);
                if (!target_pane)
                {
                    auto all = dst_dock.split_view().all_panes();
                    if (!all.empty())
                        target_pane = all.front();
                }
                if (target_pane && target_pane->is_leaf())
                    target_pane->add_figure(figure_id);
                dst_dock.set_active_figure_index(figure_id);
            }
        }

        // Update InputHandler in target window
        if (to_wctx->ui_ctx)
        {
            Figure* fig = registry_ ? registry_->get(figure_id) : nullptr;
            if (fig)
            {
                to_wctx->ui_ctx->input_handler.set_figure(fig);
                if (!fig->axes().empty() && fig->axes()[0])
                {
                    to_wctx->ui_ctx->input_handler.set_active_axes(fig->axes()[0].get());
                    auto& vp = fig->axes()[0]->viewport();
                    to_wctx->ui_ctx->input_handler.set_viewport(vp.x, vp.y, vp.w, vp.h);
                }
            }
        }

        // Reset source window's InputHandler so it no longer references the
        // moved figure's axes (which would cause cross-window interaction).
        if (from_wctx->ui_ctx && from_wctx->ui_ctx->fig_mgr)
        {
            FigureId remaining_id = from_wctx->ui_ctx->fig_mgr->active_index();
            Figure*  remaining    = (remaining_id != INVALID_FIGURE_ID && registry_)
                                        ? registry_->get(remaining_id)
                                        : nullptr;
            if (remaining)
            {
                from_wctx->ui_ctx->input_handler.set_figure(remaining);
                if (!remaining->axes().empty() && remaining->axes()[0])
                {
                    from_wctx->ui_ctx->input_handler.set_active_axes(remaining->axes()[0].get());
                    auto& vp = remaining->axes()[0]->viewport();
                    from_wctx->ui_ctx->input_handler.set_viewport(vp.x, vp.y, vp.w, vp.h);
                }
                from_wctx->ui_ctx->input_handler.set_active_axes_base(nullptr);
            }
            else
            {
                from_wctx->ui_ctx->input_handler.set_figure(nullptr);
                from_wctx->ui_ctx->input_handler.set_active_axes(nullptr);
                from_wctx->ui_ctx->input_handler.set_active_axes_base(nullptr);
            }
        }
    }
#endif

    SPECTRA_LOG_INFO("window_manager",
                     "Moved figure " + std::to_string(figure_id) + " from window "
                         + std::to_string(from_window_id) + " to window "
                         + std::to_string(to_window_id));
    return true;
}

// --- compute_cross_window_drop_zone ---

int WindowManager::compute_cross_window_drop_zone(uint32_t target_wid, float local_x, float local_y)
{
    cross_drop_info_ = CrossWindowDropInfo{};

#ifdef SPECTRA_USE_IMGUI
    auto* wctx = find_window(target_wid);
    if (!wctx || !wctx->ui_ctx)
        return 0;

    auto& dock = wctx->ui_ctx->dock_system;

    // Use the DockSystem's compute_drop_target logic by temporarily
    // beginning a drag, computing the target, then cancelling.
    // But we can't call begin_drag (it sets dragging state).
    // Instead, replicate the edge-zone math from DockSystem::compute_drop_target.

    // Get the root pane bounds (canvas area)
    auto panes = dock.split_view().all_panes();
    if (panes.empty())
        return 0;

    // Find the pane under the cursor
    SplitPane* target_pane = nullptr;
    for (auto* p : panes)
    {
        if (!p->is_leaf())
            continue;
        Rect b = p->bounds();
        if (local_x >= b.x && local_x < b.x + b.w && local_y >= b.y && local_y < b.y + b.h)
        {
            target_pane = const_cast<SplitPane*>(p);
            break;
        }
    }

    if (!target_pane)
    {
        // Cursor not over any pane — try using the full window area
        // (common for non-split windows where pane bounds may not cover the tab bar area)
        if (panes.size() == 1)
            target_pane = const_cast<SplitPane*>(panes[0]);
        else
            return 0;
    }

    Rect b = target_pane->bounds();
    if (b.w < 1.0f || b.h < 1.0f)
        return 0;

    constexpr float DROP_ZONE_FRACTION = 0.25f;
    constexpr float DROP_ZONE_MIN_SIZE = 40.0f;

    float edge_w = std::max(b.w * DROP_ZONE_FRACTION, DROP_ZONE_MIN_SIZE);
    float edge_h = std::max(b.h * DROP_ZONE_FRACTION, DROP_ZONE_MIN_SIZE);
    edge_w       = std::min(edge_w, b.w * 0.4f);
    edge_h       = std::min(edge_h, b.h * 0.4f);

    float rel_x = local_x - b.x;
    float rel_y = local_y - b.y;

    // DropZone: 0=None, 1=Left, 2=Right, 3=Top, 4=Bottom, 5=Center
    int zone = 5;   // Default: Center

    if (rel_x < edge_w)
        zone = 1;   // Left
    else if (rel_x > b.w - edge_w)
        zone = 2;   // Right
    else if (rel_y < edge_h)
        zone = 3;   // Top
    else if (rel_y > b.h - edge_h)
        zone = 4;   // Bottom

    // Compute highlight rect
    float hx = b.x, hy = b.y, hw = b.w, hh = b.h;
    switch (zone)
    {
        case 1:   // Left
            hw = b.w * 0.5f;
            break;
        case 2:   // Right
            hx = b.x + b.w * 0.5f;
            hw = b.w * 0.5f;
            break;
        case 3:   // Top
            hh = b.h * 0.5f;
            break;
        case 4:   // Bottom
            hy = b.y + b.h * 0.5f;
            hh = b.h * 0.5f;
            break;
        case 5:   // Center
            break;
        default:
            break;
    }

    cross_drop_info_.zone             = zone;
    cross_drop_info_.hx               = hx;
    cross_drop_info_.hy               = hy;
    cross_drop_info_.hw               = hw;
    cross_drop_info_.hh               = hh;
    cross_drop_info_.target_figure_id = target_pane->figure_index();
    return zone;
#else
    (void)target_wid;
    (void)local_x;
    (void)local_y;
    return 0;
#endif
}

// --- find_by_glfw_window ---

WindowContext* WindowManager::find_by_glfw_window(GLFWwindow* window) const
{
#ifdef SPECTRA_USE_GLFW
    for (auto& w : windows_)
    {
        if (w->glfw_window == window)
            return w.get();
    }
#else
    (void)window;
#endif
    return nullptr;
}

// --- create_first_window_with_ui ---

WindowContext* WindowManager::create_first_window_with_ui(void*                        glfw_window,
                                                          const std::vector<FigureId>& figure_ids)
{
    if (!backend_)
    {
        SPECTRA_LOG_ERROR("window_manager", "create_first_window_with_ui: not initialized");
        return nullptr;
    }

    if (!registry_)
    {
        SPECTRA_LOG_ERROR("window_manager", "create_first_window_with_ui: no registry");
        return nullptr;
    }

    // Take ownership of the backend's initial WindowContext (already has
    // surface + swapchain initialized by the App init path).
    auto wctx_ptr = backend_->release_initial_window();
    if (!wctx_ptr)
    {
        SPECTRA_LOG_ERROR("window_manager",
                          "create_first_window_with_ui: no initial window to take");
        return nullptr;
    }

    wctx_ptr->id          = next_window_id_++;
    wctx_ptr->glfw_window = glfw_window;
    wctx_ptr->is_focused  = true;

    // Set figure assignments (all figures go to the first window)
    FigureId active_id              = figure_ids.empty() ? INVALID_FIGURE_ID : figure_ids[0];
    wctx_ptr->assigned_figure_index = active_id;
    wctx_ptr->assigned_figures      = figure_ids;
    wctx_ptr->active_figure_id      = active_id;
    wctx_ptr->title                 = "Spectra";

#ifdef SPECTRA_USE_GLFW
    auto* glfw_win = static_cast<GLFWwindow*>(glfw_window);
    if (glfw_win)
    {
        glfwSetWindowUserPointer(glfw_win, this);
    }
#endif

    // Set active window so the backend can continue operating
    backend_->set_active_window(wctx_ptr.get());

    // Ensure pipelines exist before ImGui init (needs render pass)
    backend_->ensure_pipelines();

    // Initialize the full UI subsystem bundle (ImGui, FigureManager, etc.)
    // This is the SAME path that secondary windows use.
    if (!init_window_ui(*wctx_ptr, active_id))
    {
        SPECTRA_LOG_ERROR("window_manager", "create_first_window_with_ui: UI init failed");
    }

    // For the first window, FigureManager should have ALL figures, not just one.
    // init_window_ui() strips all but the initial figure, so re-add the rest.
#ifdef SPECTRA_USE_IMGUI
    if (wctx_ptr->ui_ctx && wctx_ptr->ui_ctx->fig_mgr && !figure_ids.empty())
    {
        auto* fm = wctx_ptr->ui_ctx->fig_mgr;
        for (size_t i = 1; i < figure_ids.size(); ++i)
        {
            fm->add_figure(figure_ids[i], FigureState{});
        }
        // add_figure() calls switch_to() internally, so after the loop
        // the active figure is the LAST one added.  Switch back to the
        // first figure so the window starts on figure_ids[0].
        fm->switch_to(figure_ids[0]);
        wctx_ptr->active_figure_id = fm->active_index();
    }
#endif

#ifdef SPECTRA_USE_GLFW
    // Install full input callbacks (same as secondary windows)
    install_input_callbacks(*wctx_ptr);
#endif

    WindowContext* raw = wctx_ptr.get();
    windows_.push_back(std::move(wctx_ptr));
    rebuild_active_list();

    SPECTRA_LOG_INFO("window_manager",
                     "Created first window with UI (id=" + std::to_string(raw->id)
                         + ", figures=" + std::to_string(figure_ids.size()) + ")");
    return raw;
}

// --- create_window_with_ui ---

WindowContext* WindowManager::create_window_with_ui(uint32_t           width,
                                                    uint32_t           height,
                                                    const std::string& title,
                                                    FigureId           initial_figure_id,
                                                    int                screen_x,
                                                    int                screen_y)
{
    if (!backend_)
    {
        SPECTRA_LOG_ERROR("window_manager", "create_window_with_ui: not initialized");
        return nullptr;
    }

    if (!registry_)
    {
        SPECTRA_LOG_ERROR("window_manager", "create_window_with_ui: no FigureRegistry");
        return nullptr;
    }

    // Create the base window (GLFW + Vulkan resources)
    auto* wctx = create_window(width, height, title);
    if (!wctx)
        return nullptr;

    // Set window position
    set_window_position(*wctx, screen_x, screen_y);

    // Set figure assignment
    wctx->assigned_figure_index = initial_figure_id;
    wctx->assigned_figures      = {initial_figure_id};
    wctx->active_figure_id      = initial_figure_id;
    wctx->title                 = title;

#ifdef SPECTRA_USE_GLFW
    // Install full input callbacks (mouse, key, scroll) in addition to the
    // framebuffer/close/focus callbacks already installed by create_window().
    auto* glfw_win = static_cast<GLFWwindow*>(wctx->glfw_window);
    if (glfw_win)
    {
        glfwSetCursorPosCallback(glfw_win, glfw_cursor_pos_callback);
        glfwSetMouseButtonCallback(glfw_win, glfw_mouse_button_callback);
        glfwSetScrollCallback(glfw_win, glfw_scroll_callback);
        glfwSetKeyCallback(glfw_win, glfw_key_callback);
        glfwSetCharCallback(glfw_win, glfw_char_callback);
        glfwSetCursorEnterCallback(glfw_win, glfw_cursor_enter_callback);
    }
#endif

    // Initialize the full UI subsystem bundle
    if (!init_window_ui(*wctx, initial_figure_id))
    {
        SPECTRA_LOG_ERROR(
            "window_manager",
            "create_window_with_ui: UI init failed for window " + std::to_string(wctx->id));
        // Window still usable as a bare render window — don't destroy it
    }

    SPECTRA_LOG_INFO("window_manager",
                     "Created window with UI " + std::to_string(wctx->id) + ": "
                         + std::to_string(width) + "x" + std::to_string(height) + " \"" + title
                         + "\" figure=" + std::to_string(initial_figure_id));
    return wctx;
}

// --- init_window_ui ---

bool WindowManager::init_window_ui(WindowContext& wctx, FigureId initial_figure_id)
{
    if (!registry_)
        return false;

#ifdef SPECTRA_USE_IMGUI
    auto ui = std::make_unique<WindowUIContext>();

    // Create per-window FigureManager with only the assigned figure.
    // FigureManager's constructor imports ALL registry figures, so we
    // remove everything except the initial figure for this window.
    ui->fig_mgr_owned = std::make_unique<FigureManager>(*registry_);
    ui->fig_mgr       = ui->fig_mgr_owned.get();
    {
        auto all = ui->fig_mgr->figure_ids();   // copy — we'll mutate
        for (auto id : all)
        {
            if (id != initial_figure_id)
                ui->fig_mgr->remove_figure(id);
        }
    }

    // Create per-window TabBar
    ui->figure_tabs = std::make_unique<TabBar>();
    ui->fig_mgr->set_tab_bar(ui->figure_tabs.get());

    // Wire "close last tab → close window" callback
    uint32_t       wctx_id = wctx.id;
    WindowManager* wm_self = this;
    ui->fig_mgr->set_on_window_close_request([wm_self, wctx_id]()
                                             { wm_self->request_close(wctx_id); });

    // Wire TabBar callbacks → FigureManager + DockSystem
    auto* fig_mgr_ptr = ui->fig_mgr;
    auto* dock_ptr    = &ui->dock_system;
    auto* guard_ptr   = &ui->dock_tab_sync_guard;

    ui->figure_tabs->set_tab_change_callback(
        [fig_mgr_ptr, dock_ptr, guard_ptr](size_t new_index)
        {
            if (*guard_ptr)
                return;
            *guard_ptr = true;
            // Convert positional tab index → FigureId
            const auto& ids = fig_mgr_ptr->figure_ids();
            if (new_index < ids.size())
            {
                FigureId fid = ids[new_index];
                fig_mgr_ptr->queue_switch(fid);
                dock_ptr->set_active_figure_index(fid);
            }
            *guard_ptr = false;
        });
    ui->figure_tabs->set_tab_close_callback(
        [fig_mgr_ptr](size_t index)
        {
            const auto& ids = fig_mgr_ptr->figure_ids();
            if (index < ids.size())
                fig_mgr_ptr->queue_close(ids[index]);
        });
    ui->figure_tabs->set_tab_add_callback([fig_mgr_ptr]() { fig_mgr_ptr->queue_create(); });
    ui->figure_tabs->set_tab_duplicate_callback(
        [fig_mgr_ptr](size_t index)
        {
            const auto& ids = fig_mgr_ptr->figure_ids();
            if (index < ids.size())
                fig_mgr_ptr->duplicate_figure(ids[index]);
        });
    ui->figure_tabs->set_tab_close_all_except_callback(
        [fig_mgr_ptr](size_t index)
        {
            const auto& ids = fig_mgr_ptr->figure_ids();
            if (index < ids.size())
                fig_mgr_ptr->close_all_except(ids[index]);
        });
    ui->figure_tabs->set_tab_close_to_right_callback(
        [fig_mgr_ptr](size_t index)
        {
            const auto& ids = fig_mgr_ptr->figure_ids();
            if (index < ids.size())
                fig_mgr_ptr->close_to_right(ids[index]);
        });
    ui->figure_tabs->set_tab_rename_callback(
        [fig_mgr_ptr](size_t index, const std::string& t)
        {
            const auto& ids = fig_mgr_ptr->figure_ids();
            if (index < ids.size())
                fig_mgr_ptr->set_title(ids[index], t);
        });

    // Tab drag-to-dock callbacks
    ui->figure_tabs->set_tab_drag_out_callback([dock_ptr](size_t index, float mx, float my)
                                               { dock_ptr->begin_drag(index, mx, my); });
    ui->figure_tabs->set_tab_drag_update_callback([dock_ptr](size_t /*index*/, float mx, float my)
                                                  { dock_ptr->update_drag(mx, my); });
    ui->figure_tabs->set_tab_drag_end_callback([dock_ptr](size_t /*index*/, float mx, float my)
                                               { dock_ptr->end_drag(mx, my); });
    ui->figure_tabs->set_tab_drag_cancel_callback([dock_ptr](size_t /*index*/)
                                                  { dock_ptr->cancel_drag(); });

    // Create per-window ImGuiIntegration
    ui->imgui_ui = std::make_unique<ImGuiIntegration>();

    auto* glfw_win = static_cast<GLFWwindow*>(wctx.glfw_window);
    if (glfw_win && backend_)
    {
        // Save current ImGui context — the primary window may be mid-frame
        // (between NewFrame/EndFrame) so we must restore it after init.
        ImGuiContext* prev_imgui_ctx = ImGui::GetCurrentContext();

        // Set active window so render_pass() returns this window's render pass
        auto* prev_active = backend_->active_window();
        backend_->set_active_window(&wctx);

        // Pass install_callbacks=false so ImGui does NOT install its own
        // GLFW callbacks on this secondary window.  ImGui's callbacks use
        // ImGui::GetCurrentContext() which is the primary window's context
        // during glfwPollEvents(), causing input on the secondary window to
        // be routed to the primary.  Instead, WindowManager's GLFW callbacks
        // switch to the correct ImGui context and forward events manually.
        if (!ui->imgui_ui->init(*backend_, glfw_win, /*install_callbacks=*/false))
        {
            SPECTRA_LOG_ERROR(
                "window_manager",
                "init_window_ui: ImGui init failed for window " + std::to_string(wctx.id));
            backend_->set_active_window(prev_active);
            ImGui::SetCurrentContext(prev_imgui_ctx);
            return false;
        }

        // Store the new window's ImGui context for later use
        wctx.imgui_context = ImGui::GetCurrentContext();

        // Restore previous active window and ImGui context
        backend_->set_active_window(prev_active);
        ImGui::SetCurrentContext(prev_imgui_ctx);
    }

    // Wire subsystems to ImGuiIntegration
    ui->imgui_ui->set_dock_system(&ui->dock_system);
    ui->imgui_ui->set_tab_bar(ui->figure_tabs.get());
    ui->imgui_ui->set_command_palette(&ui->cmd_palette);
    ui->imgui_ui->set_command_registry(&ui->cmd_registry);
    ui->imgui_ui->set_shortcut_manager(&ui->shortcut_mgr);
    ui->imgui_ui->set_undo_manager(&ui->undo_mgr);
    ui->imgui_ui->set_axis_link_manager(&ui->axis_link_mgr);
    ui->imgui_ui->set_input_handler(&ui->input_handler);
    ui->imgui_ui->set_timeline_editor(&ui->timeline_editor);
    ui->imgui_ui->set_keyframe_interpolator(&ui->keyframe_interpolator);
    ui->imgui_ui->set_curve_editor(&ui->curve_editor);
    ui->imgui_ui->set_mode_transition(&ui->mode_transition);
    ui->imgui_ui->set_knob_manager(&ui->knob_manager);
    ui->imgui_ui->set_series_clipboard(&shared_clipboard_);
    // (text_renderer wiring removed — plot text now rendered by Renderer::render_plot_text)

    // Wire TabDragController for drag-to-detach support
    ui->tab_drag_controller.set_window_manager(this);
    ui->tab_drag_controller.set_dock_system(&ui->dock_system);
    ui->tab_drag_controller.set_source_window_id(wctx.id);
    ui->imgui_ui->set_tab_drag_controller(&ui->tab_drag_controller);
    ui->imgui_ui->set_window_id(wctx.id);
    ui->imgui_ui->set_window_manager(this);

    // Wire stored tab drag handlers so every window supports tear-off and cross-window move
    if (tab_detach_handler_)
    {
        auto* reg     = registry_;
        auto  handler = tab_detach_handler_;
        ui->tab_drag_controller.set_on_drop_outside(
            [fig_mgr_ptr, reg, handler](FigureId fid, float sx, float sy)
            {
                auto* fig = reg->get(fid);
                if (!fig)
                    return;
                uint32_t    w     = fig->width() > 0 ? fig->width() : 800;
                uint32_t    h     = fig->height() > 0 ? fig->height() : 600;
                std::string title = fig_mgr_ptr->get_title(fid);
                handler(fid, w, h, title, static_cast<int>(sx), static_cast<int>(sy));
            });
    }
    if (tab_move_handler_)
    {
        auto  handler = tab_move_handler_;
        auto* wm      = this;
        ui->tab_drag_controller.set_on_drop_on_window(
            [handler, wm](FigureId fid, uint32_t target_wid, float /*sx*/, float /*sy*/)
            {
                // Use the last computed cross-window drop zone (updated each frame
                // during drag by TabDragController → compute_cross_window_drop_zone)
                auto info = wm->cross_window_drop_info();
                handler(fid, target_wid, info.zone, info.hx, info.hy, info.target_figure_id);
            });
    }

    // Wire DataInteraction
    ui->data_interaction = std::make_unique<DataInteraction>();
    ui->imgui_ui->set_data_interaction(ui->data_interaction.get());
    ui->input_handler.set_data_interaction(ui->data_interaction.get());

    // Wire box zoom overlay
    ui->box_zoom_overlay.set_input_handler(&ui->input_handler);
    ui->imgui_ui->set_box_zoom_overlay(&ui->box_zoom_overlay);

    // Wire input handler
    ui->input_handler.set_animation_controller(&ui->anim_controller);
    ui->input_handler.set_gesture_recognizer(&ui->gesture);
    ui->input_handler.set_shortcut_manager(&ui->shortcut_mgr);
    ui->input_handler.set_undo_manager(&ui->undo_mgr);
    ui->input_handler.set_axis_link_manager(&ui->axis_link_mgr);

    // Wire series click-to-select (left-click toggles selection)
    auto* imgui_raw = ui->imgui_ui.get();
    ui->data_interaction->set_on_series_selected(
        [imgui_raw](Figure* fig, Axes* ax, int ax_idx, Series* s, int s_idx)
        {
            if (imgui_raw)
                imgui_raw->select_series(fig, ax, ax_idx, s, s_idx);
        });
    // Wire right-click series selection (no toggle — always selects for context menu)
    ui->data_interaction->set_on_series_right_click_selected(
        [imgui_raw](Figure* fig, Axes* ax, int ax_idx, Series* s, int s_idx)
        {
            if (imgui_raw)
                imgui_raw->select_series_no_toggle(fig, ax, ax_idx, s, s_idx);
        });
    // Wire series deselect (left-click on empty canvas)
    ui->data_interaction->set_on_series_deselected(
        [imgui_raw]()
        {
            if (imgui_raw)
                imgui_raw->deselect_series();
        });
    ui->data_interaction->set_axis_link_manager(&ui->axis_link_mgr);

    // Wire pane tab context menu callbacks
    ui->imgui_ui->set_pane_tab_duplicate_cb([fig_mgr_ptr](FigureId index)
                                            { fig_mgr_ptr->duplicate_figure(index); });
    ui->imgui_ui->set_pane_tab_close_cb([fig_mgr_ptr](FigureId index)
                                        { fig_mgr_ptr->queue_close(index); });
    ui->imgui_ui->set_pane_tab_split_right_cb(
        [dock_ptr](FigureId index)
        {
            auto* pane = dock_ptr->split_view().root()
                             ? dock_ptr->split_view().root()->find_by_figure(index)
                             : nullptr;
            if (!pane || pane->figure_count() < 2)
                return;
            auto* new_pane = dock_ptr->split_figure_right(index, index);
            if (!new_pane)
                return;
            // Remove the moved figure from the source (first child) pane
            auto* parent = new_pane->parent();
            if (parent && parent->first())
                parent->first()->remove_figure(index);
            dock_ptr->set_active_figure_index(index);
        });
    ui->imgui_ui->set_pane_tab_split_down_cb(
        [dock_ptr](FigureId index)
        {
            auto* pane = dock_ptr->split_view().root()
                             ? dock_ptr->split_view().root()->find_by_figure(index)
                             : nullptr;
            if (!pane || pane->figure_count() < 2)
                return;
            auto* new_pane = dock_ptr->split_figure_down(index, index);
            if (!new_pane)
                return;
            // Remove the moved figure from the source (first child) pane
            auto* parent = new_pane->parent();
            if (parent && parent->first())
                parent->first()->remove_figure(index);
            dock_ptr->set_active_figure_index(index);
        });
    ui->imgui_ui->set_pane_tab_rename_cb([fig_mgr_ptr](size_t index, const std::string& t)
                                         { fig_mgr_ptr->set_title(index, t); });

    // Figure title lookup — fig_idx is a FigureId, not a positional index
    ui->imgui_ui->set_figure_title_callback(
        [fig_mgr_ptr](size_t fig_idx) -> std::string
        { return fig_mgr_ptr->get_title(static_cast<FigureId>(fig_idx)); });

    // Figure pointer resolver — used for split-mode legend drawing
    ui->imgui_ui->set_figure_ptr_callback([fig_mgr_ptr](FigureId id) -> Figure*
                                          { return fig_mgr_ptr->get_figure(id); });

    // Dock system → tab bar sync
    auto* figure_tabs_raw = ui->figure_tabs.get();
    ui->dock_system.split_view().set_on_active_changed(
        [figure_tabs_raw, fig_mgr_ptr, guard_ptr](size_t figure_index)
        {
            if (*guard_ptr)
                return;
            *guard_ptr = true;
            // figure_index here is a FigureId from the dock system.
            // Find its positional index for the tab bar.
            const auto& ids = fig_mgr_ptr->figure_ids();
            for (size_t i = 0; i < ids.size(); ++i)
            {
                if (ids[i] == figure_index)
                {
                    if (figure_tabs_raw && i < figure_tabs_raw->get_tab_count())
                        figure_tabs_raw->set_active_tab(i);
                    break;
                }
            }
            fig_mgr_ptr->queue_switch(figure_index);
            *guard_ptr = false;
        });

    // Wire timeline/interpolator
    ui->timeline_editor.set_interpolator(&ui->keyframe_interpolator);
    ui->curve_editor.set_interpolator(&ui->keyframe_interpolator);

    // Wire shortcut manager
    ui->shortcut_mgr.set_command_registry(&ui->cmd_registry);
    ui->shortcut_mgr.register_defaults();
    ui->cmd_palette.set_command_registry(&ui->cmd_registry);
    ui->cmd_palette.set_shortcut_manager(&ui->shortcut_mgr);

    // Set the initial figure in the input handler
    Figure* fig = registry_->get(initial_figure_id);
    if (fig)
    {
        ui->input_handler.set_figure(fig);
        if (!fig->axes().empty() && fig->axes()[0])
        {
            ui->input_handler.set_active_axes(fig->axes()[0].get());
            auto& vp = fig->axes()[0]->viewport();
            ui->input_handler.set_viewport(vp.x, vp.y, vp.w, vp.h);
        }
    }

    // Initialize per-window active figure so command lambdas have a valid target.
    ui->per_window_active_figure    = fig;
    ui->per_window_active_figure_id = initial_figure_id;

    // Register standard commands (clipboard, view, file, etc.) for this window.
    // This is critical — without it, keyboard shortcuts (Ctrl+C/V/X, Delete, etc.)
    // have no command handlers and silently fail in secondary windows.
    {
        CommandBindings cb;
        cb.ui_ctx           = ui.get();
        cb.registry         = registry_;
        cb.active_figure    = &ui->per_window_active_figure;
        cb.active_figure_id = &ui->per_window_active_figure_id;
        cb.window_mgr       = this;
        register_standard_commands(cb);
    }

    SPECTRA_LOG_INFO("imgui", "Created ImGui context for window " + std::to_string(wctx.id));

    wctx.ui_ctx = std::move(ui);
#else
    (void)wctx;
    (void)initial_figure_id;
#endif

    return true;
}

// --- Full GLFW input callbacks for windows with UI ---

#ifdef SPECTRA_USE_GLFW

void WindowManager::glfw_cursor_pos_callback(GLFWwindow* window, double x, double y)
{
    auto* mgr = static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (!mgr)
        return;

    WindowContext* wctx = mgr->find_by_glfw_window(window);
    if (!wctx || !wctx->ui_ctx)
        return;

    #ifdef SPECTRA_USE_IMGUI
    auto& ui = *wctx->ui_ctx;

    // Switch to this window's ImGui context so input is routed correctly.
    // We passed install_callbacks=false during init, so we must forward
    // events to ImGui manually.
    ImGuiContext* prev_ctx = ImGui::GetCurrentContext();
    if (wctx->imgui_context)
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(wctx->imgui_context));
    ImGui_ImplGlfw_CursorPosCallback(window, x, y);

    auto& input_handler = ui.input_handler;
    auto& imgui_ui      = ui.imgui_ui;
    auto& dock_system   = ui.dock_system;

    bool input_is_dragging =
        input_handler.mode() == InteractionMode::Dragging || input_handler.is_measure_dragging()
        || input_handler.is_middle_pan_dragging() || input_handler.has_measure_result();

    if (!input_is_dragging && imgui_ui
        && (imgui_ui->wants_capture_mouse() || imgui_ui->is_tab_interacting()))
    {
        if (prev_ctx)
            ImGui::SetCurrentContext(prev_ctx);
        return;
    }

    if (dock_system.is_split())
    {
        SplitPane* root = dock_system.split_view().root();
        if (root)
        {
            SplitPane* pane = root->find_at_point(static_cast<float>(x), static_cast<float>(y));
            if (pane && pane->is_leaf())
            {
                FigureId fi = pane->figure_index();
                if (mgr->registry_)
                {
                    Figure* pfig = mgr->registry_->get(fi);
                    if (pfig)
                        input_handler.set_figure(pfig);
                }
            }
        }
    }

    input_handler.on_mouse_move(x, y);

    // Restore previous ImGui context
    if (prev_ctx)
        ImGui::SetCurrentContext(prev_ctx);
    #else
    (void)x;
    (void)y;
    #endif
}

void WindowManager::glfw_mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    auto* mgr = static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (!mgr)
        return;

    // Track mouse release for tab drag (callback-based).
    // This runs before the ui_ctx check so it catches events on preview windows too.
    // We suppress releases that arrive within the suppression window — the WM
    // temporarily grabs the pointer when a new GLFW window is created/mapped,
    // sending a real ButtonRelease to the source window.  That release is an
    // artifact, not the user lifting their finger.
    if (mgr->mouse_release_tracking_ && button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
    {
        auto now = std::chrono::steady_clock::now();
        if (now >= mgr->suppress_release_until_)
            mgr->mouse_release_seen_ = true;
    }

    WindowContext* wctx = mgr->find_by_glfw_window(window);
    if (!wctx || !wctx->ui_ctx)
        return;

    #ifdef SPECTRA_USE_IMGUI
    auto& ui = *wctx->ui_ctx;

    // Switch to this window's ImGui context for correct input routing
    ImGuiContext* prev_ctx = ImGui::GetCurrentContext();
    if (wctx->imgui_context)
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(wctx->imgui_context));
    ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);

    auto& input_handler = ui.input_handler;
    auto& imgui_ui      = ui.imgui_ui;
    auto& dock_system   = ui.dock_system;

    double x = 0.0, y = 0.0;
    glfwGetCursorPos(window, &x, &y);

    bool input_is_dragging = input_handler.mode() == InteractionMode::Dragging
                             || input_handler.is_measure_dragging()
                             || input_handler.is_middle_pan_dragging();

    if (!input_is_dragging && imgui_ui
        && (imgui_ui->wants_capture_mouse() || imgui_ui->is_tab_interacting()))
    {
        constexpr int GLFW_RELEASE_VAL = 0;
        if (action == GLFW_RELEASE_VAL)
            input_handler.on_mouse_button(button, action, mods, x, y);
        if (prev_ctx)
            ImGui::SetCurrentContext(prev_ctx);
        return;
    }

    if (dock_system.is_split())
    {
        SplitPane* root = dock_system.split_view().root();
        if (root)
        {
            SplitPane* pane = root->find_at_point(static_cast<float>(x), static_cast<float>(y));
            if (pane && pane->is_leaf())
            {
                FigureId fi = pane->figure_index();
                if (mgr->registry_)
                {
                    Figure* pfig = mgr->registry_->get(fi);
                    if (pfig)
                        input_handler.set_figure(pfig);
                }
            }
        }
    }

    input_handler.on_mouse_button(button, action, mods, x, y);

    // Restore previous ImGui context
    if (prev_ctx)
        ImGui::SetCurrentContext(prev_ctx);
    #else
    (void)button;
    (void)action;
    (void)mods;
    #endif
}

void WindowManager::glfw_scroll_callback(GLFWwindow* window, double x_offset, double y_offset)
{
    auto* mgr = static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (!mgr)
        return;

    WindowContext* wctx = mgr->find_by_glfw_window(window);
    if (!wctx || !wctx->ui_ctx)
        return;

    #ifdef SPECTRA_USE_IMGUI
    auto& ui = *wctx->ui_ctx;

    // Switch to this window's ImGui context for correct input routing
    ImGuiContext* prev_ctx = ImGui::GetCurrentContext();
    if (wctx->imgui_context)
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(wctx->imgui_context));
    ImGui_ImplGlfw_ScrollCallback(window, x_offset, y_offset);

    auto& input_handler = ui.input_handler;
    auto& imgui_ui      = ui.imgui_ui;
    auto& dock_system   = ui.dock_system;
    auto& cmd_palette   = ui.cmd_palette;

    if (cmd_palette.is_open())
    {
        if (prev_ctx)
            ImGui::SetCurrentContext(prev_ctx);
        return;
    }
    if (imgui_ui && imgui_ui->wants_capture_mouse())
    {
        if (prev_ctx)
            ImGui::SetCurrentContext(prev_ctx);
        return;
    }

    double cx = 0.0, cy = 0.0;
    glfwGetCursorPos(window, &cx, &cy);

    if (dock_system.is_split())
    {
        SplitPane* root = dock_system.split_view().root();
        if (root)
        {
            SplitPane* pane = root->find_at_point(static_cast<float>(cx), static_cast<float>(cy));
            if (pane && pane->is_leaf())
            {
                FigureId fi = pane->figure_index();
                if (mgr->registry_)
                {
                    Figure* pfig = mgr->registry_->get(fi);
                    if (pfig)
                        input_handler.set_figure(pfig);
                }
            }
        }
    }

    input_handler.on_scroll(x_offset, y_offset, cx, cy);

    // Restore previous ImGui context
    if (prev_ctx)
        ImGui::SetCurrentContext(prev_ctx);
    #else
    (void)x_offset;
    (void)y_offset;
    #endif
}

void WindowManager::glfw_key_callback(GLFWwindow* window,
                                      int         key,
                                      int         scancode,
                                      int         action,
                                      int         mods)
{
    auto* mgr = static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (!mgr)
        return;

    WindowContext* wctx = mgr->find_by_glfw_window(window);
    if (!wctx || !wctx->ui_ctx)
        return;

    #ifdef SPECTRA_USE_IMGUI
    auto& ui = *wctx->ui_ctx;

    // Switch to this window's ImGui context for correct input routing
    ImGuiContext* prev_ctx = ImGui::GetCurrentContext();
    if (wctx->imgui_context)
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(wctx->imgui_context));
    ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);

    auto& input_handler = ui.input_handler;
    auto& imgui_ui      = ui.imgui_ui;
    auto& shortcut_mgr  = ui.shortcut_mgr;

    // Always let the shortcut manager try modifier-key combos (Ctrl+C/V/X etc.)
    // and Delete, even when ImGui wants keyboard focus (e.g. inspector open).
    // This ensures clipboard shortcuts work regardless of panel state.
    constexpr int GLFW_PRESS_VAL_  = 1;
    constexpr int GLFW_MOD_CTRL_   = 0x0002;
    constexpr int GLFW_KEY_DELETE_ = 261;
    constexpr int GLFW_KEY_ESC_    = 256;
    bool          is_app_shortcut =
        (action == GLFW_PRESS_VAL_)
        && ((mods & GLFW_MOD_CTRL_) != 0 || key == GLFW_KEY_DELETE_ || key == GLFW_KEY_ESC_);
    if (is_app_shortcut && shortcut_mgr.on_key(key, action, mods))
    {
        if (prev_ctx)
            ImGui::SetCurrentContext(prev_ctx);
        return;
    }

    if (imgui_ui && imgui_ui->wants_capture_keyboard())
    {
        if (prev_ctx)
            ImGui::SetCurrentContext(prev_ctx);
        return;
    }
    if (shortcut_mgr.on_key(key, action, mods))
    {
        if (prev_ctx)
            ImGui::SetCurrentContext(prev_ctx);
        return;
    }

    // Q (no modifiers) = close active tab; if last tab, close window
    constexpr int GLFW_KEY_Q_VAL = 81;   // GLFW_KEY_Q
    constexpr int GLFW_PRESS_VAL = 1;    // GLFW_PRESS
    if (key == GLFW_KEY_Q_VAL && action == GLFW_PRESS_VAL && mods == 0)
    {
        auto* fm = ui.fig_mgr;
        if (fm)
        {
            fm->queue_close(fm->active_index());
            if (prev_ctx)
                ImGui::SetCurrentContext(prev_ctx);
            return;
        }
    }

    input_handler.on_key(key, action, mods);

    // Restore previous ImGui context
    if (prev_ctx)
        ImGui::SetCurrentContext(prev_ctx);
    #else
    (void)scancode;
    (void)key;
    (void)action;
    (void)mods;
    #endif
}

void WindowManager::glfw_char_callback(GLFWwindow* window, unsigned int codepoint)
{
    auto* mgr = static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (!mgr)
        return;

    WindowContext* wctx = mgr->find_by_glfw_window(window);
    if (!wctx || !wctx->ui_ctx)
        return;

    #ifdef SPECTRA_USE_IMGUI
    // Switch to this window's ImGui context and forward char event
    ImGuiContext* prev_ctx = ImGui::GetCurrentContext();
    if (wctx->imgui_context)
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(wctx->imgui_context));
    ImGui_ImplGlfw_CharCallback(window, codepoint);
    if (prev_ctx)
        ImGui::SetCurrentContext(prev_ctx);
    #else
    (void)codepoint;
    #endif
}

void WindowManager::glfw_cursor_enter_callback(GLFWwindow* window, int entered)
{
    auto* mgr = static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (!mgr)
        return;

    WindowContext* wctx = mgr->find_by_glfw_window(window);
    if (!wctx || !wctx->ui_ctx)
        return;

    #ifdef SPECTRA_USE_IMGUI
    // Switch to this window's ImGui context and forward cursor enter/leave
    ImGuiContext* prev_ctx = ImGui::GetCurrentContext();
    if (wctx->imgui_context)
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(wctx->imgui_context));
    ImGui_ImplGlfw_CursorEnterCallback(window, entered);
    if (prev_ctx)
        ImGui::SetCurrentContext(prev_ctx);
    #else
    (void)entered;
    #endif
}

#else

// Stubs when GLFW is not available
void WindowManager::glfw_cursor_pos_callback(GLFWwindow*, double, double) {}
void WindowManager::glfw_mouse_button_callback(GLFWwindow*, int, int, int) {}
void WindowManager::glfw_scroll_callback(GLFWwindow*, double, double) {}
void WindowManager::glfw_key_callback(GLFWwindow*, int, int, int, int) {}
void WindowManager::glfw_char_callback(GLFWwindow*, unsigned int) {}
void WindowManager::glfw_cursor_enter_callback(GLFWwindow*, int) {}

WindowContext* WindowManager::find_by_glfw_window(GLFWwindow*) const
{
    return nullptr;
}

#endif

}   // namespace spectra
