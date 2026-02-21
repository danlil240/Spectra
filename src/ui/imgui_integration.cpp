#ifdef SPECTRA_USE_IMGUI

    #include "imgui_integration.hpp"

    #include <imgui.h>
    #include <imgui_impl_glfw.h>
    #include <imgui_impl_vulkan.h>
    #include <spectra/axes.hpp>
    #include <spectra/axes3d.hpp>
    #include <spectra/camera.hpp>
    #include <spectra/figure.hpp>
    #include <spectra/logger.hpp>
    #include <spectra/math3d.hpp>
    #include <spectra/series.hpp>
    #include <unordered_map>

    #include "../render/vulkan/vk_backend.hpp"
    #include "animation_curve_editor.hpp"
    #include "axis_link.hpp"
    #include "box_zoom_overlay.hpp"
    #include "command_palette.hpp"
    #include "command_registry.hpp"
    #include "data_interaction.hpp"
    #include "data_transform.hpp"
    #include "design_tokens.hpp"
    #include "dock_system.hpp"
    #include "icons.hpp"
    #include "keyframe_interpolator.hpp"
    #include "knob_manager.hpp"
    #include "mode_transition.hpp"
    #include "tab_bar.hpp"
    #include "tab_drag_controller.hpp"
    #include "theme.hpp"
    #include "timeline_editor.hpp"
    #include "widgets.hpp"
    #include "window_manager.hpp"

    #define GLFW_INCLUDE_NONE
    #define GLFW_INCLUDE_VULKAN
    #include <GLFW/glfw3.h>

    // Compressed Inter font data
    #include "../../third_party/inter_font.hpp"

    // Embedded icon font data (PUA codepoints U+E001-U+E062)
    #include <algorithm>
    #include <cmath>
    #include <cstdio>
    #include <string>

    #include "../../third_party/icon_font_data.hpp"

    #ifndef M_PI
        #define M_PI 3.14159265358979323846
    #endif

namespace spectra
{

// ─── Lifecycle ──────────────────────────────────────────────────────────────

ImGuiIntegration::~ImGuiIntegration()
{
    shutdown();
}

bool ImGuiIntegration::init(VulkanBackend& backend, GLFWwindow* window, bool install_callbacks)
{
    if (initialized_)
        return true;
    if (!window)
        return false;

    glfw_window_ = window;
    layout_manager_ = std::make_unique<LayoutManager>();

    IMGUI_CHECKVERSION();
    // Each window gets its own font atlas so that creating a secondary
    // window mid-frame doesn't hit the "locked ImFontAtlas" assertion
    // (the primary window's shared atlas is locked between NewFrame/EndFrame).
    owned_font_atlas_ = std::make_unique<ImFontAtlas>();
    imgui_context_ = ImGui::CreateContext(owned_font_atlas_.get());
    // CreateContext() restores the previous context if one exists (ImGui 1.90+).
    // We must explicitly switch to the new context so load_fonts() and
    // backend init operate on the correct context/atlas.
    ImGui::SetCurrentContext(imgui_context_);

    // Initialize theme system
    ui::ThemeManager::instance();

    // Initialize icon font system
    ui::IconFont::instance().initialize();

    load_fonts();
    apply_modern_style();

    // Wire inspector fonts
    inspector_.set_fonts(font_body_, font_heading_, font_title_);

    // For secondary windows, pass install_callbacks=false so ImGui doesn't
    // install its own GLFW callbacks.  WindowManager handles context switching
    // and input forwarding for secondary windows.  If ImGui installs callbacks
    // on a secondary window, they fire during glfwPollEvents() with the wrong
    // ImGui context (the primary's), routing all input to the primary window.
    ImGui_ImplGlfw_InitForVulkan(window, install_callbacks);

    ImGui_ImplVulkan_InitInfo ii{};
    ii.Instance = backend.instance();
    ii.PhysicalDevice = backend.physical_device();
    ii.Device = backend.device();
    ii.QueueFamily = backend.graphics_queue_family();
    ii.Queue = backend.graphics_queue();
    ii.DescriptorPool = backend.descriptor_pool();
    ii.MinImageCount = backend.min_image_count();
    ii.ImageCount = backend.image_count();
    ii.RenderPass = backend.render_pass();
    ii.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&ii);
    ImGui_ImplVulkan_CreateFontsTexture();

    cached_render_pass_ = reinterpret_cast<uint64_t>(ii.RenderPass);
    initialized_ = true;
    return true;
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

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext(this_ctx);
    imgui_context_ = nullptr;

    // Restore previous context (if it was a different context)
    if (prev_ctx && prev_ctx != this_ctx)
        ImGui::SetCurrentContext(prev_ctx);
    else
        ImGui::SetCurrentContext(nullptr);

    layout_manager_.reset();
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
    VkRenderPass current_rp = backend.render_pass();
    auto current_rp_bits = reinterpret_cast<uint64_t>(current_rp);
    if (current_rp_bits != cached_render_pass_ && current_rp != VK_NULL_HANDLE)
    {
        SPECTRA_LOG_WARN(
            "imgui",
            "Render pass changed after swapchain recreation — reinitializing ImGui Vulkan backend");
        ImGui_ImplVulkan_Shutdown();

        ImGui_ImplVulkan_InitInfo ii{};
        ii.Instance = backend.instance();
        ii.PhysicalDevice = backend.physical_device();
        ii.Device = backend.device();
        ii.QueueFamily = backend.graphics_queue_family();
        ii.Queue = backend.graphics_queue();
        ii.DescriptorPool = backend.descriptor_pool();
        ii.MinImageCount = backend.min_image_count();
        ii.ImageCount = backend.image_count();
        ii.RenderPass = current_rp;
        ii.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

        ImGui_ImplVulkan_Init(&ii);
        ImGui_ImplVulkan_CreateFontsTexture();

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

void ImGuiIntegration::new_frame()
{
    if (!initialized_)
        return;
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Update layout with current window size and delta time
    ImGuiIO& io = ImGui::GetIO();
    update_layout(io.DisplaySize.x, io.DisplaySize.y, io.DeltaTime);
}

void ImGuiIntegration::build_ui(Figure& figure)
{
    if (!initialized_)
    {
        SPECTRA_LOG_WARN("ui", "build_ui called but ImGui is not initialized");
        return;
    }

    SPECTRA_LOG_TRACE("ui", "Building UI for figure");
    current_figure_ = &figure;

    float dt = ImGui::GetIO().DeltaTime;
    ui::ThemeManager::instance().update(dt);
    ui::widgets::update_section_animations(dt);
    float target = panel_open_ ? 1.0f : 0.0f;
    panel_anim_ += (target - panel_anim_) * std::min(1.0f, 10.0f * dt);
    if (std::abs(panel_anim_ - target) < 0.002f)
        panel_anim_ = target;

    // Update bottom panel height so canvas shrinks when timeline is open
    if (layout_manager_)
    {
        float target_h = (show_timeline_ && timeline_editor_) ? 200.0f : 0.0f;
        float cur_h = layout_manager_->bottom_panel_height();
        float new_h = cur_h + (target_h - cur_h) * std::min(1.0f, 12.0f * dt);
        if (std::abs(new_h - target_h) < 0.5f)
            new_h = target_h;
        layout_manager_->set_bottom_panel_height(new_h);
    }

    // Draw all zones using layout manager
    draw_command_bar();
    draw_nav_rail();
    draw_canvas(figure);
    draw_plot_text(figure);
    draw_axis_link_indicators(figure);
    draw_axes_context_menu(figure);
    if (layout_manager_->is_inspector_visible())
    {
        draw_inspector(figure);
    }
    draw_status_bar();
    draw_pane_tab_headers();  // Must run before splitters so pane_tab_hovered_ is set
    draw_split_view_splitters();
    #if SPECTRA_FLOATING_TOOLBAR
    draw_floating_toolbar();
    #endif

    // Draw timeline panel (Agent G — bottom dock)
    if (show_timeline_ && timeline_editor_)
    {
        draw_timeline_panel();
    }

    // Draw curve editor window (Agent G — floating)
    if (show_curve_editor_ && curve_editor_)
    {
        draw_curve_editor_panel();
    }

    // Draw deferred tooltip (command bar) on top of everything
    if (deferred_tooltip_)
    {
        ImGui::SetNextWindowPos(ImGui::GetIO().MousePos, ImGuiCond_Always, ImVec2(0.5f, 1.0f));
        ImGui::BeginTooltip();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 6));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::tokens::RADIUS_MD);
        ImGui::PushStyleColor(ImGuiCol_PopupBg,
                              ImVec4(ui::theme().bg_elevated.r,
                                     ui::theme().bg_elevated.g,
                                     ui::theme().bg_elevated.b,
                                     0.95f));
        ImGui::PushStyleColor(ImGuiCol_Border,
                              ImVec4(ui::theme().border_subtle.r,
                                     ui::theme().border_subtle.g,
                                     ui::theme().border_subtle.b,
                                     0.3f));
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(ui::theme().text_primary.r,
                                     ui::theme().text_primary.g,
                                     ui::theme().text_primary.b,
                                     ui::theme().text_primary.a));
        ImGui::TextUnformatted(deferred_tooltip_);
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);
        ImGui::EndTooltip();
        deferred_tooltip_ = nullptr;  // Clear for next frame
    }

    // Draw data interaction overlays (tooltip, crosshair, markers) on top of everything
    if (data_interaction_)
    {
        ImGuiIO& io = ImGui::GetIO();
        data_interaction_->draw_overlays(io.DisplaySize.x, io.DisplaySize.y);
    }

    // Draw box zoom overlay (Agent B Week 7) — on top of data overlays
    if (box_zoom_overlay_)
    {
        box_zoom_overlay_->update(dt);
        ImGuiIO& io = ImGui::GetIO();
        box_zoom_overlay_->draw(io.DisplaySize.x, io.DisplaySize.y);
    }

    // Draw measure overlay (Measure tool mode)
    // Crosshair is handled by data_interaction (auto-enabled when entering Measure mode)
    if (input_handler_ && input_handler_->tool_mode() == ToolMode::Measure)
    {
        Axes* ax = input_handler_->active_axes();
        bool dragging = input_handler_->is_measure_dragging();
        bool has_result = input_handler_->has_measure_result();
        if (ax && (dragging || has_result))
        {
            const auto& vp = ax->viewport();
            auto xlim = ax->x_limits();
            auto ylim = ax->y_limits();

            auto data_to_screen = [&](float dx_, float dy_, float& scr_x, float& scr_y)
            {
                scr_x = vp.x + (dx_ - xlim.min) / (xlim.max - xlim.min) * vp.w;
                scr_y = vp.y + (1.0f - (dy_ - ylim.min) / (ylim.max - ylim.min)) * vp.h;
            };

            float sx = input_handler_->measure_start_data_x();
            float sy = input_handler_->measure_start_data_y();
            float ex = input_handler_->measure_end_data_x();
            float ey = input_handler_->measure_end_data_y();

            float mdx = ex - sx;
            float mdy = ey - sy;
            float dist = std::sqrt(mdx * mdx + mdy * mdy);
            if (dist > 1e-6f)
            {
                auto* dl = ImGui::GetForegroundDrawList();
                auto accent = ui::theme().accent;
                ImU32 line_col = IM_COL32(
                    uint8_t(accent.r * 255), uint8_t(accent.g * 255), uint8_t(accent.b * 255), 220);
                ImU32 dot_col = IM_COL32(
                    uint8_t(accent.r * 255), uint8_t(accent.g * 255), uint8_t(accent.b * 255), 255);
                ImU32 bg_col = IM_COL32(uint8_t(ui::theme().bg_elevated.r * 255),
                                        uint8_t(ui::theme().bg_elevated.g * 255),
                                        uint8_t(ui::theme().bg_elevated.b * 255),
                                        230);

                float scr_sx, scr_sy, scr_ex, scr_ey;
                data_to_screen(sx, sy, scr_sx, scr_sy);
                data_to_screen(ex, ey, scr_ex, scr_ey);

                // Measurement line
                dl->AddLine(ImVec2(scr_sx, scr_sy), ImVec2(scr_ex, scr_ey), line_col, 2.0f);

                // Endpoint dots
                dl->AddCircleFilled(ImVec2(scr_sx, scr_sy), 4.0f, dot_col);
                dl->AddCircleFilled(ImVec2(scr_ex, scr_ey), 4.0f, dot_col);

                // Distance label at midpoint
                float mid_x = (scr_sx + scr_ex) * 0.5f;
                float mid_y = (scr_sy + scr_ey) * 0.5f;
                char label[128];
                std::snprintf(
                    label, sizeof(label), "dX: %.4f  dY: %.4f  dist: %.4f", mdx, mdy, dist);
                ImVec2 tsz = ImGui::CalcTextSize(label);
                float pad = 6.0f;
                dl->AddRectFilled(ImVec2(mid_x - tsz.x * 0.5f - pad, mid_y - tsz.y - pad * 2),
                                  ImVec2(mid_x + tsz.x * 0.5f + pad, mid_y - pad * 0.5f),
                                  bg_col,
                                  4.0f);
                dl->AddText(ImVec2(mid_x - tsz.x * 0.5f, mid_y - tsz.y - pad),
                            IM_COL32(255, 255, 255, 240),
                            label);
            }
        }
    }

    // Draw theme settings window if open
    if (show_theme_settings_)
    {
        draw_theme_settings();
    }

    // Draw directional dock highlight overlay when another window is dragging a tab over this one
    if (window_manager_ && window_id_ != 0 && window_manager_->drag_target_window() == window_id_)
    {
        auto& theme = ui::theme();
        auto drop_info = window_manager_->cross_window_drop_info();
        ImDrawList* dl = ImGui::GetForegroundDrawList();

        if (drop_info.zone >= 1 && drop_info.zone <= 5)
        {
            // Draw highlight rect for the active drop zone
            ImU32 highlight_color = IM_COL32(static_cast<int>(theme.accent.r * 255),
                                             static_cast<int>(theme.accent.g * 255),
                                             static_cast<int>(theme.accent.b * 255),
                                             40);
            ImU32 highlight_border = IM_COL32(static_cast<int>(theme.accent.r * 255),
                                              static_cast<int>(theme.accent.g * 255),
                                              static_cast<int>(theme.accent.b * 255),
                                              160);

            float hx = drop_info.hx, hy = drop_info.hy;
            float hw = drop_info.hw, hh = drop_info.hh;

            dl->AddRectFilled(ImVec2(hx, hy), ImVec2(hx + hw, hy + hh), highlight_color, 4.0f);
            dl->AddRect(
                ImVec2(hx, hy), ImVec2(hx + hw, hy + hh), highlight_border, 4.0f, 0, 2.0f);

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
                float lx = hx + (hw - lsz.x) * 0.5f;
                float ly = hy + (hh - lsz.y) * 0.5f;
                float pad = 10.0f;
                dl->AddRectFilled(ImVec2(lx - pad, ly - pad),
                                  ImVec2(lx + lsz.x + pad, ly + lsz.y + pad),
                                  IM_COL32(30, 30, 30, 200),
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

    // Draw knobs panel last (above all other windows, user-moveable)
    if (knob_manager_ && !knob_manager_->empty())
    {
        draw_knobs_panel();
    }

    // Draw command palette overlay (Agent F) — must be last to render on top
    if (command_palette_)
    {
        ImGuiIO& io = ImGui::GetIO();
        command_palette_->draw(io.DisplaySize.x, io.DisplaySize.y);
    }
}

void ImGuiIntegration::render(VulkanBackend& backend)
{
    if (!initialized_)
        return;
    ImGui::Render();
    auto* dd = ImGui::GetDrawData();
    if (dd)
        ImGui_ImplVulkan_RenderDrawData(dd, backend.current_command_buffer());
}

bool ImGuiIntegration::wants_capture_mouse() const
{
    if (!initialized_)
        return false;

    bool wants_capture = ImGui::GetIO().WantCaptureMouse;
    bool any_window_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
    bool any_item_hovered = ImGui::IsAnyItemHovered();
    bool any_item_active = ImGui::IsAnyItemActive();

    SPECTRA_LOG_TRACE("input",
                      "ImGui mouse capture state - wants_capture: "
                          + std::string(wants_capture ? "true" : "false") + ", window_hovered: "
                          + std::string(any_window_hovered ? "true" : "false")
                          + ", item_hovered: " + std::string(any_item_hovered ? "true" : "false")
                          + ", item_active: " + std::string(any_item_active ? "true" : "false"));

    // If an ImGui item is actively being interacted with (e.g. dragging a slider),
    // always capture — regardless of cursor position.
    if (any_item_active)
        return true;

    // If the cursor is inside the canvas area, let mouse events pass through to
    // InputHandler even when ImGui windows overlap (floating toolbar, status bar
    // edges, etc.).  The canvas ##window has NoInputs so it shouldn't capture,
    // but adjacent/overlapping windows cause false positives.
    if (layout_manager_)
    {
        Rect canvas = layout_manager_->canvas_rect();
        ImVec2 mouse = ImGui::GetIO().MousePos;
        if (mouse.x >= canvas.x && mouse.x <= canvas.x + canvas.w && mouse.y >= canvas.y
            && mouse.y <= canvas.y + canvas.h)
        {
            // Capture if an interactive item is hovered OR if the mouse is over
            // any ImGui window (e.g. the knobs panel title bar being dragged).
            return any_item_hovered || any_window_hovered;
        }
    }

    // Outside canvas: original logic
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

    // Icon font glyph range: PUA U+E001 - U+E063
    static const ImWchar icon_ranges[] = {0xE001, 0xE063, 0};

    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;  // we own the static data

    ImFontConfig icon_cfg;
    icon_cfg.FontDataOwnedByAtlas = false;
    icon_cfg.MergeMode = true;  // merge into previous font
    icon_cfg.GlyphMinAdvanceX = 0.0f;
    icon_cfg.PixelSnapH = true;

    // Body font (16px) + icon merge
    cfg.SizePixels = 0;
    font_body_ = io.Fonts->AddFontFromMemoryCompressedTTF(
        InterFont_compressed_data, InterFont_compressed_size, 16.0f, &cfg);
    io.Fonts->AddFontFromMemoryTTF(
        (void*)SpectraIcons_data, SpectraIcons_size, 16.0f, &icon_cfg, icon_ranges);

    // Heading font (12.5px) + icon merge
    font_heading_ = io.Fonts->AddFontFromMemoryCompressedTTF(
        InterFont_compressed_data, InterFont_compressed_size, 12.5f, &cfg);
    io.Fonts->AddFontFromMemoryTTF(
        (void*)SpectraIcons_data, SpectraIcons_size, 12.5f, &icon_cfg, icon_ranges);

    // Icon font (20px) — primary icon font with Inter merged in
    font_icon_ = io.Fonts->AddFontFromMemoryCompressedTTF(
        InterFont_compressed_data, InterFont_compressed_size, 20.0f, &cfg);
    io.Fonts->AddFontFromMemoryTTF(
        (void*)SpectraIcons_data, SpectraIcons_size, 20.0f, &icon_cfg, icon_ranges);

    // Title font (18px) + icon merge
    font_title_ = io.Fonts->AddFontFromMemoryCompressedTTF(
        InterFont_compressed_data, InterFont_compressed_size, 18.0f, &cfg);
    io.Fonts->AddFontFromMemoryTTF(
        (void*)SpectraIcons_data, SpectraIcons_size, 18.0f, &icon_cfg, icon_ranges);

    // Menubar font (15px) + icon merge
    font_menubar_ = io.Fonts->AddFontFromMemoryCompressedTTF(
        InterFont_compressed_data, InterFont_compressed_size, 15.0f, &cfg);
    io.Fonts->AddFontFromMemoryTTF(
        (void*)SpectraIcons_data, SpectraIcons_size, 15.0f, &icon_cfg, icon_ranges);

    io.FontDefault = font_body_;
}

// ─── Style ──────────────────────────────────────────────────────────────────

void ImGuiIntegration::apply_modern_style()
{
    // Apply theme colors through ThemeManager
    ui::ThemeManager::instance().apply_to_imgui();
}

// ─── Icon sidebar ───────────────────────────────────────────────────────────

// Helper: draw a clickable icon button with modern visual feedback — no hard borders
static bool icon_button(const char* label, bool active, ImFont* font, float size)
{
    using namespace ui;

    const auto& colors = theme();
    ImGui::PushFont(font);

    if (active)
    {
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImVec4(colors.accent_muted.r, colors.accent_muted.g, colors.accent_muted.b, 0.4f));
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, colors.accent.a));
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(colors.text_secondary.r,
                                     colors.text_secondary.g,
                                     colors.text_secondary.b,
                                     colors.text_secondary.a));
    }
    ImGui::PushStyleColor(
        ImGuiCol_ButtonHovered,
        ImVec4(colors.accent_subtle.r, colors.accent_subtle.g, colors.accent_subtle.b, 0.5f));
    ImGui::PushStyleColor(
        ImGuiCol_ButtonActive,
        ImVec4(colors.accent_muted.r, colors.accent_muted.g, colors.accent_muted.b, 0.6f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::tokens::RADIUS_MD);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(ui::tokens::SPACE_2, ui::tokens::SPACE_2));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

    bool clicked = ImGui::Button(label, ImVec2(size, size));

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(4);
    ImGui::PopFont();
    return clicked;
}

