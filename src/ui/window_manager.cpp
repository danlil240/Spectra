#include "window_manager.hpp"

#include <spectra/logger.hpp>

#include "../render/vulkan/vk_backend.hpp"
#include "../render/vulkan/window_context.hpp"
#include "window_ui_context.hpp"

#ifdef SPECTRA_USE_GLFW
    #define GLFW_INCLUDE_NONE
    #define GLFW_INCLUDE_VULKAN
    #include <GLFW/glfw3.h>
#endif

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
    #include <imgui_impl_glfw.h>
    #include "figure_manager.hpp"
    #include "figure_registry.hpp"
    #include "imgui_integration.hpp"
    #include "tab_bar.hpp"
#endif

#include <algorithm>

namespace spectra
{

WindowManager::~WindowManager()
{
    shutdown();
}

void WindowManager::init(VulkanBackend* backend, FigureRegistry* registry,
                         Renderer* renderer)
{
    backend_ = backend;
    registry_ = registry;
    renderer_ = renderer;
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

    // Window close policy: destroy all figures assigned to this secondary window.
    // Only unregister figures that are NOT assigned to any other window.
    if (registry_ && !wctx.assigned_figures.empty())
    {
        for (FigureId fig_id : wctx.assigned_figures)
        {
            // Check if any other window still references this figure
            bool used_elsewhere = false;
            auto& primary = backend_->primary_window();
            if (&primary != &wctx)
            {
                if (std::find(primary.assigned_figures.begin(),
                              primary.assigned_figures.end(), fig_id)
                    != primary.assigned_figures.end())
                {
                    used_elsewhere = true;
                }
            }
            if (!used_elsewhere)
            {
                for (auto& other : windows_)
                {
                    if (other.get() == &wctx)
                        continue;
                    if (std::find(other->assigned_figures.begin(),
                                  other->assigned_figures.end(), fig_id)
                        != other->assigned_figures.end())
                    {
                        used_elsewhere = true;
                        break;
                    }
                }
            }
            if (!used_elsewhere)
            {
                registry_->unregister_figure(fig_id);
                SPECTRA_LOG_INFO("window_manager",
                                 "Destroyed figure " + std::to_string(fig_id)
                                     + " (window " + std::to_string(window_id) + " closed)");
            }
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
            backend_->set_active_window(prev_active);
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

    // Wait for all GPU work to complete before destroying any window resources.
    if (backend_->device() != VK_NULL_HANDLE)
        vkDeviceWaitIdle(backend_->device());

    // Destroy all secondary windows (reverse order)
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
#ifdef SPECTRA_USE_IMGUI
            // Forward focus event to ImGui for this window's context
            if (w->imgui_context && w->ui_ctx)
            {
                ImGuiContext* prev_ctx = ImGui::GetCurrentContext();
                ImGui::SetCurrentContext(static_cast<ImGuiContext*>(w->imgui_context));
                ImGui_ImplGlfw_WindowFocusCallback(window, focused);
                if (prev_ctx)
                    ImGui::SetCurrentContext(prev_ctx);
            }
#endif
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
            wctx->assigned_figures = {figure_id};
            wctx->active_figure_id = figure_id;
            set_window_position(*wctx, screen_x, screen_y);
        }
    }

    if (!wctx)
    {
        SPECTRA_LOG_ERROR("window_manager",
                          "detach_figure: failed to create window for figure "
                              + std::to_string(figure_id));
        return nullptr;
    }

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

    // Verify the source window has this figure in its assigned_figures list
    auto& src_figs = from_wctx->assigned_figures;
    auto src_it = std::find(src_figs.begin(), src_figs.end(), figure_id);
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
        from_wctx->active_figure_id =
            src_figs.empty() ? INVALID_FIGURE_ID : src_figs.front();
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
    to_wctx->active_figure_id = figure_id;
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

        // Add to target FigureManager with the transferred state
        if (to_wctx->ui_ctx && to_wctx->ui_ctx->fig_mgr)
        {
            to_wctx->ui_ctx->fig_mgr->add_figure(figure_id, std::move(transferred_state));
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
    }
#endif

    SPECTRA_LOG_INFO("window_manager",
                     "Moved figure " + std::to_string(figure_id) + " from window "
                         + std::to_string(from_window_id) + " to window "
                         + std::to_string(to_window_id));
    return true;
}

// --- find_by_glfw_window ---

WindowContext* WindowManager::find_by_glfw_window(GLFWwindow* window) const
{
#ifdef SPECTRA_USE_GLFW
    if (!backend_)
        return nullptr;

    auto& primary = backend_->primary_window();
    if (primary.glfw_window == window)
        return &const_cast<WindowContext&>(primary);

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

// --- create_window_with_ui ---

WindowContext* WindowManager::create_window_with_ui(uint32_t width, uint32_t height,
                                                     const std::string& title,
                                                     FigureId initial_figure_id,
                                                     int screen_x, int screen_y)
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
    wctx->assigned_figures = {initial_figure_id};
    wctx->active_figure_id = initial_figure_id;
    wctx->title = title;

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
        SPECTRA_LOG_ERROR("window_manager",
                          "create_window_with_ui: UI init failed for window "
                              + std::to_string(wctx->id));
        // Window still usable as a bare render window — don't destroy it
    }

