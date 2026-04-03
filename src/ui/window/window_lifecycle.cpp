// Window lifecycle: creation, destruction, shutdown, UI initialization.
// Split from window_manager.cpp — see QW-5 in ARCHITECTURE_REVIEW_V2.md.

#include "window_manager.hpp"

#include <spectra/event_bus.hpp>
#include <spectra/logger.hpp>

#include "render/renderer.hpp"
#include "render/vulkan/vk_backend.hpp"
#include "render/vulkan/window_context.hpp"
#include "ui/app/window_ui_context.hpp"
#include "ui/theme/theme.hpp"
#include "io/export_registry.hpp"
#include "ui/workspace/plugin_api.hpp"

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
    #include <spectra/figure_registry.hpp>
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

void WindowManager::init(VulkanBackend*    backend,
                         FigureRegistry*   registry,
                         Renderer*         renderer,
                         ui::ThemeManager* theme_mgr)
{
    backend_   = backend;
    registry_  = registry;
    renderer_  = renderer;
    theme_mgr_ = theme_mgr;
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

    wctx->id            = next_window_id_++;
    wctx->native_window = glfw_window;
    wctx->glfw_window   = glfw_window;
    wctx->is_focused    = true;

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
    if (event_system_)
        event_system_->window_opened().emit({ptr->id});
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
    auto wctx           = std::make_unique<WindowContext>();
    wctx->id            = next_window_id_++;
    wctx->native_window = glfw_win;
    wctx->glfw_window   = glfw_win;

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
    glfwSetDropCallback(glfw_win, glfw_drop_callback);

    WindowContext* ptr = wctx.get();
    windows_.push_back(std::move(wctx));
    rebuild_active_list();

    SPECTRA_LOG_INFO("window_manager",
                     "Created window " + std::to_string(ptr->id) + ": " + std::to_string(width)
                         + "x" + std::to_string(height) + " \"" + title + "\"");
    if (event_system_)
        event_system_->window_opened().emit({ptr->id});
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
        wctx.native_window = nullptr;
        wctx.glfw_window   = nullptr;
    }
#endif

    SPECTRA_LOG_INFO("window_manager", "Destroyed window " + std::to_string(window_id));

    if (event_system_)
        event_system_->window_closed().emit({window_id});

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
            wctx.native_window = nullptr;
            wctx.glfw_window   = nullptr;
        }
#endif

        windows_.pop_back();
    }

    active_ptrs_.clear();
    pending_close_ids_.clear();

    // Destroy pooled preview window if it exists
    if (pooled_preview_)
    {
        if (pooled_preview_->ui_ctx)
        {
#ifdef SPECTRA_USE_IMGUI
            if (pooled_preview_->ui_ctx->imgui_ui)
            {
                auto* prev_active = backend_->active_window();
                backend_->set_active_window(pooled_preview_.get());
                pooled_preview_->ui_ctx->imgui_ui->shutdown();
                backend_->set_active_window(prev_active);
            }
            pooled_preview_->imgui_context = nullptr;
#endif
            pooled_preview_->ui_ctx.reset();
        }
        backend_->destroy_window_context(*pooled_preview_);
#ifdef SPECTRA_USE_GLFW
        if (pooled_preview_->glfw_window)
        {
            glfwDestroyWindow(static_cast<GLFWwindow*>(pooled_preview_->glfw_window));
            pooled_preview_->native_window = nullptr;
            pooled_preview_->glfw_window   = nullptr;
        }
#endif
        pooled_preview_.reset();
    }

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

// ── Panel windows ───────────────────────────────────────────────────────────