// ─── Legacy Methods (To be removed after migration) ───────────────────────────

// These methods are kept temporarily for compatibility but will be removed
// once Agent C implements the proper inspector system

void ImGuiIntegration::draw_menubar()
{
    // Legacy method - replaced by draw_command_bar()
    draw_command_bar();
}

void ImGuiIntegration::draw_icon_bar()
{
    // Legacy method - replaced by draw_nav_rail()
    draw_nav_rail();
}

void ImGuiIntegration::draw_panel(Figure& figure)
{
    // Legacy method - replaced by draw_inspector()
    draw_inspector(figure);
}

// ─── Legacy Panel Drawing Methods (To be removed after Agent C migration) ───

// Helper for drawing dropdown menus — modern 2026 style with:
//   • auto-close on mouse leave
//   • hover-switch between adjacent menus
//   • popup anchored to button's bottom-left corner
void ImGuiIntegration::draw_menubar_menu(const char* label, const std::vector<MenuItem>& items)
{
    const auto& colors = ui::theme();

    ImGui::PushFont(font_menubar_);
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImVec4(colors.text_secondary.r,
                                 colors.text_secondary.g,
                                 colors.text_secondary.b,
                                 colors.text_secondary.a));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(
        ImGuiCol_ButtonHovered,
        ImVec4(colors.accent_subtle.r, colors.accent_subtle.g, colors.accent_subtle.b, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(colors.accent_muted.r,
                                 colors.accent_muted.g,
                                 colors.accent_muted.b,
                                 colors.accent_muted.a));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::tokens::RADIUS_MD);

    // Remember button rect for popup positioning and auto-close
    ImVec2 btn_pos = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::Button(label);
    ImVec2 btn_size = ImGui::GetItemRectSize();
    ImVec2 btn_max = ImVec2(btn_pos.x + btn_size.x, btn_pos.y + btn_size.y);
    bool btn_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);

    // Click opens this menu
    if (clicked)
    {
        SPECTRA_LOG_DEBUG("menu", "Click open: " + std::string(label));
        ImGui::OpenPopup(label);
        open_menu_label_ = label;
    }

    // Hover-switch: if another menu is open and user hovers this button, switch
    if (btn_hovered && !open_menu_label_.empty() && open_menu_label_ != label)
    {
        SPECTRA_LOG_DEBUG("menu",
                          "Hover switch: " + std::string(open_menu_label_) + " -> " + label);
        ImGui::OpenPopup(label);
        open_menu_label_ = label;
    }

    // Anchor popup at button's bottom-left corner (not at mouse position)
    ImGui::SetNextWindowPos(ImVec2(btn_pos.x, btn_max.y + 2.0f));

    // Modern popup styling
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, ui::tokens::RADIUS_LG);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 2));
    ImGui::PushStyleColor(
        ImGuiCol_PopupBg,
        ImVec4(colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b, 0.97f));
    ImGui::PushStyleColor(
        ImGuiCol_Border,
        ImVec4(colors.border_subtle.r, colors.border_subtle.g, colors.border_subtle.b, 0.4f));

    if (ImGui::BeginPopup(label))
    {
        // Track that this menu is the open one
        open_menu_label_ = label;

        // ── Auto-close: dismiss when mouse moves away from button + popup ──
        ImVec2 mouse = ImGui::GetIO().MousePos;
        ImVec2 popup_pos = ImGui::GetWindowPos();
        ImVec2 popup_size = ImGui::GetWindowSize();
        float margin = 20.0f;

        // Combined rect of button + popup + margin
        float combined_min_x = std::min(btn_pos.x, popup_pos.x) - margin;
        float combined_min_y = std::min(btn_pos.y, popup_pos.y) - margin;
        float combined_max_x = std::max(btn_max.x, popup_pos.x + popup_size.x) + margin;
        float combined_max_y = std::max(btn_max.y, popup_pos.y + popup_size.y) + margin;

        bool mouse_in_zone = (mouse.x >= combined_min_x && mouse.x <= combined_max_x
                              && mouse.y >= combined_min_y && mouse.y <= combined_max_y);

        if (!mouse_in_zone && !ImGui::IsAnyItemActive())
        {
            SPECTRA_LOG_DEBUG("menu", "Auto-close: " + std::string(label));
            ImGui::CloseCurrentPopup();
            open_menu_label_.clear();
        }

        // Draw shadow behind popup
        ImDrawList* bg_dl = ImGui::GetBackgroundDrawList();
        bg_dl->AddRectFilled(ImVec2(popup_pos.x + 2, popup_pos.y + 3),
                             ImVec2(popup_pos.x + popup_size.x + 2, popup_pos.y + popup_size.y + 5),
                             IM_COL32(0, 0, 0, 30),
                             ui::tokens::RADIUS_LG + 2);

        for (const auto& item : items)
        {
            if (item.label.empty())
            {
                ImGui::Dummy(ImVec2(0, 2));
                ImGui::PushStyleColor(ImGuiCol_Separator,
                                      ImVec4(colors.border_subtle.r,
                                             colors.border_subtle.g,
                                             colors.border_subtle.b,
                                             0.3f));
                ImGui::Separator();
                ImGui::PopStyleColor();
                ImGui::Dummy(ImVec2(0, 2));
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImVec4(colors.text_primary.r,
                                             colors.text_primary.g,
                                             colors.text_primary.b,
                                             colors.text_primary.a));
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                                      ImVec4(colors.accent_subtle.r,
                                             colors.accent_subtle.g,
                                             colors.accent_subtle.b,
                                             0.5f));
                ImGui::PushStyleColor(
                    ImGuiCol_HeaderActive,
                    ImVec4(
                        colors.accent_muted.r, colors.accent_muted.g, colors.accent_muted.b, 0.7f));
                ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));

                float item_h = ImGui::GetTextLineHeight() + 10.0f;
                if (ImGui::Selectable(
                        item.label.c_str(), false, ImGuiSelectableFlags_None, ImVec2(0, item_h)))
                {
                    if (item.callback)
                        item.callback();
                    open_menu_label_.clear();
                }

                ImGui::PopStyleVar();
                ImGui::PopStyleColor(4);
            }
        }

        ImGui::EndPopup();
    }
    else
    {
        // Popup closed (e.g. by clicking outside) — clear tracking if this was the open one
        if (open_menu_label_ == label)
        {
            open_menu_label_.clear();
        }
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    ImGui::PopFont();
}

// Helper for drawing toolbar buttons with modern hover styling and themed tooltips
void ImGuiIntegration::draw_toolbar_button(const char* icon,
                                           std::function<void()> callback,
                                           const char* tooltip,
                                           bool is_active)
{
    const auto& colors = ui::theme();
    // Use per-instance font_icon_ (not the IconFont singleton) so that
    // secondary windows use their own atlas font, avoiding TexID mismatch.
    ImGui::PushFont(font_icon_);

    if (is_active)
    {
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImVec4(colors.accent_muted.r, colors.accent_muted.g, colors.accent_muted.b, 0.5f));
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, colors.accent.a));
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(colors.text_secondary.r,
                                     colors.text_secondary.g,
                                     colors.text_secondary.b,
                                     colors.text_secondary.a));
    }
    ImGui::PushStyleColor(
        ImGuiCol_ButtonHovered,
        ImVec4(colors.accent_subtle.r, colors.accent_subtle.g, colors.accent_subtle.b, 0.5f));
    ImGui::PushStyleColor(
        ImGuiCol_ButtonActive,
        ImVec4(colors.accent_muted.r, colors.accent_muted.g, colors.accent_muted.b, 0.7f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::tokens::RADIUS_MD);

    if (ImGui::Button(icon))
    {
        if (callback)
            callback();
    }

    // Store tooltip for deferred rendering at the end of build_ui
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && tooltip)
    {
        deferred_tooltip_ = tooltip;
    }

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    ImGui::PopFont();
}

// ─── Layout-Based Drawing Methods ─────────────────────────────────────────────

void ImGuiIntegration::draw_command_bar()
{
    if (!layout_manager_)
    {
        SPECTRA_LOG_WARN("ui", "draw_command_bar called but layout_manager_ is null");
        return;
    }

    SPECTRA_LOG_TRACE("ui", "Drawing command bar");

    Rect bounds = layout_manager_->command_bar_rect();
    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y));
    ImGui::SetNextWindowSize(ImVec2(bounds.w, bounds.h));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings
                             | ImGuiWindowFlags_NoBringToFrontOnFocus
                             | ImGuiWindowFlags_NoFocusOnAppearing;

    // Enhanced styling for 2026 modern look
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 10));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
                          ImVec4(ui::theme().bg_secondary.r,
                                 ui::theme().bg_secondary.g,
                                 ui::theme().bg_secondary.b,
                                 ui::theme().bg_secondary.a));
    ImGui::PushStyleColor(ImGuiCol_Border,
                          ImVec4(ui::theme().border_default.r,
                                 ui::theme().border_default.g,
                                 ui::theme().border_default.b,
                                 ui::theme().border_default.a));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

    if (ImGui::Begin("##commandbar", nullptr, flags))
    {
        SPECTRA_LOG_TRACE("ui", "Command bar window began successfully");
        // App title/brand on the left
        ImGui::PushFont(font_title_);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(ui::theme().accent.r,
                                     ui::theme().accent.g,
                                     ui::theme().accent.b,
                                     ui::theme().accent.a));
        ImGui::TextUnformatted("Spectra");
        ImGui::PopStyleColor();
        ImGui::PopFont();

        ImGui::SameLine();

        draw_toolbar_button(
            ui::icon_str(ui::Icon::Home),
            [this]()
            {
                SPECTRA_LOG_DEBUG("ui_button", "Home button clicked - setting reset_view flag");
                reset_view_ = true;
                SPECTRA_LOG_DEBUG("ui_button", "Reset view flag set successfully");
            },
            "Reset View (Home)");

        ImGui::SameLine();

        // File menu
        draw_menubar_menu("File",
                          {MenuItem("Export PNG",
                                    [this]()
                                    {
                                        if (command_registry_)
                                            command_registry_->execute("file.export_png");
                                    }),
                           MenuItem("Export SVG",
                                    [this]()
                                    {
                                        if (command_registry_)
                                            command_registry_->execute("file.export_svg");
                                    }),
                           MenuItem("Save Workspace",
                                    [this]()
                                    {
                                        if (command_registry_)
                                            command_registry_->execute("file.save_workspace");
                                    }),
                           MenuItem("Load Workspace",
                                    [this]()
                                    {
                                        if (command_registry_)
                                            command_registry_->execute("file.load_workspace");
                                    }),
                           MenuItem("", nullptr),  // Separator
                           MenuItem("Exit",
                                    [this]()
                                    {
                                        if (command_registry_)
                                            command_registry_->execute("app.cancel");
                                    })});

        ImGui::SameLine();

        // View menu
        draw_menubar_menu(
            "View",
            {MenuItem("Toggle Inspector",
                      [this]()
                      {
                          if (command_registry_)
                              command_registry_->execute("panel.toggle_inspector");
                          else
                          {
                              bool new_vis = !layout_manager_->is_inspector_visible();
                              layout_manager_->set_inspector_visible(new_vis);
                              panel_open_ = new_vis;
                          }
                      }),
             MenuItem("Toggle Navigation Rail", [this]() { show_nav_rail_ = !show_nav_rail_; }),
             MenuItem("Toggle 2D/3D View",
                      [this]()
                      {
                          if (command_registry_)
                              command_registry_->execute("view.toggle_3d");
                      }),
             MenuItem("Zoom to Fit",
                      [this]()
                      {
                          if (command_registry_)
                              command_registry_->execute("view.autofit");
                      }),
             MenuItem("Reset View",
                      [this]()
                      {
                          if (command_registry_)
                              command_registry_->execute("view.reset");
                      }),
             MenuItem("Toggle Grid",
                      [this]()
                      {
                          if (command_registry_)
                              command_registry_->execute("view.toggle_grid");
                      }),
             MenuItem("Toggle Legend",
                      [this]()
                      {
                          if (command_registry_)
                              command_registry_->execute("view.toggle_legend");
                      }),
             MenuItem("", nullptr),  // Separator
             MenuItem("Toggle Timeline",
                      [this]()
                      {
                          if (command_registry_)
                              command_registry_->execute("panel.toggle_timeline");
                      }),
             MenuItem("Toggle Curve Editor",
                      [this]()
                      {
                          if (command_registry_)
                              command_registry_->execute("panel.toggle_curve_editor");
                      })});

        ImGui::SameLine();

        // Axes menu — link/unlink axes across subplots
        {
            std::vector<MenuItem> axes_items;

            axes_items.emplace_back(
                "Link X Axes",
                [this]()
                {
                    if (!axis_link_mgr_ || !current_figure_ || current_figure_->axes().size() < 2)
                        return;
                    auto gid = axis_link_mgr_->create_group("X Link", LinkAxis::X);
                    for (auto& ax : current_figure_->axes_mut())
                    {
                        if (ax)
                            axis_link_mgr_->add_to_group(gid, ax.get());
                    }
                    SPECTRA_LOG_INFO("axes_link", "Linked all axes on X");
                });
            axes_items.emplace_back(
                "Link Y Axes",
                [this]()
                {
                    if (!axis_link_mgr_ || !current_figure_ || current_figure_->axes().size() < 2)
                        return;
                    auto gid = axis_link_mgr_->create_group("Y Link", LinkAxis::Y);
                    for (auto& ax : current_figure_->axes_mut())
                    {
                        if (ax)
                            axis_link_mgr_->add_to_group(gid, ax.get());
                    }
                    SPECTRA_LOG_INFO("axes_link", "Linked all axes on Y");
                });
            axes_items.emplace_back(
                "Link All Axes",
                [this]()
                {
                    if (!axis_link_mgr_ || !current_figure_ || current_figure_->axes().size() < 2)
                        return;
                    auto gid = axis_link_mgr_->create_group("XY Link", LinkAxis::Both);
                    for (auto& ax : current_figure_->axes_mut())
                    {
                        if (ax)
                            axis_link_mgr_->add_to_group(gid, ax.get());
                    }
                    SPECTRA_LOG_INFO("axes_link", "Linked all axes on X+Y");
                });
            axes_items.emplace_back("", nullptr);  // separator
            axes_items.emplace_back("Unlink All",
                                    [this]()
                                    {
                                        if (!axis_link_mgr_)
                                            return;
                                        // Collect group IDs first (can't modify while iterating)
                                        std::vector<LinkGroupId> ids;
                                        for (auto& [id, group] : axis_link_mgr_->groups())
                                        {
                                            ids.push_back(id);
                                        }
                                        for (auto id : ids)
                                        {
                                            axis_link_mgr_->remove_group(id);
                                        }
                                        axis_link_mgr_->clear_shared_cursor();
                                        SPECTRA_LOG_INFO("axes_link", "Unlinked all axes");
                                    });

            draw_menubar_menu("Axes", axes_items);
        }

        ImGui::SameLine();

        // Transforms menu — apply data transforms to series
        {
            std::vector<MenuItem> xform_items;
            auto& registry = TransformRegistry::instance();
            auto names = registry.available_transforms();

            // Built-in transforms
            for (const auto& name : names)
            {
                xform_items.emplace_back(
                    name,
                    [this, name]()
                    {
                        if (!current_figure_)
                            return;
                        DataTransform xform;
                        if (!TransformRegistry::instance().get_transform(name, xform))
                            return;

                        // Apply to all visible series in all axes
                        for (auto& ax : current_figure_->axes_mut())
                        {
                            if (!ax)
                                continue;
                            for (auto& series_ptr : ax->series_mut())
                            {
                                if (!series_ptr || !series_ptr->visible())
                                    continue;

                                if (auto* ls = dynamic_cast<LineSeries*>(series_ptr.get()))
                                {
                                    std::vector<float> rx, ry;
                                    xform.apply_y(ls->x_data(), ls->y_data(), rx, ry);
                                    ls->set_x(rx).set_y(ry);
                                }
                                else if (auto* sc = dynamic_cast<ScatterSeries*>(series_ptr.get()))
                                {
                                    std::vector<float> rx, ry;
                                    xform.apply_y(sc->x_data(), sc->y_data(), rx, ry);
                                    sc->set_x(rx).set_y(ry);
                                }
                            }
                            ax->auto_fit();
                        }
                        SPECTRA_LOG_INFO("transform", "Applied transform: " + name);
                    });
            }

            draw_menubar_menu("Transforms", xform_items);
        }

        ImGui::SameLine();

        // Tools menu
        draw_menubar_menu(
            "Tools",
            {MenuItem("Screenshot (PNG)",
                      [this]()
                      {
                          if (command_registry_)
                              command_registry_->execute("file.export_png");
                      }),
             MenuItem("Undo",
                      [this]()
                      {
                          if (command_registry_)
                              command_registry_->execute("edit.undo");
                      }),
             MenuItem("Redo",
                      [this]()
                      {
                          if (command_registry_)
                              command_registry_->execute("edit.redo");
                      }),
             MenuItem("", nullptr),  // Separator
             MenuItem("Theme Settings", [this]() { show_theme_settings_ = !show_theme_settings_; }),
             MenuItem("Command Palette",
                      [this]()
                      {
                          if (command_registry_)
                              command_registry_->execute("app.command_palette");
                      })});

        // Push status info to the right
        ImGui::SameLine(0.0f, ImGui::GetContentRegionAvail().x - 220.0f);

        // Status info
        ImGuiIO& io = ImGui::GetIO();
        ImGui::PushFont(font_menubar_);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(ui::theme().text_secondary.r,
                                     ui::theme().text_secondary.g,
                                     ui::theme().text_secondary.b,
                                     ui::theme().text_secondary.a));

        char status[128];
        std::snprintf(status,
                      sizeof(status),
                      "Display: %dx%d | FPS: %.0f | GPU",
                      static_cast<int>(io.DisplaySize.x),
                      static_cast<int>(io.DisplaySize.y),
                      io.Framerate);
        ImGui::TextUnformatted(status);

        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);
}