    SPECTRA_LOG_INFO("window_manager",
                     "Created window with UI " + std::to_string(wctx->id) + ": "
                         + std::to_string(width) + "x" + std::to_string(height)
                         + " \"" + title + "\" figure=" + std::to_string(initial_figure_id));
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
    ui->fig_mgr = ui->fig_mgr_owned.get();
    {
        auto all = ui->fig_mgr->figure_ids();  // copy — we'll mutate
        for (auto id : all)
        {
            if (id != initial_figure_id)
                ui->fig_mgr->remove_figure(id);
        }
    }

    // Create per-window TabBar
    ui->figure_tabs = std::make_unique<TabBar>();
    ui->fig_mgr->set_tab_bar(ui->figure_tabs.get());

    // Wire TabBar callbacks → FigureManager + DockSystem
    auto* fig_mgr_ptr = ui->fig_mgr;
    auto* dock_ptr = &ui->dock_system;
    auto* guard_ptr = &ui->dock_tab_sync_guard;

    ui->figure_tabs->set_tab_change_callback(
        [fig_mgr_ptr, dock_ptr, guard_ptr](size_t new_index)
        {
            if (*guard_ptr)
                return;
            *guard_ptr = true;
            fig_mgr_ptr->queue_switch(new_index);
            dock_ptr->set_active_figure_index(new_index);
            *guard_ptr = false;
        });
    ui->figure_tabs->set_tab_close_callback(
        [fig_mgr_ptr](size_t index) { fig_mgr_ptr->queue_close(index); });
    ui->figure_tabs->set_tab_add_callback(
        [fig_mgr_ptr]() { fig_mgr_ptr->queue_create(); });
    ui->figure_tabs->set_tab_duplicate_callback(
        [fig_mgr_ptr](size_t index) { fig_mgr_ptr->duplicate_figure(index); });
    ui->figure_tabs->set_tab_close_all_except_callback(
        [fig_mgr_ptr](size_t index) { fig_mgr_ptr->close_all_except(index); });
    ui->figure_tabs->set_tab_close_to_right_callback(
        [fig_mgr_ptr](size_t index) { fig_mgr_ptr->close_to_right(index); });
    ui->figure_tabs->set_tab_rename_callback(
        [fig_mgr_ptr](size_t index, const std::string& t)
        { fig_mgr_ptr->set_title(index, t); });

    // Tab drag-to-dock callbacks
    ui->figure_tabs->set_tab_drag_out_callback(
        [dock_ptr](size_t index, float mx, float my)
        { dock_ptr->begin_drag(index, mx, my); });
    ui->figure_tabs->set_tab_drag_update_callback(
        [dock_ptr](size_t /*index*/, float mx, float my)
        { dock_ptr->update_drag(mx, my); });
    ui->figure_tabs->set_tab_drag_end_callback(
        [dock_ptr](size_t /*index*/, float mx, float my)
        { dock_ptr->end_drag(mx, my); });
    ui->figure_tabs->set_tab_drag_cancel_callback(
        [dock_ptr](size_t /*index*/) { dock_ptr->cancel_drag(); });

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
            SPECTRA_LOG_ERROR("window_manager",
                              "init_window_ui: ImGui init failed for window "
                                  + std::to_string(wctx.id));
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
    ui->input_handler.set_axis_link_manager(&ui->axis_link_mgr);

