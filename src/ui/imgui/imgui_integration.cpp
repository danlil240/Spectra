#ifdef SPECTRA_USE_IMGUI

    #include "imgui_integration_internal.hpp"

    #include <filesystem>
    #include <cmath>
    #include <format>

    #define GLFW_INCLUDE_NONE
    #define GLFW_INCLUDE_VULKAN
    #ifdef SPECTRA_USE_GLFW
        #include <GLFW/glfw3.h>
        #include <cmath>
    #endif
    #ifdef SPECTRA_USE_SDL3
        #include <SDL3/SDL.h>
        #include <SDL3/SDL_vulkan.h>
    #endif

    // Compressed Inter font data
    #include "../../../third_party/inter_font.hpp"

    // Embedded Font Awesome 6 Free Solid icon font data
    #include "../../../third_party/fa_solid_900.hpp"

    // stb_image for decoding embedded PNG (implementation in stb_impl.cpp)
    #include "stb_image.h"

    // Embedded Spectra logo icon
    #include "spectra_icon_embedded.hpp"

    #include "ui/overlay/measure_math.hpp"
    #include "ui/theme/glass_draw.hpp"
    #include "ui/theme/theme_imgui.hpp"
    #include "ui/overlay/toast_queue.hpp"
    #include "ui/ui_interaction_log.hpp"

    #include "../../../third_party/tinyfiledialogs.h"
    #include "../dialog_env_guard.hpp"
    #include "../native_dialog_policy.hpp"
    #include "../data_sidebar/builtin_providers.hpp"
    #include "../data_sidebar/data_sidebar.hpp"
    #include "../logs/log_browser_panel.hpp"
    #include "../topics/topics_panel.hpp"
    #include "../workspace/layout_session.hpp"
    #include "../settings/settings_panel.hpp"
    #include "ui/shell/spectra_app_shell.hpp"
    #include "ui/commands/welcome_actions.hpp"
    #include "ui/app/launch_files.hpp"
    #include "../settings/settings_store.hpp"

    #ifndef M_PI
        #define M_PI 3.14159265358979323846
    #endif

static bool register_backend_texture(spectra::VulkanBackend& backend,
                                     spectra::TextureHandle  texture,
                                     uint64_t*               out_id)
{
    if (!texture)
        return false;

    VkSampler   sampler = VK_NULL_HANDLE;
    VkImageView view    = VK_NULL_HANDLE;
    if (!backend.texture_vulkan_handles(texture, &sampler, &view))
        return false;

    VkDescriptorSet ds =
        ImGui_ImplVulkan_AddTexture(sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (out_id)
        *out_id = vk_rp_to_u64(ds);
    return true;
}

static bool load_embedded_texture(spectra::VulkanBackend& backend,
                                  const unsigned char*    bytes,
                                  unsigned int            size,
                                  spectra::TextureHandle* out_texture,
                                  uint64_t*               out_id,
                                  int*                    out_w,
                                  int*                    out_h,
                                  const char*             label)
{
    int            w        = 0;
    int            h        = 0;
    int            channels = 0;
    unsigned char* pixels =
        stbi_load_from_memory(bytes, static_cast<int>(size), &w, &h, &channels, 4);
    if (!pixels)
    {
        SPECTRA_LOG_WARN("imgui", std::string("Failed to decode embedded ") + label + " PNG");
        return false;
    }

    spectra::TextureHandle texture =
        backend.create_texture(static_cast<uint32_t>(w), static_cast<uint32_t>(h), pixels);
    stbi_image_free(pixels);
    if (!texture)
    {
        SPECTRA_LOG_WARN("imgui", std::string("Failed to create ") + label + " Vulkan texture");
        return false;
    }

    uint64_t id = 0;
    if (!register_backend_texture(backend, texture, &id))
    {
        backend.destroy_texture(texture);
        SPECTRA_LOG_WARN("imgui",
                         std::string("Failed to register ") + label + " texture with ImGui");
        return false;
    }

    if (out_texture)
        *out_texture = texture;
    if (out_id)
        *out_id = id;
    if (out_w)
        *out_w = w;
    if (out_h)
        *out_h = h;
    return true;
}

namespace spectra
{
std::unique_ptr<ImGuiIntegration> make_imgui_integration()
{
    return std::make_unique<ImGuiIntegration>();
}

// ─── Lifecycle ──────────────────────────────────────────────────────────────

ImGuiIntegration::ImGuiIntegration() = default;

ImGuiIntegration::~ImGuiIntegration()
{
    shutdown();
}

const ui::ThemeColors& ImGuiIntegration::theme_colors() const
{
    if (theme_mgr_)
        return theme_mgr_->colors();
    return ui::theme();
}

    #ifdef SPECTRA_USE_GLFW
bool ImGuiIntegration::init(VulkanBackend& backend, GLFWwindow* window, bool install_callbacks)
{
    if (initialized_)
        return true;
    if (!window)
        return false;

    glfw_window_    = window;
    layout_manager_ = std::make_unique<LayoutManager>();

    IMGUI_CHECKVERSION();
    // In ImGui 1.92.x with RendererHasTextures the atlas must be owned by
    // the context (OwnerContext == ctx). Pass nullptr so CreateContext creates
    // its own atlas. The old shared-atlas workaround is not needed in 1.92.x.
    imgui_context_ = ImGui::CreateContext(nullptr);
    // CreateContext() restores the previous context if one exists (ImGui 1.90+).
    // We must explicitly switch to the new context so load_fonts() and
    // backend init operate on the correct context/atlas.
    ImGui::SetCurrentContext(imgui_context_);

    // Disable ImGui's automatic imgui.ini persistence for this window.  Spectra
    // handles layout/workspace persistence via WorkspaceData (JSON) rather than
    // imgui.ini, so letting ImGui write/read its own .ini file only causes
    // conflicts (especially in spectra-ros where docking state is managed by
    // RosSessionManager).
    ImGui::GetIO().IniFilename = nullptr;

    // Initialize theme system
    if (theme_mgr_)
        theme_mgr_->ensure_initialized();

    load_fonts();

    // Initialize icon font system (must be AFTER load_fonts so the atlas
    // contains merged FA6 glyphs when IconFont caches font pointers)
    ui::IconFont::instance().initialize();

    apply_modern_style();

    // Wire inspector fonts
    inspector_.set_fonts(font_body_, font_heading_, font_title_);
    data_editor_.set_fonts(font_body_, font_heading_, font_title_);

    ImGui_ImplGlfw_InitForVulkan(window, install_callbacks);

    // ImGui has no crosshair cursor; GLFW does.  Applied by hand over the plot
    // for the zoom / measure / ROI tools (see apply_os_cursor_after_backend).
    // Silence the error callback like ImGui's backend does for its cursors:
    // some platforms lack the shape and return nullptr.
    {
        GLFWerrorfun prev_error_cb = glfwSetErrorCallback(nullptr);
        crosshair_cursor_          = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
        arrow_cursor_              = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
        glfwSetErrorCallback(prev_error_cb);
        #ifndef __EMSCRIPTEN__
        // Emscripten's GLFW port has no glfwGetError.
        (void)glfwGetError(nullptr);
        #endif
    }

    ImGui_ImplVulkan_InitInfo ii{};
    ii.Instance                     = backend.instance();
    ii.PhysicalDevice               = backend.physical_device();
    ii.Device                       = backend.device();
    ii.QueueFamily                  = backend.graphics_queue_family();
    ii.Queue                        = backend.graphics_queue();
    ii.DescriptorPool               = backend.descriptor_pool();
    ii.MinImageCount                = backend.min_image_count();
    ii.ImageCount                   = backend.image_count();
    ii.PipelineInfoMain.RenderPass  = backend.render_pass();
    ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&ii);

    cached_render_pass_ = vk_rp_to_u64(ii.PipelineInfoMain.RenderPass);
    initialized_        = true;
    backend_            = &backend;

    load_logo_textures(backend);

    return true;
}
    #endif   // SPECTRA_USE_GLFW

    #ifdef SPECTRA_USE_SDL3
bool ImGuiIntegration::init_sdl3(VulkanBackend& backend, SDL_Window* window)
{
    if (initialized_)
        return true;
    if (!window)
        return false;

    glfw_window_    = window;
    layout_manager_ = std::make_unique<LayoutManager>();

    IMGUI_CHECKVERSION();
    imgui_context_ = ImGui::CreateContext(nullptr);
    ImGui::SetCurrentContext(imgui_context_);
    ImGui::GetIO().IniFilename = nullptr;

    if (theme_mgr_)
        theme_mgr_->ensure_initialized();

    load_fonts();
    ui::IconFont::instance().initialize();
    apply_modern_style();

    inspector_.set_fonts(font_body_, font_heading_, font_title_);
    data_editor_.set_fonts(font_body_, font_heading_, font_title_);

    ImGui_ImplSDL3_InitForVulkan(window);

    ImGui_ImplVulkan_InitInfo ii{};
    ii.Instance                     = backend.instance();
    ii.PhysicalDevice               = backend.physical_device();
    ii.Device                       = backend.device();
    ii.QueueFamily                  = backend.graphics_queue_family();
    ii.Queue                        = backend.graphics_queue();
    ii.DescriptorPool               = backend.descriptor_pool();
    ii.MinImageCount                = backend.min_image_count();
    ii.ImageCount                   = backend.image_count();
    ii.PipelineInfoMain.RenderPass  = backend.render_pass();
    ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&ii);

    cached_render_pass_ = vk_rp_to_u64(ii.PipelineInfoMain.RenderPass);
    initialized_        = true;
    backend_            = &backend;

    load_logo_textures(backend);

    return true;
}
    #endif   // SPECTRA_USE_SDL3

