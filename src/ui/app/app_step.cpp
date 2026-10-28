// app_step.cpp — Frame-by-frame control API for App.
// Extracts init / step / shutdown from app_inproc.cpp so external
// drivers (QA agent, test harnesses) can pump frames individually.

#include <spectra/animator.hpp>
#include <spectra/app.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/frame.hpp>
#include <spectra/logger.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

#include "anim/frame_scheduler.hpp"
#include "adapters/data_source_registry.hpp"
#include "io/export_registry.hpp"
#include "math/data_transform.hpp"
#include "render/renderer.hpp"
#include "render/series_type_registry.hpp"
#include "render/vulkan/vk_backend.hpp"
#include "render/vulkan/window_context.hpp"
#ifdef SPECTRA_USE_WEBGPU
    #include "render/webgpu/wgpu_backend.hpp"
#endif
#include "ui/commands/command_queue.hpp"
#include "session_runtime.hpp"
#include "window_runtime.hpp"
#include "window_ui_context_runtime.hpp"
#include "window_manager_bootstrap.hpp"
#include "window_ui_context_builder.hpp"
#include "window_ui_context.hpp"
#include "window_png_export.hpp"
#include "ui/workspace/plugin_api.hpp"
#include "ui/settings/settings_store.hpp"

#ifdef SPECTRA_USE_GLFW
    #define GLFW_INCLUDE_NONE
    #define GLFW_INCLUDE_VULKAN
    #include <GLFW/glfw3.h>

    #include "ui/window/glfw_adapter.hpp"
#endif

#ifdef SPECTRA_USE_SDL3
    #include <SDL3/SDL.h>
    #include "ui/window/sdl3_adapter.hpp"
#endif

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    #include "ui/window/window_manager.hpp"
#endif

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>

    #include "register_commands.hpp"
    #include "ui/theme/theme.hpp"
    #include "ui/workspace/workspace.hpp"
#endif

#include "ui/automation/automation_security.hpp"
#include "ui/automation/automation_server.hpp"
#include "ui/automation/mcp_server.hpp"

#ifndef _WIN32
    #include "../../app/inproc_topic_server.hpp"
#endif

#include "perf_metrics.hpp"

#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <memory>

namespace spectra
{

// ─── App ctor/dtor (must be here where AppRuntime is complete) ───────────────
App::App(const AppConfig& config) : config_(config)
{
    // Install the default console/file sinks unless the host application has
    // already configured logging (its own sinks or setup_dual_logging()).
    spectra::ensure_default_logging();

    SPECTRA_LOG_INFO("app",
                     "Initializing Spectra application (headless: "
                         + std::string(config_.headless ? "true" : "false") + ")");

    bool multiproc = !config_.socket_path.empty();
    if (!multiproc)
    {
        const char* env = std::getenv("SPECTRA_SOCKET");
        multiproc       = ((env != nullptr) && env[0] != '\0');
    }
    SPECTRA_LOG_INFO("app", "Runtime mode: " + std::string(multiproc ? "multiproc" : "inproc"));

#ifdef SPECTRA_USE_SDL3
    // SDL must be initialized before VulkanBackend::init() because
    // SDL_Vulkan_GetInstanceExtensions() is called during Vulkan instance
    // creation and requires the SDL video subsystem to be running.
    if (!config_.headless)
    {
        if (!SDL_Init(SDL_INIT_VIDEO))
        {
            SPECTRA_LOG_ERROR("app",
                              std::string("Failed to pre-initialize SDL video: ") + SDL_GetError());
            return;
        }
    }
#endif

    backend_ = std::make_unique<VulkanBackend>();
#ifdef SPECTRA_USE_WEBGPU
    if (config_.backend == RenderBackend::WebGPU)
        backend_ = std::make_unique<WebGPUBackend>();
#endif
    if (!backend_->init(config_.headless))
    {
        SPECTRA_LOG_ERROR("app",
                          "Failed to initialize Vulkan backend (no usable Vulkan driver/device?). "
                          "Nothing will be rendered or exported; check App::ok().");
#ifdef SPECTRA_USE_GLFW
        // Vulkan instance creation initializes GLFW to query the required surface
        // extensions. No window will ever be created now, so release the display
        // connection instead of leaking it for the lifetime of the process.
        if (!config_.headless)
            glfwTerminate();
#endif
        return;
    }

    // Create the App-owned ThemeManager and register it as the active instance
    // so ThemeManager::instance() returns this object instead of the fallback
    // singleton for the lifetime of this App.
    theme_mgr_ = std::make_unique<ui::ThemeManager>();
    ui::ThemeManager::set_current(theme_mgr_.get());
    theme_mgr_->ensure_initialized();

    renderer_ = std::make_unique<Renderer>(*backend_, *theme_mgr_);
    if (!renderer_->init())
    {
        SPECTRA_LOG_ERROR("app", "Failed to initialize renderer");
        return;
    }

    initialized_ = true;
    SPECTRA_LOG_INFO("app", "Spectra application initialized successfully");
}

App::~App()
{
    // Guard each destruction step with try-catch.  On CI with lavapipe
    // (software Vulkan), non-deterministic std::system_error("Invalid
    // argument") can be thrown from deep inside member destructors
    // (likely from pthread operations in the software driver).  Since
    // destructors are noexcept, an uncaught throw triggers std::terminate.
    try
    {
        shutdown_runtime();
    }
    catch (const std::exception& e)
    {
        SPECTRA_LOG_ERROR("shutdown", std::string("Exception in shutdown_runtime: ") + e.what());
    }
    catch (...)
    {
    }

    try
    {
        runtime_.reset();
    }
    catch (...)
    {
    }

    try
    {
        renderer_.reset();
    }
    catch (const std::exception& e)
    {
        SPECTRA_LOG_ERROR("shutdown", std::string("Exception in ~Renderer: ") + e.what());
    }
    catch (...)
    {
    }

    // Clear the singleton pointer before the ThemeManager member is destroyed
    // to prevent any remaining call sites from dereferencing a dangling pointer.
    ui::ThemeManager::set_current(nullptr);
    theme_mgr_.reset();
    if (backend_)
    {
        try
        {
            backend_->shutdown();
        }
        catch (...)
        {
        }
    }

#ifdef SPECTRA_USE_SDL3
    // Balance the pre-backend SDL_Init(SDL_INIT_VIDEO) call made before
    // VulkanBackend::init().  SdlAdapter::shutdown() already decremented the
    // SDL subsystem reference count once (via SDL_Quit); this call brings
    // the count to zero and fully releases the video subsystem.
    if (!config_.headless && SDL_WasInit(SDL_INIT_VIDEO))
        SDL_Quit();
#endif
}

// ─── AppRuntime: all state that lives across frames ──────────────────────────
struct App::AppRuntime
{
    AppRuntime(const AppRuntime&)            = delete;
    AppRuntime& operator=(const AppRuntime&) = delete;