    // Wire series click-to-select
    auto* imgui_raw = ui->imgui_ui.get();
    ui->data_interaction->set_on_series_selected(
        [imgui_raw](Figure* fig, Axes* ax, int ax_idx, Series* s, int s_idx)
        {
            if (imgui_raw)
                imgui_raw->select_series(fig, ax, ax_idx, s, s_idx);
        });
    ui->data_interaction->set_axis_link_manager(&ui->axis_link_mgr);

    // Wire pane tab context menu callbacks
    ui->imgui_ui->set_pane_tab_duplicate_cb(
        [fig_mgr_ptr](FigureId index) { fig_mgr_ptr->duplicate_figure(index); });
    ui->imgui_ui->set_pane_tab_close_cb(
        [fig_mgr_ptr](FigureId index) { fig_mgr_ptr->queue_close(index); });
    ui->imgui_ui->set_pane_tab_split_right_cb(
        [dock_ptr, fig_mgr_ptr](FigureId index)
        {
            FigureId new_fig = fig_mgr_ptr->duplicate_figure(index);
            if (new_fig == INVALID_FIGURE_ID)
                return;
            dock_ptr->split_figure_right(index, new_fig);
            dock_ptr->set_active_figure_index(index);
        });
    ui->imgui_ui->set_pane_tab_split_down_cb(
        [dock_ptr, fig_mgr_ptr](FigureId index)
        {
            FigureId new_fig = fig_mgr_ptr->duplicate_figure(index);
            if (new_fig == INVALID_FIGURE_ID)
                return;
            dock_ptr->split_figure_down(index, new_fig);
            dock_ptr->set_active_figure_index(index);
        });
    ui->imgui_ui->set_pane_tab_rename_cb(
        [fig_mgr_ptr](size_t index, const std::string& t)
        { fig_mgr_ptr->set_title(index, t); });

    // Figure title lookup
    auto* tabs_raw = ui->figure_tabs.get();
    ui->imgui_ui->set_figure_title_callback(
        [tabs_raw](size_t fig_idx) -> std::string
        {
            if (tabs_raw && fig_idx < tabs_raw->get_tab_count())
                return tabs_raw->get_tab_title(fig_idx);
            return "Figure " + std::to_string(fig_idx + 1);
        });

    // Dock system → tab bar sync
    auto* figure_tabs_raw = ui->figure_tabs.get();
    ui->dock_system.split_view().set_on_active_changed(
        [figure_tabs_raw, fig_mgr_ptr, guard_ptr](size_t figure_index)
        {
            if (*guard_ptr)
                return;
            *guard_ptr = true;
            if (figure_tabs_raw && figure_index < figure_tabs_raw->get_tab_count())
                figure_tabs_raw->set_active_tab(figure_index);
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

    SPECTRA_LOG_INFO("imgui",
                     "Created ImGui context for window " + std::to_string(wctx.id));

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
    auto& imgui_ui = ui.imgui_ui;
    auto& dock_system = ui.dock_system;

    bool input_is_dragging =
        input_handler.mode() == InteractionMode::Dragging
        || input_handler.is_measure_dragging()
        || input_handler.is_middle_pan_dragging()
        || input_handler.has_measure_result();

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
    auto& imgui_ui = ui.imgui_ui;
    auto& dock_system = ui.dock_system;

    double x = 0.0, y = 0.0;
    glfwGetCursorPos(window, &x, &y);

    bool input_is_dragging =
        input_handler.mode() == InteractionMode::Dragging
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
    auto& imgui_ui = ui.imgui_ui;
    auto& dock_system = ui.dock_system;
    auto& cmd_palette = ui.cmd_palette;

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

void WindowManager::glfw_key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
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
    auto& imgui_ui = ui.imgui_ui;
    auto& shortcut_mgr = ui.shortcut_mgr;

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

WindowContext* WindowManager::find_by_glfw_window(GLFWwindow*) const { return nullptr; }

#endif

}  // namespace spectra