void ImGuiIntegration::draw_tab_bar()
{
    if (!layout_manager_ || !tab_bar_)
        return;
    if (!layout_manager_->is_tab_bar_visible())
        return;

    Rect bounds = layout_manager_->tab_bar_rect();
    if (bounds.w < 1.0f || bounds.h < 1.0f)
        return;

    // Create an ImGui window for the tab bar so that GetWindowDrawList(),
    // OpenPopup(), and BeginPopup() all work correctly inside TabBar::draw()
    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y));
    ImGui::SetNextWindowSize(ImVec2(bounds.w, bounds.h));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings
                             | ImGuiWindowFlags_NoBringToFrontOnFocus
                             | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollbar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
                          ImVec4(ui::theme().bg_secondary.r,
                                 ui::theme().bg_secondary.g,
                                 ui::theme().bg_secondary.b,
                                 ui::theme().bg_secondary.a));

    if (ImGui::Begin("##spectra_tab_bar", nullptr, flags))
    {
        // Let the TabBar handle all input, drawing, and context menus
        tab_bar_->draw(bounds);
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

void ImGuiIntegration::draw_nav_rail()
{
    if (!layout_manager_ || !show_nav_rail_)
        return;

    Rect bounds = layout_manager_->nav_rail_rect();

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings
                             | ImGuiWindowFlags_NoBringToFrontOnFocus
                             | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollbar;

    float btn_size = 32.0f;
    float spacing = ui::tokens::SPACE_2;
    float margin = ui::tokens::SPACE_3;
    float toolbar_w = btn_size + margin * 2.0f;

    // Compute floating toolbar height: 3 nav + separator + 4 tools + separator + 1 settings
    float section_gap = ui::tokens::SPACE_4;
    float nav_section_h = btn_size * 3.0f + spacing * 2.0f;
    float tool_section_h = btn_size * 4.0f + spacing * 3.0f;
    float settings_section_h = btn_size;
    // Each separator: 2× Dummy of (section_gap - spacing)*0.5 + implicit item spacing around them
    float separator_h = section_gap + spacing;
    float total_content_h =
        nav_section_h + separator_h + tool_section_h + separator_h + settings_section_h;
    float vert_pad = ui::tokens::SPACE_4;  // generous top/bottom padding
    float toolbar_h = total_content_h + vert_pad * 2.0f;

    // Position: floating with a left margin, vertically centered in the content area
    float left_margin = ui::tokens::SPACE_3;
    float float_x = left_margin;
    float float_y = bounds.y + (bounds.h - toolbar_h) * 0.5f;
    // Clamp within content area
    float_y = std::clamp(float_y,
                         bounds.y + ui::tokens::SPACE_3,
                         bounds.y + bounds.h - toolbar_h - ui::tokens::SPACE_3);

    // Floating elevated style with rounded corners and subtle shadow
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(margin, vert_pad));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::tokens::RADIUS_LG);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, spacing));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
                          ImVec4(ui::theme().bg_elevated.r,
                                 ui::theme().bg_elevated.g,
                                 ui::theme().bg_elevated.b,
                                 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Border,
                          ImVec4(ui::theme().border_default.r,
                                 ui::theme().border_default.g,
                                 ui::theme().border_default.b,
                                 0.5f));

    // Draw shadow behind the toolbar via background draw list
    ImDrawList* bg_dl = ImGui::GetBackgroundDrawList();
    float shadow_offset = 4.0f;
    float shadow_radius = ui::tokens::RADIUS_LG + 2.0f;
    bg_dl->AddRectFilled(
        ImVec2(float_x + shadow_offset, float_y + shadow_offset),
        ImVec2(float_x + toolbar_w + shadow_offset, float_y + toolbar_h + shadow_offset),
        IM_COL32(0, 0, 0, 40),
        shadow_radius);

    ImGui::SetNextWindowPos(ImVec2(float_x, float_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(toolbar_w, toolbar_h), ImGuiCond_Always);

    if (ImGui::Begin("##navrail", nullptr, flags))
    {
        float pad_x = std::max(0.0f, (toolbar_w - margin * 2.0f - btn_size) * 0.5f);

        // ── Inspector section buttons ──
        // Modern tooltip helper for nav rail
        auto modern_tooltip = [&](const char* tip)
        {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            {
                ImGui::BeginTooltip();
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 6));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::tokens::RADIUS_MD);
                ImGui::PushStyleColor(ImGuiCol_PopupBg,
                                      ImVec4(ui::theme().bg_elevated.r,
                                             ui::theme().bg_elevated.g,
                                             ui::theme().bg_elevated.b,
                                             0.95f));
                ImGui::PushStyleColor(ImGuiCol_Border,
                                      ImVec4(ui::theme().border_subtle.r,
                                             ui::theme().border_subtle.g,
                                             ui::theme().border_subtle.b,
                                             0.3f));
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImVec4(ui::theme().text_primary.r,
                                             ui::theme().text_primary.g,
                                             ui::theme().text_primary.b,
                                             1.0f));
                ImGui::TextUnformatted(tip);
                ImGui::PopStyleColor(3);
                ImGui::PopStyleVar(2);
                ImGui::EndTooltip();
            }
        };

        auto nav_btn = [&](ui::Icon icon, const char* tooltip, Section section)
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad_x);
            bool is_active = panel_open_ && active_section_ == section;
            if (icon_button(ui::icon_str(icon), is_active, font_icon_, btn_size))
            {
                if (is_active)
                {
                    panel_open_ = false;
                    layout_manager_->set_inspector_visible(false);
                }
                else
                {
                    active_section_ = section;
                    panel_open_ = true;
                    layout_manager_->set_inspector_visible(true);
                }
            }
            modern_tooltip(tooltip);
        };

        nav_btn(ui::Icon::ScatterChart, "Figures", Section::Figure);
        nav_btn(ui::Icon::ChartLine, "Series", Section::Series);
        nav_btn(ui::Icon::Axes, "Axes", Section::Axes);

        // ── Separator ──
        ImGui::Dummy(ImVec2(0, (section_gap - spacing) * 0.5f));
        {
            float sep_pad = 6.0f;
            ImVec2 p0 = ImVec2(ImGui::GetWindowPos().x + sep_pad, ImGui::GetCursorScreenPos().y);
            ImVec2 p1 = ImVec2(ImGui::GetWindowPos().x + toolbar_w - sep_pad, p0.y);
            ImGui::GetWindowDrawList()->AddLine(p0,
                                                p1,
                                                IM_COL32(ui::theme().border_default.r * 255,
                                                         ui::theme().border_default.g * 255,
                                                         ui::theme().border_default.b * 255,
                                                         80),
                                                1.0f);
        }
        ImGui::Dummy(ImVec2(0, (section_gap - spacing) * 0.5f));

        // ── Tool mode buttons (from floating toolbar) ──
        auto tool_btn = [&](ui::Icon icon, const char* tooltip, ToolMode mode)
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad_x);
            bool is_active = (interaction_mode_ == mode);
            if (icon_button(ui::icon_str(icon), is_active, font_icon_, btn_size))
            {
                interaction_mode_ = mode;
            }
            modern_tooltip(tooltip);
        };

        tool_btn(ui::Icon::Hand, "Pan (P)", ToolMode::Pan);
        tool_btn(ui::Icon::ZoomIn, "Box Zoom (Z)", ToolMode::BoxZoom);
        tool_btn(ui::Icon::Crosshair, "Select (S)", ToolMode::Select);

        // Measure button — switches to Measure tool mode for click-drag distance measurement
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad_x);
        {
            bool is_active = (interaction_mode_ == ToolMode::Measure);
            if (icon_button(ui::icon_str(ui::Icon::Ruler), is_active, font_icon_, btn_size))
            {
                interaction_mode_ = ToolMode::Measure;
            }
        }
        modern_tooltip("Measure (M)");

        // ── Separator ──
        ImGui::Dummy(ImVec2(0, (section_gap - spacing) * 0.5f));
        {
            float sep_pad = 6.0f;
            ImVec2 p0 = ImVec2(ImGui::GetWindowPos().x + sep_pad, ImGui::GetCursorScreenPos().y);
            ImVec2 p1 = ImVec2(ImGui::GetWindowPos().x + toolbar_w - sep_pad, p0.y);
            ImGui::GetWindowDrawList()->AddLine(p0,
                                                p1,
                                                IM_COL32(ui::theme().border_default.r * 255,
                                                         ui::theme().border_default.g * 255,
                                                         ui::theme().border_default.b * 255,
                                                         80),
                                                1.0f);
        }
        ImGui::Dummy(ImVec2(0, (section_gap - spacing) * 0.5f));

        // ── Settings at bottom ──
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad_x);
        if (icon_button(
                ui::icon_str(ui::Icon::Settings), show_theme_settings_, font_icon_, btn_size))
        {
            show_theme_settings_ = !show_theme_settings_;
        }
        modern_tooltip("Settings");
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(5);
}

void ImGuiIntegration::draw_canvas(Figure& /*figure*/)
{
    if (!layout_manager_)
        return;

    Rect bounds = layout_manager_->canvas_rect();

    // Canvas is primarily handled by the Vulkan renderer
    // We just set up the viewport here for ImGui coordination
    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y));
    ImGui::SetNextWindowSize(ImVec2(bounds.w, bounds.h));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings
                             | ImGuiWindowFlags_NoBringToFrontOnFocus
                             | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground
                             | ImGuiWindowFlags_NoInputs;

    // Transparent window for canvas area
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

    if (ImGui::Begin("##canvas", nullptr, flags))
    {
        // Canvas content is rendered by Vulkan, not ImGui
        // This window is just for input handling coordination
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
}

void ImGuiIntegration::draw_inspector(Figure& figure)
{
    if (!layout_manager_)
        return;

    Rect bounds = layout_manager_->inspector_rect();
    if (bounds.w < 1.0f)
        return;  // Fully collapsed

    // Draw resize handle as a separate invisible window so it extends outside the inspector
    {
        float handle_w = LayoutManager::RESIZE_HANDLE_WIDTH;
        float handle_x = bounds.x - handle_w * 0.5f;
        ImGui::SetNextWindowPos(ImVec2(handle_x, bounds.y));
        ImGui::SetNextWindowSize(ImVec2(handle_w, bounds.h));
        ImGuiWindowFlags handle_flags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground
            | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        if (ImGui::Begin("##inspector_resize_handle", nullptr, handle_flags))
        {
            ImGui::SetCursorScreenPos(ImVec2(handle_x, bounds.y));
            ImGui::InvisibleButton("##resize_grip", ImVec2(handle_w, bounds.h));
            bool hovered = ImGui::IsItemHovered();
            bool active = ImGui::IsItemActive();
            layout_manager_->set_inspector_resize_hovered(hovered);

            if (hovered || active)
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            }
            if (ImGui::IsItemClicked())
            {
                layout_manager_->set_inspector_resize_active(true);
            }
            if (active)
            {
                float right_edge = bounds.x + bounds.w;
                float new_width = right_edge - ImGui::GetIO().MousePos.x;
                layout_manager_->set_inspector_width(new_width);
            }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                layout_manager_->set_inspector_resize_active(false);
            }

            // Visual resize indicator line
            if (hovered || active)
            {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                float line_x = bounds.x;
                auto accent = ui::theme().accent;
                ImU32 line_col = active ? IM_COL32(uint8_t(accent.r * 255),
                                                   uint8_t(accent.g * 255),
                                                   uint8_t(accent.b * 255),
                                                   255)
                                        : IM_COL32(uint8_t(accent.r * 255),
                                                   uint8_t(accent.g * 255),
                                                   uint8_t(accent.b * 255),
                                                   120);
                dl->AddLine(ImVec2(line_x, bounds.y),
                            ImVec2(line_x, bounds.y + bounds.h),
                            line_col,
                            active ? 3.0f : 2.0f);
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    // Inspector panel itself
    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y));
    ImGui::SetNextWindowSize(ImVec2(bounds.w, bounds.h));

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(ui::tokens::SPACE_5, ui::tokens::SPACE_5));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
                          ImVec4(ui::theme().bg_secondary.r,
                                 ui::theme().bg_secondary.g,
                                 ui::theme().bg_secondary.b,
                                 ui::theme().bg_secondary.a));
    ImGui::PushStyleColor(ImGuiCol_Border,
                          ImVec4(ui::theme().border_default.r,
                                 ui::theme().border_default.g,
                                 ui::theme().border_default.b,
                                 ui::theme().border_default.a));

    if (ImGui::Begin("##inspector", nullptr, flags))
    {
        // Close button in top-right corner
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(ui::theme().accent_subtle.r,
                                     ui::theme().accent_subtle.g,
                                     ui::theme().accent_subtle.b,
                                     ui::theme().accent_subtle.a));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::tokens::RADIUS_SM);
        if (ImGui::Button(ui::icon_str(ui::Icon::Close), ImVec2(20, 20)))
        {
            layout_manager_->set_inspector_visible(false);
            panel_open_ = false;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);

        // Scrollable content area
        ImGui::BeginChild(
            "##inspector_content", ImVec2(0, 0), false, ImGuiWindowFlags_NoBackground);

        if (panel_open_)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, panel_anim_);

            // Update selection context based on active nav rail section.
            // When the Series section is active and the user has drilled into
            // a specific series, preserve that selection so the properties
            // panel stays visible. Switching to any other section always
            // overrides the selection.
            switch (active_section_)
            {
                case Section::Figure:
                    selection_ctx_.select_figure(&figure);
                    break;
                case Section::Series:
                    // Only show browser if user hasn't selected a specific series
                    if (selection_ctx_.type != ui::SelectionType::Series)
                    {
                        selection_ctx_.select_series_browser(&figure);
                    }
                    break;
                case Section::Axes:
                    if (selection_ctx_.type != ui::SelectionType::Axes && !figure.axes().empty())
                    {
                        selection_ctx_.select_axes(&figure, figure.axes_mut()[0].get(), 0);
                    }
                    break;
            }

            inspector_.set_context(selection_ctx_);
            inspector_.draw(figure);

            // Read back context (inspector may change selection, e.g. clicking a series)
            selection_ctx_ = inspector_.context();

            ImGui::PopStyleVar();
        }

        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

void ImGuiIntegration::draw_status_bar()
{
    if (!layout_manager_)
        return;

    Rect bounds = layout_manager_->status_bar_rect();
    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y));
    ImGui::SetNextWindowSize(ImVec2(bounds.w, bounds.h));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings
                             | ImGuiWindowFlags_NoBringToFrontOnFocus
                             | ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(ui::tokens::SPACE_3, ui::tokens::SPACE_1));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
                          ImVec4(ui::theme().bg_secondary.r,
                                 ui::theme().bg_secondary.g,
                                 ui::theme().bg_secondary.b,
                                 ui::theme().bg_secondary.a));
    ImGui::PushStyleColor(ImGuiCol_Border,
                          ImVec4(ui::theme().border_subtle.r,
                                 ui::theme().border_subtle.g,
                                 ui::theme().border_subtle.b,
                                 ui::theme().border_subtle.a));

    if (ImGui::Begin("##statusbar", nullptr, flags))
    {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::PushFont(font_heading_);

        // Left: cursor data readout
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(ui::theme().text_secondary.r,
                                     ui::theme().text_secondary.g,
                                     ui::theme().text_secondary.b,
                                     ui::theme().text_secondary.a));
        char cursor_buf[64];
        std::snprintf(
            cursor_buf, sizeof(cursor_buf), "X: %.4f  Y: %.4f", cursor_data_x_, cursor_data_y_);
        ImGui::TextUnformatted(cursor_buf);
        ImGui::PopStyleColor();

        // Center: mode indicator
        ImGui::SameLine(0.0f, ui::tokens::SPACE_6);
        {
            const char* mode_label = "Navigate";
            auto mode_color = ui::theme().text_secondary;
            switch (interaction_mode_)
            {
                case ToolMode::Pan:
                    mode_label = "Pan";
                    mode_color = ui::theme().accent;
                    break;
                case ToolMode::BoxZoom:
                    mode_label = "Box Zoom";
                    mode_color = ui::theme().warning;
                    break;
                case ToolMode::Select:
                    mode_label = "Select";
                    mode_color = ui::theme().info;
                    break;
                case ToolMode::Measure:
                    mode_label = "Measure";
                    mode_color = ui::theme().success;
                    break;
                default:
                    break;
            }
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(mode_color.r, mode_color.g, mode_color.b, mode_color.a));
            ImGui::TextUnformatted(mode_label);
            ImGui::PopStyleColor();
        }

        // Separator dot
        ImGui::SameLine(0.0f, ui::tokens::SPACE_3);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(ui::theme().border_default.r,
                                     ui::theme().border_default.g,
                                     ui::theme().border_default.b,
                                     ui::theme().border_default.a));
        ImGui::TextUnformatted("|");
        ImGui::PopStyleColor();

        // Zoom level
        ImGui::SameLine(0.0f, ui::tokens::SPACE_3);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(ui::theme().text_secondary.r,
                                     ui::theme().text_secondary.g,
                                     ui::theme().text_secondary.b,
                                     ui::theme().text_secondary.a));
        char zoom_buf[32];
        std::snprintf(
            zoom_buf, sizeof(zoom_buf), "Zoom: %d%%", static_cast<int>(zoom_level_ * 100.0f));
        ImGui::TextUnformatted(zoom_buf);
        ImGui::PopStyleColor();

        // Right side: performance info
        float right_offset = ImGui::GetContentRegionAvail().x - 160.0f;
        if (right_offset > 0.0f)
        {
            ImGui::SameLine(0.0f, right_offset);
        }
        else
        {
            ImGui::SameLine();
        }

        // FPS with color coding
        float fps_val = io.Framerate;
        auto fps_color = ui::theme().success;
        if (fps_val < 30.0f)
            fps_color = ui::theme().error;
        else if (fps_val < 55.0f)
            fps_color = ui::theme().warning;

        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(fps_color.r, fps_color.g, fps_color.b, fps_color.a));
        char fps_buf[32];
        std::snprintf(fps_buf, sizeof(fps_buf), "%d fps", static_cast<int>(fps_val));
        ImGui::TextUnformatted(fps_buf);
        ImGui::PopStyleColor();

        // GPU time
        ImGui::SameLine(0.0f, ui::tokens::SPACE_3);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(ui::theme().text_tertiary.r,
                                     ui::theme().text_tertiary.g,
                                     ui::theme().text_tertiary.b,
                                     ui::theme().text_tertiary.a));
        char gpu_buf[32];
        std::snprintf(gpu_buf, sizeof(gpu_buf), "GPU: %.1fms", gpu_time_ms_);
        ImGui::TextUnformatted(gpu_buf);
        ImGui::PopStyleColor();

        ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