    CommandQueue   cmd_queue;
    FrameScheduler scheduler;
    Animator       animator;
    SessionRuntime session;

    PluginManager        plugin_manager;
    ExportFormatRegistry export_format_registry;
    DataSourceRegistry   data_source_registry;
    SeriesTypeRegistry   series_type_registry;

    std::unique_ptr<ui::settings::SettingsStore> settings_store;

    FrameState frame_state;
    uint64_t   frame_number = 0;

    WindowUIContext*                 ui_ctx_ptr = nullptr;
    std::unique_ptr<WindowUIContext> headless_ui_ctx;

    Figure*  active_figure    = nullptr;
    FigureId active_figure_id = INVALID_FIGURE_ID;

#ifdef SPECTRA_USE_GLFW
    std::unique_ptr<GlfwAdapter>   glfw;
    std::unique_ptr<WindowManager> window_mgr;
#endif

#ifdef SPECTRA_USE_SDL3
    std::unique_ptr<Sdl3Adapter>   sdl3;
    std::unique_ptr<WindowManager> window_mgr;
#endif

#ifdef SPECTRA_USE_FFMPEG
    std::unique_ptr<VideoExporter> video_exporter;
    std::vector<uint8_t>           video_frame_pixels;
    bool                           is_recording         = false;
    uint64_t                       video_frames_written = 0;
#endif

    // Wall-clock for frame timing
    std::chrono::steady_clock::time_point last_step_time;

    std::unique_ptr<AutomationServer> auto_server;
    std::unique_ptr<McpServer>        mcp_server;

#ifndef _WIN32
    std::unique_ptr<InprocTopicServer> topic_server;
    // Window whose TopicsPanel is wired to topic_server (only one at a time).
    // When that window closes, the panel is rewired to a surviving window.
    bool     topics_panel_wired     = false;
    uint32_t topics_panel_window_id = 0;
#endif

    // Pending windowed PNG / clipboard capture (pre-present readback whose
    // size is resolved from the frame that is actually captured).
    WindowPngExport png_export;

    AppRuntime(float fps, Backend& backend, Renderer& renderer, FigureRegistry& registry)
        : scheduler(fps), session(backend, renderer, registry)
    {
    }