bool ImGuiIntegration::init_headless(VulkanBackend& backend, uint32_t width, uint32_t height)
{
    if (initialized_)
        return true;

    headless_       = true;
    layout_manager_ = std::make_unique<LayoutManager>();

    IMGUI_CHECKVERSION();
    imgui_context_ = ImGui::CreateContext(nullptr);
    ImGui::SetCurrentContext(imgui_context_);

    // Configure IO for headless operation
    ImGuiIO& io    = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DeltaTime   = 1.0f / 60.0f;
    io.ConfigFlags |= ImGuiConfigFlags_NavNoCaptureKeyboard;
    // No ini file for headless
    io.IniFilename = nullptr;

    // Initialize theme system
    if (theme_mgr_)
        theme_mgr_->ensure_initialized();

    load_fonts();

    // Initialize icon font system (must be AFTER load_fonts so the atlas
    // contains merged FA6 glyphs when IconFont caches font pointers)
    ui::IconFont::instance().initialize();

    apply_modern_style();

    // Wire inspector fonts
    inspector_.set_fonts(font_body_, font_heading_, font_title_);
    data_editor_.set_fonts(font_body_, font_heading_, font_title_);

    // Skip ImGui_ImplGlfw_* — no GLFW window in headless mode.
    // Only init the Vulkan rendering backend.
    ImGui_ImplVulkan_InitInfo ii{};
    ii.Instance       = backend.instance();
    ii.PhysicalDevice = backend.physical_device();
    ii.Device         = backend.device();
    ii.QueueFamily    = backend.graphics_queue_family();
    ii.Queue          = backend.graphics_queue();
    ii.DescriptorPool = backend.descriptor_pool();
    // ImGui Vulkan backend asserts MinImageCount >= 2. The headless backend
    // uses a single offscreen framebuffer (image_count=1), so clamp to 2.
    ii.MinImageCount                = std::max(backend.min_image_count(), 2u);
    ii.ImageCount                   = std::max(backend.image_count(), 2u);
    ii.PipelineInfoMain.RenderPass  = backend.render_pass();
    ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&ii);

    cached_render_pass_ = vk_rp_to_u64(ii.PipelineInfoMain.RenderPass);
    initialized_        = true;
    backend_            = &backend;
    load_logo_textures(backend);
    return true;
}

void ImGuiIntegration::enable_docking()
{
    #ifdef IMGUI_HAS_DOCK
    if (imgui_context_)
    {
        ImGuiContext* prev = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(imgui_context_);
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        ImGui::SetCurrentContext(prev);
    }
    #endif
}

void ImGuiIntegration::enable_viewports()
{
    // Multi-viewport is incompatible with Spectra's overlay coordinate
    // system (all positions are window-relative, ViewportsEnable switches
    // them to screen-absolute).  Kept as a no-op for API stability.
}

void ImGuiIntegration::shutdown()
{
    if (!initialized_)
        return;

    // Switch to this integration's context before tearing down backends,
    // then restore the previous context so the caller is not left with a
    // dangling current context (fixes crash when closing secondary windows).
    ImGuiContext* prev_ctx = ImGui::GetCurrentContext();
    ImGuiContext* this_ctx = imgui_context_;
    if (this_ctx)
        ImGui::SetCurrentContext(this_ctx);

    destroy_logo_textures();
    ImGui_ImplVulkan_Shutdown();
    if (!headless_)
    {
    #ifdef SPECTRA_USE_GLFW
        if (crosshair_cursor_)
        {
            // GLFW resets any window still showing it to the default cursor.
            glfwDestroyCursor(static_cast<GLFWcursor*>(crosshair_cursor_));
            crosshair_cursor_  = nullptr;
            crosshair_applied_ = false;
        }
        if (arrow_cursor_)
        {
            glfwDestroyCursor(static_cast<GLFWcursor*>(arrow_cursor_));
            arrow_cursor_ = nullptr;
        }
        ImGui_ImplGlfw_Shutdown();
    #elif defined(SPECTRA_USE_SDL3)
        ImGui_ImplSDL3_Shutdown();
    #endif
    }
    ImGui::DestroyContext(this_ctx);
    imgui_context_ = nullptr;

    // Restore previous context (if it was a different context)
    if (prev_ctx && prev_ctx != this_ctx)
        ImGui::SetCurrentContext(prev_ctx);
    else
        ImGui::SetCurrentContext(nullptr);

    layout_manager_.reset();
    backend_     = nullptr;
    initialized_ = false;
}

void ImGuiIntegration::on_swapchain_recreated(VulkanBackend& backend)
{
    if (!initialized_)
        return;

    ImGui_ImplVulkan_SetMinImageCount(backend.min_image_count());

    // Section 3F constraint 5: If the render pass handle changed (e.g. format
    // change on multi-monitor), ImGui holds a stale VkRenderPass.  Re-init the
    // Vulkan backend to pick up the new render pass.  This is a no-op in the
    // common case where recreate_swapchain reuses the render pass handle.
    VkRenderPass current_rp      = backend.render_pass();
    auto         current_rp_bits = vk_rp_to_u64(current_rp);
    if (current_rp_bits != cached_render_pass_ && current_rp != VK_NULL_HANDLE)
    {
        SPECTRA_LOG_WARN(
            "imgui",
            "Render pass changed after swapchain recreation — reinitializing ImGui Vulkan backend");
        ImGui_ImplVulkan_Shutdown();

        ImGui_ImplVulkan_InitInfo ii{};
        ii.Instance                     = backend.instance();
        ii.PhysicalDevice               = backend.physical_device();
        ii.Device                       = backend.device();
        ii.QueueFamily                  = backend.graphics_queue_family();
        ii.Queue                        = backend.graphics_queue();
        ii.DescriptorPool               = backend.descriptor_pool();
        ii.MinImageCount                = backend.min_image_count();
        ii.ImageCount                   = backend.image_count();
        ii.PipelineInfoMain.RenderPass  = current_rp;
        ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

        ImGui_ImplVulkan_Init(&ii);

        // Re-register the single shared logo texture after ImGui reinit
        // (corner is an alias — no separate remove/register needed)
        if (welcome_logo_texture_id_ != 0)
            ImGui_ImplVulkan_RemoveTexture(
                u64_to_handle<VkDescriptorSet>(welcome_logo_texture_id_));

        welcome_logo_texture_id_ = 0;
        corner_logo_texture_id_  = 0;
        register_backend_texture(backend, welcome_logo_texture_, &welcome_logo_texture_id_);
        corner_logo_texture_id_ = welcome_logo_texture_id_;

        cached_render_pass_ = current_rp_bits;
    }
}

void ImGuiIntegration::update_layout(float window_width, float window_height, float dt)
{
    if (layout_manager_)
    {
        layout_manager_->update(window_width, window_height, dt);
    }
}

bool ImGuiIntegration::has_active_chrome_animations() const
{
    if (layout_manager_ && layout_manager_->is_animating())
        return true;
    if (ui::widgets::any_section_animations_active())
        return true;
    if (theme_mgr_ && theme_mgr_->is_transitioning())
        return true;
    if (mode_transition_ && mode_transition_->is_active())
        return true;
    // Timeline playback moves the playhead (and the panel) every frame.
    if (timeline_editor_ && (timeline_editor_->is_playing() || timeline_editor_->is_recording()))
        return true;
    // Modal scrim fading in/out (Settings, Theme Settings): ImGui advances the
    // dim ratio per frame, so keep frames coming until it settles.
    if (const ImGuiContext* g = ImGui::GetCurrentContext();
        g && g->DimBgRatio > 0.0f && g->DimBgRatio < 1.0f)
        return true;
    // The FPS badge still reads live after rendering stopped: draw one more
    // frame so it can settle to "Idle".
    if (fps_badge_.needs_settle_frame(ui::FpsBadgeState::Clock::now()))
        return true;
    // Toasts age (and fade) per frame; export watches poll the file per frame.
    if (toasts_ && toasts_->needs_frames())
        return true;
    return false;
}

void ImGuiIntegration::set_nav_rail_visible(bool v)
{
    show_nav_rail_ = v;
    if (layout_manager_)
        layout_manager_->set_nav_rail_visible(v);
}

void ImGuiIntegration::new_frame()
{
    if (!initialized_)
        return;
    ImGui_ImplVulkan_NewFrame();
    #ifdef SPECTRA_USE_GLFW
    apply_os_cursor_before_backend();
    ImGui_ImplGlfw_NewFrame();
    apply_os_cursor_after_backend();
    #elif defined(SPECTRA_USE_SDL3)
    ImGui_ImplSDL3_NewFrame();
    #endif
    // Follow the app UI scale (Settings -> Appearance) in every ImGui context:
    // a change made in another window re-styles this one before its frame.
    if (theme_mgr_ && ImGui::GetStyle().FontScaleMain != theme_mgr_->ui_scale())
        theme_mgr_->apply_to_imgui();
    ImGui::NewFrame();

    // Update layout with current window size and delta time
    ImGuiIO& io = ImGui::GetIO();
    update_layout(io.DisplaySize.x, io.DisplaySize.y, io.DeltaTime);
}

void ImGuiIntegration::new_frame_headless(const HeadlessFrameInput& input)
{
    if (!initialized_)
        return;

    ImGui::SetCurrentContext(imgui_context_);

    // Manually populate IO — replaces ImGui_ImplGlfw_NewFrame()
    ImGuiIO& io    = ImGui::GetIO();
    io.DisplaySize = ImVec2(input.display_w, input.display_h);
    io.DeltaTime   = (input.dt > 0.0f) ? input.dt : (1.0f / 60.0f);

    // Mouse state
    io.MousePos = ImVec2(input.mouse_x, input.mouse_y);
    for (int i = 0; i < 5; ++i)
        io.MouseDown[i] = input.mouse_down[i];
    io.MouseWheel  = input.mouse_wheel;
    io.MouseWheelH = input.mouse_wheel_h;

    // Display scaling
    io.DisplayFramebufferScale = ImVec2(input.dpi_scale, input.dpi_scale);

    ImGui_ImplVulkan_NewFrame();
    // Follow the app UI scale (Settings -> Appearance) in every ImGui context:
    // a change made in another window re-styles this one before its frame.
    if (theme_mgr_ && ImGui::GetStyle().FontScaleMain != theme_mgr_->ui_scale())
        theme_mgr_->apply_to_imgui();
    ImGui::NewFrame();

    // Update layout with current display size and delta time
    update_layout(input.display_w, input.display_h, io.DeltaTime);
}