void ImGuiIntegration::draw_split_view_splitters()
{
    if (!dock_system_)
        return;

    auto* draw_list = ImGui::GetForegroundDrawList();
    auto& theme = ui::theme();
    ImVec2 mouse = ImGui::GetMousePos();

    // ── Non-split drag-to-split overlay ──────────────────────────────────
    // When NOT split and a tab is being dock-dragged, show edge zone
    // highlights to suggest splitting (like VSCode).
    if (!dock_system_->is_split() && dock_system_->is_dragging())
    {
        auto target = dock_system_->current_drop_target();
        // Only show edge zones (Left/Right/Top/Bottom), not Center
        if (target.zone != DropZone::None && target.zone != DropZone::Center)
        {
            Rect hr = target.highlight_rect;
            ImU32 highlight_color = IM_COL32(static_cast<int>(theme.accent.r * 255),
                                             static_cast<int>(theme.accent.g * 255),
                                             static_cast<int>(theme.accent.b * 255),
                                             40);
            ImU32 highlight_border = IM_COL32(static_cast<int>(theme.accent.r * 255),
                                              static_cast<int>(theme.accent.g * 255),
                                              static_cast<int>(theme.accent.b * 255),
                                              160);

            draw_list->AddRectFilled(
                ImVec2(hr.x, hr.y), ImVec2(hr.x + hr.w, hr.y + hr.h), highlight_color, 4.0f);
            draw_list->AddRect(ImVec2(hr.x, hr.y),
                               ImVec2(hr.x + hr.w, hr.y + hr.h),
                               highlight_border,
                               4.0f,
                               0,
                               2.0f);

            // Draw a label indicating the split direction
            const char* label = nullptr;
            switch (target.zone)
            {
                case DropZone::Left:
                    label = "Split Left";
                    break;
                case DropZone::Right:
                    label = "Split Right";
                    break;
                case DropZone::Top:
                    label = "Split Up";
                    break;
                case DropZone::Bottom:
                    label = "Split Down";
                    break;
                default:
                    break;
            }
            if (label)
            {
                ImVec2 lsz = ImGui::CalcTextSize(label);
                float lx = hr.x + (hr.w - lsz.x) * 0.5f;
                float ly = hr.y + (hr.h - lsz.y) * 0.5f;
                draw_list->AddText(ImVec2(lx, ly),
                                   IM_COL32(static_cast<int>(theme.accent.r * 255),
                                            static_cast<int>(theme.accent.g * 255),
                                            static_cast<int>(theme.accent.b * 255),
                                            200),
                                   label);
            }
        }
        return;  // No splitters to draw in non-split mode
    }

    if (!dock_system_->is_split())
        return;

    // Handle pane activation on mouse click in canvas area
    // (skip if mouse is over a pane tab header — that's handled by draw_pane_tab_headers)
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().WantCaptureMouse
        && !pane_tab_hovered_)
    {
        dock_system_->activate_pane_at(mouse.x, mouse.y);
    }

    // Handle splitter interaction
    if (dock_system_->is_over_splitter(mouse.x, mouse.y))
    {
        auto dir = dock_system_->splitter_direction_at(mouse.x, mouse.y);
        ImGui::SetMouseCursor(dir == SplitDirection::Horizontal ? ImGuiMouseCursor_ResizeEW
                                                                : ImGuiMouseCursor_ResizeNS);

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            dock_system_->begin_splitter_drag(mouse.x, mouse.y);
        }
    }

    if (dock_system_->is_dragging_splitter())
    {
        auto* sp = dock_system_->split_view().dragging_splitter();
        if (sp)
        {
            float pos = (sp->split_direction() == SplitDirection::Horizontal) ? mouse.x : mouse.y;
            dock_system_->update_splitter_drag(pos);
            ImGui::SetMouseCursor(sp->split_direction() == SplitDirection::Horizontal
                                      ? ImGuiMouseCursor_ResizeEW
                                      : ImGuiMouseCursor_ResizeNS);
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            dock_system_->end_splitter_drag();
        }
    }

    // Draw splitter handles for all internal nodes
    auto pane_infos = dock_system_->get_pane_infos();

    // Walk the split tree to find internal nodes and draw their splitters
    std::function<void(SplitPane*)> draw_splitters = [&](SplitPane* node)
    {
        if (!node || node->is_leaf())
            return;

        Rect sr = node->splitter_rect();
        bool is_dragging = dock_system_->is_dragging_splitter()
                           && dock_system_->split_view().dragging_splitter() == node;

        // Splitter background
        ImU32 splitter_color;
        if (is_dragging)
        {
            splitter_color = IM_COL32(static_cast<int>(theme.accent.r * 255),
                                      static_cast<int>(theme.accent.g * 255),
                                      static_cast<int>(theme.accent.b * 255),
                                      200);
        }
        else
        {
            splitter_color = IM_COL32(static_cast<int>(theme.border_default.r * 255),
                                      static_cast<int>(theme.border_default.g * 255),
                                      static_cast<int>(theme.border_default.b * 255),
                                      120);
        }

        draw_list->AddRectFilled(
            ImVec2(sr.x, sr.y), ImVec2(sr.x + sr.w, sr.y + sr.h), splitter_color);

        // Draw a subtle grip indicator in the center of the splitter
        float cx = sr.x + sr.w * 0.5f;
        float cy = sr.y + sr.h * 0.5f;
        ImU32 grip_color = IM_COL32(static_cast<int>(theme.text_tertiary.r * 255),
                                    static_cast<int>(theme.text_tertiary.g * 255),
                                    static_cast<int>(theme.text_tertiary.b * 255),
                                    150);

        if (node->split_direction() == SplitDirection::Horizontal)
        {
            // Vertical splitter — draw horizontal grip dots
            for (int i = -2; i <= 2; ++i)
            {
                draw_list->AddCircleFilled(ImVec2(cx, cy + i * 6.0f), 1.5f, grip_color);
            }
        }
        else
        {
            // Horizontal splitter — draw vertical grip dots
            for (int i = -2; i <= 2; ++i)
            {
                draw_list->AddCircleFilled(ImVec2(cx + i * 6.0f, cy), 1.5f, grip_color);
            }
        }

        // Recurse into children
        draw_splitters(node->first());
        draw_splitters(node->second());
    };

    draw_splitters(dock_system_->split_view().root());

    // Draw active pane border highlight
    for (const auto& info : pane_infos)
    {
        if (info.is_active && pane_infos.size() > 1)
        {
            ImU32 border_color = IM_COL32(static_cast<int>(theme.accent.r * 255),
                                          static_cast<int>(theme.accent.g * 255),
                                          static_cast<int>(theme.accent.b * 255),
                                          180);
            draw_list->AddRect(ImVec2(info.bounds.x, info.bounds.y),
                               ImVec2(info.bounds.x + info.bounds.w, info.bounds.y + info.bounds.h),
                               border_color,
                               0.0f,
                               0,
                               2.0f);
        }
    }

    // Draw drop zone highlight during drag-to-dock
    if (dock_system_->is_dragging())
    {
        auto target = dock_system_->current_drop_target();
        if (target.zone != DropZone::None)
        {
            Rect hr = target.highlight_rect;
            ImU32 highlight_color = IM_COL32(static_cast<int>(theme.accent.r * 255),
                                             static_cast<int>(theme.accent.g * 255),
                                             static_cast<int>(theme.accent.b * 255),
                                             60);
            ImU32 highlight_border = IM_COL32(static_cast<int>(theme.accent.r * 255),
                                              static_cast<int>(theme.accent.g * 255),
                                              static_cast<int>(theme.accent.b * 255),
                                              180);

            draw_list->AddRectFilled(
                ImVec2(hr.x, hr.y), ImVec2(hr.x + hr.w, hr.y + hr.h), highlight_color);
            draw_list->AddRect(ImVec2(hr.x, hr.y),
                               ImVec2(hr.x + hr.w, hr.y + hr.h),
                               highlight_border,
                               0.0f,
                               0,
                               2.0f);
        }
    }
}

// ─── Per-pane tab headers ────────────────────────────────────────────────────
// Draws a compact tab bar above each split pane leaf. Supports:
//  - Click to switch active figure within a pane
//  - Drag tabs between panes (cross-pane drag)
//  - Smooth animated tab positions and drag ghost