    // Explicit noexcept(false) destructor: the implicit default destructor
    // is noexcept, which means any exception thrown during member destruction
    // triggers std::terminate.  On CI with lavapipe (software Vulkan),
    // non-deterministic std::system_error("Invalid argument") can occur
    // during member cleanup.  Making the destructor noexcept(false) lets
    // the exception propagate to callers that have try-catch guards.
    ~AppRuntime() noexcept(false) = default;
};

#if !defined(_WIN32) && (defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3))
// Keep exactly one open window's TopicsPanel wired to the topic server.  The
// panel unwires itself when its window is destroyed; this wires the panel of
// the first surviving regular window instead.  Main thread only.
static void rewire_topics_panel_if_needed(InprocTopicServer& server,
                                          WindowManager&     window_mgr,
                                          FigureRegistry&    registry,
                                          bool&              wired,
                                          uint32_t&          wired_window_id)
{
    WindowContext* candidate = nullptr;
    for (auto* wctx : window_mgr.windows())
    {
        if (!wctx || !wctx->ui_ctx || wctx->should_close || wctx->is_preview
            || wctx->panel_draw_callback)
            continue;
        if (wired && wctx->id == wired_window_id)
            return;   // still wired to an open window
        if (!candidate)
            candidate = wctx;
    }

    wired = false;
    if (!candidate)
        return;

    server.wire_topics_panel(candidate->ui_ctx->topics_panel, &registry);
    wired           = true;
    wired_window_id = candidate->id;
}
#endif

// ─── init_runtime ────────────────────────────────────────────────────────────
void App::init_runtime()
{
    PerfMetrics::instance().mark_startup_begin();

    if (!initialized_ || !backend_ || !renderer_)
    {
        SPECTRA_LOG_ERROR("app", "Cannot run: backend or renderer not initialized");
        return;
    }

    auto       all_ids       = registry_.all_ids();
    auto       window_groups = compute_window_groups();
    const bool empty_start   = all_ids.empty();

    float    init_fps       = 60.0f;
    Figure*  init_active    = nullptr;
    FigureId init_active_id = INVALID_FIGURE_ID;

    if (!empty_start)
    {
        init_active_id = all_ids[0];
        init_active    = registry_.get(init_active_id);
        if (init_active && init_active->anim_.fps > 0.0f)
            init_fps = init_active->anim_.fps;
    }

    uint32_t init_w = init_active ? init_active->width() : DEFAULT_WINDOW_WIDTH;
    uint32_t init_h = init_active ? init_active->height() : DEFAULT_WINDOW_HEIGHT;

    runtime_ = std::make_unique<AppRuntime>(init_fps, *backend_, *renderer_, registry_);
    auto& rt = *runtime_;

    // Headless apps ignore ~/.config/spectra/settings.json unless asked
    // (AppConfig::use_user_settings) so exports look the same on every machine;
    // explicit AppConfig::theme / data_palette override either source.
    rt.settings_store = std::make_unique<ui::settings::SettingsStore>();
    const bool read_user =
        ui::settings::reads_user_settings(config_.headless, config_.use_user_settings);
    if (read_user)
    {
        rt.settings_store->load();
        rt.settings_store->set_on_change([&store = *rt.settings_store]() { store.save(); });
    }
    {
        const auto startup =
            ui::settings::resolve_startup_theme(read_user ? &rt.settings_store->data() : nullptr,
                                                config_.theme,
                                                config_.data_palette);
        theme_mgr_->set_theme(startup.theme);
        if (theme_mgr_->current_theme_name() != startup.theme)
            SPECTRA_LOG_WARN("app",
                             "Unknown theme '{}', using '{}'",
                             startup.theme,
                             theme_mgr_->current_theme_name());
        theme_mgr_->set_data_palette(startup.data_palette);
        if (theme_mgr_->current_data_palette_name() != startup.data_palette)
            SPECTRA_LOG_WARN("app",
                             "Unknown data palette '{}', using '{}'",
                             startup.data_palette,
                             theme_mgr_->current_data_palette_name());
        // UI scale is a per-user preference: never applied to headless exports.
        if (read_user)
            theme_mgr_->set_ui_scale(rt.settings_store->data().ui_scale);
    }

    rt.frame_state.active_figure_id = init_active_id;
    rt.frame_state.active_figure    = init_active;
    rt.active_figure                = init_active;
    rt.active_figure_id             = init_active_id;

    rt.frame_state.has_animation = init_active ? init_active->has_animation() : false;

    if (!config_.headless)
    {
        rt.scheduler.set_mode(FrameScheduler::Mode::VSync);
    }

#ifdef SPECTRA_USE_FFMPEG
    rt.is_recording = (init_active != nullptr) && !init_active->export_req_.video_path.empty();
#else
    if (init_active && !init_active->export_req_.video_path.empty())
    {
        SPECTRA_LOG_WARN("app", "Video recording requested but SPECTRA_USE_FFMPEG is not enabled");
    }
#endif

#ifdef SPECTRA_USE_FFMPEG
    if (rt.is_recording)
    {
        VideoExporter::Config vcfg;
        vcfg.output_path  = init_active->export_req_.video_path;
        vcfg.width        = init_active->width();
        vcfg.height       = init_active->height();
        vcfg.fps          = init_active->anim_.fps;
        rt.video_exporter = std::make_unique<VideoExporter>(vcfg);
        // Offline recording: drive animation time from the frame counter so the
        // file has exactly duration * fps frames regardless of render speed.
        rt.scheduler.set_target_fps(init_active->anim_.fps);
        rt.scheduler.set_mode(FrameScheduler::Mode::Offline);
        if (!rt.video_exporter->is_open())
        {
            SPECTRA_LOG_ERROR("app",
                              "Failed to open video exporter for: {}",
                              init_active->export_req_.video_path);
            rt.video_exporter.reset();
        }
        else
        {
            rt.video_frame_pixels.resize(static_cast<size_t>(init_active->width())
                                         * init_active->height() * 4);
        }
        if (!config_.headless)
        {
            config_.headless = true;
        }
    }
#endif

#ifdef SPECTRA_USE_GLFW
    if (!config_.headless)
    {
        rt.glfw = std::make_unique<GlfwAdapter>();
        if (!rt.glfw->init(init_w, init_h, "Spectra"))
        {
            SPECTRA_LOG_ERROR("app", "Failed to create GLFW window");
            rt.glfw.reset();
        }
        else
        {
            backend_->create_surface(rt.glfw->native_window());
            backend_->create_swapchain(init_w, init_h);

            // WindowManager requires VulkanBackend — skip for other backends.
            if (config_.backend == RenderBackend::Vulkan)
            {
                WindowManagerBootstrapOptions wm_opts;
                wm_opts.backend                = static_cast<VulkanBackend*>(backend_.get());
                wm_opts.registry               = &registry_;
                wm_opts.renderer               = renderer_.get();
                wm_opts.theme_mgr              = theme_mgr_.get();
                wm_opts.session                = &rt.session;
                wm_opts.settings_store         = rt.settings_store.get();
                wm_opts.plugin_manager         = &rt.plugin_manager;
                wm_opts.export_format_registry = &rt.export_format_registry;
                rt.window_mgr                  = create_configured_window_manager(wm_opts);
                rt.window_mgr->set_interactive_frame_handler(
                    [&rt]()
                    {
                        rt.session.pump_interactive_frame(rt.scheduler,
                                                          rt.animator,
                                                          rt.window_mgr.get(),
                                                          rt.frame_state);
                    });

                std::vector<FigureId> first_group =
                    window_groups.empty() ? std::vector<FigureId>{} : window_groups[0];
                auto* initial_wctx =
                    rt.window_mgr->create_first_window_with_ui(rt.glfw->native_window(),
                                                               first_group);

                if (initial_wctx && initial_wctx->ui_ctx)
                {
                    rt.ui_ctx_ptr              = initial_wctx->ui_ctx.get();
                    rt.ui_ctx_ptr->glfw_window = initial_wctx->glfw_window;
                }

                // Pre-create a hidden preview window so tab tearoff is instant.
                rt.window_mgr->warmup_preview_window();

                for (size_t gi = 1; gi < window_groups.size(); ++gi)
                {
                    auto& group = window_groups[gi];
                    if (group.empty())
                        continue;

                    auto*    fig0 = registry_.get(group[0]);
                    uint32_t w    = fig0 ? fig0->width() : 800;
                    uint32_t h    = fig0 ? fig0->height() : 600;

                    auto* new_wctx =
                        rt.window_mgr->create_window_with_ui(w, h, "Spectra", group[0]);

                    if (new_wctx && new_wctx->ui_ctx && new_wctx->ui_ctx->fig_mgr)
                    {
                        for (size_t fi = 1; fi < group.size(); ++fi)
                        {
                            new_wctx->ui_ctx->fig_mgr->add_figure(group[fi], FigureState{});
                            new_wctx->assigned_figures.push_back(group[fi]);
                        }
                    }
                }
            }   // if (config_.backend == RenderBackend::Vulkan)
        }
    }
#endif

#ifdef SPECTRA_USE_SDL3
    if (!config_.headless)
    {
        rt.sdl3 = std::make_unique<Sdl3Adapter>();
        if (!rt.sdl3->init(init_w, init_h, "Spectra"))
        {
            SPECTRA_LOG_ERROR("app", "Failed to create SDL3 window");
            rt.sdl3.reset();
        }
        else
        {
            backend_->create_surface(rt.sdl3->native_window());
            backend_->create_swapchain(init_w, init_h);

            if (config_.backend == RenderBackend::Vulkan)
            {
                WindowManagerBootstrapOptions wm_opts;
                wm_opts.backend                = static_cast<VulkanBackend*>(backend_.get());
                wm_opts.registry               = &registry_;
                wm_opts.renderer               = renderer_.get();
                wm_opts.theme_mgr              = theme_mgr_.get();
                wm_opts.session                = &rt.session;
                wm_opts.settings_store         = rt.settings_store.get();
                wm_opts.plugin_manager         = &rt.plugin_manager;
                wm_opts.export_format_registry = &rt.export_format_registry;
                rt.window_mgr                  = create_configured_window_manager(wm_opts);
                rt.window_mgr->set_interactive_frame_handler(
                    [&rt]()
                    {
                        rt.session.pump_interactive_frame(rt.scheduler,
                                                          rt.animator,
                                                          rt.window_mgr.get(),
                                                          rt.frame_state);
                    });

                std::vector<FigureId> first_group =
                    window_groups.empty() ? std::vector<FigureId>{} : window_groups[0];
                auto* initial_wctx =
                    rt.window_mgr->create_first_window_with_ui(rt.sdl3->native_window(),
                                                               first_group);

                if (initial_wctx && initial_wctx->ui_ctx)
                {
                    rt.ui_ctx_ptr              = initial_wctx->ui_ctx.get();
                    rt.ui_ctx_ptr->glfw_window = initial_wctx->glfw_window;
                }

                for (size_t gi = 1; gi < window_groups.size(); ++gi)
                {
                    auto& group = window_groups[gi];
                    if (group.empty())
                        continue;
                    auto*    fig0 = registry_.get(group[0]);
                    uint32_t w    = fig0 ? fig0->width() : 800;
                    uint32_t h    = fig0 ? fig0->height() : 600;
                    auto*    new_wctx =
                        rt.window_mgr->create_window_with_ui(w, h, "Spectra", group[0]);
                    if (new_wctx && new_wctx->ui_ctx && new_wctx->ui_ctx->fig_mgr)
                    {
                        for (size_t fi = 1; fi < group.size(); ++fi)
                        {
                            new_wctx->ui_ctx->fig_mgr->add_figure(group[fi], FigureState{});
                            new_wctx->assigned_figures.push_back(group[fi]);
                        }
                    }
                }
            }   // if (config_.backend == RenderBackend::Vulkan)
        }
    }
#endif

    if (!rt.ui_ctx_ptr)
    {
        // Headless mode: use the shared builder with `headless = true`
        // for a minimal context (FigureManager + ThemeManager only),
        // avoiding destruction-order issues in rapid create/destroy cycles.
        WindowUIContextBuildOptions headless_opts;
        headless_opts.registry       = &registry_;
        headless_opts.theme_mgr      = theme_mgr_.get();
        headless_opts.mode           = WindowUIContextBuildMode::Headless;
        headless_opts.settings_store = rt.settings_store.get();
        rt.headless_ui_ctx           = build_window_ui_context(headless_opts);
        rt.ui_ctx_ptr                = rt.headless_ui_ctx.get();
    }

    // Wire plugin host services after UI context creation.
    rt.plugin_manager.set_command_registry(&rt.ui_ctx_ptr->cmd_registry);
    rt.plugin_manager.set_shortcut_manager(&rt.ui_ctx_ptr->shortcut_mgr);
    rt.plugin_manager.set_undo_manager(&rt.ui_ctx_ptr->undo_mgr);
    rt.plugin_manager.set_transform_registry(&TransformRegistry::instance());
    rt.plugin_manager.set_export_format_registry(&rt.export_format_registry);
    rt.plugin_manager.set_data_source_registry(&rt.data_source_registry);
    rt.plugin_manager.set_series_type_registry(&rt.series_type_registry);
    rt.plugin_manager.set_backend(backend_.get());
    renderer_->set_series_type_registry(&rt.series_type_registry);
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    if (rt.window_mgr)
        rt.plugin_manager.set_overlay_registry(&rt.window_mgr->overlay_registry());
#endif

    rt.ui_ctx_ptr->plugin_manager = &rt.plugin_manager;
    if (rt.ui_ctx_ptr->imgui_ui)
    {
        rt.ui_ctx_ptr->imgui_ui->set_plugin_manager(&rt.plugin_manager);
        rt.ui_ctx_ptr->imgui_ui->set_export_format_registry(&rt.export_format_registry);
    }

#ifdef SPECTRA_USE_IMGUI
    if (knob_manager_ && !knob_manager_->empty() && rt.ui_ctx_ptr->imgui_ui)
    {
        rt.ui_ctx_ptr->imgui_ui->set_knob_manager(knob_manager_);
    }

    if (rt.ui_ctx_ptr && rt.ui_ctx_ptr->fig_mgr)
    {
        WindowUIContextRuntimeWireOptions wire_opts;
        wire_opts.ui_ctx                       = rt.ui_ctx_ptr;
        wire_opts.registry                     = &registry_;
        wire_opts.session                      = &rt.session;
        wire_opts.active_figure                = init_active;
        wire_opts.has_animation                = rt.frame_state.has_animation;
        wire_opts.tab_split_mode               = TabSplitMode::SplitPane;
        wire_opts.tab_drag_already_wired       = (rt.window_mgr != nullptr);
        wire_opts.wire_demo_animation_channels = (init_active != nullptr);
        wire_opts.enable_window_tab_callbacks  = !config_.headless;
    #if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
        wire_opts.window_manager = rt.window_mgr.get();
    #endif
        wire_window_ui_runtime(wire_opts);
    }

    if (!config_.headless && rt.ui_ctx_ptr && rt.ui_ctx_ptr->imgui_ui)
    {
        auto& fig_mgr  = *rt.ui_ctx_ptr->fig_mgr;
        auto& imgui_ui = rt.ui_ctx_ptr->imgui_ui;
        imgui_ui->set_csv_plot_callback(
            [&fig_mgr, this](const std::string& /*path*/,
                             const std::vector<float>& x,
                             const std::vector<float>& y,
                             const std::string& /* x_label */,
                             const std::string& y_label,
                             const std::vector<float>* /*z*/,
                             const std::string* /*z_label*/,
                             double x_offset)
            {
                FigureId active_id = fig_mgr.active_index();
                Figure*  fig       = registry_.get(active_id);
                if (!fig)
                {
                    active_id = fig_mgr.create_figure(FigureConfig{});
                    fig       = registry_.get(active_id);
                    if (!fig)
                        return;
                }

                auto& ax   = fig->subplot(1, 1, 1);
                auto& line = ax.line(x, y);
                line.label(y_label);
                // Preserve absolute x values (e.g. epoch timestamps): the
                // axis shows x_offset + x[i] at full double precision.
                line.x_offset(x_offset);
                ax.auto_fit();
            });

        // Wire OS drag-and-drop: CSV/TSV/TXT files trigger the Load from CSV flow.
        auto* imgui_ui_ptr = imgui_ui.get();
        if (rt.window_mgr)
        {
            rt.window_mgr->set_file_drop_handler(
                [imgui_ui_ptr](uint32_t /*window_id*/, const std::string& path)
                {
                    namespace fs = std::filesystem;
                    auto ext     = fs::path(path).extension().string();
                    std::transform(ext.begin(),
                                   ext.end(),
                                   ext.begin(),
                                   [](unsigned char c) { return std::tolower(c); });
                    if (ext == ".csv" || ext == ".tsv" || ext == ".txt")
                        imgui_ui_ptr->request_csv_load(path);
                });
        }
    }
#endif

    if (config_.headless)
    {
        backend_->create_offscreen_framebuffer(init_w, init_h);
        if (config_.backend == RenderBackend::Vulkan)
            static_cast<VulkanBackend*>(backend_.get())->ensure_pipelines();
    }

    rt.scheduler.reset();

    rt.last_step_time = std::chrono::steady_clock::now();

    // Start automation server for MCP-driven testing.
    // Opt-in only (AppConfig::enable_automation, SPECTRA_AUTOMATION=1, or an explicit
    // SPECTRA_AUTO_SOCKET path): the endpoints allow remote input injection, command
    // execution and file writes, so they must never be on by default.  See SECURITY.md.
    {
        const char* auto_env = std::getenv("SPECTRA_AUTO_SOCKET");
        const bool  want_automation =
            automation_security::automation_requested(config_.enable_automation);

        if (want_automation)
        {
            std::string auto_sock;
            if (auto_env && auto_env[0] != '\0')
                auto_sock = auto_env;
            else
                auto_sock = AutomationServer::default_socket_path();

            rt.auto_server = std::make_unique<AutomationServer>();
            if (rt.auto_server->start(auto_sock))
            {
                SPECTRA_LOG_INFO("app", "Automation server started: " + auto_sock);

                std::string mcp_bind = "127.0.0.1";
                if (const char* mcp_bind_env = std::getenv("SPECTRA_MCP_BIND"))
                {
                    if (mcp_bind_env[0] != '\0')
                        mcp_bind = mcp_bind_env;
                }
                if (mcp_bind != "127.0.0.1")
                    SPECTRA_LOG_WARN("app",
                                     "SPECTRA_MCP_BIND=" + mcp_bind
                                         + " exposes the MCP server beyond loopback; it is "
                                           "protected only by its bearer token");

                uint16_t mcp_port        = 8765;
                bool     mcp_port_pinned = false;
                if (const char* mcp_port_env = std::getenv("SPECTRA_MCP_PORT"))
                {
                    if (mcp_port_env[0] != '\0')
                    {
                        const long parsed_port = std::strtol(mcp_port_env, nullptr, 10);
                        if (parsed_port > 0 && parsed_port <= 65535)
                        {
                            mcp_port        = static_cast<uint16_t>(parsed_port);
                            mcp_port_pinned = true;
                        }
                    }
                }

                rt.mcp_server = std::make_unique<McpServer>();
                // If the user didn't pin a port, probe a small range so multiple
                // Spectra instances on the same machine don't collide on 8765.
                bool           started   = false;
                const uint16_t max_tries = mcp_port_pinned ? 1 : 16;
                for (uint16_t i = 0; i < max_tries; ++i)
                {
                    const auto try_port = static_cast<uint16_t>(mcp_port + i);
                    if (rt.mcp_server->start(*rt.auto_server, mcp_bind, try_port))
                    {
                        started = true;
                        break;
                    }
                }
                if (started)
                    SPECTRA_LOG_INFO("app", "MCP server started: " + rt.mcp_server->endpoint());
                else
                    rt.mcp_server.reset();
            }
            else
            {
                SPECTRA_LOG_WARN("app", "Automation server failed to start");
                rt.auto_server.reset();
            }
        }
    }

    // ── InprocTopicServer ─────────────────────────────────────────────────
    // Start a publisher-facing socket server so external publishers can push
    // topics into this inproc app without a separate spectra-backend daemon.
    // Only in windowed (non-headless) mode to avoid socket leaks in test runs.
#ifndef _WIN32
    if (!config_.headless)
    {
        rt.topic_server = std::make_unique<InprocTopicServer>();
        rt.topic_server->start(&registry_);
        bool wired_via_window_mgr = false;
    #if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
        if (rt.window_mgr)
        {
            rewire_topics_panel_if_needed(*rt.topic_server,
                                          *rt.window_mgr,
                                          registry_,
                                          rt.topics_panel_wired,
                                          rt.topics_panel_window_id);
            wired_via_window_mgr = true;
        }
    #endif
        if (!wired_via_window_mgr && rt.ui_ctx_ptr)
            rt.topic_server->wire_topics_panel(rt.ui_ctx_ptr->topics_panel, &registry_);
    }
#endif

    PerfMetrics::instance().mark_startup_end();
    SPECTRA_LOG_INFO("app",
                     "Startup completed in "
                         + std::to_string(PerfMetrics::instance().startup_total_us() / 1000.0)
                         + " ms");
}

// ─── step ────────────────────────────────────────────────────────────────────
App::StepResult App::step()
{
    StepResult result;
    if (!runtime_)
    {
        result.should_exit = true;
        return result;
    }

    auto& rt = *runtime_;

    auto step_start = std::chrono::steady_clock::now();

    // Poll automation server for pending remote commands
    if (rt.auto_server)
    {
        rt.auto_server->poll(*this, rt.ui_ctx_ptr);
        // Automation commands will have been drained into cmd_queue;
        // the tick() will mark dirty when it processes them.
    }

#ifndef _WIN32
    // Deliver topic samples queued by the topic server's worker thread.  Must
    // run on this (main) thread before tick() commits pending series data so
    // the samples show up in this frame.
    if (rt.topic_server)
        rt.topic_server->drain_pending();
#endif

    // A PNG capture with its own theme: the frame it reads back is drawn with
    // that theme (only the plot follows it; ImGui keeps its applied style),
    // then the window is redrawn in the app theme.
    std::optional<ui::ScopedExportTheme> capture_theme;
    if (rt.png_export.awaiting_capture() && !rt.png_export.theme().empty())
    {
        capture_theme.emplace(theme_mgr_.get(), rt.png_export.theme());
        rt.session.redraw_tracker().mark_dirty("themed export");
    }

    rt.session.tick(rt.scheduler,
                    rt.animator,
                    rt.cmd_queue,
                    config_.headless,
                    rt.ui_ctx_ptr,
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
                    rt.window_mgr.get(),
#endif
                    rt.frame_state);
    rt.active_figure = rt.frame_state.active_figure;
    if (capture_theme)
    {
        capture_theme.reset();
        rt.session.redraw_tracker().mark_dirty("themed export done");
    }

#if !defined(_WIN32) && (defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3))
    // tick() may have closed the window whose TopicsPanel was wired.
    if (rt.topic_server && rt.window_mgr)
        rewire_topics_panel_if_needed(*rt.topic_server,
                                      *rt.window_mgr,
                                      registry_,
                                      rt.topics_panel_wired,
                                      rt.topics_panel_window_id);
#endif

    if (rt.active_figure && !registry_.get(rt.frame_state.active_figure_id))
    {
        rt.active_figure                = nullptr;
        rt.frame_state.active_figure    = nullptr;
        rt.frame_state.active_figure_id = INVALID_FIGURE_ID;
    }

#ifdef SPECTRA_USE_FFMPEG
    if (rt.video_exporter && rt.video_exporter->is_open() && rt.active_figure)
    {
        if (backend_->readback_framebuffer(rt.video_frame_pixels.data(),
                                           rt.active_figure->width(),
                                           rt.active_figure->height()))
        {
            if (rt.video_exporter->write_frame(rt.video_frame_pixels.data()))
                ++rt.video_frames_written;
        }
    }
#endif

    // Phase 1: Write PNG from a previously completed pre-present capture.
    // Phase 2: Schedule a pre-present capture for a pending export.  It runs
    // inside end_frame() of the next rendered frame (before vkQueuePresentKHR,
    // so the swapchain image contents are valid) and takes that frame's size.
    {
        auto* vk = (config_.backend == RenderBackend::Vulkan)
                       ? static_cast<VulkanBackend*>(backend_.get())
                       : nullptr;
        rt.png_export.collect(vk);
        if (!config_.headless && rt.active_figure && !rt.png_export.active())
        {
            WindowContext* target_wctx = nullptr;
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
            // Target the window that owns this figure so multi-window captures
            // read the correct swapchain image.
            target_wctx =
                find_window_for_figure(rt.window_mgr.get(), rt.frame_state.active_figure_id);
#endif
            if (rt.png_export.schedule(*rt.active_figure, vk, target_wctx))
                rt.session.redraw_tracker().mark_dirty("png export");
        }
    }

    // Check animation duration termination
    if (rt.active_figure && rt.active_figure->anim_.duration > 0.0f
        && !rt.active_figure->anim_.loop)
    {
#ifdef SPECTRA_USE_FFMPEG
        if (rt.is_recording && rt.video_exporter)
        {
            // Offline recording: stop after exactly duration * fps frames.
            const auto target_frames = static_cast<uint64_t>(
                std::llround(static_cast<double>(rt.active_figure->anim_.duration)
                             * static_cast<double>(rt.active_figure->anim_.fps)));
            if (rt.video_frames_written >= std::max<uint64_t>(1, target_frames))
                rt.session.request_exit();
        }
        else
#endif
            if (rt.scheduler.elapsed_seconds() >= rt.active_figure->anim_.duration)
        {
            rt.session.request_exit();
        }
    }

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    if (!rt.window_mgr)
    {
    #ifdef SPECTRA_USE_GLFW
        if (rt.glfw)
        {
            rt.glfw->poll_events();
            if (rt.glfw->should_close())
            {
                SPECTRA_LOG_INFO("main_loop", "Window closed, exiting loop");
                rt.session.request_exit();
            }
        }
    #elif defined(SPECTRA_USE_SDL3)
        if (rt.sdl3)
        {
            rt.sdl3->poll_events();
            if (rt.sdl3->should_close())
            {
                SPECTRA_LOG_INFO("main_loop", "Window closed, exiting loop");
                rt.session.request_exit();
            }
        }
    #endif
    }
#endif

    rt.frame_number++;
    PerfMetrics::instance().increment_frame_count();

    auto  step_end = std::chrono::steady_clock::now();
    float ms       = std::chrono::duration<float, std::milli>(step_end - step_start).count();

    result.should_exit   = rt.session.should_exit();
    result.frame_time_ms = ms;
    result.frame_number  = rt.frame_number;

    return result;
}

// ─── shutdown_runtime ────────────────────────────────────────────────────────
void App::shutdown_runtime()
{
    if (!runtime_)
        return;

    auto& rt = *runtime_;

    SPECTRA_LOG_INFO("main_loop", "Exited main render loop");

#ifndef _WIN32
    // Stop before MCP/automation: the topic worker may call UI callbacks that
    // reference TopicsPanel, which is destroyed when the user closes the window.
    if (rt.topic_server)
    {
        rt.topic_server->stop();
        rt.topic_server.reset();
    }
#endif

    if (rt.mcp_server)
    {
        rt.mcp_server->stop();
        rt.mcp_server.reset();
    }

    if (rt.auto_server)
    {
        rt.auto_server->stop();
        rt.auto_server.reset();
    }

#ifdef SPECTRA_USE_FFMPEG
    if (rt.video_exporter)
    {
        rt.video_exporter->finish();
        rt.video_exporter.reset();
    }
#endif

    // Process exports for all figures (headless batch mode)
    bool framebuffer_holds_active = true;
    for (auto id : registry_.all_ids())
    {
        Figure* fig_ptr = registry_.get(id);
        if (!fig_ptr)
            continue;
        auto& f = *fig_ptr;

        if (config_.headless && !f.export_req_.png_path.empty())
        {
            uint32_t export_w = f.export_req_.png_width > 0 ? f.export_req_.png_width : f.width();
            uint32_t export_h =
                f.export_req_.png_height > 0 ? f.export_req_.png_height : f.height();

            // The framebuffer still holds the active figure's last frame only
            // until another figure has been re-rendered into it.
            // A per-export theme draws this figure (only) with that theme.
            ui::ScopedExportTheme export_theme(theme_mgr_.get(), f.export_req_.png_theme);
            bool needs_render = (&f != rt.active_figure) || !framebuffer_holds_active
                                || (export_w != f.width()) || (export_h != f.height())
                                || export_theme.active();

            if (needs_render)
            {
                framebuffer_holds_active = false;
                backend_->create_offscreen_framebuffer(export_w, export_h);
                if (config_.backend == RenderBackend::Vulkan)
                    static_cast<VulkanBackend*>(backend_.get())->ensure_pipelines();

                // A larger-than-figure export keeps the figure's layout and draws
                // it scaled up (HiDPI), so lines, markers and text keep their size.
                ExportViewport ev =
                    compute_export_viewport(f.width(), f.height(), export_w, export_h);
                backend_->set_pixel_scale(ev.pixel_scale);
                if (backend_->pixel_scale() != ev.pixel_scale)   // backend cannot scale
                    ev = {export_w, export_h, 1.0f};
                uint32_t orig_w  = f.config_.width;
                uint32_t orig_h  = f.config_.height;
                f.config_.width  = ev.layout_width;
                f.config_.height = ev.layout_height;
                f.compute_layout();

                if (backend_->begin_frame())
                {
                    renderer_->render_figure(f);
                    backend_->end_frame();
                }
                backend_->set_pixel_scale(1.0f);

                f.config_.width  = orig_w;
                f.config_.height = orig_h;
                f.compute_layout();
            }

            // Device lost is unrecoverable: no valid image can be read back and
            // every further frame would fail, so stop exporting.
            if (backend_->is_device_lost())
            {
                SPECTRA_LOG_CRITICAL("app", "GPU device lost during export, aborting");
                break;
            }

            std::vector<uint8_t> pixels(static_cast<size_t>(export_w) * export_h * 4);
            if (backend_->readback_framebuffer(pixels.data(), export_w, export_h))
            {
                if (!ImageExporter::write_png(f.export_req_.png_path,
                                              pixels.data(),
                                              export_w,
                                              export_h))
                {
                    SPECTRA_LOG_ERROR("app", "Failed to write PNG: {}", f.export_req_.png_path);
                }
            }
            else
            {
                SPECTRA_LOG_ERROR("app", "Failed to readback framebuffer");
            }
        }

        write_pending_svg(f, theme_mgr_.get());
    }

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    if (rt.window_mgr)
    {
    #ifdef SPECTRA_USE_GLFW
        if (rt.glfw)
            rt.glfw->release_window();
    #elif defined(SPECTRA_USE_SDL3)
        if (rt.sdl3)
            rt.sdl3->release_window();
    #endif
        rt.window_mgr->shutdown();
        rt.window_mgr.reset();
    }
#endif

    if (backend_)
    {
        backend_->wait_idle();
    }

    // Renderer holds a non-owning pointer into AppRuntime::series_type_registry.
    // Destroy custom pipelines while both are still alive, then detach so
    // ~Renderer (in ~App) does not touch freed registry memory.
    if (renderer_ && backend_)
    {
        rt.series_type_registry.destroy_pipelines(*backend_);
        renderer_->set_series_type_registry(nullptr);
    }
    rt.plugin_manager.set_series_type_registry(nullptr);

    try
    {
        runtime_.reset();
    }
    catch (const std::exception& e)
    {
        SPECTRA_LOG_ERROR("shutdown",
                          std::string("Exception during AppRuntime destruction: ") + e.what());
        // Force-null the pointer to avoid double-free in ~App()
        runtime_.reset();
    }
    catch (...)
    {
        runtime_.reset();
    }
}

// ─── Accessors ───────────────────────────────────────────────────────────────
WindowUIContext* App::ui_context()
{
    if (!runtime_)
        return nullptr;

    auto& rt = *runtime_;

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    if (rt.window_mgr)
    {
        for (auto* wctx : rt.window_mgr->windows())
        {
            if (wctx && wctx->ui_ctx)
            {
                rt.ui_ctx_ptr = wctx->ui_ctx.get();
                return rt.ui_ctx_ptr;
            }
        }

        // All windows were closed/destroyed this frame.
        rt.ui_ctx_ptr = nullptr;
        return nullptr;
    }
#endif

    if (rt.headless_ui_ctx)
        rt.ui_ctx_ptr = rt.headless_ui_ctx.get();

    return rt.ui_ctx_ptr;
}

SessionRuntime* App::session()
{
    return runtime_ ? &runtime_->session : nullptr;
}

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
WindowManager* App::window_manager()
{
    return runtime_ ? runtime_->window_mgr.get() : nullptr;
}
#endif

}   // namespace spectra