void ImGuiIntegration::build_ui(Figure& figure, FigureViewModel* vm)
{
    if (!initialized_)
    {
        SPECTRA_LOG_WARN("ui", "build_ui called but ImGui is not initialized");
        return;
    }

    current_figure_ = &figure;
    inspector_.set_figure_view_model(vm);

    if (app_shell_)
        app_shell_->set_current_figure(&figure);

    if (app_shell_)
        app_shell_->ensure_initialized();

    float dt = ImGui::GetIO().DeltaTime;
    if (theme_mgr_)
    {
        theme_mgr_->update(dt);
        // Resolve palette colours before the legend / inspector read them, for
        // the same plot surface the renderer uses (background override aware).
        const ui::ThemeColors surface = theme_mgr_->plot_surface_colors(figure.style());
        ui::resolve_auto_series_colors(figure,
                                       theme_mgr_->plot_surface_data_palette(figure.style()),
                                       ui::is_light_plot_background(surface),
                                       ui::effective_figure_background(figure.style(), surface));
    }
    ui::widgets::update_section_animations(dt);

    // Sync panel_open_ from layout manager so external toggles (commands, undo)
    // that only call set_inspector_visible() also open the panel content.
    if (layout_manager_)
        panel_open_ = layout_manager_->is_inspector_visible();

    // Fade tracks the slide animation so open/close feel cohesive.
    if (layout_manager_)
        panel_anim_ = layout_manager_->inspector_open_amount();

    // Update bottom panel height so canvas shrinks when timeline is open
    if (layout_manager_)
    {
        float target_h = (show_timeline_ && timeline_editor_) ? 200.0f : 0.0f;
        float cur_h    = layout_manager_->bottom_panel_height();
        float new_h    = cur_h + (target_h - cur_h) * std::min(1.0f, 12.0f * dt);
        if (std::abs(new_h - target_h) < 0.5f)
            new_h = target_h;
        layout_manager_->set_bottom_panel_height(new_h);
    }

    // Opaque chrome backdrops cover the plot clear color in UI zones during
    // live resize when layout briefly lags the swapchain extent.
    draw_chrome_backdrops();

    // Draw all zones using layout manager.
    // Each draw call is gated so adapter shells (e.g. spectra-ros) can suppress
    // Spectra's own chrome and replace it with their own menu/status/canvas.
    if (command_bar_visible_)
        draw_command_bar();
    draw_nav_rail();
    if (canvas_visible_)
    {
        draw_canvas(figure);
        draw_plot_overlays(figure);
        draw_axis_link_indicators(figure);
        draw_axes_context_menu(figure);
        draw_topic_drop_target(figure);
        draw_log_field_drop_target(figure);
        draw_data_field_drop_target(figure);
    }
    if (canvas_visible_)
    {
        if (app_shell_)
        {
            app_shell_->draw_registered_panels();
        }
        else if (layout_manager_->inspector_animated_width() >= 1.0f)
        {
            draw_inspector(figure);
        }
    }
    if (canvas_visible_)
    {
        draw_inspector_toggle();
    }
    if (status_bar_visible_)
        draw_status_bar();
    if (canvas_visible_)
    {
        draw_pane_tab_headers();   // Must run before splitters so pane_tab_hovered_ is set
        draw_split_view_splitters();
    }

    // Draw timeline / curve editor via shell panel registry when available.
    if (!app_shell_)
    {
        if (show_timeline_ && timeline_editor_)
            draw_timeline_panel();

        if (show_curve_editor_ && curve_editor_)
            draw_curve_editor_panel();
    }

    // Full text of an ellipsized plot title / axis label under the cursor.
    if (!deferred_tooltip_ && canvas_visible_ && !ImGui::IsAnyItemActive()
        && !ImGui::IsAnyItemHovered() && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow))
    {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        for (const auto& region : truncated_text_regions_)
        {
            const Rect& r = region.rect;
            if (mouse.x >= r.x && mouse.x < r.x + r.w && mouse.y >= r.y && mouse.y < r.y + r.h)
            {
                deferred_tooltip_ = region.full_text.c_str();
                break;
            }
        }
    }

    // Draw deferred tooltip (command bar, ellipsized plot labels) on top of everything
    if (deferred_tooltip_)
    {
        // Clamp tooltip position to stay within window bounds
        {
            ImVec2 mouse   = ImGui::GetIO().MousePos;
            ImVec2 display = ImGui::GetIO().DisplaySize;
            float  margin  = 8.0f;
            float  est_tip_w =
                ImGui::CalcTextSize(deferred_tooltip_).x + ui::tokens::SPACE_3 * 2.0f + 4.0f;
            float est_tip_h =
                ImGui::GetTextLineHeight() + (ui::tokens::SPACE_2 + 1.0f) * 2.0f + 6.0f;

            // Default: tooltip floats above the cursor (anchored by its bottom).
            // If there isn't room above (e.g. command-bar controls at the top of
            // the screen), drop it below the cursor so it never clips off-screen.
            bool  place_below = (mouse.y - est_tip_h - 4.0f) < margin;
            float pivot_y     = place_below ? 0.0f : 1.0f;
            float tip_y       = place_below ? (mouse.y + 24.0f) : (mouse.y - 4.0f);
            tip_y             = std::clamp(tip_y, margin, display.y - margin);

            // If tooltip would clip right edge, anchor from right side
            if (mouse.x + est_tip_w * 0.5f > display.x - margin)
            {
                float tip_x = display.x - margin;
                ImGui::SetNextWindowPos(ImVec2(tip_x, tip_y),
                                        ImGuiCond_Always,
                                        ImVec2(1.0f, pivot_y));
            }
            else if (mouse.x - est_tip_w * 0.5f < margin)
            {
                float tip_x = margin;
                ImGui::SetNextWindowPos(ImVec2(tip_x, tip_y),
                                        ImGuiCond_Always,
                                        ImVec2(0.0f, pivot_y));
            }
            else
            {
                ImGui::SetNextWindowPos(ImVec2(mouse.x, tip_y),
                                        ImGuiCond_Always,
                                        ImVec2(0.5f, pivot_y));
            }
        }
        ImGui::BeginTooltip();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            ImVec2(ui::tokens::SPACE_3 + 2.0f, ui::tokens::SPACE_2 + 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::tokens::RADIUS_MD);
        // Transparent popup bg — frosted glass card is drawn manually below.
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(theme_colors().text_primary.r,
                                     theme_colors().text_primary.g,
                                     theme_colors().text_primary.b,
                                     theme_colors().text_primary.a));
        {
            ImDrawList* tdl = ImGui::GetWindowDrawList();
            ImVec2      tp0 = ImGui::GetWindowPos();
            ImVec2      tsz = ImGui::GetWindowSize();
            ImVec2      tp1 = ImVec2(tp0.x + tsz.x, tp0.y + tsz.y);
            tdl->PushClipRectFullScreen();
            ui::glass_draw::draw_glass_card(tdl,
                                            tp0,
                                            tp1,
                                            ui::tokens::RADIUS_MD,
                                            theme_colors(),
                                            0.78f,
                                            theme_colors().glow_intensity);
            tdl->PopClipRect();
        }
        ImGui::TextUnformatted(deferred_tooltip_);
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);
        ImGui::EndTooltip();
        deferred_tooltip_ = nullptr;   // Clear for next frame
    }

    // Draw data interaction overlays (tooltip, crosshair, markers) into a regular
    // ImGui window so they render behind menus/popups in z-order.
    ImDrawList* overlay_dl = nullptr;
    {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGuiWindowFlags overlay_flags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus
            | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground
            | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
            | ImGuiWindowFlags_NoInputs;
        if (ImGui::Begin("##overlay_host", nullptr, overlay_flags))
            overlay_dl = ImGui::GetWindowDrawList();
        ImGui::End();
        ImGui::PopStyleVar(2);
    }
    if (data_interaction_)
    {
        ImGuiIO& io = ImGui::GetIO();
        data_interaction_->draw_overlays(io.DisplaySize.x, io.DisplaySize.y, &figure, overlay_dl);

        // In split mode, draw_overlays only draws legends for the active figure.
        // Draw the legend of every other pane's *visible* figure as well — not
        // of figures behind another tab in a pane, whose stale axes viewports
        // would put ghost legends over the visible plots.
        if (dock_system_ && dock_system_->is_split() && get_figure_ptr_)
        {
            auto panes = dock_system_->split_view().all_panes();
            for (auto* pane : panes)
            {
                if (!pane || !pane->is_leaf())
                    continue;
                Figure* fig = get_figure_ptr_(pane->figure_index());
                if (fig && fig != &figure)
                    data_interaction_->draw_legend_for_figure(*fig);
            }
        }
    }

    // Draw box zoom overlay (Agent B Week 7) — on top of data overlays
    if (box_zoom_overlay_)
    {
        box_zoom_overlay_->update(dt);
        ImGuiIO& io = ImGui::GetIO();
        box_zoom_overlay_->draw(io.DisplaySize.x, io.DisplaySize.y);
    }

    // Draw select rectangle overlay (Select tool rubber-band)
    if (input_handler_ && input_handler_->is_select_rect_active())
    {
        const auto& sr = input_handler_->select_rect();
        auto        x0 = static_cast<float>(std::min(sr.x0, sr.x1));
        auto        y0 = static_cast<float>(std::min(sr.y0, sr.y1));
        auto        x1 = static_cast<float>(std::max(sr.x0, sr.x1));
        auto        y1 = static_cast<float>(std::max(sr.y0, sr.y1));

        ImDrawList* fg     = ImGui::GetForegroundDrawList();
        const auto& colors = theme_colors();
        ImU32       fill   = IM_COL32(static_cast<uint8_t>(colors.selection_fill.r * 255),
                              static_cast<uint8_t>(colors.selection_fill.g * 255),
                              static_cast<uint8_t>(colors.selection_fill.b * 255),
                              40);
        ImU32       border = IM_COL32(static_cast<uint8_t>(colors.selection_border.r * 255),
                                static_cast<uint8_t>(colors.selection_border.g * 255),
                                static_cast<uint8_t>(colors.selection_border.b * 255),
                                200);
        fg->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), fill);
        fg->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), border, 0.0f, 0, 1.0f);
    }

    // Draw measure overlay (Measure tool mode)
    // Crosshair is handled by data_interaction (auto-enabled when entering Measure mode)
    if (input_handler_ && input_handler_->tool_mode() == ToolMode::Measure)
    {
        draw_measure_overlay();
    }

    process_pending_opens();

    // Draw CSV load dialog if open
    if (csv_dialog_open_)
    {
        draw_csv_dialog();
    }

    // Requested layout opens (startup --layout, File > Recent Layouts) and
    // the Data Bindings window.
    if (layout_session_)
    {
        layout_session_->poll();
        layout_session_->draw();
    }

    // Draw custom transform dialog if open
    if (custom_transform_dialog_.is_open())
    {
        draw_custom_transform_dialog();
    }

    if (plot_overlay_dialog_.is_open())
    {
        draw_plot_overlay_dialog();
    }

    // Draw plugin / topics panels (legacy path when shell registry is unavailable).
    if (!app_shell_)
    {
        if (show_plugins_panel_)
            draw_plugins_panel();

        if (topics_panel_)
            topics_panel_->draw();
        if (log_browser_panel_)
            log_browser_panel_->draw();
    }

    // Draw Settings panel.
    if (settings_panel_)
    {
        settings_panel_->draw();
    }

    // Theme settings modal (draws nothing unless open; called every frame so the
    // popup also closes when show_theme_settings_ is cleared elsewhere)
    draw_theme_settings();

    // ── ROS2 Adapter error modal ──────────────────────────────────────────────
    // Shown when the tools.ros2_adapter command encounters a launch failure.
    // The flag is set from the command callback; cleared here after the user
    // dismisses the modal.
    #ifdef SPECTRA_USE_ROS2
    if (ros2_adapter_has_error())
    {
        ImGui::OpenPopup("ROS2 Adapter Error##ros2err");
    }
    if (ImGui::BeginPopupModal("ROS2 Adapter Error##ros2err",
                               nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        const auto& colors = theme_colors();
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(colors.text_primary.r,
                                     colors.text_primary.g,
                                     colors.text_primary.b,
                                     colors.text_primary.a));

        ImGui::TextUnformatted("Could not launch spectra-ros:");
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(colors.text_secondary.r,
                                     colors.text_secondary.g,
                                     colors.text_secondary.b,
                                     colors.text_secondary.a));
        ImGui::TextWrapped("%s", ros2_adapter_pending_error().c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::TextUnformatted("Make sure a ROS2 workspace is sourced and\n"
                               "spectra-ros is on your PATH.");
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float btn_w = 80.0f;
        float avail = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX((avail - btn_w) * 0.5f + ImGui::GetStyle().WindowPadding.x);
        if (ImGui::Button("OK", ImVec2(btn_w, 0)))
        {
            ros2_adapter_clear_error();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    #endif   // SPECTRA_USE_ROS2

    // Draw directional dock highlight overlay when another window is dragging a tab over this one
    if (window_manager_ && window_id_ != 0 && window_manager_->drag_target_window() == window_id_)
    {
        auto&       theme     = theme_colors();
        auto        drop_info = window_manager_->cross_window_drop_info();
        ImDrawList* dl        = ImGui::GetForegroundDrawList();

        if (drop_info.zone >= 1 && drop_info.zone <= 5)
        {
            // Draw highlight rect for the active drop zone
            ImU32 highlight_color  = IM_COL32(static_cast<int>(theme.accent.r * 255),
                                             static_cast<int>(theme.accent.g * 255),
                                             static_cast<int>(theme.accent.b * 255),
                                             40);
            ImU32 highlight_border = IM_COL32(static_cast<int>(theme.accent.r * 255),
                                              static_cast<int>(theme.accent.g * 255),
                                              static_cast<int>(theme.accent.b * 255),
                                              160);

            float hx = drop_info.hx;
            float hy = drop_info.hy;
            float hw = drop_info.hw;
            float hh = drop_info.hh;

            dl->AddRectFilled(ImVec2(hx, hy), ImVec2(hx + hw, hy + hh), highlight_color, 4.0f);
            dl->AddRect(ImVec2(hx, hy), ImVec2(hx + hw, hy + hh), highlight_border, 4.0f, 0, 2.0f);

            // Draw a label indicating the action
            const char* label = nullptr;
            switch (drop_info.zone)
            {
                case 1:
                    label = "Split Left";
                    break;
                case 2:
                    label = "Split Right";
                    break;
                case 3:
                    label = "Split Up";
                    break;
                case 4:
                    label = "Split Down";
                    break;
                case 5:
                    label = "Add Tab";
                    break;
                default:
                    break;
            }
            if (label)
            {
                ImVec2 lsz = ImGui::CalcTextSize(label);
                float  lx  = hx + (hw - lsz.x) * 0.5f;
                float  ly  = hy + (hh - lsz.y) * 0.5f;
                float  pad = 10.0f;
                dl->AddRectFilled(ImVec2(lx - pad, ly - pad),
                                  ImVec2(lx + lsz.x + pad, ly + lsz.y + pad),
                                  ui::to_imu32(theme.bg_elevated, 0.92f),
                                  6.0f);
                dl->AddText(ImVec2(lx, ly),
                            IM_COL32(static_cast<int>(theme.accent.r * 255),
                                     static_cast<int>(theme.accent.g * 255),
                                     static_cast<int>(theme.accent.b * 255),
                                     220),
                            label);
            }
        }
    }

    // Extra draw callback (used by spectra-ros to inject ROS2 panels)
    if (extra_draw_cb_)
        extra_draw_cb_();

    // Draw knobs panel last (above all other windows, user-moveable)
    if (knob_manager_ && !knob_manager_->empty())
    {
        draw_knobs_panel();
    }

    // Toasts, unsaved-changes and recovery prompts (imgui_file_model.cpp).
    draw_file_model_overlays();

    // Draw command palette overlay (Agent F) — must be last to render on top
    if (command_palette_)
    {
        ImGuiIO& io = ImGui::GetIO();
        command_palette_->draw(io.DisplaySize.x, io.DisplaySize.y);
    }

    // After every widget had its say on the cursor this frame.
    update_plot_cursor();

    ui::log_imgui_frame_activations();
}

// ─── Measure tool overlay ───────────────────────────────────────────────────

void ImGuiIntegration::draw_measure_overlay()
{
    auto*       dl     = ImGui::GetForegroundDrawList();
    const auto& colors = theme_colors();
    const ImU32 accent = ui::to_imu32(colors.accent, 1.0f);

    // Snap ring: hollow circle around a point that sits on a data sample.
    auto snap_ring = [&](float x, float y)
    {
        dl->AddCircle(ImVec2(x, y), 7.5f, ui::to_imu32(colors.bg_primary, 0.9f), 0, 3.5f);
        dl->AddCircle(ImVec2(x, y), 7.5f, accent, 0, 1.75f);
    };

    Axes*      ax         = input_handler_->measure_axes();
    const bool dragging   = input_handler_->is_measure_dragging();
    const bool has_result = input_handler_->has_measure_result();

    // Hover preview before the first point: show where a press would snap.
    if (!ax && !dragging && !has_result)
    {
        Axes*       hover  = input_handler_->active_axes();
        const auto& cursor = input_handler_->cursor_readout();
        if (hover && cursor.valid)
        {
            double     px = 0.0;
            double     py = 0.0;
            const bool snapped =
                input_handler_->measure_point_at(cursor.screen_x, cursor.screen_y, px, py);
            const AxisMapping map(*hover);
            if (snapped && map.x_drawable(px) && map.y_drawable(py))
                snap_ring(static_cast<float>(map.to_screen_x(px)),
                          static_cast<float>(map.to_screen_y(py)));
        }
        return;
    }
    if (!ax || !(dragging || has_result))
        return;

    // Measure in data space, draw in axis space (log axes stay straight).
    const AxisMapping map(*ax);
    const double      sx = input_handler_->measure_start_data_x();
    const double      sy = input_handler_->measure_start_data_y();
    const double      ex = input_handler_->measure_end_data_x();
    const double      ey = input_handler_->measure_end_data_y();
    if (!map.x_drawable(sx) || !map.y_drawable(sy) || !map.x_drawable(ex) || !map.y_drawable(ey))
        return;
    // Screen positions in double, clipped to the plot before drawing: zoomed
    // far in, an endpoint can sit millions of pixels off screen.
    const Rect&  vp     = ax->viewport();
    const double ax0    = map.to_screen_x(sx);
    const double ay0    = map.to_screen_y(sy);
    const double bx0    = map.to_screen_x(ex);
    const double by0    = map.to_screen_y(ey);
    auto         inside = [&](double x, double y)
    { return x >= vp.x && x <= vp.x + vp.w && y >= vp.y && y <= vp.y + vp.h; };
    // Draws the visible part of a segment; returns its midpoint when drawn.
    auto clipped_line =
        [&](double x0, double y0, double x1, double y1, ImU32 col, float w, ImVec2* mid = nullptr)
    {
        if (!measure::clip_segment(vp, x0, y0, x1, y1))
            return false;
        dl->AddLine(ImVec2(static_cast<float>(x0), static_cast<float>(y0)),
                    ImVec2(static_cast<float>(x1), static_cast<float>(y1)),
                    col,
                    w);
        if (mid)
            *mid = ImVec2(static_cast<float>((x0 + x1) * 0.5), static_cast<float>((y0 + y1) * 0.5));
        return true;
    };

    if (inside(ax0, ay0))
    {
        dl->AddCircleFilled(ImVec2(static_cast<float>(ax0), static_cast<float>(ay0)), 4.0f, accent);
        if (input_handler_->measure_start_snapped())
            snap_ring(static_cast<float>(ax0), static_cast<float>(ay0));
    }
    if (std::hypot(bx0 - ax0, by0 - ay0) < 1.0)
        return;   // first point only (two-click mode, before moving)

    // Δx / Δy legs (dashed by alpha) and the measurement line.
    const ImU32 leg = ui::to_imu32(colors.accent, 0.45f);
    clipped_line(ax0, ay0, bx0, ay0, leg, 1.0f);
    clipped_line(bx0, ay0, bx0, by0, leg, 1.0f);
    ImVec2 mid;
    if (!clipped_line(ax0, ay0, bx0, by0, ui::to_imu32(colors.accent, 0.86f), 2.0f, &mid))
        return;   // the measurement is entirely off screen
    if (inside(bx0, by0))
    {
        dl->AddCircleFilled(ImVec2(static_cast<float>(bx0), static_cast<float>(by0)), 4.0f, accent);
        if (input_handler_->measure_end_snapped())
            snap_ring(static_cast<float>(bx0), static_cast<float>(by0));
    }

    measure::MeasureFormat fmt = measure::measure_format(*ax, ex, ey);
    if (input_handler_->measure_start_snapped() && input_handler_->measure_end_snapped())
    {
        // Both ends are exact samples: full precision, not pixel precision.
        fmt.x_resolution = 0.0;
        fmt.y_resolution = 0.0;
    }
    const measure::Measurement m = measure::compute_measurement(sx,
                                                                sy,
                                                                ex,
                                                                ey,
                                                                fmt.xscale,
                                                                fmt.yscale,
                                                                fmt.x_unit,
                                                                fmt.y_unit);
    DataInteraction::draw_measurement_label(dl,
                                            mid.x,
                                            mid.y,
                                            measure::measurement_lines(m, fmt),
                                            colors);
}

// ─── Tool mode and plot cursor ──────────────────────────────────────────────

void ImGuiIntegration::set_interaction_mode(ToolMode mode)
{
    interaction_mode_ = mode;
    if (input_handler_)
        input_handler_->set_tool_mode(mode);
}

std::string ImGuiIntegration::tool_tooltip(const char* label, ToolMode mode) const
{
    const std::string key =
        menu_shortcut_text(shortcut_manager_, command_registry_, tool_command_id(mode));
    if (key.empty())
        return label;
    return std::string(label) + " (" + key + ")";
}

void ImGuiIntegration::update_plot_cursor()
{
    plot_cursor_ = PlotCursor::Default;
    if (!input_handler_ || !canvas_visible_)
        return;

    const bool   panning = plot_is_panning(*input_handler_);
    const ImVec2 mouse   = ImGui::GetIO().MousePos;
    bool         over    = false;
    if (!panning && ImGui::IsMousePosValid(&mouse) && !wants_capture_mouse()
        && ImGui::GetMouseCursor() == ImGuiMouseCursor_Arrow)
    {
        over = input_handler_->hit_test_all_axes(mouse.x, mouse.y) != nullptr;
    }
    plot_cursor_ = plot_cursor_for(input_handler_->tool_mode(), over, panning);

    // Axis gutters (tick labels): wheel / drag there acts on that axis only.
    AxisGutter gutter = input_handler_->gutter_drag_axis();
    if (gutter == AxisGutter::None && !panning && !over && ImGui::IsMousePosValid(&mouse)
        && !wants_capture_mouse() && ImGui::GetMouseCursor() == ImGuiMouseCursor_Arrow)
    {
        gutter = input_handler_->hit_test_gutter(mouse.x, mouse.y);
    }
    if (gutter != AxisGutter::None)
    {
        plot_cursor_ = PlotCursor::Default;
        ImGui::SetMouseCursor(gutter == AxisGutter::X ? ImGuiMouseCursor_ResizeEW
                                                      : ImGuiMouseCursor_ResizeNS);
        return;
    }

    switch (plot_cursor_)
    {
        case PlotCursor::Default:
            break;
        case PlotCursor::OpenHand:
            // GLFW/ImGui have no open-hand shape: the hand (pointer) is closest.
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            break;
        case PlotCursor::Grabbing:
            // No grab shape either: the four-way move cursor reads as "dragging".
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            break;
        case PlotCursor::Crosshair:
            // With a GLFW crosshair the backend keeps the arrow and the
            // crosshair is set on the window after it (new_frame).  Without
            // one (SDL3, headless hosts) the arrow is the closest ImGui shape:
            // ResizeAll looks like a cross but already means "dragging".
            ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
            break;
        case PlotCursor::Text:
            ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
            break;
    }
}

void ImGuiIntegration::apply_os_cursor_before_backend()
{
    #ifdef SPECTRA_USE_GLFW
    // Leaving the crosshair: restore the arrow (what ImGui's backend believes
    // is set) *before* the backend runs, so a shape it sets this frame (hand,
    // resize) wins.  An explicit arrow, not nullptr: the window's default
    // cursor is inherited from the root window and need not be an arrow.
    if (crosshair_applied_ && plot_cursor_ != PlotCursor::Crosshair && glfw_window_)
    {
        glfwSetCursor(static_cast<GLFWwindow*>(glfw_window_),
                      static_cast<GLFWcursor*>(arrow_cursor_));
        crosshair_applied_ = false;
    }
    #endif
}

void ImGuiIntegration::apply_os_cursor_after_backend()
{
    #ifdef SPECTRA_USE_GLFW
    // ImGui asked for the arrow (update_plot_cursor), which its backend may
    // have just set; put the crosshair over it.  Re-applied every frame since
    // the backend only sets a cursor when ImGui's shape changes.
    if (plot_cursor_ == PlotCursor::Crosshair && crosshair_cursor_ && glfw_window_ && !headless_
        && !(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange))
    {
        glfwSetCursor(static_cast<GLFWwindow*>(glfw_window_),
                      static_cast<GLFWcursor*>(crosshair_cursor_));
        crosshair_applied_ = true;
    }
    #endif
}

void ImGuiIntegration::build_empty_ui()
{
    if (!initialized_)
        return;

    current_figure_ = nullptr;
    plot_cursor_    = PlotCursor::Default;   // no plot: hand the cursor back

    float dt = ImGui::GetIO().DeltaTime;
    if (theme_mgr_)
        theme_mgr_->update(dt);

    // Draw command bar (menu) so user can create figures / load CSV.
    // Suppressed when an adapter shell provides its own menu (e.g. spectra-ros).
    if (command_bar_visible_)
        draw_command_bar();

    // Fill the rest with the background color
    auto&       theme = theme_colors();
    ImDrawList* bg    = ImGui::GetBackgroundDrawList();
    ImGuiIO&    io    = ImGui::GetIO();
    bg->AddRectFilled(ImVec2(0, 0),
                      ImVec2(io.DisplaySize.x, io.DisplaySize.y),
                      IM_COL32(static_cast<int>(theme.bg_primary.r * 255),
                               static_cast<int>(theme.bg_primary.g * 255),
                               static_cast<int>(theme.bg_primary.b * 255),
                               255));

    // Draw welcome screen with centered logo
    draw_welcome_screen(io.DisplaySize.x, io.DisplaySize.y, dt);

    // CSV files dropped or passed on the command line before any figure exists.
    process_pending_opens();

    // Draw CSV dialog if open (user may have opened it from the menu)
    if (csv_dialog_open_)
        draw_csv_dialog();

    // A layout may be opened from the welcome screen too (startup --layout).
    if (layout_session_)
    {
        layout_session_->poll();
        layout_session_->draw();
    }

    // Plugins panel can be opened while no figure exists.
    if (show_plugins_panel_)
        draw_plugins_panel();

    // Topics panel can be opened while no figure exists.
    if (topics_panel_)
        topics_panel_->draw();

    // So can the log browser (open a log, then drop a field to create a figure).
    if (log_browser_panel_)
        log_browser_panel_->draw();

    // Settings panel can be opened while no figure exists.
    if (settings_panel_)
        settings_panel_->draw();

    // Theme Settings is reachable from the Tools menu on the welcome screen too.
    draw_theme_settings();

    // Welcome-state drop target: allows dragging a topic to auto-create a new figure.
    draw_topic_drop_target_welcome();
    draw_log_field_drop_target_welcome();

    // Toasts, unsaved-changes and recovery prompts (imgui_file_model.cpp).
    draw_file_model_overlays();

    // Command palette overlay (Ctrl+K / Tools menu) — last so it renders on top.
    if (command_palette_)
        command_palette_->draw(io.DisplaySize.x, io.DisplaySize.y);
}

// ─── Welcome screen logo texture ────────────────────────────────────────────

void ImGuiIntegration::load_logo_textures(VulkanBackend& backend)
{
    if (logo_loaded_)
        return;
    logo_loaded_ = true;   // Don't retry on failure

    const bool welcome_ok = load_embedded_texture(backend,
                                                  SpectraIcon_png_data,
                                                  SpectraIcon_png_size,
                                                  &welcome_logo_texture_,
                                                  &welcome_logo_texture_id_,
                                                  &welcome_logo_width_,
                                                  &welcome_logo_height_,
                                                  "welcome logo");

    // Reuse the same icon for the corner mark (the round dark icon reads well at 24px)
    corner_logo_texture_    = welcome_logo_texture_;
    corner_logo_texture_id_ = welcome_logo_texture_id_;
    corner_logo_width_      = welcome_logo_width_;
    corner_logo_height_     = welcome_logo_height_;
    const bool corner_ok    = welcome_ok;

    if (welcome_ok)
        SPECTRA_LOG_DEBUG("imgui",
                          "Welcome logo texture loaded: {}x{}",
                          welcome_logo_width_,
                          welcome_logo_height_);
    if (corner_ok)
        SPECTRA_LOG_DEBUG("imgui",
                          "Corner logo: reusing welcome texture {}x{}",
                          corner_logo_width_,
                          corner_logo_height_);
}

void ImGuiIntegration::destroy_logo_textures()
{
    // Corner is an alias of welcome — clear it first without destroying
    corner_logo_texture_id_ = 0;
    corner_logo_texture_    = {};
    corner_logo_width_      = 0;
    corner_logo_height_     = 0;

    if (welcome_logo_texture_id_ != 0)
    {
        ImGui_ImplVulkan_RemoveTexture(u64_to_handle<VkDescriptorSet>(welcome_logo_texture_id_));
        welcome_logo_texture_id_ = 0;
    }

    if (backend_ && welcome_logo_texture_)
    {
        backend_->destroy_texture(welcome_logo_texture_);
        welcome_logo_texture_ = {};
    }

    welcome_logo_width_  = 0;
    welcome_logo_height_ = 0;
}

void ImGuiIntegration::open_csv_with_dialog()
{
    if (!native_dialogs_enabled())
        return;
    DialogEnvGuard env_guard;
    char const*    filters[3] = {"*.csv", "*.tsv", "*.txt"};
    const char*    home_env   = std::getenv("HOME");
    std::string    home_dir   = home_env ? std::string(home_env) + "/" : "/";
    char const*    result =
        tinyfd_openFileDialog("Open CSV File", home_dir.c_str(), 3, filters, "CSV files", 0);
    if (!result)
        return;
    load_csv_into_dialog(result);
}

void ImGuiIntegration::open_log_with_dialog()
{
    if (!log_browser_panel_ || !native_dialogs_enabled())
        return;

    // Filters from the registry, e.g. {"*.ulg", "*.mcap"}.
    std::vector<std::string> patterns;
    for (const auto& ext : log_browser_panel_->registry().extensions())
        patterns.push_back("*." + ext);
    std::vector<const char*> filters;
    for (const auto& pat : patterns)
        filters.push_back(pat.c_str());

    DialogEnvGuard env_guard;
    const char*    home_env = std::getenv("HOME");
    std::string    home_dir = home_env ? std::string(home_env) + "/" : "/";
    char const*    result   = tinyfd_openFileDialog("Open Log File",
                                               home_dir.c_str(),
                                               static_cast<int>(filters.size()),
                                               filters.empty() ? nullptr : filters.data(),
                                               "Log files",
                                               0);
    if (!result)
        return;
    open_log_file(result);
}

bool ImGuiIntegration::open_log_file(const std::string& path)
{
    if (!log_browser_panel_)
        return false;
    if (app_shell_)
        app_shell_->set_panel_visible("core.logs", true);
    if (!log_browser_panel_->open_file(path))
        return false;
    if (settings_store_)
        settings_store_->add_recent_file(path);
    return true;
}

void ImGuiIntegration::open_file_with_dialog()
{
    if (!native_dialogs_enabled())
        return;
    std::vector<std::string> patterns = {"*.csv", "*.tsv", "*.txt"};
    if (log_browser_panel_)
    {
        for (const auto& ext : log_browser_panel_->registry().extensions())
            patterns.push_back("*." + ext);
    }
    patterns.push_back("*.spectra");
    std::vector<const char*> filters;
    for (const auto& pat : patterns)
        filters.push_back(pat.c_str());

    DialogEnvGuard env_guard;
    const char*    home_env = std::getenv("HOME");
    std::string    home_dir = home_env ? std::string(home_env) + "/" : "/";
    char const*    result   = tinyfd_openFileDialog("Open File",
                                               home_dir.c_str(),
                                               static_cast<int>(filters.size()),
                                               filters.data(),
                                               "Data, logs and layouts",
                                               0);
    if (!result)
        return;
    open_path(result);
}

bool ImGuiIntegration::open_path(const std::string& path)
{
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec))
    {
        SPECTRA_LOG_WARN("app", "Cannot open {}: file not found", path);
        if (settings_store_)
            settings_store_->forget_recent_file(path);
        if (open_file_handler_)
            open_file_handler_(path);   // reports it (toast)
        return false;
    }
    if (open_file_handler_)
        return open_file_handler_(path);
    const auto is_log = [this](const std::string& p)
    { return log_browser_panel_ && log_browser_panel_->can_open(p); };
    switch (classify_open_file(path, is_log))
    {
        case OpenFileKind::Csv:
            request_csv_load(path);
            return true;
        case OpenFileKind::Log:
            return open_log_file(path);
        case OpenFileKind::Layout:
            if (!layout_session_)
                return false;
            layout_session_->request_open(path);
            return true;
        case OpenFileKind::Unsupported:
            break;
    }
    SPECTRA_LOG_WARN("app",
                     "Cannot open {}: unsupported file type (CSV, TSV, ULog, MCAP or "
                     ".spectra layout expected)",
                     path);
    return false;
}