void ImGuiIntegration::draw_pane_tab_headers()
{
    if (!dock_system_)
        return;

    // Normally use GetForegroundDrawList() so tab headers render on top of the
    // canvas and other UI. But when any popup is open (menus, context menus),
    // or when the mouse is over the knobs panel, fall back to
    // GetBackgroundDrawList() so those elements render on top of tabs.
    bool any_popup =
        ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    // If the knobs panel has non-zero size, it's visible and overlaps the tab
    // bar area — use background draw list so the knobs panel (an ImGui window)
    // renders visually above the tab bar draw calls.
    bool knobs_panel_visible =
        knob_manager_ && !knob_manager_->empty() && knobs_panel_rect_.w > 0.0f;
    auto* draw_list = (any_popup || knobs_panel_visible) ? ImGui::GetBackgroundDrawList()
                                                         : ImGui::GetForegroundDrawList();

    auto& theme = ui::theme();
    float dt = ImGui::GetIO().DeltaTime;
    ImVec2 mouse = ImGui::GetMousePos();

    constexpr float TAB_H = SplitPane::PANE_TAB_HEIGHT;
    constexpr float TAB_PAD = 8.0f;
    constexpr float TAB_MIN_W = 60.0f;
    constexpr float TAB_MAX_W = 150.0f;
    constexpr float CLOSE_SZ = 12.0f;
    constexpr float ANIM_SPEED = 14.0f;
    constexpr float DRAG_THRESHOLD = 5.0f;

    auto panes = dock_system_->split_view().all_panes();
    (void)dock_system_->active_figure_index();  // Available if needed

    // Helper: get figure title
    auto fig_title = [&](size_t fig_idx) -> std::string
    {
        if (get_figure_title_)
            return get_figure_title_(fig_idx);
        return "Figure " + std::to_string(fig_idx + 1);
    };

    // Helper: ImU32 from theme color
    auto to_col = [](const ui::Color& c, float a = -1.0f) -> ImU32
    {
        float alpha = a >= 0.0f ? a : c.a;
        return IM_COL32(
            uint8_t(c.r * 255), uint8_t(c.g * 255), uint8_t(c.b * 255), uint8_t(alpha * 255));
    };

    // ── Phase 1: Compute tab layouts per pane ────────────────────────────

    struct TabRect
    {
        size_t figure_index;
        float x, y, w, h;
        bool is_active;
        bool is_hovered;
    };

    struct PaneHeader
    {
        SplitPane* pane;
        Rect header_rect;
        std::vector<TabRect> tabs;
    };

    std::vector<PaneHeader> headers;
    headers.reserve(panes.size());

    // Compute insertion gap: when dragging a tab over a pane header,
    // determine which position the tab would be inserted at
    constexpr float GAP_WIDTH = 60.0f;  // Width of the insertion gap
    bool has_active_gap = false;
    uint32_t gap_pane_id = 0;
    size_t gap_insert_after = SIZE_MAX;  // Insert after this local index

    if (pane_tab_drag_.dragging && pane_tab_drag_.dragged_figure_index != INVALID_FIGURE_ID)
    {
        for (auto* pane_const : panes)
        {
            auto* pane = const_cast<SplitPane*>(pane_const);
            if (!pane->is_leaf())
                continue;
            Rect b = pane->bounds();
            Rect hr{b.x, b.y, b.w, TAB_H};
            if (mouse.x >= hr.x && mouse.x < hr.x + hr.w && mouse.y >= hr.y - 10
                && mouse.y < hr.y + hr.h + 10)
            {
                // Mouse is over this pane's header — compute insertion index
                if (pane->id() != pane_tab_drag_.source_pane_id || pane->figure_count() > 1)
                {
                    gap_pane_id = pane->id();
                    has_active_gap = true;
                    gap_insert_after = SIZE_MAX;  // Before first tab by default
                    const auto& figs = pane->figure_indices();
                    float cx = hr.x + 2.0f;
                    for (size_t li = 0; li < figs.size(); ++li)
                    {
                        if (figs[li] == pane_tab_drag_.dragged_figure_index)
                        {
                            cx += 0;  // Skip the dragged tab's width
                            continue;
                        }
                        std::string t = fig_title(figs[li]);
                        ImVec2 tsz = ImGui::CalcTextSize(t.c_str());
                        float w = std::clamp(tsz.x + TAB_PAD * 2 + CLOSE_SZ, TAB_MIN_W, TAB_MAX_W);
                        if (mouse.x > cx + w * 0.5f)
                        {
                            gap_insert_after = li;
                        }
                        cx += w + 1.0f;
                    }
                }
                break;
            }
        }
    }

    // Update insertion gap animation
    float lerp_t_gap = std::min(1.0f, ANIM_SPEED * dt);
    if (has_active_gap)
    {
        insertion_gap_.target_pane_id = gap_pane_id;
        insertion_gap_.insert_after_idx = gap_insert_after;
        insertion_gap_.target_gap = GAP_WIDTH;
    }
    else
    {
        insertion_gap_.target_gap = 0.0f;
    }
    insertion_gap_.current_gap +=
        (insertion_gap_.target_gap - insertion_gap_.current_gap) * lerp_t_gap;
    if (insertion_gap_.current_gap < 0.5f && insertion_gap_.target_gap == 0.0f)
    {
        insertion_gap_.current_gap = 0.0f;
        insertion_gap_.target_pane_id = 0;
        insertion_gap_.insert_after_idx = SIZE_MAX;
    }

    for (auto* pane_const : panes)
    {
        auto* pane = const_cast<SplitPane*>(pane_const);
        if (!pane->is_leaf())
            continue;

        Rect b = pane->bounds();
        Rect hr{b.x, b.y, b.w, TAB_H};

        PaneHeader ph;
        ph.pane = pane;
        ph.header_rect = hr;

        const auto& figs = pane->figure_indices();
        float cur_x = hr.x + 2.0f;

        // Check if this pane has an active insertion gap
        bool pane_has_gap =
            (insertion_gap_.current_gap > 0.1f && pane->id() == insertion_gap_.target_pane_id);

        for (size_t li = 0; li < figs.size(); ++li)
        {
            size_t fig_idx = figs[li];
            std::string title = fig_title(fig_idx);

            ImVec2 text_sz = ImGui::CalcTextSize(title.c_str());
            float tw = std::clamp(text_sz.x + TAB_PAD * 2 + CLOSE_SZ, TAB_MIN_W, TAB_MAX_W);

            // Add insertion gap before this tab if needed
            if (pane_has_gap && insertion_gap_.insert_after_idx == SIZE_MAX && li == 0)
            {
                cur_x += insertion_gap_.current_gap;
            }
            else if (pane_has_gap && li > 0 && (li - 1) == insertion_gap_.insert_after_idx)
            {
                cur_x += insertion_gap_.current_gap;
            }

            // Animate position (keyed by pane+figure to avoid cross-pane interference)
            auto& anim = pane_tab_anims_[{ph.pane->id(), fig_idx}];
            anim.target_x = cur_x;
            if (anim.current_x == 0.0f && anim.target_x != 0.0f)
            {
                anim.current_x = anim.target_x;  // First frame: snap
            }
            float lerp_t = std::min(1.0f, ANIM_SPEED * dt);
            anim.current_x += (anim.target_x - anim.current_x) * lerp_t;
            anim.opacity += (anim.target_opacity - anim.opacity) * lerp_t;

            float draw_x = anim.current_x;

            bool is_active_local = (li == pane->active_local_index());
            bool hovered = (mouse.x >= draw_x && mouse.x < draw_x + tw && mouse.y >= hr.y
                            && mouse.y < hr.y + TAB_H);

            TabRect tr;
            tr.figure_index = fig_idx;
            tr.x = draw_x;
            tr.y = hr.y;
            tr.w = tw;
            tr.h = TAB_H;
            tr.is_active = is_active_local;
            tr.is_hovered = hovered;
            ph.tabs.push_back(tr);

            cur_x += tw + 1.0f;
        }

        headers.push_back(std::move(ph));
    }

    // ── Phase 2: Input handling ──────────────────────────────────────────

    pane_tab_hovered_ = false;

    for (auto& ph : headers)
    {
        // Draw header background
        Rect hr = ph.header_rect;
        draw_list->AddRectFilled(
            ImVec2(hr.x, hr.y), ImVec2(hr.x + hr.w, hr.y + hr.h), to_col(theme.bg_secondary));
        draw_list->AddLine(ImVec2(hr.x, hr.y + hr.h - 1),
                           ImVec2(hr.x + hr.w, hr.y + hr.h - 1),
                           to_col(theme.border_subtle),
                           1.0f);

        for (auto& tr : ph.tabs)
        {
            bool is_being_dragged =
                pane_tab_drag_.dragging && pane_tab_drag_.dragged_figure_index == tr.figure_index;

            // Skip drawing the tab in its original position if it's being dragged
            // cross-pane or if the tearoff preview card is active
            if (is_being_dragged && (pane_tab_drag_.cross_pane || pane_tab_drag_.preview_active))
                continue;

            // Tab background
            ImU32 bg;
            if (is_being_dragged)
            {
                bg = to_col(theme.bg_elevated);
            }
            else if (tr.is_active)
            {
                bg = to_col(theme.bg_tertiary);
            }
            else if (tr.is_hovered)
            {
                bg = to_col(theme.accent_subtle);
            }
            else
            {
                bg = to_col(theme.bg_secondary, 0.0f);
            }

            float inset_y = 3.0f;
            ImVec2 tl(tr.x, tr.y + inset_y);
            ImVec2 br(tr.x + tr.w, tr.y + tr.h);
            draw_list->AddRectFilled(tl, br, bg, 4.0f, ImDrawFlags_RoundCornersTop);

            // Active underline
            if (tr.is_active)
            {
                draw_list->AddLine(ImVec2(tl.x + 3, br.y - 1),
                                   ImVec2(br.x - 3, br.y - 1),
                                   to_col(theme.accent),
                                   2.0f);
            }

            // Title text
            std::string title = fig_title(tr.figure_index);
            ImVec2 text_sz = ImGui::CalcTextSize(title.c_str());
            ImVec2 text_pos(tr.x + TAB_PAD, tr.y + (tr.h - text_sz.y) * 0.5f);

            draw_list->PushClipRect(
                ImVec2(tr.x, tr.y), ImVec2(tr.x + tr.w - CLOSE_SZ - 2, tr.y + tr.h), true);
            draw_list->AddText(
                text_pos,
                tr.is_active ? to_col(theme.text_primary) : to_col(theme.text_secondary),
                title.c_str());
            draw_list->PopClipRect();

            // Close button (always show on active or hovered tabs)
            if (tr.is_active || tr.is_hovered)
            {
                float cx = tr.x + tr.w - CLOSE_SZ * 0.5f - 4.0f;
                float cy = tr.y + tr.h * 0.5f;
                float sz = 3.5f;

                bool close_hovered = (std::abs(mouse.x - cx) < CLOSE_SZ * 0.5f
                                      && std::abs(mouse.y - cy) < CLOSE_SZ * 0.5f);
                if (close_hovered)
                {
                    draw_list->AddCircleFilled(
                        ImVec2(cx, cy), CLOSE_SZ * 0.5f, to_col(theme.error, 0.15f));
                }
                ImU32 x_col = close_hovered ? to_col(theme.error) : to_col(theme.text_tertiary);
                draw_list->AddLine(ImVec2(cx - sz, cy - sz), ImVec2(cx + sz, cy + sz), x_col, 1.5f);
                draw_list->AddLine(ImVec2(cx - sz, cy + sz), ImVec2(cx + sz, cy - sz), x_col, 1.5f);

                // Close click — route through FigureManager callback
                if (close_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    if (pane_tab_close_cb_)
                        pane_tab_close_cb_(tr.figure_index);
                    pane_tab_hovered_ = true;
                    continue;
                }
            }

            // Click / drag handling
            if (tr.is_hovered)
            {
                pane_tab_hovered_ = true;

                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    // Activate this tab
                    for (size_t li = 0; li < ph.pane->figure_indices().size(); ++li)
                    {
                        if (ph.pane->figure_indices()[li] == tr.figure_index)
                        {
                            dock_system_->activate_local_tab(ph.pane->id(), li);
                            break;
                        }
                    }
                    // Start potential drag via TabDragController
                    if (tab_drag_controller_)
                    {
                        tab_drag_controller_->on_mouse_down(
                            ph.pane->id(), tr.figure_index, mouse.x, mouse.y);
                        tab_drag_controller_->set_ghost_title(fig_title(tr.figure_index));
                    }
                    // Sync to legacy state for rendering compatibility
                    pane_tab_drag_.dragging = false;
                    pane_tab_drag_.source_pane_id = ph.pane->id();
                    pane_tab_drag_.dragged_figure_index = tr.figure_index;
                    pane_tab_drag_.drag_start_x = mouse.x;
                    pane_tab_drag_.drag_start_y = mouse.y;
                    pane_tab_drag_.cross_pane = false;
                    pane_tab_drag_.dock_dragging = false;
                    // Capture source tab rect for preview animation origin
                    pane_tab_drag_.source_tab_x = tr.x;
                    pane_tab_drag_.source_tab_y = tr.y;
                    pane_tab_drag_.source_tab_w = tr.w;
                    pane_tab_drag_.source_tab_h = tr.h;
                    pane_tab_drag_.preview_active = false;
                    pane_tab_drag_.preview_scale = 0.0f;
                    pane_tab_drag_.preview_opacity = 0.0f;
                    pane_tab_drag_.preview_shadow = 0.0f;
                }

                // Right-click context menu
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                {
                    pane_ctx_menu_fig_ = tr.figure_index;
                    pane_ctx_menu_open_ = true;
                    ImGui::OpenPopup("##pane_tab_ctx");
                }
            }
        }
    }

    // ── Phase 3: Drag update ─────────────────────────────────────────────
    // The TabDragController manages the state machine (threshold detection,
    // dock-drag transitions, drop/cancel).  We call update() each frame and
    // sync its state back to pane_tab_drag_ for rendering compatibility.

    if (tab_drag_controller_ && tab_drag_controller_->is_active())
    {
        // Compute screen-space cursor position via GLFW (not ImGui).
        // ImGui::GetMousePos() returns garbage when the cursor leaves
        // the GLFW window, causing int overflow → INT_MIN coordinates.
        // GLFW's glfwGetCursorPos works correctly even outside the window.
        float screen_mx, screen_my;
        {
            double sx, sy;
            if (tab_drag_controller_->get_screen_cursor(sx, sy))
            {
                screen_mx = static_cast<float>(sx);
                screen_my = static_cast<float>(sy);
            }
            else
            {
                ImVec2 wpos = ImGui::GetMainViewport()->Pos;
                screen_mx = wpos.x + mouse.x;
                screen_my = wpos.y + mouse.y;
            }
        }

        // Check mouse button across ALL GLFW windows.  On X11, creating
        // a new GLFW window (the preview) during an active drag can break
        // the implicit pointer grab on the source window, causing
        // glfwGetMouseButton on the source to return RELEASE even though
        // the user is still holding the button.  The grab may transfer
        // to the newly created preview window.
        bool mouse_held = tab_drag_controller_->check_mouse_held();
        tab_drag_controller_->update(mouse.x, mouse.y, mouse_held, screen_mx, screen_my);

        // Sync controller state → legacy pane_tab_drag_ for rendering
        if (tab_drag_controller_->is_dragging())
        {
            pane_tab_drag_.dragging = true;
            pane_tab_drag_.cross_pane = tab_drag_controller_->is_cross_pane();
            pane_tab_drag_.dock_dragging = tab_drag_controller_->is_dock_dragging();
        }

        // If controller returned to Idle, the drop/cancel already executed
        // via callbacks — reset legacy state.
        if (!tab_drag_controller_->is_active())
        {
            pane_tab_drag_.dragging = false;
            pane_tab_drag_.dragged_figure_index = INVALID_FIGURE_ID;
            pane_tab_drag_.cross_pane = false;
            pane_tab_drag_.dock_dragging = false;
        }
    }
    else if (pane_tab_drag_.dragged_figure_index != INVALID_FIGURE_ID
             && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        // Fallback: no controller — use legacy inline logic
        float dx = mouse.x - pane_tab_drag_.drag_start_x;
        float dy = mouse.y - pane_tab_drag_.drag_start_y;
        float dist = std::sqrt(dx * dx + dy * dy);

        if (!pane_tab_drag_.dragging && dist > DRAG_THRESHOLD)
        {
            pane_tab_drag_.dragging = true;
        }

        if (pane_tab_drag_.dragging)
        {
            constexpr float DOCK_DRAG_THRESHOLD = 30.0f;
            if (!pane_tab_drag_.dock_dragging && std::abs(dy) > DOCK_DRAG_THRESHOLD)
            {
                bool over_any_header = false;
                for (auto& ph : headers)
                {
                    Rect hr = ph.header_rect;
                    if (mouse.x >= hr.x && mouse.x < hr.x + hr.w && mouse.y >= hr.y - 10
                        && mouse.y < hr.y + hr.h + 10)
                    {
                        over_any_header = true;
                        break;
                    }
                }
                if (!over_any_header)
                {
                    pane_tab_drag_.dock_dragging = true;
                    dock_system_->begin_drag(pane_tab_drag_.dragged_figure_index, mouse.x, mouse.y);
                }
            }

            if (pane_tab_drag_.dock_dragging)
            {
                dock_system_->update_drag(mouse.x, mouse.y);
            }
        }
    }

    // Cross-pane detection (shared by both controller and legacy paths)
    if (pane_tab_drag_.dragging)
    {
        bool over_source = false;
        for (auto& ph : headers)
        {
            Rect hr = ph.header_rect;
            if (mouse.x >= hr.x && mouse.x < hr.x + hr.w && mouse.y >= hr.y
                && mouse.y < hr.y + hr.h)
            {
                if (ph.pane->id() == pane_tab_drag_.source_pane_id)
                {
                    over_source = true;
                }
                else
                {
                    pane_tab_drag_.cross_pane = true;
                }
                break;
            }
        }
        if (!over_source && !pane_tab_drag_.dock_dragging)
        {
            pane_tab_drag_.cross_pane = true;
        }
        if (tab_drag_controller_)
        {
            tab_drag_controller_->set_cross_pane(pane_tab_drag_.cross_pane);
        }

        // ── Ghost tab / preview sync ──────────────────────────────────
        std::string title = fig_title(pane_tab_drag_.dragged_figure_index);

        // Sync preview_active from TabDragController's preview state
        // (the real preview window is managed by TabDragController + WindowManager)
        if (tab_drag_controller_ && tab_drag_controller_->is_preview_active())
            pane_tab_drag_.preview_active = true;

        if (!pane_tab_drag_.preview_active)
        {
            // Preview window not yet created — draw small ghost tab at cursor
            ImVec2 text_sz = ImGui::CalcTextSize(title.c_str());
            float ghost_w = std::clamp(text_sz.x + TAB_PAD * 2 + CLOSE_SZ, TAB_MIN_W, TAB_MAX_W);
            float ghost_h = TAB_H;
            float ghost_x = mouse.x - ghost_w * 0.5f;
            float ghost_y = mouse.y - ghost_h * 0.5f;
            draw_list->AddRectFilled(ImVec2(ghost_x + 2, ghost_y + 2),
                                     ImVec2(ghost_x + ghost_w + 2, ghost_y + ghost_h + 2),
                                     IM_COL32(0, 0, 0, 40),
                                     6.0f);
            draw_list->AddRectFilled(ImVec2(ghost_x, ghost_y),
                                     ImVec2(ghost_x + ghost_w, ghost_y + ghost_h),
                                     to_col(theme.bg_elevated),
                                     6.0f);
            draw_list->AddRect(ImVec2(ghost_x, ghost_y),
                               ImVec2(ghost_x + ghost_w, ghost_y + ghost_h),
                               to_col(theme.accent, 0.6f),
                               6.0f,
                               0,
                               1.5f);
            ImVec2 gtext_pos(ghost_x + TAB_PAD, ghost_y + (ghost_h - text_sz.y) * 0.5f);
            draw_list->AddText(gtext_pos, to_col(theme.text_primary), title.c_str());
        }
        // else: preview is rendered in a separate OS window by WindowManager

        // Draw drop indicator on target pane header
        for (auto& ph : headers)
        {
            if (ph.pane->id() == pane_tab_drag_.source_pane_id && ph.pane->figure_count() <= 1)
                continue;

            Rect hr = ph.header_rect;
            if (mouse.x >= hr.x && mouse.x < hr.x + hr.w && mouse.y >= hr.y - 10
                && mouse.y < hr.y + hr.h + 10)
            {
                // Highlight target header
                draw_list->AddRectFilled(ImVec2(hr.x, hr.y),
                                         ImVec2(hr.x + hr.w, hr.y + hr.h),
                                         to_col(theme.accent, 0.08f));

                // Draw insertion line
                float insert_x = hr.x + 4.0f;
                for (auto& tr : ph.tabs)
                {
                    if (mouse.x > tr.x + tr.w * 0.5f)
                    {
                        insert_x = tr.x + tr.w + 1.0f;
                    }
                }
                draw_list->AddLine(ImVec2(insert_x, hr.y + 4),
                                   ImVec2(insert_x, hr.y + hr.h - 4),
                                   to_col(theme.accent),
                                   2.0f);
            }
        }
    }

    // ── Phase 4: Drag end (drop) ─────────────────────────────────────────
    // When TabDragController is active, drop/cancel is handled by its
    // update() call above (callbacks fire on state transitions).
    // The legacy fallback handles the case when no controller is set.

    if (!tab_drag_controller_ && pane_tab_drag_.dragged_figure_index != INVALID_FIGURE_ID
        && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        if (pane_tab_drag_.dragging && pane_tab_drag_.dock_dragging)
        {
            // Check if mouse is outside the window → detach to new window
            ImVec2 display_size = ImGui::GetIO().DisplaySize;
            bool outside = (mouse.x < 0 || mouse.y < 0 || mouse.x >= display_size.x
                            || mouse.y >= display_size.y);

            if (outside && pane_tab_detach_cb_)
            {
                dock_system_->cancel_drag();
                ImVec2 wpos = ImGui::GetMainViewport()->Pos;
                pane_tab_detach_cb_(
                    pane_tab_drag_.dragged_figure_index, wpos.x + mouse.x, wpos.y + mouse.y);
            }
            else
            {
                // Dock-drag mode: let dock system handle the split
                dock_system_->end_drag(mouse.x, mouse.y);
            }
        }
        else if (pane_tab_drag_.dragging && pane_tab_drag_.cross_pane)
        {
            // Cross-pane tab move: find target pane under mouse
            for (auto& ph : headers)
            {
                Rect hr = ph.header_rect;
                if (mouse.x >= hr.x && mouse.x < hr.x + hr.w && mouse.y >= hr.y - 10
                    && mouse.y < hr.y + hr.h + 10)
                {
                    if (ph.pane->id() != pane_tab_drag_.source_pane_id)
                    {
                        dock_system_->move_figure_to_pane(pane_tab_drag_.dragged_figure_index,
                                                          ph.pane->id());
                    }
                    break;
                }
            }
        }

        // Reset drag state
        pane_tab_drag_.dragging = false;
        pane_tab_drag_.dragged_figure_index = INVALID_FIGURE_ID;
        pane_tab_drag_.cross_pane = false;
        pane_tab_drag_.dock_dragging = false;
    }

    // Cancel drag on escape or right-click
    if (pane_tab_drag_.dragged_figure_index != INVALID_FIGURE_ID
        && (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
    {
        if (tab_drag_controller_ && tab_drag_controller_->is_active())
        {
            tab_drag_controller_->cancel();
        }
        else
        {
            if (pane_tab_drag_.dock_dragging)
            {
                dock_system_->cancel_drag();
            }
        }
        pane_tab_drag_.dragging = false;
        pane_tab_drag_.dragged_figure_index = INVALID_FIGURE_ID;
        pane_tab_drag_.cross_pane = false;
        pane_tab_drag_.dock_dragging = false;
    }

    // ── Phase 5: Right-click context menu ────────────────────────────────
    // OpenPopup/BeginPopup require an active ImGui window context.
    // Create an invisible overlay window for the popup scope.
    {
        ImGuiIO& popup_io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(popup_io.DisplaySize.x, popup_io.DisplaySize.y));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGuiWindowFlags popup_host_flags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus
            | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground
            | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav;
        ImGui::Begin("##pane_tab_popup_host", nullptr, popup_host_flags);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);

        // Open the popup if right-click was detected in Phase 2
        if (pane_ctx_menu_open_ && pane_ctx_menu_fig_ != INVALID_FIGURE_ID)
        {
            ImGui::OpenPopup("##pane_tab_ctx");
            pane_ctx_menu_open_ = false;  // Only open once
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 8));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 2));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 4));
        ImGui::PushStyleColor(
            ImGuiCol_PopupBg,
            ImVec4(theme.bg_elevated.r, theme.bg_elevated.g, theme.bg_elevated.b, 0.98f));
        ImGui::PushStyleColor(
            ImGuiCol_Border,
            ImVec4(theme.border_default.r, theme.border_default.g, theme.border_default.b, 0.5f));

        if (ImGui::BeginPopup("##pane_tab_ctx"))
        {
            if (pane_ctx_menu_fig_ != INVALID_FIGURE_ID)
            {
                auto menu_item = [&](const char* label) -> bool
                {
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                                          ImVec4(theme.accent_subtle.r,
                                                 theme.accent_subtle.g,
                                                 theme.accent_subtle.b,
                                                 0.5f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderActive,
                                          ImVec4(theme.accent_muted.r,
                                                 theme.accent_muted.g,
                                                 theme.accent_muted.b,
                                                 0.7f));
                    float item_h = ImGui::GetTextLineHeight() + 8.0f;
                    bool clicked = ImGui::Selectable(
                        label, false, ImGuiSelectableFlags_None, ImVec2(0, item_h));
                    ImGui::PopStyleColor(3);
                    return clicked;
                };

                if (menu_item("Rename..."))
                {
                    pane_tab_renaming_ = true;
                    pane_tab_rename_fig_ = pane_ctx_menu_fig_;
                    std::string title = fig_title(pane_ctx_menu_fig_);
                    strncpy(pane_tab_rename_buf_, title.c_str(), sizeof(pane_tab_rename_buf_) - 1);
                    pane_tab_rename_buf_[sizeof(pane_tab_rename_buf_) - 1] = '\0';
                }

                if (menu_item("Duplicate"))
                {
                    if (pane_tab_duplicate_cb_)
                        pane_tab_duplicate_cb_(pane_ctx_menu_fig_);
                }

                ImGui::Dummy(ImVec2(0, 2));
                ImGui::PushStyleColor(
                    ImGuiCol_Separator,
                    ImVec4(
                        theme.border_subtle.r, theme.border_subtle.g, theme.border_subtle.b, 0.3f));
                ImGui::Separator();
                ImGui::PopStyleColor();
                ImGui::Dummy(ImVec2(0, 2));

                if (menu_item("Split Right"))
                {
                    if (pane_tab_split_right_cb_)
                        pane_tab_split_right_cb_(pane_ctx_menu_fig_);
                }

                if (menu_item("Split Down"))
                {
                    if (pane_tab_split_down_cb_)
                        pane_tab_split_down_cb_(pane_ctx_menu_fig_);
                }

                if (menu_item("Detach to Window"))
                {
                    if (pane_tab_detach_cb_)
                    {
                        ImVec2 m = ImGui::GetMousePos();
                        ImVec2 wpos = ImGui::GetMainViewport()->Pos;
                        pane_tab_detach_cb_(pane_ctx_menu_fig_, wpos.x + m.x, wpos.y + m.y);
                    }
                }

                ImGui::Dummy(ImVec2(0, 2));
                ImGui::PushStyleColor(
                    ImGuiCol_Separator,
                    ImVec4(
                        theme.border_subtle.r, theme.border_subtle.g, theme.border_subtle.b, 0.3f));
                ImGui::Separator();
                ImGui::PopStyleColor();
                ImGui::Dummy(ImVec2(0, 2));

                if (menu_item("Close"))
                {
                    if (pane_tab_close_cb_)
                        pane_tab_close_cb_(pane_ctx_menu_fig_);
                }
            }
            ImGui::EndPopup();
        }
        else
        {
            pane_ctx_menu_open_ = false;
            pane_ctx_menu_fig_ = INVALID_FIGURE_ID;
        }

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);

        // ── Rename popup ─────────────────────────────────────────────────────

        if (pane_tab_renaming_ && pane_tab_rename_fig_ != INVALID_FIGURE_ID)
        {
            ImGui::OpenPopup("##pane_tab_rename");
            pane_tab_renaming_ = false;
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 12));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleColor(
            ImGuiCol_PopupBg,
            ImVec4(theme.bg_elevated.r, theme.bg_elevated.g, theme.bg_elevated.b, 0.98f));

        if (ImGui::BeginPopup("##pane_tab_rename"))
        {
            ImGui::TextUnformatted("Rename tab");
            ImGui::Spacing();
            bool enter = ImGui::InputText("##pane_rename_input",
                                          pane_tab_rename_buf_,
                                          sizeof(pane_tab_rename_buf_),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere(-1);
            ImGui::Spacing();

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16, 6));
            if (enter || ImGui::Button("OK"))
            {
                std::string new_title(pane_tab_rename_buf_);
                if (!new_title.empty() && pane_tab_rename_fig_ != INVALID_FIGURE_ID)
                {
                    if (pane_tab_rename_cb_)
                        pane_tab_rename_cb_(pane_tab_rename_fig_, new_title);
                }
                pane_tab_rename_fig_ = INVALID_FIGURE_ID;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                pane_tab_rename_fig_ = INVALID_FIGURE_ID;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleVar();
            ImGui::EndPopup();
        }

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);

        ImGui::End();  // ##pane_tab_popup_host
    }                  // Phase 5 scope
}

    #if SPECTRA_FLOATING_TOOLBAR
void ImGuiIntegration::draw_floating_toolbar()
{
    if (!layout_manager_)
        return;

    float opacity = layout_manager_->floating_toolbar_opacity();
    if (opacity < 0.01f)
        return;  // Fully hidden, skip drawing

    Rect bounds = layout_manager_->floating_toolbar_rect();

    // Check if mouse is hovering near the toolbar — reveal on hover
    ImVec2 mouse = ImGui::GetIO().MousePos;
    float hover_margin = 30.0f;
    bool mouse_near =
        (mouse.x >= bounds.x - hover_margin && mouse.x <= bounds.x + bounds.w + hover_margin
         && mouse.y >= bounds.y - hover_margin && mouse.y <= bounds.y + bounds.h + hover_margin);
    if (mouse_near)
    {
        layout_manager_->notify_toolbar_activity();
        opacity = layout_manager_->floating_toolbar_opacity();
    }

    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(bounds.w, bounds.h));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollWithMouse
                             | ImGuiWindowFlags_NoSavedSettings
                             | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

    // Apply opacity to all toolbar colors
    float bg_alpha = 0.95f * opacity;
    float border_alpha = 0.6f * opacity;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 20.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, opacity);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
                          ImVec4(ui::theme().bg_elevated.r,
                                 ui::theme().bg_elevated.g,
                                 ui::theme().bg_elevated.b,
                                 bg_alpha));
    ImGui::PushStyleColor(ImGuiCol_Border,
                          ImVec4(ui::theme().border_default.r,
                                 ui::theme().border_default.g,
                                 ui::theme().border_default.b,
                                 border_alpha));

    if (ImGui::Begin("##floatingtoolbar", nullptr, flags))
    {
        // Handle dragging — drag on empty area of the toolbar to reposition
        if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f))
        {
            if (!toolbar_dragging_)
            {
                // Check we're not clicking a button (only drag from empty space)
                if (!ImGui::IsAnyItemHovered())
                {
                    toolbar_dragging_ = true;
                }
            }
        }
        if (toolbar_dragging_)
        {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                ImVec2 delta = ImGui::GetIO().MouseDelta;
                layout_manager_->set_floating_toolbar_drag_offset(bounds.x + delta.x,
                                                                  bounds.y + delta.y);
                layout_manager_->notify_toolbar_activity();
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                toolbar_dragging_ = false;
            }
        }

        // Quick access tools
        draw_toolbar_button(
            ui::icon_str(ui::Icon::ZoomIn),
            [this]()
            {
                interaction_mode_ = ToolMode::BoxZoom;
                layout_manager_->notify_toolbar_activity();
            },
            "Zoom",
            interaction_mode_ == ToolMode::BoxZoom);
        ImGui::SameLine();
        draw_toolbar_button(
            ui::icon_str(ui::Icon::Hand),
            [this]()
            {
                interaction_mode_ = ToolMode::Pan;
                layout_manager_->notify_toolbar_activity();
            },
            "Pan",
            interaction_mode_ == ToolMode::Pan);
        ImGui::SameLine();
        draw_toolbar_button(
            ui::icon_str(ui::Icon::Crosshair),
            [this]()
            {
                interaction_mode_ = ToolMode::Select;
                layout_manager_->notify_toolbar_activity();
            },
            "Select",
            interaction_mode_ == ToolMode::Select);
        ImGui::SameLine();
        draw_toolbar_button(
            ui::icon_str(ui::Icon::Ruler),
            [this]() { layout_manager_->notify_toolbar_activity(); },
            "Measure");

        // Double-click to reset position
        if (ImGui::IsWindowHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
            && !ImGui::IsAnyItemHovered())
        {
            layout_manager_->reset_floating_toolbar_position();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
}
    #endif