WindowContext* WindowManager::create_panel_window(uint32_t              width,
                                                  uint32_t              height,
                                                  const std::string&    title,
                                                  std::function<void()> draw_callback,
                                                  int                   screen_x,
                                                  int                   screen_y)
{
    if (!backend_ || backend_->is_headless())
        return nullptr;

#ifdef SPECTRA_USE_GLFW
    // Create an undecorated, resizable window with custom title bar.
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    auto* wctx = create_window(width, height, title);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);   // Reset for future windows
    if (!wctx)
        return nullptr;

    set_window_position(*wctx, screen_x, screen_y);
    wctx->title               = title;
    wctx->is_panel            = true;
    wctx->panel_draw_callback = std::move(draw_callback);

    // Install full input callbacks (cursor, mouse, key, scroll).
    auto* glfw_win = static_cast<GLFWwindow*>(wctx->glfw_window);
    if (glfw_win)
    {
        glfwSetCursorPosCallback(glfw_win, glfw_cursor_pos_callback);
        glfwSetMouseButtonCallback(glfw_win, glfw_mouse_button_callback);
        glfwSetScrollCallback(glfw_win, glfw_scroll_callback);
        glfwSetKeyCallback(glfw_win, glfw_key_callback);
        glfwSetCharCallback(glfw_win, glfw_char_callback);
        glfwSetCursorEnterCallback(glfw_win, glfw_cursor_enter_callback);
        glfwSetDropCallback(glfw_win, glfw_drop_callback);
    }

    // Minimal UI context: ImGuiIntegration only (no FigureManager/DockSystem).
    auto ui       = std::make_unique<WindowUIContext>();
    ui->theme_mgr = theme_mgr_;
    ui->imgui_ui  = std::make_unique<ImGuiIntegration>();
    ui->imgui_ui->set_theme_manager(theme_mgr_);

    ImGuiContext* prev_imgui_ctx = ImGui::GetCurrentContext();
    auto*         prev_active    = backend_->active_window();
    backend_->set_active_window(wctx);

    if (!ui->imgui_ui->init(*backend_, glfw_win, /*install_callbacks=*/false))
    {
        SPECTRA_LOG_ERROR(
            "window_manager",
            "create_panel_window: ImGui init failed for window " + std::to_string(wctx->id));
        backend_->set_active_window(prev_active);
        ImGui::SetCurrentContext(prev_imgui_ctx);
        return nullptr;
    }

    wctx->imgui_context = ImGui::GetCurrentContext();
    backend_->set_active_window(prev_active);
    ImGui::SetCurrentContext(prev_imgui_ctx);

    wctx->ui_ctx = std::move(ui);

    SPECTRA_LOG_INFO("window_manager",
                     "Created panel window " + std::to_string(wctx->id) + ": "
                         + std::to_string(width) + "x" + std::to_string(height) + " \"" + title
                         + "\"");
    return wctx;
#else
    (void)width;
    (void)height;
    (void)title;
    (void)draw_callback;
    (void)screen_x;
    (void)screen_y;
    return nullptr;
#endif
}

void WindowManager::destroy_panel_window(uint32_t window_id)
{
    destroy_window(window_id);
}

// ── First window with UI ────────────────────────────────────────────────────

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

    wctx_ptr->id            = next_window_id_++;
    wctx_ptr->native_window = glfw_window;
    wctx_ptr->glfw_window   = glfw_window;
    wctx_ptr->is_focused    = true;

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

// ── Secondary window with UI ────────────────────────────────────────────────

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
        glfwSetDropCallback(glfw_win, glfw_drop_callback);
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

// ── init_window_ui ──────────────────────────────────────────────────────────

bool WindowManager::init_window_ui(WindowContext& wctx, FigureId initial_figure_id)
{
    if (!registry_)
        return false;

#ifdef SPECTRA_USE_IMGUI
    auto ui       = std::make_unique<WindowUIContext>();
    ui->theme_mgr = theme_mgr_;

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
    ui->fig_mgr->set_on_figure_closed(
        [wm_self, reg = registry_](FigureId id)
        {
            if (!wm_self || !reg)
                return;
            if (auto* fig = reg->get(id))
                wm_self->clear_figure_caches(fig);
        });

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
    ui->imgui_ui->set_theme_manager(theme_mgr_);

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

    // Shared plugin services.
    ui->overlay_registry = &shared_overlay_registry_;
    ui->plugin_manager   = plugin_manager_;

    ui->imgui_ui->set_overlay_registry(ui->overlay_registry);
    ui->imgui_ui->set_plugin_manager(ui->plugin_manager);
    ui->imgui_ui->set_export_format_registry(export_format_registry_);
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
    ui->data_interaction->set_theme_manager(theme_mgr_);
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
    // Wire rectangle multi-select (Select tool drag)
    ui->data_interaction->set_on_rect_series_selected(
        [imgui_raw](const std::vector<DataInteraction::RectSelectedEntry>& entries)
        {
            if (imgui_raw)
                imgui_raw->select_series_in_rect(entries);
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
        [figure_tabs_raw, fig_mgr_ptr, guard_ptr](FigureId figure_index)
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

}   // namespace spectra