void ImGuiIntegration::process_pending_opens()
{
    // Recent-file menu clicks, one frame later.
    if (!pending_open_paths_.empty())
    {
        std::vector<std::string> paths;
        paths.swap(pending_open_paths_);
        for (const auto& p : paths)
            open_path(p);
    }

    // Deferred CSV open request (canvas welcome button).
    if (pending_open_csv_)
    {
        pending_open_csv_ = false;
        open_csv_with_dialog();
    }

    // CSV files dropped via OS drag-and-drop (or given on the command line),
    // one column picker at a time.
    if (!csv_dialog_open_ && !pending_csv_loads_.empty())
    {
        std::string path = std::move(pending_csv_loads_.front());
        pending_csv_loads_.pop_front();
        load_csv_into_dialog(path);
    }
}

void ImGuiIntegration::show_topics_panel()
{
    if (app_shell_)
        app_shell_->set_panel_visible("core.topics", true);
    if (topics_panel_)
        topics_panel_->set_visible(true);
}

void ImGuiIntegration::run_welcome_action(const std::string& id)
{
    if (id == WELCOME_OPEN_FILE_ID)
        open_file_with_dialog();
    else if (id == WELCOME_TOPICS_ID)
        show_topics_panel();
    else if (command_registry_)
        command_registry_->execute(id);
}