void ImGuiIntegration::draw_plot_text(Figure& figure)
{
    if (!layout_manager_)
        return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const auto& colors = ui::ThemeManager::instance().colors();
    ImU32 tick_col = IM_COL32(static_cast<uint8_t>(colors.tick_label.r * 255),
                              static_cast<uint8_t>(colors.tick_label.g * 255),
                              static_cast<uint8_t>(colors.tick_label.b * 255),
                              static_cast<uint8_t>(colors.tick_label.a * 255));
    ImU32 label_col = IM_COL32(static_cast<uint8_t>(colors.text_primary.r * 255),
                               static_cast<uint8_t>(colors.text_primary.g * 255),
                               static_cast<uint8_t>(colors.text_primary.b * 255),
                               static_cast<uint8_t>(colors.text_primary.a * 255));
    ImU32 title_col = label_col;

    constexpr float tick_padding = 5.0f;

    for (auto& axes_ptr : figure.axes())
    {
        if (!axes_ptr)
            continue;
        auto& axes = *axes_ptr;
        const auto& vp = axes.viewport();
        auto xlim = axes.x_limits();
        auto ylim = axes.y_limits();

        float x_range = xlim.max - xlim.min;
        float y_range = ylim.max - ylim.min;
        if (x_range == 0.0f)
            x_range = 1.0f;
        if (y_range == 0.0f)
            y_range = 1.0f;

        auto data_to_px_x = [&](float dx) -> float
        { return vp.x + (dx - xlim.min) / x_range * vp.w; };
        auto data_to_px_y = [&](float dy) -> float
        { return vp.y + (1.0f - (dy - ylim.min) / y_range) * vp.h; };

        // --- Tick marks and labels ---
        const auto& as = axes.axis_style();
        float tl = as.tick_length;  // tick mark length in pixels
        ImU32 axis_col = IM_COL32(static_cast<uint8_t>(colors.axis_line.r * 255),
                                  static_cast<uint8_t>(colors.axis_line.g * 255),
                                  static_cast<uint8_t>(colors.axis_line.b * 255),
                                  static_cast<uint8_t>(colors.axis_line.a * 255));

        auto x_ticks = axes.compute_x_ticks();
        auto y_ticks = axes.compute_y_ticks();

        // X tick marks (at bottom edge of viewport)
        for (size_t i = 0; i < x_ticks.positions.size(); ++i)
        {
            float px = data_to_px_x(x_ticks.positions[i]);
            if (tl > 0.0f)
                dl->AddLine(ImVec2(px, vp.y + vp.h), ImVec2(px, vp.y + vp.h + tl), axis_col, 1.0f);
        }

        // Y tick marks (at left edge of viewport)
        for (size_t i = 0; i < y_ticks.positions.size(); ++i)
        {
            float py = data_to_px_y(y_ticks.positions[i]);
            if (tl > 0.0f)
                dl->AddLine(ImVec2(vp.x, py), ImVec2(vp.x - tl, py), axis_col, 1.0f);
        }

        // X tick labels
        ImGui::PushFont(font_body_);
        for (size_t i = 0; i < x_ticks.positions.size(); ++i)
        {
            float px = data_to_px_x(x_ticks.positions[i]);
            const char* txt = x_ticks.labels[i].c_str();
            ImVec2 sz = ImGui::CalcTextSize(txt);
            dl->AddText(ImVec2(px - sz.x * 0.5f, vp.y + vp.h + tl + tick_padding), tick_col, txt);
        }

        // Y tick labels
        for (size_t i = 0; i < y_ticks.positions.size(); ++i)
        {
            float py = data_to_px_y(y_ticks.positions[i]);
            const char* txt = y_ticks.labels[i].c_str();
            ImVec2 sz = ImGui::CalcTextSize(txt);
            dl->AddText(ImVec2(vp.x - tl - tick_padding - sz.x, py - sz.y * 0.5f), tick_col, txt);
        }
        ImGui::PopFont();

        // --- X axis label ---
        if (!axes.get_xlabel().empty())
        {
            ImGui::PushFont(font_menubar_);
            const char* txt = axes.get_xlabel().c_str();
            ImVec2 sz = ImGui::CalcTextSize(txt);
            float cx = vp.x + vp.w * 0.5f;
            float py = vp.y + vp.h + tick_padding + 16.0f + tick_padding;
            dl->AddText(ImVec2(cx - sz.x * 0.5f, py), label_col, txt);
            ImGui::PopFont();
        }

        // --- Y axis label (rotated -90°) ---
        if (!axes.get_ylabel().empty())
        {
            ImGui::PushFont(font_menubar_);
            const char* txt = axes.get_ylabel().c_str();
            ImVec2 sz = ImGui::CalcTextSize(txt);

            // Where the rotated label should be centered
            float center_x = vp.x - tick_padding * 2.0f - 20.0f;
            float center_y = vp.y + vp.h * 0.5f;

            ImDrawList* dl = ImGui::GetForegroundDrawList();

            // Render text at origin, then rotate the emitted vertices -90°
            // Place text so its center lands at (0,0) before rotation
            ImVec2 text_pos(center_x - sz.x * 0.5f, center_y - sz.y * 0.5f);

            int vtx_begin = dl->VtxBuffer.Size;
            dl->AddText(text_pos, label_col, txt);
            int vtx_end = dl->VtxBuffer.Size;

            // Rotate all new vertices -90° around (center_x, center_y)
            float cos_a = 0.0f;   // cos(-90°)
            float sin_a = -1.0f;  // sin(-90°)
            for (int i = vtx_begin; i < vtx_end; ++i)
            {
                ImDrawVert& v = dl->VtxBuffer[i];
                float dx = v.pos.x - center_x;
                float dy = v.pos.y - center_y;
                v.pos.x = center_x + dx * cos_a - dy * sin_a;
                v.pos.y = center_y + dx * sin_a + dy * cos_a;
            }

            ImGui::PopFont();
        }

        // --- Title (clamped inside viewport so tab bar doesn't cover it) ---
        if (!axes.get_title().empty())
        {
            ImGui::PushFont(font_title_);
            const char* txt = axes.get_title().c_str();
            ImVec2 sz = ImGui::CalcTextSize(txt);
            float cx = vp.x + vp.w * 0.5f;
            float py = vp.y - sz.y - tick_padding;
            if (py < vp.y + 2.0f)
                py = vp.y + 2.0f;
            dl->AddText(ImVec2(cx - sz.x * 0.5f, py), title_col, txt);
            ImGui::PopFont();
        }
    }

    // ─── 3D Axes: billboarded tick labels, axis labels, title ────────────
    for (auto& axes_ptr : figure.all_axes())
    {
        if (!axes_ptr)
            continue;
        auto* axes3d = dynamic_cast<Axes3D*>(axes_ptr.get());
        if (!axes3d)
            continue;

        const auto& vp = axes3d->viewport();
        const auto& cam = axes3d->camera();

        // Build MVP matrix: projection * view * model
        float aspect = vp.w / std::max(vp.h, 1.0f);
        mat4 proj = cam.projection_matrix(aspect);
        mat4 view = cam.view_matrix();
        mat4 model = axes3d->data_to_normalized_matrix();
        mat4 mvp = mat4_mul(proj, mat4_mul(view, model));

        // Helper: project a 3D world point to screen coords within the viewport
        auto world_to_screen = [&](vec3 world_pos, float& sx, float& sy) -> bool
        {
            // Multiply by MVP
            float clip_x = mvp.m[0] * world_pos.x + mvp.m[4] * world_pos.y + mvp.m[8] * world_pos.z
                           + mvp.m[12];
            float clip_y = mvp.m[1] * world_pos.x + mvp.m[5] * world_pos.y + mvp.m[9] * world_pos.z
                           + mvp.m[13];
            float clip_w = mvp.m[3] * world_pos.x + mvp.m[7] * world_pos.y + mvp.m[11] * world_pos.z
                           + mvp.m[15];

            // Behind camera
            if (clip_w <= 0.001f)
                return false;

            // NDC
            float ndc_x = clip_x / clip_w;
            float ndc_y = clip_y / clip_w;

            // NDC to screen (Vulkan Y-down: ndc_y=-1 is top, +1 is bottom)
            sx = vp.x + (ndc_x + 1.0f) * 0.5f * vp.w;
            sy = vp.y + (ndc_y + 1.0f) * 0.5f * vp.h;

            // Cull if far outside viewport (generous margin so arrows/labels
            // at bounding box edges remain visible at oblique camera angles)
            float margin = 200.0f;
            if (sx < vp.x - margin || sx > vp.x + vp.w + margin || sy < vp.y - margin
                || sy > vp.y + vp.h + margin)
                return false;

            return true;
        };

        auto xlim = axes3d->x_limits();
        auto ylim = axes3d->y_limits();
        auto zlim = axes3d->z_limits();

        float x0 = xlim.min, y0 = ylim.min, z0 = zlim.min;

        // Detect top-down view: camera looking down Y (orbit elevation≈90°) or Z (transition
        // camera) Threshold 0.98 ≈ 78° elevation — only trigger when nearly straight down
        vec3 view_dir_early = vec3_normalize(cam.target - cam.position);
        bool looking_down_y = std::abs(view_dir_early.y) > 0.98f;
        bool looking_down_z = std::abs(view_dir_early.z) > 0.98f;
        // bool is_top_down_early = looking_down_y || looking_down_z;  // Currently unused

        // Tick offset: slightly beyond the tick mark end
        float x_tick_offset = (ylim.max - ylim.min) * 0.04f;
        float y_tick_offset = (xlim.max - xlim.min) * 0.04f;
        float z_tick_offset = (xlim.max - xlim.min) * 0.04f;

        // --- X-axis tick labels (along y=y0, z=z0 edge) ---
        // Visible in all views (X is always a screen axis in top-down)
        ImGui::PushFont(font_body_);
        auto x_ticks = axes3d->compute_x_ticks();
        for (size_t i = 0; i < x_ticks.positions.size(); ++i)
        {
            float sx, sy;
            vec3 pos = {x_ticks.positions[i], y0 - x_tick_offset, z0};
            if (!world_to_screen(pos, sx, sy))
                continue;
            const char* txt = x_ticks.labels[i].c_str();
            ImVec2 sz = ImGui::CalcTextSize(txt);
            dl->AddText(ImVec2(sx - sz.x * 0.5f, sy), tick_col, txt);
        }

        // --- Y-axis tick labels (along x=x0, z=z0 edge) ---
        // Hidden when looking down Y (Y is depth axis, all labels overlap)
        if (!looking_down_y)
        {
            auto y_ticks = axes3d->compute_y_ticks();
            for (size_t i = 0; i < y_ticks.positions.size(); ++i)
            {
                float sx, sy;
                vec3 pos = {x0 - y_tick_offset, y_ticks.positions[i], z0};
                if (!world_to_screen(pos, sx, sy))
                    continue;
                const char* txt = y_ticks.labels[i].c_str();
                ImVec2 sz = ImGui::CalcTextSize(txt);
                dl->AddText(ImVec2(sx - sz.x, sy - sz.y * 0.5f), tick_col, txt);
            }
        }

        // --- Z-axis tick labels (along x=x0, y=y0 edge) ---
        // Hidden when looking down Z (Z is depth axis, all labels overlap)
        if (!looking_down_z)
        {
            auto z_ticks = axes3d->compute_z_ticks();
            for (size_t i = 0; i < z_ticks.positions.size(); ++i)
            {
                float sx, sy;
                vec3 pos = {x0 - z_tick_offset, y0, z_ticks.positions[i]};
                if (!world_to_screen(pos, sx, sy))
                    continue;
                const char* txt = z_ticks.labels[i].c_str();
                ImVec2 sz = ImGui::CalcTextSize(txt);
                dl->AddText(ImVec2(sx - sz.x - tick_padding, sy - sz.y * 0.5f), tick_col, txt);
            }
        }
        ImGui::PopFont();

        // --- Axis direction arrows with labels ---
        // In top-down views, only show arrows for the two visible axes,
        // placed on the visible grid plane. Hide the depth-axis arrow.
        // Looking down Y: visible plane is XZ at y=y0, show X and Z arrows
        // Looking down Z: visible plane is XY at z=z0, show X and Y arrows
        // 3D view: show all three arrows at bounding box corners
        {
            float x1 = xlim.max, y1 = ylim.max, z1 = zlim.max;
            float x_range = xlim.max - xlim.min;
            float y_range = ylim.max - ylim.min;
            float z_range = zlim.max - zlim.min;
            float arrow_len_x = x_range * 0.18f;
            float arrow_len_y = y_range * 0.18f;
            float arrow_len_z = z_range * 0.18f;

            // Arrow colors: X=red, Y=green, Z=blue (standard convention)
            ImU32 x_arrow_col = IM_COL32(230, 70, 70, 220);
            ImU32 y_arrow_col = IM_COL32(70, 200, 70, 220);
            ImU32 z_arrow_col = IM_COL32(80, 130, 255, 220);

            // Helper: draw a 2D arrowhead at screen position
            auto draw_arrowhead =
                [&](float from_x, float from_y, float tip_x, float tip_y, ImU32 col)
            {
                float dx = tip_x - from_x;
                float dy = tip_y - from_y;
                float len = std::sqrt(dx * dx + dy * dy);
                if (len < 1.0f)
                    return;
                float ux = dx / len;
                float uy = dy / len;
                float head_size = 6.0f;
                float px = -uy * head_size;
                float py = ux * head_size;
                float base_x = tip_x - ux * head_size * 1.8f;
                float base_y = tip_y - uy * head_size * 1.8f;
                ImVec2 tri[3] = {ImVec2(tip_x, tip_y),
                                 ImVec2(base_x + px, base_y + py),
                                 ImVec2(base_x - px, base_y - py)};
                dl->AddTriangleFilled(tri[0], tri[1], tri[2], col);
            };

            // Helper: draw an arrow with label
            auto draw_arrow_with_label = [&](vec3 start,
                                             vec3 end,
                                             ImU32 col,
                                             const char* default_lbl,
                                             const std::string& user_lbl)
            {
                float sx0, sy0, sx1, sy1;
                if (!world_to_screen(start, sx0, sy0) || !world_to_screen(end, sx1, sy1))
                    return;
                dl->AddLine(ImVec2(sx0, sy0), ImVec2(sx1, sy1), col, 2.0f);
                draw_arrowhead(sx0, sy0, sx1, sy1, col);
                ImGui::PushFont(font_menubar_);
                const char* lbl = user_lbl.empty() ? default_lbl : user_lbl.c_str();
                ImVec2 sz = ImGui::CalcTextSize(lbl);
                float label_offset = 8.0f;
                float dir_x = sx1 - sx0;
                float dir_y = sy1 - sy0;
                float dir_len = std::sqrt(dir_x * dir_x + dir_y * dir_y);
                float lx = sx1 + (dir_len > 1.0f ? dir_x / dir_len * label_offset : label_offset);
                float ly =
                    sy1 + (dir_len > 1.0f ? dir_y / dir_len * label_offset : 0.0f) - sz.y * 0.5f;
                dl->AddText(ImVec2(lx, ly), col, lbl);
                ImGui::PopFont();
            };

            // Always show all three arrows in 3D mode (regardless of camera angle)
            // Place them at the bounding box corners as in original 3D behavior
            draw_arrow_with_label(
                {x1, y0, z0}, {x1 + arrow_len_x, y0, z0}, x_arrow_col, "X", axes3d->get_xlabel());
            draw_arrow_with_label(
                {x0, y1, z0}, {x0, y1 + arrow_len_y, z0}, y_arrow_col, "Y", axes3d->get_ylabel());
            draw_arrow_with_label(
                {x0, y0, z1}, {x0, y0, z1 + arrow_len_z}, z_arrow_col, "Z", axes3d->get_zlabel());
        }

        // --- 3D Title (clamped inside viewport so tab bar doesn't cover it) ---
        if (!axes3d->get_title().empty())
        {
            ImGui::PushFont(font_title_);
            const char* txt = axes3d->get_title().c_str();
            ImVec2 sz = ImGui::CalcTextSize(txt);
            float cx = vp.x + vp.w * 0.5f;
            float py = vp.y - sz.y - tick_padding;
            // Clamp title to stay within or just at the top of the viewport
            if (py < vp.y + 2.0f)
                py = vp.y + 2.0f;
            dl->AddText(ImVec2(cx - sz.x * 0.5f, py), title_col, txt);
            ImGui::PopFont();
        }
    }

    // Legend is drawn by LegendInteraction (in data_interaction.cpp) which
    // supports click-to-toggle visibility and drag-to-reposition.
}

// ─── Timeline Panel ──────────────────────────────────────────────────────────

// Helper: transport icon button with modern styling
static bool transport_button(const char* icon_label,
                             bool active,
                             bool accent,
                             ImFont* font,
                             float size,
                             const ui::ThemeColors& colors)
{
    ImGui::PushFont(font);

    ImVec4 bg, bg_hover, bg_active, text_col;
    if (accent)
    {
        bg = ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 0.9f);
        bg_hover = ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f);
        bg_active =
            ImVec4(colors.accent.r * 0.8f, colors.accent.g * 0.8f, colors.accent.b * 0.8f, 1.0f);
        text_col = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    }
    else if (active)
    {
        bg = ImVec4(colors.accent_muted.r, colors.accent_muted.g, colors.accent_muted.b, 0.35f);
        bg_hover =
            ImVec4(colors.accent_subtle.r, colors.accent_subtle.g, colors.accent_subtle.b, 0.5f);
        bg_active =
            ImVec4(colors.accent_muted.r, colors.accent_muted.g, colors.accent_muted.b, 0.6f);
        text_col = ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f);
    }
    else
    {
        bg = ImVec4(0, 0, 0, 0);
        bg_hover =
            ImVec4(colors.text_secondary.r, colors.text_secondary.g, colors.text_secondary.b, 0.1f);
        bg_active =
            ImVec4(colors.text_secondary.r, colors.text_secondary.g, colors.text_secondary.b, 0.2f);
        text_col = ImVec4(
            colors.text_secondary.r, colors.text_secondary.g, colors.text_secondary.b, 0.85f);
    }

    ImGui::PushStyleColor(ImGuiCol_Button, bg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bg_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, bg_active);
    ImGui::PushStyleColor(ImGuiCol_Text, text_col);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::tokens::RADIUS_MD);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

    bool clicked = ImGui::Button(icon_label, ImVec2(size, size));

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(4);
    ImGui::PopFont();
    return clicked;
}