void ImGuiIntegration::load_csv_into_dialog(const std::string& path)
{
    csv_file_path_   = path;
    csv_data_        = parse_csv(csv_file_path_);
    csv_data_loaded_ = csv_data_.error.empty();
    csv_error_       = csv_data_.error;
    csv_col_x_       = 0;
    csv_selected_y_.clear();
    for (size_t c = 1; c < csv_data_.num_cols; ++c)
        csv_selected_y_.push_back(static_cast<int>(c));
    if (csv_selected_y_.empty() && csv_data_.num_cols > 0)
        csv_selected_y_.push_back(0);
    csv_col_z_ = -1;
    if (csv_data_loaded_ && settings_store_)
        settings_store_->add_recent_file(path);
    // Opened on failure too: the picker shows csv_error_ and disables "Plot".
    csv_dialog_open_ = true;
    // Loaded files stay browsable in the Data sidebar.
    if (csv_data_loaded_ && data_sidebar_ && data_sidebar_->csv_provider())
        data_sidebar_->csv_provider()->add_file(csv_file_path_, csv_data_);
}

void ImGuiIntegration::draw_welcome_screen(float display_w, float display_h, float dt)
{
    (void)dt;
    namespace tokens   = ui::tokens;
    const auto& colors = theme_colors();
    ImDrawList* bg     = ImGui::GetBackgroundDrawList();

    // Menu bar height offset
    const float menu_h    = ImGui::GetFrameHeight() + 2.0f;
    const float content_h = std::max(0.0f, display_h - menu_h);
    const float cx        = std::floor(display_w * 0.5f);

    ImFont*     body_font    = ImGui::GetFont();
    const float body_size    = ImGui::GetFontSize();
    ImFont*     title_font   = font_title_ ? font_title_ : body_font;
    const float title_size   = font_title_ ? font_title_->LegacySize * 1.35f : 24.0f;
    ImFont*     heading_font = font_heading_ ? font_heading_ : body_font;
    const float heading_size = font_heading_ ? font_heading_->LegacySize : 12.0f;
    const float small_size   = 12.0f;

    // Draws `text` centred on cx, measured at the same size it is drawn with.
    auto centered_text = [&](ImFont* font, float size, float y, ImU32 col, const char* text)
    {
        ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
        bg->AddText(font, size, ImVec2(std::floor(cx - sz.x * 0.5f), y), col, text);
    };
    // `text` shortened with an ellipsis to fit `max_w` at the body font size.
    auto fit_text = [&](const std::string& text, float max_w) -> std::string
    {
        if (max_w <= 0.0f)
            return {};
        if (body_font->CalcTextSizeA(body_size, FLT_MAX, 0.0f, text.c_str()).x <= max_w)
            return text;
        std::string out = text;
        while (!out.empty())
        {
            out.pop_back();
            const std::string candidate = out + "\xE2\x80\xA6";
            if (body_font->CalcTextSizeA(body_size, FLT_MAX, 0.0f, candidate.c_str()).x <= max_w)
                return candidate;
        }
        return {};
    };

    const std::vector<WelcomeAction> actions =
        build_welcome_actions(command_registry_, shortcut_manager_);
    static const std::vector<std::string> no_entries;
    const std::vector<std::string>&       recent_files =
        settings_store_ ? settings_store_->data().recent_files : no_entries;
    const std::vector<std::string>& recent_layouts =
        layout_session_ ? layout_session_->recent().entries() : no_entries;
    std::vector<WelcomeRecentItem> recent = build_welcome_recent(recent_files, recent_layouts);

    // ── Geometry ──
    // Two columns side by side when they fit, otherwise stacked; the logo and
    // then recent rows give way on short windows.
    const float row_h    = std::floor(ImGui::GetFrameHeight() + 6.0f);
    const float header_h = std::floor(heading_size + tokens::SPACE_3 + 1.0f);
    const float side     = tokens::SPACE_6;
    const float col_gap  = tokens::SPACE_12;
    const float wide_col = 300.0f;
    const bool  two_cols = display_w >= 2.0f * wide_col + col_gap + 2.0f * side;
    const float col_w =
        two_cols ? wide_col : std::max(160.0f, std::min(360.0f, display_w - 2.0f * side));
    const float footer_h      = 64.0f;
    const float avail_h       = std::max(0.0f, content_h - footer_h - tokens::SPACE_4);
    const float start_h       = header_h + row_h * static_cast<float>(actions.size());
    const float subtitle_size = small_size;
    float       logo_sz       = std::clamp(content_h * 0.16f, 72.0f, 128.0f);
    bool        show_logo     = welcome_logo_texture_id_ != 0;
    bool        show_hero     = true;
    const float hero_gap      = tokens::SPACE_8;

    auto hero_height = [&]()
    {
        if (!show_hero)
            return 0.0f;
        float h = title_size + tokens::SPACE_1 + subtitle_size + hero_gap;
        if (show_logo)
            h += logo_sz + tokens::SPACE_4;
        return h;
    };
    auto cols_height = [&](size_t recent_rows)
    {
        const float recent_h =
            header_h + row_h * static_cast<float>(std::max<size_t>(1, recent_rows));
        return two_cols ? std::max(start_h, recent_h) : start_h + tokens::SPACE_6 + recent_h;
    };

    size_t recent_rows = recent.size();
    if (hero_height() + cols_height(recent_rows) > avail_h)
        show_logo = false;
    while (recent_rows > 1 && hero_height() + cols_height(recent_rows) > avail_h)
        --recent_rows;
    if (hero_height() + cols_height(recent_rows) > avail_h)
        show_hero = false;
    recent.resize(std::min(recent.size(), recent_rows));

    const float total_h = hero_height() + cols_height(recent_rows);
    float       y       = menu_h + std::max(tokens::SPACE_4, (avail_h - total_h) * 0.45f);

    // ── Logo, title, subtitle ──
    if (show_hero)
    {
        if (show_logo)
        {
            const float lx = cx - logo_sz * 0.5f;
            bg->AddCircleFilled(ImVec2(cx, y + logo_sz * 0.5f),
                                logo_sz * 0.62f,
                                ui::to_imu32(colors.accent, 0.06f),
                                96);
            bg->AddImage(imgui_texture_id_from_u64(welcome_logo_texture_id_),
                         ImVec2(lx, y),
                         ImVec2(lx + logo_sz, y + logo_sz));
            y += logo_sz + tokens::SPACE_4;
        }
        centered_text(title_font, title_size, y, ui::to_imu32(colors.text_primary), "Spectra");
        y += title_size + tokens::SPACE_1;
        centered_text(body_font,
                      subtitle_size,
                      y,
                      ui::to_imu32(colors.text_secondary, 0.86f),
                      "GPU-accelerated scientific visualization");
        y += subtitle_size + hero_gap;
    }

    // ── Columns ──
    const ImGuiWindowFlags col_flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollWithMouse;

    // Column header: small upper-case label over a hairline.
    auto column_header = [&](const char* label)
    {
        ImDrawList*  dl  = ImGui::GetWindowDrawList();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        dl->AddText(heading_font,
                    heading_size,
                    ImVec2(pos.x + tokens::SPACE_2, pos.y),
                    ui::to_imu32(colors.text_tertiary),
                    label);
        const float line_y = std::floor(pos.y + header_h - tokens::SPACE_2) + 0.5f;
        dl->AddLine(ImVec2(pos.x, line_y),
                    ImVec2(pos.x + col_w, line_y),
                    ui::to_imu32(colors.border_subtle),
                    1.0f);
        ImGui::Dummy(ImVec2(col_w, header_h));
    };

    // One clickable row: icon, label, dimmed detail, right-aligned tag/shortcut.
    auto row = [&](const char*        id,
                   uint16_t           icon,
                   bool               accent_icon,
                   const std::string& label,
                   const std::string& detail,
                   const std::string& right) -> bool
    {
        ImDrawList*  dl      = ImGui::GetWindowDrawList();
        const ImVec2 pos     = ImGui::GetCursorScreenPos();
        const bool   clicked = ImGui::InvisibleButton(id, ImVec2(col_w, row_h));
        const bool   hovered = ImGui::IsItemHovered();
        const bool   active  = ImGui::IsItemActive();
        if (hovered)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (hovered || active)
            dl->AddRectFilled(pos,
                              ImVec2(pos.x + col_w, pos.y + row_h),
                              ui::to_imu32(colors.accent, active ? 0.18f : 0.10f),
                              tokens::RADIUS_MD);

        const float pad    = tokens::SPACE_2;
        const float text_y = std::floor(pos.y + (row_h - body_size) * 0.5f);
        float       x      = pos.x + pad;
        if (icon != 0)
        {
            const float icon_sz = 14.0f;
            if (ImFont* ifont = ui::icon_font(icon_sz))
            {
                const char*  glyph = ui::icon_str(static_cast<ui::Icon>(icon));
                const ImVec2 isz   = ifont->CalcTextSizeA(icon_sz, FLT_MAX, 0.0f, glyph);
                dl->AddText(ifont,
                            icon_sz,
                            ImVec2(std::floor(x + (18.0f - isz.x) * 0.5f),
                                   std::floor(pos.y + (row_h - isz.y) * 0.5f)),
                            accent_icon || hovered ? ui::to_imu32(colors.accent)
                                                   : ui::to_imu32(colors.text_secondary),
                            glyph);
            }
            x += 18.0f + tokens::SPACE_3;
        }

        float right_w = 0.0f;
        if (!right.empty())
        {
            right_w = body_font->CalcTextSizeA(small_size, FLT_MAX, 0.0f, right.c_str()).x;
            dl->AddText(body_font,
                        small_size,
                        ImVec2(pos.x + col_w - pad - right_w,
                               std::floor(pos.y + (row_h - small_size) * 0.5f)),
                        ui::to_imu32(colors.text_tertiary),
                        right.c_str());
            right_w += tokens::SPACE_3;
        }

        const float       text_max = pos.x + col_w - pad - right_w;
        const std::string shown    = fit_text(label, text_max - x);
        dl->AddText(body_font,
                    body_size,
                    ImVec2(x, text_y),
                    ui::to_imu32(colors.text_primary),
                    shown.c_str());
        if (!detail.empty() && shown == label)
        {
            const float label_w =
                body_font->CalcTextSizeA(body_size, FLT_MAX, 0.0f, label.c_str()).x;
            const float       dx = x + label_w + tokens::SPACE_2;
            const std::string d  = fit_text(detail, text_max - dx);
            if (!d.empty())
                dl->AddText(body_font,
                            body_size,
                            ImVec2(dx, text_y),
                            ui::to_imu32(colors.text_tertiary),
                            d.c_str());
        }
        return clicked;
    };

    const float cols_w   = two_cols ? 2.0f * col_w + col_gap : col_w;
    const float start_x  = std::floor(cx - cols_w * 0.5f);
    const float recent_x = two_cols ? start_x + col_w + col_gap : start_x;
    const float recent_y = two_cols ? y : y + start_h + tokens::SPACE_6;
    const float recent_h =
        header_h + row_h * static_cast<float>(std::max<size_t>(1, recent.size()));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

    std::string clicked_action;
    ImGui::SetNextWindowPos(ImVec2(start_x, y));
    ImGui::SetNextWindowSize(ImVec2(col_w, start_h));
    if (ImGui::Begin("##welcome_start", nullptr, col_flags))
    {
        column_header("START");
        for (const WelcomeAction& a : actions)
        {
            if (row(a.id.c_str(), a.icon, a.primary, a.label, {}, a.shortcut))
                clicked_action = a.id;
        }
    }
    ImGui::End();

    int clicked_recent = -1;
    ImGui::SetNextWindowPos(ImVec2(recent_x, recent_y));
    ImGui::SetNextWindowSize(ImVec2(col_w, recent_h));
    if (ImGui::Begin("##welcome_recent", nullptr, col_flags))
    {
        column_header("RECENT");
        if (recent.empty())
        {
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddText(
                body_font,
                body_size,
                ImVec2(pos.x + tokens::SPACE_2, std::floor(pos.y + (row_h - body_size) * 0.5f)),
                ui::to_imu32(colors.text_tertiary),
                "Files and layouts you open show up here");
            ImGui::Dummy(ImVec2(col_w, row_h));
        }
        for (size_t i = 0; i < recent.size(); ++i)
        {
            const WelcomeRecentItem& item = recent[i];
            ImGui::PushID(static_cast<int>(i));
            const uint16_t icon =
                static_cast<uint16_t>(item.layout ? ui::Icon::Layout : ui::Icon::FileText);
            if (row("##recent", icon, false, item.name, item.folder, item.kind))
                clicked_recent = static_cast<int>(i);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip("%s", item.path.c_str());
            ImGui::PopID();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);

    // Acted on after drawing: opening a file edits the lists drawn above.
    if (!clicked_action.empty())
        run_welcome_action(clicked_action);
    if (clicked_recent >= 0)
    {
        const WelcomeRecentItem& item = recent[static_cast<size_t>(clicked_recent)];
        if (item.layout && layout_session_)
        {
            std::error_code ec;
            if (std::filesystem::is_regular_file(item.path, ec))
                layout_session_->request_open(item.path);
            else
            {
                SPECTRA_LOG_WARN("layout", "Recent layout {} no longer exists", item.path);
                layout_session_->recent().remove(item.path);
            }
        }
        else
        {
            open_path(item.path);
        }
    }

    // ── Footer: drop hint (+ command palette shortcut) and version ──
    std::string       hint = "Drop CSV, ULog or MCAP files anywhere";
    const std::string palette =
        welcome_command_shortcut(command_registry_, shortcut_manager_, "app.command_palette");
    if (!palette.empty())
        hint += "  \xC2\xB7  " + palette + "  Command palette";
    if (body_font->CalcTextSizeA(small_size, FLT_MAX, 0.0f, hint.c_str()).x
        > display_w - 2.0f * side)
        hint = "Drop CSV, ULog or MCAP files anywhere";
    centered_text(body_font,
                  small_size,
                  display_h - 52.0f,
                  ui::to_imu32(colors.text_secondary, 0.9f),
                  hint.c_str());
    {
    #ifdef SPECTRA_VERSION_STRING
        const char* ver = "v" SPECTRA_VERSION_STRING;
    #else
        const char* ver = "v0.1.2";
    #endif
        centered_text(body_font,
                      11.0f,
                      display_h - 28.0f,
                      ui::to_imu32(colors.text_tertiary, 0.86f),
                      ver);
    }
}

void ImGuiIntegration::render(VulkanBackend& backend) const
{
    if (!initialized_)
        return;
    ImGui::Render();
    auto* dd = ImGui::GetDrawData();
    if (dd)
        ImGui_ImplVulkan_RenderDrawData(dd, backend.current_command_buffer());
}

void ImGuiIntegration::render_viewports()
{
    // No-op — see enable_viewports().
}

bool ImGuiIntegration::wants_capture_mouse() const
{
    if (!initialized_)
        return false;

    bool wants_capture      = ImGui::GetIO().WantCaptureMouse;
    bool any_window_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
    bool any_item_hovered   = ImGui::IsAnyItemHovered();
    bool any_item_active    = ImGui::IsAnyItemActive();

    // If an ImGui item is actively being interacted with (e.g. dragging a slider),
    // always capture — regardless of cursor position.
    if (any_item_active)
        return true;

    if (layout_manager_)
    {
        Rect       canvas    = layout_manager_->canvas_rect();
        ImVec2     mouse     = ImGui::GetIO().MousePos;
        const bool in_canvas = mouse.x >= canvas.x && mouse.x < canvas.x + canvas.w
                               && mouse.y >= canvas.y && mouse.y < canvas.y + canvas.h;

        // Outside the plot canvas: never forward pointer events to InputHandler.
        // Chrome windows (command bar, nav rail, status bar, inspector) may not set
        // WantCaptureMouse on empty padding, but they still own the pointer.
        if (!in_canvas)
            return true;

        // Inside canvas: pass through unless an ImGui item/window is hovered
        // (floating panels, inspector toggle, pane tab headers, etc.).
        return any_item_hovered || any_window_hovered;
    }

    return wants_capture && (any_window_hovered || any_item_hovered);
}
bool ImGuiIntegration::wants_capture_keyboard() const
{
    return initialized_ && ImGui::GetIO().WantCaptureKeyboard;
}

// ─── Fonts ──────────────────────────────────────────────────────────────────

void ImGuiIntegration::load_fonts()
{
    ImGuiIO& io = ImGui::GetIO();

    // Font Awesome 6 Free Solid — only the codepoints we actually use.
    // Two coalesced ranges covering our Icon enum values:
    //   U+E473–U+E522  (supplemental: chart-simple, magnifying-glass-chart)
    //   U+F002–U+F698  (main FA6 block — covers all our icons)
    static const ImWchar icon_ranges[] = {0xE473, 0xE522, 0xF002, 0xF698, 0};

    // Text glyphs baked from Inter: Latin + Latin-1, Greek, General
    // Punctuation (en/em dash, curly quotes, ellipsis, bullet), super/
    // subscripts, letterlike symbols, arrows and common math operators.
    // Must stay in sync (roughly) with TextRenderer's plot-text atlas.
    static const ImWchar text_ranges[] = {
        0x0020,
        0x00FF,   // Basic Latin + Latin-1 Supplement
        0x0391,
        0x03C9,   // Greek
        0x2010,
        0x205E,   // General Punctuation
        0x2070,
        0x209F,   // Superscripts and Subscripts
        0x20AC,
        0x20AC,   // Euro sign
        0x2100,
        0x214F,   // Letterlike Symbols
        0x2190,
        0x21FF,   // Arrows
        0x2200,
        0x22FF,   // Mathematical Operators
        0,
    };

    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;   // we own the static data
    cfg.OversampleH          = 4;       // sharper horizontal sub-pixel positioning
    cfg.OversampleV          = 2;       // sharper vertical edges
    cfg.PixelSnapH           = true;    // snap glyphs to whole pixels for crispness

    // Helper lambda: add Inter font at a given size, then merge FA6 icons
    auto add_font_pair = [&](float size) -> ImFont*
    {
        cfg.SizePixels = 0;
        ImFont* font   = io.Fonts->AddFontFromMemoryCompressedTTF(InterFont_compressed_data,
                                                                InterFont_compressed_size,
                                                                size,
                                                                &cfg,
                                                                text_ranges);

        ImFontConfig icon_cfg;
        icon_cfg.FontDataOwnedByAtlas = false;
        icon_cfg.MergeMode            = true;
        icon_cfg.GlyphMinAdvanceX     = 0.0f;   // natural glyph widths
        icon_cfg.PixelSnapH           = true;

        io.Fonts->AddFontFromMemoryTTF((void*)fa_solid_900_data,
                                       fa_solid_900_size,
                                       size,
                                       &icon_cfg,
                                       icon_ranges);

        // System fallback fonts for glyphs Inter lacks (maths operators,
        // CJK, monochrome emoji, ...).  ImGui 1.92 rasterises glyphs on demand
        // from the first source that has them, so merging costs nothing until
        // used.  No glyph ranges: with IMGUI_USE_WCHAR32 (CMakeLists.txt) this
        // covers the whole Unicode range, including code points above U+FFFF.
        for (const auto& file : fallback_font_files_)
        {
            ImFontConfig fb_cfg;
            fb_cfg.FontDataOwnedByAtlas = false;
            fb_cfg.MergeMode            = true;
            fb_cfg.OversampleH          = 2;
            fb_cfg.OversampleV          = 1;
            fb_cfg.PixelSnapH           = true;
            io.Fonts->AddFontFromMemoryTTF(const_cast<uint8_t*>(file.data()),
                                           static_cast<int>(file.size()),
                                           size,
                                           &fb_cfg);
        }

        // No font has the glyph: show a hollow box (U+25A1, in Inter) rather
        // than '?', which reads as part of the text.
        if (font && font->IsGlyphInFont(0x25A1))
            font->FallbackChar = static_cast<ImWchar>(0x25A1);
        return font;
    };

    fallback_font_files_.clear();
    for (const auto& path : discover_fallback_fonts())
    {
        FontFileData file;
        // Outline fonts only: ImGui's stb_truetype loader cannot use colour
        // bitmap emoji fonts (no glyf/CFF table).
        if (file.load(path) && is_outline_font(file.data(), file.size()))
            fallback_font_files_.push_back(std::move(file));
    }

    // Body font (16px) + FA6 icon merge
    font_body_ = add_font_pair(16.0f);

    // Heading font (12.5px) + FA6 icon merge
    font_heading_ = add_font_pair(11.5f);

    // Icon font (20px) — primary icon font with Inter merged in
    font_icon_ = add_font_pair(20.0f);

    // Title font (18px) + FA6 icon merge
    font_title_ = add_font_pair(20.0f);

    // Menubar font (15px) + FA6 icon merge
    font_menubar_ = add_font_pair(14.0f);

    io.FontDefault = font_body_;
}

// ─── Style ──────────────────────────────────────────────────────────────────

void ImGuiIntegration::apply_modern_style()
{
    // Apply theme colors through ThemeManager
    if (theme_mgr_)
        theme_mgr_->apply_to_imgui();
}

WidgetCursor ImGuiIntegration::widget_cursor() const
{
    switch (ImGui::GetMouseCursor())
    {
        case ImGuiMouseCursor_TextInput:
            return WidgetCursor::TextInput;
        case ImGuiMouseCursor_ResizeAll:
            return WidgetCursor::ResizeAll;
        case ImGuiMouseCursor_ResizeNS:
            return WidgetCursor::ResizeNS;
        case ImGuiMouseCursor_ResizeEW:
            return WidgetCursor::ResizeEW;
        case ImGuiMouseCursor_ResizeNESW:
            return WidgetCursor::ResizeNESW;
        case ImGuiMouseCursor_ResizeNWSE:
            return WidgetCursor::ResizeNWSE;
        case ImGuiMouseCursor_Hand:
            return WidgetCursor::Hand;
        case ImGuiMouseCursor_NotAllowed:
            return WidgetCursor::NotAllowed;
        default:
            return WidgetCursor::Arrow;
    }
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