void ImGuiIntegration::draw_timeline_panel()
{
    if (!timeline_editor_)
        return;

    const auto& colors = ui::theme();
    ImGuiIO& io = ImGui::GetIO();

    float panel_height = layout_manager_ ? layout_manager_->bottom_panel_height() : 200.0f;
    if (panel_height < 1.0f)
        return;  // Animating closed

    float status_bar_h = LayoutManager::STATUS_BAR_HEIGHT;
    float panel_y = io.DisplaySize.y - status_bar_h - panel_height;
    float nav_w = layout_manager_ ? layout_manager_->nav_rail_animated_width() : 48.0f;
    float inspector_w = (layout_manager_ && layout_manager_->is_inspector_visible())
                            ? layout_manager_->inspector_animated_width()
                            : 0.0f;
    float panel_x = nav_w;
    float panel_w = io.DisplaySize.x - nav_w - inspector_w;

    // Draw top-border accent line via background draw list
    ImDrawList* bg_dl = ImGui::GetBackgroundDrawList();
    ImU32 accent_col = IM_COL32(static_cast<int>(colors.accent.r * 255),
                                static_cast<int>(colors.accent.g * 255),
                                static_cast<int>(colors.accent.b * 255),
                                180);
    bg_dl->AddRectFilled(
        ImVec2(panel_x, panel_y - 1.0f), ImVec2(panel_x + panel_w, panel_y + 1.0f), accent_col);

    ImGui::SetNextWindowPos(ImVec2(panel_x, panel_y));
    ImGui::SetNextWindowSize(ImVec2(panel_w, panel_height));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
                             | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar
                             | ImGuiWindowFlags_NoScrollbar
                             | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleColor(
        ImGuiCol_WindowBg,
        ImVec4(colors.bg_secondary.r, colors.bg_secondary.g, colors.bg_secondary.b, 0.98f));
    ImGui::PushStyleColor(
        ImGuiCol_Border,
        ImVec4(colors.border_default.r, colors.border_default.g, colors.border_default.b, 0.3f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (ImGui::Begin("##timeline_panel", nullptr, flags))
    {
        float btn_sz = 28.0f;
        float btn_gap = 4.0f;

        // ── Header row: transport controls (left) + time display (right) ──
        auto pb_state = timeline_editor_->playback_state();
        bool is_playing = (pb_state == PlaybackState::Playing);
        bool is_paused = (pb_state == PlaybackState::Paused);

        // Step backward
        if (font_icon_)
        {
            if (transport_button(
                    ui::icon_str(ui::Icon::StepBackward), false, false, font_icon_, btn_sz, colors))
            {
                timeline_editor_->step_backward();
            }
            ImGui::SameLine(0, btn_gap);

            // Stop
            if (transport_button(
                    ui::icon_str(ui::Icon::Stop), false, false, font_icon_, btn_sz, colors))
            {
                timeline_editor_->stop();
            }
            ImGui::SameLine(0, btn_gap);

            // Play/Pause — accent filled when playing
            const char* play_icon =
                is_playing ? ui::icon_str(ui::Icon::Pause) : ui::icon_str(ui::Icon::Play);
            if (transport_button(play_icon, is_paused, is_playing, font_icon_, btn_sz, colors))
            {
                timeline_editor_->toggle_play();
            }
            ImGui::SameLine(0, btn_gap);

            // Step forward
            if (transport_button(
                    ui::icon_str(ui::Icon::StepForward), false, false, font_icon_, btn_sz, colors))
            {
                timeline_editor_->step_forward();
            }
        }

        // Time display — right-aligned
        {
            char time_buf[64];
            snprintf(time_buf,
                     sizeof(time_buf),
                     "%.2f / %.2f",
                     timeline_editor_->playhead(),
                     timeline_editor_->duration());
            float time_w = ImGui::CalcTextSize(time_buf).x;
            float avail_w = ImGui::GetContentRegionAvail().x;
            ImGui::SameLine(0, 0);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail_w - time_w - 8.0f);

            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(colors.text_secondary.r,
                                         colors.text_secondary.g,
                                         colors.text_secondary.b,
                                         0.6f));
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", time_buf);
            ImGui::PopStyleColor();
        }

        // Subtle separator
        ImGui::Spacing();
        {
            ImVec2 p = ImGui::GetCursorScreenPos();
            float w = ImGui::GetContentRegionAvail().x;
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(p.x, p.y),
                ImVec2(p.x + w, p.y),
                IM_COL32(static_cast<int>(colors.border_subtle.r * 255),
                         static_cast<int>(colors.border_subtle.g * 255),
                         static_cast<int>(colors.border_subtle.b * 255),
                         40));
            ImGui::Dummy(ImVec2(0, 1));
        }

        // Draw the timeline editor's ImGui content
        float remaining_h = ImGui::GetContentRegionAvail().y;
        timeline_editor_->draw(panel_w - 32, remaining_h);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

// ─── Curve Editor Panel ──────────────────────────────────────────────────────

void ImGuiIntegration::draw_curve_editor_panel()
{
    if (!curve_editor_)
        return;

    const auto& colors = ui::theme();
    ImGuiIO& io = ImGui::GetIO();

    float win_w = 560.0f;
    float win_h = 380.0f;
    float center_x = io.DisplaySize.x * 0.5f - win_w * 0.5f;
    float center_y = io.DisplaySize.y * 0.4f - win_h * 0.5f;

    ImGui::SetNextWindowPos(ImVec2(center_x, center_y), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(win_w, win_h), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(400, 280),
                                        ImVec2(io.DisplaySize.x * 0.8f, io.DisplaySize.y * 0.8f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;

    ImGui::PushStyleColor(
        ImGuiCol_WindowBg,
        ImVec4(colors.bg_secondary.r, colors.bg_secondary.g, colors.bg_secondary.b, 0.98f));
    ImGui::PushStyleColor(
        ImGuiCol_TitleBg,
        ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive,
                          ImVec4(colors.accent.r * 0.15f + colors.bg_tertiary.r * 0.85f,
                                 colors.accent.g * 0.15f + colors.bg_tertiary.g * 0.85f,
                                 colors.accent.b * 0.15f + colors.bg_tertiary.b * 0.85f,
                                 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::tokens::RADIUS_LG);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

    bool still_open = show_curve_editor_;
    if (ImGui::Begin("Curve Editor", &still_open, flags))
    {
        float btn_sz = 24.0f;
        float btn_gap = 4.0f;

        // Toolbar row with icon buttons
        if (font_icon_)
        {
            if (transport_button(
                    ui::icon_str(ui::Icon::Fullscreen), false, false, font_icon_, btn_sz, colors))
            {
                curve_editor_->fit_view();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Fit View");
            ImGui::SameLine(0, btn_gap);

            if (transport_button(
                    ui::icon_str(ui::Icon::Home), false, false, font_icon_, btn_sz, colors))
            {
                curve_editor_->reset_view();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Reset View");
            ImGui::SameLine(0, 16.0f);
        }

        // Toggle buttons with modern pill style
        bool show_grid = curve_editor_->show_grid();
        bool show_tangents = curve_editor_->show_tangents();

        auto toggle_pill = [&](const char* label, bool* value)
        {
            ImVec4 bg = *value ? ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 0.15f)
                               : ImVec4(0, 0, 0, 0);
            ImVec4 text = *value ? ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f)
                                 : ImVec4(colors.text_secondary.r,
                                          colors.text_secondary.g,
                                          colors.text_secondary.b,
                                          0.7f);

            ImGui::PushStyleColor(ImGuiCol_Button, bg);
            ImGui::PushStyleColor(
                ImGuiCol_ButtonHovered,
                ImVec4(
                    colors.accent_subtle.r, colors.accent_subtle.g, colors.accent_subtle.b, 0.3f));
            ImGui::PushStyleColor(
                ImGuiCol_ButtonActive,
                ImVec4(colors.accent_muted.r, colors.accent_muted.g, colors.accent_muted.b, 0.4f));
            ImGui::PushStyleColor(ImGuiCol_Text, text);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 3));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, *value ? 0.0f : 1.0f);
            if (!*value)
            {
                ImGui::PushStyleColor(ImGuiCol_Border,
                                      ImVec4(colors.border_subtle.r,
                                             colors.border_subtle.g,
                                             colors.border_subtle.b,
                                             0.3f));
            }

            if (ImGui::Button(label))
            {
                *value = !*value;
            }

            if (!*value)
                ImGui::PopStyleColor();
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(4);
        };

        toggle_pill("Grid", &show_grid);
        ImGui::SameLine(0, btn_gap);
        toggle_pill("Tangents", &show_tangents);

        curve_editor_->set_show_grid(show_grid);
        curve_editor_->set_show_tangents(show_tangents);

        // Subtle separator
        ImGui::Spacing();
        {
            ImVec2 p = ImGui::GetCursorScreenPos();
            float w = ImGui::GetContentRegionAvail().x;
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(p.x, p.y),
                ImVec2(p.x + w, p.y),
                IM_COL32(static_cast<int>(colors.border_subtle.r * 255),
                         static_cast<int>(colors.border_subtle.g * 255),
                         static_cast<int>(colors.border_subtle.b * 255),
                         40));
            ImGui::Dummy(ImVec2(0, 1));
        }

        // Sync playhead from timeline
        if (timeline_editor_)
        {
            curve_editor_->set_playhead_time(timeline_editor_->playhead());
        }

        // Draw the curve editor
        ImVec2 avail = ImGui::GetContentRegionAvail();
        curve_editor_->draw(avail.x, avail.y);
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);

    show_curve_editor_ = still_open;
}

void ImGuiIntegration::select_series(Figure* fig, Axes* ax, int ax_idx, Series* s, int s_idx)
{
    selection_ctx_.select_series(fig, ax, ax_idx, s, s_idx);
    // Ensure inspector is visible so the user can edit the selected series
    if (layout_manager_ && !layout_manager_->is_inspector_visible())
    {
        layout_manager_->set_inspector_visible(true);
    }
    SPECTRA_LOG_INFO("ui", "Series selected from canvas: " + s->label());
}

void ImGuiIntegration::draw_theme_settings()
{
    const auto& colors = ui::theme();
    auto& theme_manager = ui::ThemeManager::instance();

    // Center the modal window
    ImGuiIO& io = ImGui::GetIO();
    float window_width = 360.0f;
    float window_height = 320.0f;
    float wx = (io.DisplaySize.x - window_width) * 0.5f;
    float wy = (io.DisplaySize.y - window_height) * 0.5f;
    ImGui::SetNextWindowPos(ImVec2(wx, wy), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(window_width, window_height));

    static std::vector<std::string> available_themes = {"dark", "light", "high_contrast"};

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse;

    // Draw shadow behind dialog
    ImDrawList* bg_dl = ImGui::GetBackgroundDrawList();
    bg_dl->AddRectFilled(ImVec2(wx - 4, wy - 2),
                         ImVec2(wx + window_width + 4, wy + window_height + 10),
                         IM_COL32(0, 0, 0, 35),
                         ui::tokens::RADIUS_LG + 6);

    // Modern modal styling
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::tokens::RADIUS_LG);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.5f);
    ImGui::PushStyleColor(
        ImGuiCol_WindowBg,
        ImVec4(colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b, 0.98f));
    ImGui::PushStyleColor(
        ImGuiCol_Border,
        ImVec4(colors.border_subtle.r, colors.border_subtle.g, colors.border_subtle.b, 0.3f));
    ImGui::PushStyleColor(
        ImGuiCol_TitleBg,
        ImVec4(colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b, 1.0f));
    ImGui::PushStyleColor(
        ImGuiCol_TitleBgActive,
        ImVec4(colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b, 1.0f));

    bool is_open = true;
    if (ImGui::Begin("Theme Settings", &is_open, flags))
    {
        ImGui::PushFont(font_heading_);
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImVec4(colors.text_primary.r, colors.text_primary.g, colors.text_primary.b, 1.0f));
        ImGui::TextUnformatted("Appearance");
        ImGui::PopStyleColor();
        ImGui::PopFont();

        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PushStyleColor(
            ImGuiCol_Separator,
            ImVec4(colors.border_subtle.r, colors.border_subtle.g, colors.border_subtle.b, 0.3f));
        ImGui::Separator();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 8));

        // Theme selection buttons — card-like
        for (const auto& theme_name : available_themes)
        {
            bool is_current = (theme_manager.current_theme_name() == theme_name);

            if (is_current)
            {
                ImGui::PushStyleColor(ImGuiCol_Button,
                                      ImVec4(colors.accent_muted.r,
                                             colors.accent_muted.g,
                                             colors.accent_muted.b,
                                             0.35f));
                ImGui::PushStyleColor(
                    ImGuiCol_Text, ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f));
            }
            else
            {
                ImGui::PushStyleColor(
                    ImGuiCol_Button,
                    ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 0.6f));
                ImGui::PushStyleColor(
                    ImGuiCol_Text,
                    ImVec4(
                        colors.text_primary.r, colors.text_primary.g, colors.text_primary.b, 1.0f));
            }

            ImGui::PushStyleColor(
                ImGuiCol_ButtonHovered,
                ImVec4(
                    colors.accent_subtle.r, colors.accent_subtle.g, colors.accent_subtle.b, 0.5f));
            ImGui::PushStyleColor(
                ImGuiCol_ButtonActive,
                ImVec4(colors.accent_muted.r, colors.accent_muted.g, colors.accent_muted.b, 0.6f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::tokens::RADIUS_MD);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                                ImVec2(ui::tokens::SPACE_4, ui::tokens::SPACE_3));

            // Capitalize + prettify name
            std::string display_name = theme_name;
            if (!display_name.empty())
            {
                display_name[0] = static_cast<char>(std::toupper(display_name[0]));
                size_t pos = 0;
                while ((pos = display_name.find('_', pos)) != std::string::npos)
                {
                    display_name.replace(pos, 1, " ");
                    if (pos + 1 < display_name.length())
                        display_name[pos + 1] =
                            static_cast<char>(std::toupper(display_name[pos + 1]));
                    pos += 1;
                }
            }

            // Prepend checkmark for current theme
            std::string label =
                is_current ? std::string("  ") + display_name : std::string("    ") + display_name;

            if (ImGui::Button(label.c_str(), ImVec2(-1, 0)))
            {
                theme_manager.set_theme(theme_name);
            }

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(4);
            ImGui::Dummy(ImVec2(0, 2));
        }

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::PushStyleColor(
            ImGuiCol_Separator,
            ImVec4(colors.border_subtle.r, colors.border_subtle.g, colors.border_subtle.b, 0.3f));
        ImGui::Separator();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4));

        // Close button — right-aligned, pill-shaped
        float close_w = 90.0f;
        ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - close_w + ImGui::GetCursorPosX());
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::tokens::RADIUS_PILL);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20, 6));
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 0.5f));
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            ImVec4(colors.accent_subtle.r, colors.accent_subtle.g, colors.accent_subtle.b, 0.5f));
        if (ImGui::Button("Close", ImVec2(close_w, 0)))
        {
            is_open = false;
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }

    ImGui::End();
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(3);

    if (!is_open)
    {
        show_theme_settings_ = false;
    }
}

// ─── Axes Right-Click Context Menu (Axis Linking) ────────────────────────────

void ImGuiIntegration::draw_axes_context_menu(Figure& figure)
{
    if (!input_handler_ || !axis_link_mgr_)
        return;

    // Detect right-click on canvas (not captured by ImGui)
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !io.WantCaptureMouse)
    {
        // Hit-test which axes was right-clicked
        Axes* hit = input_handler_->hit_test_axes(static_cast<double>(io.MousePos.x),
                                                  static_cast<double>(io.MousePos.y));
        if (hit)
        {
            context_menu_axes_ = hit;
            ImGui::OpenPopup("##axes_context_menu");
        }
    }

    const auto& colors = ui::theme();

    // Popup styling
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, ui::tokens::RADIUS_LG);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 4));
    ImGui::PushStyleColor(
        ImGuiCol_PopupBg,
        ImVec4(colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b, 0.97f));
    ImGui::PushStyleColor(
        ImGuiCol_Border,
        ImVec4(colors.border_subtle.r, colors.border_subtle.g, colors.border_subtle.b, 0.4f));

    if (ImGui::BeginPopup("##axes_context_menu"))
    {
        Axes* ax = context_menu_axes_;
        if (!ax)
        {
            ImGui::EndPopup();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(4);
            return;
        }

        // Find axes index for display
        int axes_idx = -1;
        for (size_t i = 0; i < figure.axes().size(); ++i)
        {
            if (figure.axes()[i].get() == ax)
            {
                axes_idx = static_cast<int>(i);
                break;
            }
        }
        std::string axes_label =
            (axes_idx >= 0) ? "Subplot " + std::to_string(axes_idx + 1) : "Axes";

        // Header
        ImGui::PushFont(font_heading_);
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImVec4(
                colors.text_secondary.r, colors.text_secondary.g, colors.text_secondary.b, 0.7f));
        ImGui::TextUnformatted(axes_label.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 2));
        ImGui::PushStyleColor(
            ImGuiCol_Separator,
            ImVec4(colors.border_subtle.r, colors.border_subtle.g, colors.border_subtle.b, 0.3f));
        ImGui::Separator();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 2));

        // Style for menu items
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(
            ImGuiCol_HeaderHovered,
            ImVec4(colors.accent_subtle.r, colors.accent_subtle.g, colors.accent_subtle.b, 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));

        bool is_linked = axis_link_mgr_->is_linked(ax);
        bool has_multi = figure.axes().size() > 1;

        if (has_multi)
        {
            // "Link X to all" — link this axes' X to all other axes
            std::string link_x_label =
                std::string(reinterpret_cast<const char*>(u8"\xEE\x80\xBD")) + "  Link X-Axis";
            if (ImGui::Selectable(link_x_label.c_str(), false, 0, ImVec2(200, 24)))
            {
                for (auto& other : figure.axes_mut())
                {
                    if (other && other.get() != ax)
                    {
                        axis_link_mgr_->link(ax, other.get(), LinkAxis::X);
                    }
                }
                SPECTRA_LOG_INFO("axes_link",
                                 "Linked X-axis of subplot " + std::to_string(axes_idx + 1));
            }

            // "Link Y to all"
            std::string link_y_label =
                std::string(reinterpret_cast<const char*>(u8"\xEE\x80\xBD")) + "  Link Y-Axis";
            if (ImGui::Selectable(link_y_label.c_str(), false, 0, ImVec2(200, 24)))
            {
                for (auto& other : figure.axes_mut())
                {
                    if (other && other.get() != ax)
                    {
                        axis_link_mgr_->link(ax, other.get(), LinkAxis::Y);
                    }
                }
                SPECTRA_LOG_INFO("axes_link",
                                 "Linked Y-axis of subplot " + std::to_string(axes_idx + 1));
            }

            // "Link Both to all"
            std::string link_both_label =
                std::string(reinterpret_cast<const char*>(u8"\xEE\x80\xBD")) + "  Link Both Axes";
            if (ImGui::Selectable(link_both_label.c_str(), false, 0, ImVec2(200, 24)))
            {
                for (auto& other : figure.axes_mut())
                {
                    if (other && other.get() != ax)
                    {
                        axis_link_mgr_->link(ax, other.get(), LinkAxis::Both);
                    }
                }
                SPECTRA_LOG_INFO("axes_link",
                                 "Linked both axes of subplot " + std::to_string(axes_idx + 1));
            }
        }

        if (is_linked)
        {
            if (has_multi)
            {
                ImGui::Dummy(ImVec2(0, 2));
                ImGui::PushStyleColor(ImGuiCol_Separator,
                                      ImVec4(colors.border_subtle.r,
                                             colors.border_subtle.g,
                                             colors.border_subtle.b,
                                             0.3f));
                ImGui::Separator();
                ImGui::PopStyleColor();
                ImGui::Dummy(ImVec2(0, 2));
            }

            // Show which groups this axes belongs to
            auto group_ids = axis_link_mgr_->groups_for(ax);
            for (auto gid : group_ids)
            {
                const auto* grp = axis_link_mgr_->group(gid);
                if (!grp)
                    continue;
                std::string axis_str = (grp->axis == LinkAxis::X)   ? "X"
                                       : (grp->axis == LinkAxis::Y) ? "Y"
                                                                    : "XY";
                ImU32 grp_col = IM_COL32(static_cast<uint8_t>(grp->color.r * 255),
                                         static_cast<uint8_t>(grp->color.g * 255),
                                         static_cast<uint8_t>(grp->color.b * 255),
                                         255);

                // Colored dot + group info
                ImVec2 cursor = ImGui::GetCursorScreenPos();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddCircleFilled(ImVec2(cursor.x + 8, cursor.y + 10), 5.0f, grp_col);
                ImGui::Dummy(ImVec2(20, 0));
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImVec4(colors.text_secondary.r,
                                             colors.text_secondary.g,
                                             colors.text_secondary.b,
                                             0.8f));
                ImGui::Text(
                    "%s (%s, %zu axes)", grp->name.c_str(), axis_str.c_str(), grp->members.size());
                ImGui::PopStyleColor();
            }

            ImGui::Dummy(ImVec2(0, 2));
            ImGui::PushStyleColor(
                ImGuiCol_Separator,
                ImVec4(
                    colors.border_subtle.r, colors.border_subtle.g, colors.border_subtle.b, 0.3f));
            ImGui::Separator();
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, 2));

            // "Unlink this axes"
            std::string unlink_label = std::string(reinterpret_cast<const char*>(u8"\xEE\x80\xBE"))
                                       + "  Unlink This Subplot";
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.35f, 0.35f, 1.0f));
            if (ImGui::Selectable(unlink_label.c_str(), false, 0, ImVec2(200, 24)))
            {
                axis_link_mgr_->unlink(ax);
                SPECTRA_LOG_INFO("axes_link", "Unlinked subplot " + std::to_string(axes_idx + 1));
            }
            ImGui::PopStyleColor();

            // "Unlink All"
            std::string unlink_all_label =
                std::string(reinterpret_cast<const char*>(u8"\xEE\x80\xBE")) + "  Unlink All";
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.35f, 0.35f, 1.0f));
            if (ImGui::Selectable(unlink_all_label.c_str(), false, 0, ImVec2(200, 24)))
            {
                auto groups_copy = axis_link_mgr_->groups();
                for (auto& [id, grp] : groups_copy)
                {
                    axis_link_mgr_->remove_group(id);
                }
                axis_link_mgr_->clear_shared_cursor();
                SPECTRA_LOG_INFO("axes_link", "Unlinked all axes");
            }
            ImGui::PopStyleColor();
        }

        ImGui::PopStyleVar();     // SelectableTextAlign
        ImGui::PopStyleColor(2);  // Header, HeaderHovered

        ImGui::EndPopup();
    }

    ImGui::PopStyleColor(2);  // PopupBg, Border
    ImGui::PopStyleVar(4);    // WindowPadding, PopupRounding, PopupBorderSize, ItemSpacing
}

// ─── Axis Link Indicators (colored chain icon on linked axes) ────────────────

void ImGuiIntegration::draw_axis_link_indicators(Figure& figure)
{
    if (!axis_link_mgr_ || axis_link_mgr_->group_count() == 0)
        return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();

    for (auto& axes_ptr : figure.axes())
    {
        if (!axes_ptr)
            continue;
        Axes* ax = axes_ptr.get();
        if (!axis_link_mgr_->is_linked(ax))
            continue;

        const auto& vp = ax->viewport();
        auto group_ids = axis_link_mgr_->groups_for(ax);
        if (group_ids.empty())
            continue;

        // Draw one indicator per group in the top-right corner of the axes
        float icon_x = vp.x + vp.w - 8.0f;
        float icon_y = vp.y + 8.0f;

        for (size_t gi = 0; gi < group_ids.size(); ++gi)
        {
            const auto* grp = axis_link_mgr_->group(group_ids[gi]);
            if (!grp)
                continue;

            ImU32 col = IM_COL32(static_cast<uint8_t>(grp->color.r * 255),
                                 static_cast<uint8_t>(grp->color.g * 255),
                                 static_cast<uint8_t>(grp->color.b * 255),
                                 200);
            ImU32 bg_col = IM_COL32(0, 0, 0, 100);

            float cx = icon_x - gi * 22.0f;
            float cy = icon_y;

            // Background pill
            dl->AddRectFilled(ImVec2(cx - 10, cy - 8), ImVec2(cx + 10, cy + 8), bg_col, 6.0f);

            // Chain link icon (two interlocking circles)
            dl->AddCircle(ImVec2(cx - 2.5f, cy), 4.5f, col, 0, 1.8f);
            dl->AddCircle(ImVec2(cx + 2.5f, cy), 4.5f, col, 0, 1.8f);

            // Axis label below
            std::string axis_str = (grp->axis == LinkAxis::X)   ? "X"
                                   : (grp->axis == LinkAxis::Y) ? "Y"
                                                                : "XY";
            ImVec2 sz = ImGui::CalcTextSize(axis_str.c_str());
            dl->AddText(ImVec2(cx - sz.x * 0.5f, cy + 10), col, axis_str.c_str());
        }
    }
}

// ── Tearoff preview card rendering (for borderless preview window) ───────────

void ImGuiIntegration::build_preview_ui(const std::string& title, Figure* figure)
{
    const auto& theme = ui::ThemeManager::instance().colors();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImVec2 disp = ImGui::GetIO().DisplaySize;

    float w = disp.x;
    float h = disp.y;

    constexpr float RADIUS = 10.0f;
    constexpr float TB_H = 28.0f;
    constexpr float PAD = 8.0f;

    // Card background (fills entire window)
    dl->AddRectFilled(ImVec2(0, 0),
                      ImVec2(w, h),
                      IM_COL32(static_cast<uint8_t>(theme.bg_primary.r * 255),
                               static_cast<uint8_t>(theme.bg_primary.g * 255),
                               static_cast<uint8_t>(theme.bg_primary.b * 255),
                               255),
                      RADIUS);

    // Border
    dl->AddRect(ImVec2(0, 0),
                ImVec2(w, h),
                IM_COL32(static_cast<uint8_t>(theme.accent.r * 255),
                         static_cast<uint8_t>(theme.accent.g * 255),
                         static_cast<uint8_t>(theme.accent.b * 255),
                         180),
                RADIUS,
                0,
                2.0f);

    // Title bar
    dl->AddRectFilled(ImVec2(1, 1),
                      ImVec2(w - 1, TB_H),
                      IM_COL32(static_cast<uint8_t>(theme.bg_tertiary.r * 255),
                               static_cast<uint8_t>(theme.bg_tertiary.g * 255),
                               static_cast<uint8_t>(theme.bg_tertiary.b * 255),
                               255),
                      RADIUS,
                      ImDrawFlags_RoundCornersTop);

    // Title text centered
    ImVec2 tsz = ImGui::CalcTextSize(title.c_str());
    dl->AddText(ImVec2((w - tsz.x) * 0.5f, (TB_H - tsz.y) * 0.5f),
                IM_COL32(static_cast<uint8_t>(theme.text_primary.r * 255),
                         static_cast<uint8_t>(theme.text_primary.g * 255),
                         static_cast<uint8_t>(theme.text_primary.b * 255),
                         255),
                title.c_str());

    // Separator line below title bar
    dl->AddLine(ImVec2(1, TB_H),
                ImVec2(w - 1, TB_H),
                IM_COL32(static_cast<uint8_t>(theme.border_subtle.r * 255),
                         static_cast<uint8_t>(theme.border_subtle.g * 255),
                         static_cast<uint8_t>(theme.border_subtle.b * 255),
                         200),
                1.0f);

    // Plot area
    float px = PAD;
    float py = TB_H + PAD * 0.5f;
    float pw = w - PAD * 2.0f;
    float ph = h - TB_H - PAD * 1.5f;

    if (pw <= 10.0f || ph <= 10.0f)
        return;

    // Plot background
    dl->AddRectFilled(ImVec2(px, py),
                      ImVec2(px + pw, py + ph),
                      IM_COL32(static_cast<uint8_t>(theme.bg_secondary.r * 255),
                               static_cast<uint8_t>(theme.bg_secondary.g * 255),
                               static_cast<uint8_t>(theme.bg_secondary.b * 255),
                               200),
                      4.0f);

    // Grid lines
    uint8_t ga = 30;
    for (int gi = 1; gi < 4; ++gi)
    {
        float gy = py + ph * (static_cast<float>(gi) / 4.0f);
        dl->AddLine(ImVec2(px, gy), ImVec2(px + pw, gy), IM_COL32(128, 128, 128, ga), 1.0f);
    }
    for (int gi = 1; gi < 5; ++gi)
    {
        float gx = px + pw * (static_cast<float>(gi) / 5.0f);
        dl->AddLine(ImVec2(gx, py), ImVec2(gx, py + ph), IM_COL32(128, 128, 128, ga), 1.0f);
    }

    // Render actual figure data if available
    bool drew_real_data = false;
    if (figure && !figure->axes().empty())
    {
        // Use the first (active) axes
        const auto& ax = *figure->axes()[0];
        AxisLimits xl = ax.x_limits();
        AxisLimits yl = ax.y_limits();
        float x_range = xl.max - xl.min;
        float y_range = yl.max - yl.min;
        if (x_range <= 0.0f)
            x_range = 1.0f;
        if (y_range <= 0.0f)
            y_range = 1.0f;

        // Clip to plot area
        dl->PushClipRect(ImVec2(px, py), ImVec2(px + pw, py + ph), true);

        for (const auto& s : ax.series())
        {
            if (!s || !s->visible())
                continue;

            const Color& sc = s->color();
            ImU32 col = IM_COL32(static_cast<uint8_t>(sc.r * 255),
                                 static_cast<uint8_t>(sc.g * 255),
                                 static_cast<uint8_t>(sc.b * 255),
                                 static_cast<uint8_t>(sc.a * s->opacity() * 220));

            // Try LineSeries
            auto* ls = dynamic_cast<const LineSeries*>(s.get());
            if (ls && ls->point_count() >= 2)
            {
                drew_real_data = true;
                auto xd = ls->x_data();
                auto yd = ls->y_data();
                size_t n = ls->point_count();

                // Downsample to fit preview width (max ~200 segments)
                size_t step = std::max<size_t>(1, n / 200);

                for (size_t i = 0; i + step < n; i += step)
                {
                    size_t j = std::min(i + step, n - 1);
                    float sx0 = px + ((xd[i] - xl.min) / x_range) * pw;
                    float sy0 = py + ph - ((yd[i] - yl.min) / y_range) * ph;
                    float sx1 = px + ((xd[j] - xl.min) / x_range) * pw;
                    float sy1 = py + ph - ((yd[j] - yl.min) / y_range) * ph;
                    dl->AddLine(ImVec2(sx0, sy0), ImVec2(sx1, sy1), col, 1.5f);
                }
                continue;
            }

            // Try ScatterSeries
            auto* ss = dynamic_cast<const ScatterSeries*>(s.get());
            if (ss && ss->point_count() >= 1)
            {
                drew_real_data = true;
                auto xd = ss->x_data();
                auto yd = ss->y_data();
                size_t n = ss->point_count();

                // Downsample for preview
                size_t step = std::max<size_t>(1, n / 150);
                float r = std::max(1.5f, std::min(3.0f, pw / 100.0f));

                for (size_t i = 0; i < n; i += step)
                {
                    float sx = px + ((xd[i] - xl.min) / x_range) * pw;
                    float sy = py + ph - ((yd[i] - yl.min) / y_range) * ph;
                    dl->AddCircleFilled(ImVec2(sx, sy), r, col);
                }
            }
        }

        dl->PopClipRect();
    }

    // Fallback: generic sine wave if no real data
    if (!drew_real_data)
    {
        uint8_t ar = static_cast<uint8_t>(theme.accent.r * 255);
        uint8_t ag = static_cast<uint8_t>(theme.accent.g * 255);
        uint8_t ab = static_cast<uint8_t>(theme.accent.b * 255);
        ImU32 wave_col = IM_COL32(ar, ag, ab, 200);
        constexpr int SEGMENTS = 40;
        for (int si = 0; si < SEGMENTS; ++si)
        {
            float t0 = static_cast<float>(si) / SEGMENTS;
            float t1 = static_cast<float>(si + 1) / SEGMENTS;
            float y0 = py + ph * 0.5f - std::sin(t0 * 6.28f) * ph * 0.3f;
            float y1 = py + ph * 0.5f - std::sin(t1 * 6.28f) * ph * 0.3f;
            dl->AddLine(ImVec2(px + t0 * pw, y0), ImVec2(px + t1 * pw, y1), wave_col, 2.0f);
        }
    }
}

// ─── Knobs Panel ────────────────────────────────────────────────────────────
// Draws an overlay panel on the canvas with interactive parameter controls.
// Positioned at top-right of the canvas area, semi-transparent background.

void ImGuiIntegration::draw_knobs_panel()
{
    if (!knob_manager_ || knob_manager_->empty())
        return;
    if (!knob_manager_->is_visible())
        return;

    auto& theme = ui::theme();
    auto& knobs = knob_manager_->knobs();

    // Initial position: top-right of canvas with padding (user can drag it anywhere).
    float canvas_x = layout_manager_ ? layout_manager_->canvas_rect().x : 0.0f;
    float canvas_y = layout_manager_ ? layout_manager_->canvas_rect().y : 0.0f;
    float canvas_w =
        layout_manager_ ? layout_manager_->canvas_rect().w : ImGui::GetIO().DisplaySize.x;

    float panel_w = 260.0f;
    float pad = 12.0f;
    float pos_x = canvas_x + canvas_w - panel_w - pad;
    float pos_y = canvas_y + pad;

    ImGui::SetNextWindowPos(ImVec2(pos_x, pos_y), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(panel_w, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.92f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize
                             | ImGuiWindowFlags_NoSavedSettings
                             | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::tokens::RADIUS_LG);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 10));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign, ImVec2(0.0f, 0.5f));
    ImGui::PushStyleColor(
        ImGuiCol_WindowBg,
        ImVec4(theme.bg_elevated.r, theme.bg_elevated.g, theme.bg_elevated.b, 0.92f));
    ImGui::PushStyleColor(
        ImGuiCol_Border,
        ImVec4(theme.border_subtle.r, theme.border_subtle.g, theme.border_subtle.b, 0.4f));
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImVec4(theme.text_primary.r,
                                 theme.text_primary.g,
                                 theme.text_primary.b,
                                 theme.text_primary.a));
    ImGui::PushStyleColor(
        ImGuiCol_TitleBg,
        ImVec4(theme.bg_tertiary.r, theme.bg_tertiary.g, theme.bg_tertiary.b, 0.95f));
    ImGui::PushStyleColor(
        ImGuiCol_TitleBgActive,
        ImVec4(theme.accent.r * 0.3f, theme.accent.g * 0.3f, theme.accent.b * 0.3f, 0.95f));
    ImGui::PushStyleColor(
        ImGuiCol_TitleBgCollapsed,
        ImVec4(theme.bg_tertiary.r, theme.bg_tertiary.g, theme.bg_tertiary.b, 0.7f));

    bool collapsed = knob_manager_->is_collapsed();
    ImGui::SetNextWindowCollapsed(collapsed, ImGuiCond_Once);

    if (!ImGui::Begin(" Parameters", nullptr, flags | ImGuiWindowFlags_NoScrollbar))
    {
        // Window is collapsed — record rect (title bar only) and sync state
        ImVec2 wpos = ImGui::GetWindowPos();
        ImVec2 wsz = ImGui::GetWindowSize();
        knobs_panel_rect_ = {wpos.x, wpos.y, wsz.x, wsz.y};
        bool now_collapsed = ImGui::IsWindowCollapsed();
        if (now_collapsed != collapsed)
            knob_manager_->set_collapsed(now_collapsed);
        ImGui::End();
        ImGui::PopStyleColor(6);
        ImGui::PopStyleVar(4);
        return;
    }

    // Record full panel rect for tab-bar occlusion check
    {
        ImVec2 wpos = ImGui::GetWindowPos();
        ImVec2 wsz = ImGui::GetWindowSize();
        knobs_panel_rect_ = {wpos.x, wpos.y, wsz.x, wsz.y};
    }

    // Sync collapse state (user may have clicked the collapse arrow)
    {
        bool now_collapsed = ImGui::IsWindowCollapsed();
        if (now_collapsed != collapsed)
            knob_manager_->set_collapsed(now_collapsed);
        collapsed = now_collapsed;
    }

    if (!collapsed)
    {
        bool any_changed = false;

        // Accent color for sliders
        ImGui::PushStyleColor(ImGuiCol_SliderGrab,
                              ImVec4(theme.accent.r, theme.accent.g, theme.accent.b, 1.0f));
        ImGui::PushStyleColor(
            ImGuiCol_SliderGrabActive,
            ImVec4(theme.accent.r * 0.85f, theme.accent.g * 0.85f, theme.accent.b * 0.85f, 1.0f));
        ImGui::PushStyleColor(
            ImGuiCol_FrameBg,
            ImVec4(theme.bg_tertiary.r, theme.bg_tertiary.g, theme.bg_tertiary.b, 0.6f));
        ImGui::PushStyleColor(
            ImGuiCol_FrameBgHovered,
            ImVec4(theme.bg_tertiary.r, theme.bg_tertiary.g, theme.bg_tertiary.b, 0.8f));
        ImGui::PushStyleColor(
            ImGuiCol_FrameBgActive,
            ImVec4(theme.bg_tertiary.r, theme.bg_tertiary.g, theme.bg_tertiary.b, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_CheckMark,
                              ImVec4(theme.accent.r, theme.accent.g, theme.accent.b, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::tokens::RADIUS_SM);
        ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, ui::tokens::RADIUS_SM);

        float label_w = 0.0f;
        for (auto& k : knobs)
        {
            float tw = ImGui::CalcTextSize(k.name.c_str()).x;
            if (tw > label_w)
                label_w = tw;
        }
        label_w = std::min(label_w + 8.0f, panel_w * 0.4f);

        for (size_t i = 0; i < knobs.size(); ++i)
        {
            auto& k = knobs[i];
            ImGui::PushID(static_cast<int>(i));

            // Label
            ImGui::TextColored(
                ImVec4(theme.text_primary.r, theme.text_primary.g, theme.text_primary.b, 0.9f),
                "%s",
                k.name.c_str());

            // Control on same line or next line depending on type
            float avail = ImGui::GetContentRegionAvail().x;

            switch (k.type)
            {
                case KnobType::Float:
                {
                    ImGui::SetNextItemWidth(avail);
                    float old_val = k.value;
                    if (k.step > 0.0f)
                    {
                        // Discrete stepping — use drag float
                        ImGui::DragFloat("##v", &k.value, k.step, k.min_val, k.max_val, "%.3f");
                    }
                    else
                    {
                        ImGui::SliderFloat("##v", &k.value, k.min_val, k.max_val, "%.3f");
                    }
                    k.value = std::clamp(k.value, k.min_val, k.max_val);
                    if (k.value != old_val)
                    {
                        if (k.on_change)
                            k.on_change(k.value);
                        knob_manager_->mark_dirty(k.name, k.value);
                        any_changed = true;
                    }
                    break;
                }
                case KnobType::Int:
                {
                    ImGui::SetNextItemWidth(avail);
                    int iv = k.int_value();
                    int old_iv = iv;
                    ImGui::SliderInt(
                        "##v", &iv, static_cast<int>(k.min_val), static_cast<int>(k.max_val));
                    k.value = static_cast<float>(iv);
                    if (iv != old_iv)
                    {
                        if (k.on_change)
                            k.on_change(k.value);
                        knob_manager_->mark_dirty(k.name, k.value);
                        any_changed = true;
                    }
                    break;
                }
                case KnobType::Bool:
                {
                    bool bv = k.bool_value();
                    bool old_bv = bv;
                    ImGui::Checkbox("##v", &bv);
                    k.value = bv ? 1.0f : 0.0f;
                    if (bv != old_bv)
                    {
                        if (k.on_change)
                            k.on_change(k.value);
                        knob_manager_->mark_dirty(k.name, k.value);
                        any_changed = true;
                    }
                    break;
                }
                case KnobType::Choice:
                {
                    ImGui::SetNextItemWidth(avail);
                    int ci = k.choice_index();
                    int old_ci = ci;
                    if (ImGui::BeginCombo("##v",
                                          (ci >= 0 && ci < static_cast<int>(k.choices.size()))
                                              ? k.choices[ci].c_str()
                                              : ""))
                    {
                        for (int j = 0; j < static_cast<int>(k.choices.size()); ++j)
                        {
                            bool selected = (j == ci);
                            if (ImGui::Selectable(k.choices[j].c_str(), selected))
                            {
                                ci = j;
                                k.value = static_cast<float>(j);
                            }
                            if (selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    if (ci != old_ci)
                    {
                        if (k.on_change)
                            k.on_change(k.value);
                        knob_manager_->mark_dirty(k.name, k.value);
                        any_changed = true;
                    }
                    break;
                }
            }

            // Small spacing between knobs
            if (i + 1 < knobs.size())
                ImGui::Spacing();

            ImGui::PopID();
        }

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(6);

        if (any_changed)
        {
            knob_manager_->notify_any_changed();
        }
    }

    ImGui::End();
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(4);
}

}  // namespace spectra

#endif  // SPECTRA_USE_IMGUI
