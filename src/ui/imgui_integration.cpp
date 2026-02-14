#ifdef PLOTIX_USE_IMGUI

#include "imgui_integration.hpp"
#include "box_zoom_overlay.hpp"
#include "command_palette.hpp"
#include "data_interaction.hpp"
#include "dock_system.hpp"
#include "theme.hpp"
#include "design_tokens.hpp"
#include "icons.hpp"
#include "widgets.hpp"

#include <plotix/axes.hpp>
#include <plotix/figure.hpp>
#include <plotix/series.hpp>
#include <plotix/logger.hpp>

#include "../render/vulkan/vk_backend.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

// Compressed Inter font data
#include "../../third_party/inter_font.hpp"

// Embedded icon font data (PUA codepoints U+E001-U+E062)
#include "../../third_party/icon_font_data.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace plotix {

// ─── Lifecycle ──────────────────────────────────────────────────────────────

ImGuiIntegration::~ImGuiIntegration() { shutdown(); }

bool ImGuiIntegration::init(VulkanBackend& backend, GLFWwindow* window) {
    if (initialized_) return true;
    if (!window) return false;

    layout_manager_ = std::make_unique<LayoutManager>();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    // Initialize theme system
    ui::ThemeManager::instance();
    
    // Initialize icon font system
    ui::IconFont::instance().initialize();

    load_fonts();
    apply_modern_style();

    // Wire inspector fonts
    inspector_.set_fonts(font_body_, font_heading_, font_title_);

    ImGui_ImplGlfw_InitForVulkan(window, true);

    ImGui_ImplVulkan_InitInfo ii {};
    ii.Instance       = backend.instance();
    ii.PhysicalDevice = backend.physical_device();
    ii.Device         = backend.device();
    ii.QueueFamily    = backend.graphics_queue_family();
    ii.Queue          = backend.graphics_queue();
    ii.DescriptorPool = backend.descriptor_pool();
    ii.MinImageCount  = backend.min_image_count();
    ii.ImageCount     = backend.image_count();
    ii.RenderPass     = backend.render_pass();
    ii.MSAASamples    = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&ii);
    ImGui_ImplVulkan_CreateFontsTexture();

    initialized_ = true;
    return true;
}

void ImGuiIntegration::shutdown() {
    if (!initialized_) return;
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    layout_manager_.reset();
    initialized_ = false;
}

void ImGuiIntegration::on_swapchain_recreated(VulkanBackend& backend) {
    if (!initialized_) return;
    ImGui_ImplVulkan_SetMinImageCount(backend.min_image_count());
}

void ImGuiIntegration::update_layout(float window_width, float window_height, float dt) {
    if (layout_manager_) {
        layout_manager_->update(window_width, window_height, dt);
    }
}

void ImGuiIntegration::new_frame() {
    if (!initialized_) return;
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    
    // Update layout with current window size and delta time
    ImGuiIO& io = ImGui::GetIO();
    update_layout(io.DisplaySize.x, io.DisplaySize.y, io.DeltaTime);
}

void ImGuiIntegration::build_ui(Figure& figure) {
    if (!initialized_) {
        PLOTIX_LOG_WARN("ui", "build_ui called but ImGui is not initialized");
        return;
    }
    
    PLOTIX_LOG_TRACE("ui", "Building UI for figure");

    float dt = ImGui::GetIO().DeltaTime;
    ui::ThemeManager::instance().update(dt);
    ui::widgets::update_section_animations(dt);
    float target = panel_open_ ? 1.0f : 0.0f;
    panel_anim_ += (target - panel_anim_) * std::min(1.0f, 10.0f * dt);
    if (std::abs(panel_anim_ - target) < 0.002f) panel_anim_ = target;

    // Draw all zones using layout manager
    draw_command_bar();
    draw_nav_rail();
    draw_canvas(figure);
    draw_plot_text(figure);
    if (layout_manager_->is_inspector_visible()) {
        draw_inspector(figure);
    }
    draw_status_bar();
    draw_pane_tab_headers();      // Must run before splitters so pane_tab_hovered_ is set
    draw_split_view_splitters();
#if PLOTIX_FLOATING_TOOLBAR
    draw_floating_toolbar();
#endif

    // Draw data interaction overlays (tooltip, crosshair, markers) on top of everything
    if (data_interaction_) {
        ImGuiIO& io = ImGui::GetIO();
        data_interaction_->draw_overlays(io.DisplaySize.x, io.DisplaySize.y);
    }

    // Draw box zoom overlay (Agent B Week 7) — on top of data overlays
    if (box_zoom_overlay_) {
        box_zoom_overlay_->update(dt);
        ImGuiIO& io = ImGui::GetIO();
        box_zoom_overlay_->draw(io.DisplaySize.x, io.DisplaySize.y);
    }
    
    // Draw theme settings window if open
    if (show_theme_settings_) {
        draw_theme_settings();
    }

    // Draw command palette overlay (Agent F) — must be last to render on top
    if (command_palette_) {
        ImGuiIO& io = ImGui::GetIO();
        command_palette_->draw(io.DisplaySize.x, io.DisplaySize.y);
    }
}

void ImGuiIntegration::render(VulkanBackend& backend) {
    if (!initialized_) return;
    ImGui::Render();
    auto* dd = ImGui::GetDrawData();
    if (dd) ImGui_ImplVulkan_RenderDrawData(dd, backend.current_command_buffer());
}

bool ImGuiIntegration::wants_capture_mouse() const {
    if (!initialized_) return false;
    
    bool wants_capture = ImGui::GetIO().WantCaptureMouse;
    bool any_window_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
    bool any_item_hovered = ImGui::IsAnyItemHovered();
    bool any_item_active = ImGui::IsAnyItemActive();
    
    PLOTIX_LOG_TRACE("input", "ImGui mouse capture state - wants_capture: " + std::string(wants_capture ? "true" : "false") + 
                      ", window_hovered: " + std::string(any_window_hovered ? "true" : "false") + 
                      ", item_hovered: " + std::string(any_item_hovered ? "true" : "false") + 
                      ", item_active: " + std::string(any_item_active ? "true" : "false"));
    
    // Original logic: capture if ImGui wants it AND we're over any window/item
    return wants_capture && (any_window_hovered || any_item_hovered || any_item_active);
}
bool ImGuiIntegration::wants_capture_keyboard() const {
    return initialized_ && ImGui::GetIO().WantCaptureKeyboard;
}

// ─── Fonts ──────────────────────────────────────────────────────────────────

void ImGuiIntegration::load_fonts() {
    ImGuiIO& io = ImGui::GetIO();

    // Icon font glyph range: PUA U+E001 - U+E063
    static const ImWchar icon_ranges[] = { 0xE001, 0xE063, 0 };

    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;  // we own the static data

    ImFontConfig icon_cfg;
    icon_cfg.FontDataOwnedByAtlas = false;
    icon_cfg.MergeMode = true;           // merge into previous font
    icon_cfg.GlyphMinAdvanceX = 0.0f;
    icon_cfg.PixelSnapH = true;

    // Body font (16px) + icon merge
    cfg.SizePixels = 0;
    font_body_ = io.Fonts->AddFontFromMemoryCompressedTTF(
        InterFont_compressed_data, InterFont_compressed_size, 16.0f, &cfg);
    io.Fonts->AddFontFromMemoryTTF(
        (void*)PlotixIcons_data, PlotixIcons_size, 16.0f, &icon_cfg, icon_ranges);

    // Heading font (12.5px) + icon merge
    font_heading_ = io.Fonts->AddFontFromMemoryCompressedTTF(
        InterFont_compressed_data, InterFont_compressed_size, 12.5f, &cfg);
    io.Fonts->AddFontFromMemoryTTF(
        (void*)PlotixIcons_data, PlotixIcons_size, 12.5f, &icon_cfg, icon_ranges);

    // Icon font (20px) — primary icon font with Inter merged in
    font_icon_ = io.Fonts->AddFontFromMemoryCompressedTTF(
        InterFont_compressed_data, InterFont_compressed_size, 20.0f, &cfg);
    io.Fonts->AddFontFromMemoryTTF(
        (void*)PlotixIcons_data, PlotixIcons_size, 20.0f, &icon_cfg, icon_ranges);

    // Title font (18px) + icon merge
    font_title_ = io.Fonts->AddFontFromMemoryCompressedTTF(
        InterFont_compressed_data, InterFont_compressed_size, 18.0f, &cfg);
    io.Fonts->AddFontFromMemoryTTF(
        (void*)PlotixIcons_data, PlotixIcons_size, 18.0f, &icon_cfg, icon_ranges);

    // Menubar font (15px) + icon merge
    font_menubar_ = io.Fonts->AddFontFromMemoryCompressedTTF(
        InterFont_compressed_data, InterFont_compressed_size, 15.0f, &cfg);
    io.Fonts->AddFontFromMemoryTTF(
        (void*)PlotixIcons_data, PlotixIcons_size, 15.0f, &icon_cfg, icon_ranges);

    io.FontDefault = font_body_;
}

// ─── Style ──────────────────────────────────────────────────────────────────

void ImGuiIntegration::apply_modern_style() {
    // Apply theme colors through ThemeManager
    ui::ThemeManager::instance().apply_to_imgui();
}

// ─── Icon sidebar ───────────────────────────────────────────────────────────

// Helper: draw a clickable icon button with enhanced visual feedback
static bool icon_button(const char* label, bool active, ImFont* font,
                        float size) {
    using namespace ui;
    
    const auto& colors = theme();
    ImGui::PushFont(font);

    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(colors.accent_muted.r, colors.accent_muted.g, colors.accent_muted.b, colors.accent_muted.a));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, colors.accent.a));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, colors.accent.a));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(colors.text_secondary.r, colors.text_secondary.g, colors.text_secondary.b, colors.text_secondary.a));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    }
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(colors.accent_subtle.r, colors.accent_subtle.g, colors.accent_subtle.b, colors.accent_subtle.a));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(colors.accent_muted.r, colors.accent_muted.g, colors.accent_muted.b, colors.accent_muted.a));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::tokens::RADIUS_MD);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ui::tokens::SPACE_2, ui::tokens::SPACE_2));

    bool clicked = ImGui::Button(label, ImVec2(size, size));

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(5);
    ImGui::PopFont();
    return clicked;
}

// ─── Legacy Methods (To be removed after migration) ───────────────────────────

// These methods are kept temporarily for compatibility but will be removed
// once Agent C implements the proper inspector system

void ImGuiIntegration::draw_menubar() {
    // Legacy method - replaced by draw_command_bar()
    draw_command_bar();
}

void ImGuiIntegration::draw_icon_bar() {
    // Legacy method - replaced by draw_nav_rail()
    draw_nav_rail();
}

void ImGuiIntegration::draw_panel(Figure& figure) {
    // Legacy method - replaced by draw_inspector()
    draw_inspector(figure);
}

// ─── Legacy Panel Drawing Methods (To be removed after Agent C migration) ───

// Helper for drawing dropdown menus
void ImGuiIntegration::draw_menubar_menu(const char* label, const std::vector<MenuItem>& items) {
    ImGui::PushFont(font_menubar_);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(ui::theme().text_secondary.r, ui::theme().text_secondary.g, ui::theme().text_secondary.b, ui::theme().text_secondary.a));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(ui::theme().accent_subtle.r, ui::theme().accent_subtle.g, ui::theme().accent_subtle.b, ui::theme().accent_subtle.a));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(ui::theme().accent_muted.r, ui::theme().accent_muted.g, ui::theme().accent_muted.b, ui::theme().accent_muted.a));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    
    if (ImGui::Button(label)) {
        PLOTIX_LOG_DEBUG("ui_button", "Menu button clicked: " + std::string(label));
        ImGui::OpenPopup(label);
    }
    
    // Enhanced popup styling for modern look
    if (ImGui::BeginPopup(label)) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 8));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 4));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(ui::theme().bg_secondary.r, ui::theme().bg_secondary.g, ui::theme().bg_secondary.b, ui::theme().bg_secondary.a));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(ui::theme().border_default.r, ui::theme().border_default.g, ui::theme().border_default.b, ui::theme().border_default.a));
        
        for (const auto& item : items) {
            if (item.label.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(ui::theme().border_default.r, ui::theme().border_default.g, ui::theme().border_default.b, ui::theme().border_default.a));
                ImGui::Separator();
                ImGui::PopStyleColor();
            } else {
                // Enhanced menu item styling
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(ui::theme().text_primary.r, ui::theme().text_primary.g, ui::theme().text_primary.b, ui::theme().text_primary.a));
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(ui::theme().accent_subtle.r, ui::theme().accent_subtle.g, ui::theme().accent_subtle.b, ui::theme().accent_subtle.a));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(ui::theme().accent_muted.r, ui::theme().accent_muted.g, ui::theme().accent_muted.b, ui::theme().accent_muted.a));
                
                if (ImGui::MenuItem(item.label.c_str())) {
                    PLOTIX_LOG_DEBUG("ui_button", "Menu item clicked: " + item.label);
                    if (item.callback) {
                        PLOTIX_LOG_DEBUG("ui_button", "Executing menu item callback: " + item.label);
                        item.callback();
                        PLOTIX_LOG_DEBUG("ui_button", "Menu item callback completed: " + item.label);
                    } else {
                        PLOTIX_LOG_WARN("ui_button", "Menu item clicked but callback is null: " + item.label);
                    }
                }
                
                ImGui::PopStyleColor(4);
            }
        }
        
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
        ImGui::EndPopup();
    }
    
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    ImGui::PopFont();
}

// Helper for drawing toolbar buttons with tooltips
void ImGuiIntegration::draw_toolbar_button(const char* icon, std::function<void()> callback, const char* tooltip, bool is_active) {
    ImFont* icon_font = ui::icon_font(ui::tokens::ICON_MD);
    ImGui::PushFont(icon_font ? icon_font : font_icon_);
    
    if (is_active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(ui::theme().accent_muted.r, ui::theme().accent_muted.g, ui::theme().accent_muted.b, ui::theme().accent_muted.a));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(ui::theme().accent.r, ui::theme().accent.g, ui::theme().accent.b, ui::theme().accent.a));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(ui::theme().text_secondary.r, ui::theme().text_secondary.g, ui::theme().text_secondary.b, ui::theme().text_secondary.a));
    }
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(ui::theme().accent_subtle.r, ui::theme().accent_subtle.g, ui::theme().accent_subtle.b, ui::theme().accent_subtle.a));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(ui::theme().accent_muted.r, ui::theme().accent_muted.g, ui::theme().accent_muted.b, ui::theme().accent_muted.a));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    
    if (ImGui::Button(icon)) {
        PLOTIX_LOG_DEBUG("ui_button", "Toolbar button clicked: " + std::string(icon));
        if (callback) {
            PLOTIX_LOG_DEBUG("ui_button", "Executing callback for button: " + std::string(icon));
            callback();
            PLOTIX_LOG_DEBUG("ui_button", "Callback executed successfully for: " + std::string(icon));
        } else {
            PLOTIX_LOG_WARN("ui_button", "Button clicked but callback is null: " + std::string(icon));
        }
    }
    
    if (ImGui::IsItemHovered() && tooltip) {
        ImGui::SetTooltip("%s", tooltip);
    }
    
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    ImGui::PopFont();
}

// ─── Layout-Based Drawing Methods ─────────────────────────────────────────────

void ImGuiIntegration::draw_command_bar() {
    if (!layout_manager_) {
        PLOTIX_LOG_WARN("ui", "draw_command_bar called but layout_manager_ is null");
        return;
    }
    
    PLOTIX_LOG_TRACE("ui", "Drawing command bar");
    
    Rect bounds = layout_manager_->command_bar_rect();
    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y));
    ImGui::SetNextWindowSize(ImVec2(bounds.w, bounds.h));
    
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;
    
    // Enhanced styling for 2026 modern look
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 10));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(ui::theme().bg_secondary.r, ui::theme().bg_secondary.g, ui::theme().bg_secondary.b, ui::theme().bg_secondary.a));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(ui::theme().border_default.r, ui::theme().border_default.g, ui::theme().border_default.b, ui::theme().border_default.a));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    
    if (ImGui::Begin("##commandbar", nullptr, flags)) {
        PLOTIX_LOG_TRACE("ui", "Command bar window began successfully");
        // App title/brand on the left
        ImGui::PushFont(font_title_);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(ui::theme().accent.r, ui::theme().accent.g, ui::theme().accent.b, ui::theme().accent.a));
        ImGui::TextUnformatted("Plotix");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        
        ImGui::SameLine();
        
        // Home button to reset view
        draw_toolbar_button(ui::icon_str(ui::Icon::Menu), [this]() {
            PLOTIX_LOG_DEBUG("ui_button", "Menu button clicked - toggling nav rail");
            layout_manager_->set_nav_rail_expanded(!layout_manager_->is_nav_rail_expanded());
            PLOTIX_LOG_DEBUG("ui_button", "Nav rail expanded state: " + std::string(layout_manager_->is_nav_rail_expanded() ? "true" : "false"));
        }, "Toggle Navigation Rail");

        ImGui::SameLine();

        draw_toolbar_button(ui::icon_str(ui::Icon::Home), [this]() { 
            PLOTIX_LOG_DEBUG("ui_button", "Home button clicked - setting reset_view flag");
            reset_view_ = true; 
            PLOTIX_LOG_DEBUG("ui_button", "Reset view flag set successfully");
        }, "Reset View (Home)");

        ImGui::SameLine();
        
        // File menu
        draw_menubar_menu("File", {
            MenuItem("Export PNG", []() { /* TODO: Export functionality */ }),
            MenuItem("Export SVG", []() { /* TODO: Export functionality */ }),
            MenuItem("Export Video", []() { /* TODO: Video export functionality */ }),
            MenuItem("", nullptr), // Separator
            MenuItem("Exit", []() { /* TODO: Exit functionality */ })
        });
        
        ImGui::SameLine();
        
        // View menu
        draw_menubar_menu("View", {
            MenuItem("Toggle Inspector", [this]() { 
                bool new_vis = !layout_manager_->is_inspector_visible();
                layout_manager_->set_inspector_visible(new_vis);
                panel_open_ = new_vis;
            }),
            MenuItem("Toggle Navigation Rail", [this]() {
                layout_manager_->set_nav_rail_expanded(!layout_manager_->is_nav_rail_expanded());
            }),
            MenuItem("Zoom to Fit", []() { /* TODO: Zoom to fit functionality */ }),
            MenuItem("Reset View", []() { /* TODO: Reset view functionality */ }),
            MenuItem("Toggle Grid", []() { /* TODO: Grid toggle functionality */ })
        });
        
        ImGui::SameLine();
        
        // Tools menu
        draw_menubar_menu("Tools", {
            MenuItem("Screenshot", []() { /* TODO: Screenshot functionality */ }),
            MenuItem("Performance Monitor", []() { /* TODO: Performance monitor */ }),
            MenuItem("Theme Settings", [this]() { show_theme_settings_ = !show_theme_settings_; }),
            MenuItem("Preferences", []() { /* TODO: Preferences dialog */ })
        });
        
        // Push status info to the right
        ImGui::SameLine(0.0f, ImGui::GetContentRegionAvail().x - 220.0f);
        
        // Status info
        ImGuiIO& io = ImGui::GetIO();
        ImGui::PushFont(font_menubar_);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(ui::theme().text_secondary.r, ui::theme().text_secondary.g, ui::theme().text_secondary.b, ui::theme().text_secondary.a));
        
        char status[128];
        std::snprintf(status, sizeof(status), "Display: %dx%d | FPS: %.0f | GPU", 
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

void ImGuiIntegration::draw_nav_rail() {
    if (!layout_manager_) return;
    
    Rect bounds = layout_manager_->nav_rail_rect();
    
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoScrollbar;

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
    float total_content_h = nav_section_h + separator_h + tool_section_h + separator_h + settings_section_h;
    float vert_pad = ui::tokens::SPACE_4;  // generous top/bottom padding
    float toolbar_h = total_content_h + vert_pad * 2.0f;

    // Position: floating with a left margin, vertically centered in the content area
    float left_margin = ui::tokens::SPACE_3;
    float float_x = left_margin;
    float float_y = bounds.y + (bounds.h - toolbar_h) * 0.5f;
    // Clamp within content area
    float_y = std::clamp(float_y, bounds.y + ui::tokens::SPACE_3, bounds.y + bounds.h - toolbar_h - ui::tokens::SPACE_3);

    // Floating elevated style with rounded corners and subtle shadow
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(margin, vert_pad));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::tokens::RADIUS_LG);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, spacing));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(ui::theme().bg_elevated.r, ui::theme().bg_elevated.g, ui::theme().bg_elevated.b, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(ui::theme().border_default.r, ui::theme().border_default.g, ui::theme().border_default.b, 0.5f));

    // Draw shadow behind the toolbar via background draw list
    ImDrawList* bg_dl = ImGui::GetBackgroundDrawList();
    float shadow_offset = 4.0f;
    float shadow_radius = ui::tokens::RADIUS_LG + 2.0f;
    bg_dl->AddRectFilled(
        ImVec2(float_x + shadow_offset, float_y + shadow_offset),
        ImVec2(float_x + toolbar_w + shadow_offset, float_y + toolbar_h + shadow_offset),
        IM_COL32(0, 0, 0, 40), shadow_radius);

    ImGui::SetNextWindowPos(ImVec2(float_x, float_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(toolbar_w, toolbar_h), ImGuiCond_Always);

    if (ImGui::Begin("##navrail", nullptr, flags)) {
        float pad_x = std::max(0.0f, (toolbar_w - margin * 2.0f - btn_size) * 0.5f);

        // ── Inspector section buttons ──
        auto nav_btn = [&](ui::Icon icon, const char* tooltip, Section section) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad_x);
            bool is_active = panel_open_ && active_section_ == section;
            if (icon_button(ui::icon_str(icon), is_active, font_icon_, btn_size)) {
                if (is_active) {
                    panel_open_ = false;
                    layout_manager_->set_inspector_visible(false);
                } else {
                    active_section_ = section;
                    panel_open_ = true;
                    layout_manager_->set_inspector_visible(true);
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tooltip);
            }
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
            ImGui::GetWindowDrawList()->AddLine(p0, p1,
                IM_COL32(ui::theme().border_default.r * 255, ui::theme().border_default.g * 255,
                         ui::theme().border_default.b * 255, 80), 1.0f);
        }
        ImGui::Dummy(ImVec2(0, (section_gap - spacing) * 0.5f));

        // ── Tool mode buttons (from floating toolbar) ──
        auto tool_btn = [&](ui::Icon icon, const char* tooltip, ToolMode mode) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad_x);
            bool is_active = (interaction_mode_ == mode);
            if (icon_button(ui::icon_str(icon), is_active, font_icon_, btn_size)) {
                interaction_mode_ = mode;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tooltip);
            }
        };

        tool_btn(ui::Icon::Hand, "Pan (P)", ToolMode::Pan);
        tool_btn(ui::Icon::ZoomIn, "Box Zoom (Z)", ToolMode::BoxZoom);
        tool_btn(ui::Icon::Crosshair, "Select (S)", ToolMode::Select);

        // Measure button (no tool mode, standalone action)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad_x);
        icon_button(ui::icon_str(ui::Icon::Ruler), false, font_icon_, btn_size);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", "Measure");
        }

        // ── Separator ──
        ImGui::Dummy(ImVec2(0, (section_gap - spacing) * 0.5f));
        {
            float sep_pad = 6.0f;
            ImVec2 p0 = ImVec2(ImGui::GetWindowPos().x + sep_pad, ImGui::GetCursorScreenPos().y);
            ImVec2 p1 = ImVec2(ImGui::GetWindowPos().x + toolbar_w - sep_pad, p0.y);
            ImGui::GetWindowDrawList()->AddLine(p0, p1,
                IM_COL32(ui::theme().border_default.r * 255, ui::theme().border_default.g * 255,
                         ui::theme().border_default.b * 255, 80), 1.0f);
        }
        ImGui::Dummy(ImVec2(0, (section_gap - spacing) * 0.5f));

        // ── Settings at bottom ──
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad_x);
        if (icon_button(ui::icon_str(ui::Icon::Settings), show_theme_settings_, font_icon_, btn_size)) {
            show_theme_settings_ = !show_theme_settings_;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", "Settings");
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(5);
}

void ImGuiIntegration::draw_canvas(Figure& figure) {
    if (!layout_manager_) return;
    
    Rect bounds = layout_manager_->canvas_rect();
    
    // Canvas is primarily handled by the Vulkan renderer
    // We just set up the viewport here for ImGui coordination
    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y));
    ImGui::SetNextWindowSize(ImVec2(bounds.w, bounds.h));
    
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs;
    
    // Transparent window for canvas area
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    
    if (ImGui::Begin("##canvas", nullptr, flags)) {
        // Canvas content is rendered by Vulkan, not ImGui
        // This window is just for input handling coordination
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
}

void ImGuiIntegration::draw_inspector(Figure& figure) {
    if (!layout_manager_) return;
    
    Rect bounds = layout_manager_->inspector_rect();
    if (bounds.w < 1.0f) return;  // Fully collapsed
    
    // Draw resize handle as a separate invisible window so it extends outside the inspector
    {
        float handle_w = LayoutManager::RESIZE_HANDLE_WIDTH;
        float handle_x = bounds.x - handle_w * 0.5f;
        ImGui::SetNextWindowPos(ImVec2(handle_x, bounds.y));
        ImGui::SetNextWindowSize(ImVec2(handle_w, bounds.h));
        ImGuiWindowFlags handle_flags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        if (ImGui::Begin("##inspector_resize_handle", nullptr, handle_flags)) {
            ImGui::SetCursorScreenPos(ImVec2(handle_x, bounds.y));
            ImGui::InvisibleButton("##resize_grip", ImVec2(handle_w, bounds.h));
            bool hovered = ImGui::IsItemHovered();
            bool active = ImGui::IsItemActive();
            layout_manager_->set_inspector_resize_hovered(hovered);
            
            if (hovered || active) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            }
            if (ImGui::IsItemClicked()) {
                layout_manager_->set_inspector_resize_active(true);
            }
            if (active) {
                float right_edge = bounds.x + bounds.w;
                float new_width = right_edge - ImGui::GetIO().MousePos.x;
                layout_manager_->set_inspector_width(new_width);
            }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                layout_manager_->set_inspector_resize_active(false);
            }
            
            // Visual resize indicator line
            if (hovered || active) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                float line_x = bounds.x;
                auto accent = ui::theme().accent;
                ImU32 line_col = active
                    ? IM_COL32(uint8_t(accent.r*255), uint8_t(accent.g*255), uint8_t(accent.b*255), 255)
                    : IM_COL32(uint8_t(accent.r*255), uint8_t(accent.g*255), uint8_t(accent.b*255), 120);
                dl->AddLine(ImVec2(line_x, bounds.y), ImVec2(line_x, bounds.y + bounds.h), line_col, active ? 3.0f : 2.0f);
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }
    
    // Inspector panel itself
    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y));
    ImGui::SetNextWindowSize(ImVec2(bounds.w, bounds.h));
    
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui::tokens::SPACE_5, ui::tokens::SPACE_5));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(ui::theme().bg_secondary.r, ui::theme().bg_secondary.g, ui::theme().bg_secondary.b, ui::theme().bg_secondary.a));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(ui::theme().border_default.r, ui::theme().border_default.g, ui::theme().border_default.b, ui::theme().border_default.a));

    if (ImGui::Begin("##inspector", nullptr, flags)) {
        // Close button in top-right corner
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(ui::theme().accent_subtle.r, ui::theme().accent_subtle.g, ui::theme().accent_subtle.b, ui::theme().accent_subtle.a));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::tokens::RADIUS_SM);
        if (ImGui::Button(ui::icon_str(ui::Icon::Close), ImVec2(20, 20))) {
            layout_manager_->set_inspector_visible(false);
            panel_open_ = false;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        
        // Scrollable content area
        ImGui::BeginChild("##inspector_content", ImVec2(0, 0), false, ImGuiWindowFlags_NoBackground);
        
        if (panel_open_) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, panel_anim_);
            
            // Update selection context based on active nav rail section
            switch (active_section_) {
                case Section::Figure:
                    selection_ctx_.select_figure(&figure);
                    break;
                case Section::Series:
                    // For Series section, show only series browser (no figure properties)
                    selection_ctx_.select_series_browser(&figure);
                    break;
                case Section::Axes:
                    if (selection_ctx_.type != ui::SelectionType::Axes && !figure.axes().empty()) {
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

void ImGuiIntegration::draw_status_bar() {
    if (!layout_manager_) return;
    
    Rect bounds = layout_manager_->status_bar_rect();
    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y));
    ImGui::SetNextWindowSize(ImVec2(bounds.w, bounds.h));
    
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui::tokens::SPACE_3, ui::tokens::SPACE_1));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(ui::theme().bg_secondary.r, ui::theme().bg_secondary.g, ui::theme().bg_secondary.b, ui::theme().bg_secondary.a));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(ui::theme().border_subtle.r, ui::theme().border_subtle.g, ui::theme().border_subtle.b, ui::theme().border_subtle.a));
    
    if (ImGui::Begin("##statusbar", nullptr, flags)) {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::PushFont(font_heading_);
        
        // Left: cursor data readout
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(ui::theme().text_secondary.r, ui::theme().text_secondary.g, ui::theme().text_secondary.b, ui::theme().text_secondary.a));
        char cursor_buf[64];
        std::snprintf(cursor_buf, sizeof(cursor_buf), "X: %.4f  Y: %.4f", cursor_data_x_, cursor_data_y_);
        ImGui::TextUnformatted(cursor_buf);
        ImGui::PopStyleColor();
        
        // Center: mode indicator
        ImGui::SameLine(0.0f, ui::tokens::SPACE_6);
        {
            const char* mode_label = "Navigate";
            auto mode_color = ui::theme().text_secondary;
            switch (interaction_mode_) {
                case ToolMode::Pan:     mode_label = "Pan";      mode_color = ui::theme().accent; break;
                case ToolMode::BoxZoom: mode_label = "Box Zoom"; mode_color = ui::theme().warning; break;
                case ToolMode::Select:  mode_label = "Select";   mode_color = ui::theme().info; break;
                default: break;
            }
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(mode_color.r, mode_color.g, mode_color.b, mode_color.a));
            ImGui::TextUnformatted(mode_label);
            ImGui::PopStyleColor();
        }
        
        // Separator dot
        ImGui::SameLine(0.0f, ui::tokens::SPACE_3);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(ui::theme().border_default.r, ui::theme().border_default.g, ui::theme().border_default.b, ui::theme().border_default.a));
        ImGui::TextUnformatted("|");
        ImGui::PopStyleColor();
        
        // Zoom level
        ImGui::SameLine(0.0f, ui::tokens::SPACE_3);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(ui::theme().text_secondary.r, ui::theme().text_secondary.g, ui::theme().text_secondary.b, ui::theme().text_secondary.a));
        char zoom_buf[32];
        std::snprintf(zoom_buf, sizeof(zoom_buf), "Zoom: %d%%", static_cast<int>(zoom_level_ * 100.0f));
        ImGui::TextUnformatted(zoom_buf);
        ImGui::PopStyleColor();
        
        // Right side: performance info
        float right_offset = ImGui::GetContentRegionAvail().x - 160.0f;
        if (right_offset > 0.0f) {
            ImGui::SameLine(0.0f, right_offset);
        } else {
            ImGui::SameLine();
        }
        
        // FPS with color coding
        float fps_val = io.Framerate;
        auto fps_color = ui::theme().success;
        if (fps_val < 30.0f) fps_color = ui::theme().error;
        else if (fps_val < 55.0f) fps_color = ui::theme().warning;
        
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(fps_color.r, fps_color.g, fps_color.b, fps_color.a));
        char fps_buf[32];
        std::snprintf(fps_buf, sizeof(fps_buf), "%d fps", static_cast<int>(fps_val));
        ImGui::TextUnformatted(fps_buf);
        ImGui::PopStyleColor();
        
        // GPU time
        ImGui::SameLine(0.0f, ui::tokens::SPACE_3);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(ui::theme().text_tertiary.r, ui::theme().text_tertiary.g, ui::theme().text_tertiary.b, ui::theme().text_tertiary.a));
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

void ImGuiIntegration::draw_split_view_splitters() {
    if (!dock_system_) return;

    auto* draw_list = ImGui::GetForegroundDrawList();
    auto& theme = ui::theme();
    ImVec2 mouse = ImGui::GetMousePos();

    // ── Non-split drag-to-split overlay ──────────────────────────────────
    // When NOT split and a tab is being dock-dragged, show edge zone
    // highlights to suggest splitting (like VSCode).
    if (!dock_system_->is_split() && dock_system_->is_dragging()) {
        auto target = dock_system_->current_drop_target();
        // Only show edge zones (Left/Right/Top/Bottom), not Center
        if (target.zone != DropZone::None && target.zone != DropZone::Center) {
            Rect hr = target.highlight_rect;
            ImU32 highlight_color = IM_COL32(
                static_cast<int>(theme.accent.r * 255),
                static_cast<int>(theme.accent.g * 255),
                static_cast<int>(theme.accent.b * 255), 40);
            ImU32 highlight_border = IM_COL32(
                static_cast<int>(theme.accent.r * 255),
                static_cast<int>(theme.accent.g * 255),
                static_cast<int>(theme.accent.b * 255), 160);

            draw_list->AddRectFilled(
                ImVec2(hr.x, hr.y),
                ImVec2(hr.x + hr.w, hr.y + hr.h),
                highlight_color, 4.0f);
            draw_list->AddRect(
                ImVec2(hr.x, hr.y),
                ImVec2(hr.x + hr.w, hr.y + hr.h),
                highlight_border, 4.0f, 0, 2.0f);

            // Draw a label indicating the split direction
            const char* label = nullptr;
            switch (target.zone) {
                case DropZone::Left:   label = "Split Left";   break;
                case DropZone::Right:  label = "Split Right";  break;
                case DropZone::Top:    label = "Split Up";     break;
                case DropZone::Bottom: label = "Split Down";   break;
                default: break;
            }
            if (label) {
                ImVec2 lsz = ImGui::CalcTextSize(label);
                float lx = hr.x + (hr.w - lsz.x) * 0.5f;
                float ly = hr.y + (hr.h - lsz.y) * 0.5f;
                draw_list->AddText(ImVec2(lx, ly),
                    IM_COL32(static_cast<int>(theme.accent.r * 255),
                             static_cast<int>(theme.accent.g * 255),
                             static_cast<int>(theme.accent.b * 255), 200),
                    label);
            }
        }
        return;  // No splitters to draw in non-split mode
    }

    if (!dock_system_->is_split()) return;

    // Handle pane activation on mouse click in canvas area
    // (skip if mouse is over a pane tab header — that's handled by draw_pane_tab_headers)
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().WantCaptureMouse
        && !pane_tab_hovered_) {
        dock_system_->activate_pane_at(mouse.x, mouse.y);
    }

    // Handle splitter interaction
    if (dock_system_->is_over_splitter(mouse.x, mouse.y)) {
        auto dir = dock_system_->splitter_direction_at(mouse.x, mouse.y);
        ImGui::SetMouseCursor(dir == SplitDirection::Horizontal
                              ? ImGuiMouseCursor_ResizeEW
                              : ImGuiMouseCursor_ResizeNS);

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            dock_system_->begin_splitter_drag(mouse.x, mouse.y);
        }
    }

    if (dock_system_->is_dragging_splitter()) {
        auto* sp = dock_system_->split_view().dragging_splitter();
        if (sp) {
            float pos = (sp->split_direction() == SplitDirection::Horizontal)
                        ? mouse.x : mouse.y;
            dock_system_->update_splitter_drag(pos);
            ImGui::SetMouseCursor(sp->split_direction() == SplitDirection::Horizontal
                                  ? ImGuiMouseCursor_ResizeEW
                                  : ImGuiMouseCursor_ResizeNS);
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            dock_system_->end_splitter_drag();
        }
    }

    // Draw splitter handles for all internal nodes
    auto pane_infos = dock_system_->get_pane_infos();

    // Walk the split tree to find internal nodes and draw their splitters
    std::function<void(SplitPane*)> draw_splitters = [&](SplitPane* node) {
        if (!node || node->is_leaf()) return;

        Rect sr = node->splitter_rect();
        bool is_dragging = dock_system_->is_dragging_splitter() &&
                           dock_system_->split_view().dragging_splitter() == node;

        // Splitter background
        ImU32 splitter_color;
        if (is_dragging) {
            splitter_color = IM_COL32(
                static_cast<int>(theme.accent.r * 255),
                static_cast<int>(theme.accent.g * 255),
                static_cast<int>(theme.accent.b * 255), 200);
        } else {
            splitter_color = IM_COL32(
                static_cast<int>(theme.border_default.r * 255),
                static_cast<int>(theme.border_default.g * 255),
                static_cast<int>(theme.border_default.b * 255), 120);
        }

        draw_list->AddRectFilled(
            ImVec2(sr.x, sr.y),
            ImVec2(sr.x + sr.w, sr.y + sr.h),
            splitter_color);

        // Draw a subtle grip indicator in the center of the splitter
        float cx = sr.x + sr.w * 0.5f;
        float cy = sr.y + sr.h * 0.5f;
        ImU32 grip_color = IM_COL32(
            static_cast<int>(theme.text_tertiary.r * 255),
            static_cast<int>(theme.text_tertiary.g * 255),
            static_cast<int>(theme.text_tertiary.b * 255), 150);

        if (node->split_direction() == SplitDirection::Horizontal) {
            // Vertical splitter — draw horizontal grip dots
            for (int i = -2; i <= 2; ++i) {
                draw_list->AddCircleFilled(ImVec2(cx, cy + i * 6.0f), 1.5f, grip_color);
            }
        } else {
            // Horizontal splitter — draw vertical grip dots
            for (int i = -2; i <= 2; ++i) {
                draw_list->AddCircleFilled(ImVec2(cx + i * 6.0f, cy), 1.5f, grip_color);
            }
        }

        // Recurse into children
        draw_splitters(node->first());
        draw_splitters(node->second());
    };

    draw_splitters(dock_system_->split_view().root());

    // Draw active pane border highlight
    for (const auto& info : pane_infos) {
        if (info.is_active && pane_infos.size() > 1) {
            ImU32 border_color = IM_COL32(
                static_cast<int>(theme.accent.r * 255),
                static_cast<int>(theme.accent.g * 255),
                static_cast<int>(theme.accent.b * 255), 180);
            draw_list->AddRect(
                ImVec2(info.bounds.x, info.bounds.y),
                ImVec2(info.bounds.x + info.bounds.w, info.bounds.y + info.bounds.h),
                border_color, 0.0f, 0, 2.0f);
        }
    }

    // Draw drop zone highlight during drag-to-dock
    if (dock_system_->is_dragging()) {
        auto target = dock_system_->current_drop_target();
        if (target.zone != DropZone::None) {
            Rect hr = target.highlight_rect;
            ImU32 highlight_color = IM_COL32(
                static_cast<int>(theme.accent.r * 255),
                static_cast<int>(theme.accent.g * 255),
                static_cast<int>(theme.accent.b * 255), 60);
            ImU32 highlight_border = IM_COL32(
                static_cast<int>(theme.accent.r * 255),
                static_cast<int>(theme.accent.g * 255),
                static_cast<int>(theme.accent.b * 255), 180);

            draw_list->AddRectFilled(
                ImVec2(hr.x, hr.y),
                ImVec2(hr.x + hr.w, hr.y + hr.h),
                highlight_color);
            draw_list->AddRect(
                ImVec2(hr.x, hr.y),
                ImVec2(hr.x + hr.w, hr.y + hr.h),
                highlight_border, 0.0f, 0, 2.0f);
        }
    }
}

// ─── Per-pane tab headers ────────────────────────────────────────────────────
// Draws a compact tab bar above each split pane leaf. Supports:
//  - Click to switch active figure within a pane
//  - Drag tabs between panes (cross-pane drag)
//  - Smooth animated tab positions and drag ghost

void ImGuiIntegration::draw_pane_tab_headers() {
    if (!dock_system_) return;

    auto* draw_list = ImGui::GetForegroundDrawList();
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
    auto fig_title = [&](size_t fig_idx) -> std::string {
        if (get_figure_title_) return get_figure_title_(fig_idx);
        return "Figure " + std::to_string(fig_idx + 1);
    };

    // Helper: ImU32 from theme color
    auto to_col = [](const ui::Color& c, float a = -1.0f) -> ImU32 {
        float alpha = a >= 0.0f ? a : c.a;
        return IM_COL32(uint8_t(c.r*255), uint8_t(c.g*255), uint8_t(c.b*255), uint8_t(alpha*255));
    };

    // ── Phase 1: Compute tab layouts per pane ────────────────────────────

    struct TabRect {
        size_t figure_index;
        float x, y, w, h;
        bool is_active;
        bool is_hovered;
    };

    struct PaneHeader {
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

    if (pane_tab_drag_.dragging && pane_tab_drag_.dragged_figure_index != SIZE_MAX) {
        for (auto* pane_const : panes) {
            auto* pane = const_cast<SplitPane*>(pane_const);
            if (!pane->is_leaf()) continue;
            Rect b = pane->bounds();
            Rect hr{b.x, b.y, b.w, TAB_H};
            if (mouse.x >= hr.x && mouse.x < hr.x + hr.w &&
                mouse.y >= hr.y - 10 && mouse.y < hr.y + hr.h + 10) {
                // Mouse is over this pane's header — compute insertion index
                if (pane->id() != pane_tab_drag_.source_pane_id ||
                    pane->figure_count() > 1) {
                    gap_pane_id = pane->id();
                    has_active_gap = true;
                    gap_insert_after = SIZE_MAX;  // Before first tab by default
                    const auto& figs = pane->figure_indices();
                    float cx = hr.x + 2.0f;
                    for (size_t li = 0; li < figs.size(); ++li) {
                        if (figs[li] == pane_tab_drag_.dragged_figure_index) {
                            cx += 0;  // Skip the dragged tab's width
                            continue;
                        }
                        std::string t = fig_title(figs[li]);
                        ImVec2 tsz = ImGui::CalcTextSize(t.c_str());
                        float w = std::clamp(tsz.x + TAB_PAD * 2 + CLOSE_SZ, TAB_MIN_W, TAB_MAX_W);
                        if (mouse.x > cx + w * 0.5f) {
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
    if (has_active_gap) {
        insertion_gap_.target_pane_id = gap_pane_id;
        insertion_gap_.insert_after_idx = gap_insert_after;
        insertion_gap_.target_gap = GAP_WIDTH;
    } else {
        insertion_gap_.target_gap = 0.0f;
    }
    insertion_gap_.current_gap += (insertion_gap_.target_gap - insertion_gap_.current_gap) * lerp_t_gap;
    if (insertion_gap_.current_gap < 0.5f && insertion_gap_.target_gap == 0.0f) {
        insertion_gap_.current_gap = 0.0f;
        insertion_gap_.target_pane_id = 0;
        insertion_gap_.insert_after_idx = SIZE_MAX;
    }

    for (auto* pane_const : panes) {
        auto* pane = const_cast<SplitPane*>(pane_const);
        if (!pane->is_leaf()) continue;

        Rect b = pane->bounds();
        Rect hr{b.x, b.y, b.w, TAB_H};

        PaneHeader ph;
        ph.pane = pane;
        ph.header_rect = hr;

        const auto& figs = pane->figure_indices();
        float cur_x = hr.x + 2.0f;

        // Check if this pane has an active insertion gap
        bool pane_has_gap = (insertion_gap_.current_gap > 0.1f &&
                             pane->id() == insertion_gap_.target_pane_id);

        for (size_t li = 0; li < figs.size(); ++li) {
            size_t fig_idx = figs[li];
            std::string title = fig_title(fig_idx);

            ImVec2 text_sz = ImGui::CalcTextSize(title.c_str());
            float tw = std::clamp(text_sz.x + TAB_PAD * 2 + CLOSE_SZ, TAB_MIN_W, TAB_MAX_W);

            // Add insertion gap before this tab if needed
            if (pane_has_gap && insertion_gap_.insert_after_idx == SIZE_MAX && li == 0) {
                cur_x += insertion_gap_.current_gap;
            } else if (pane_has_gap && li > 0 && (li - 1) == insertion_gap_.insert_after_idx) {
                cur_x += insertion_gap_.current_gap;
            }

            // Animate position (keyed by pane+figure to avoid cross-pane interference)
            auto& anim = pane_tab_anims_[{ph.pane->id(), fig_idx}];
            anim.target_x = cur_x;
            if (anim.current_x == 0.0f && anim.target_x != 0.0f) {
                anim.current_x = anim.target_x;  // First frame: snap
            }
            float lerp_t = std::min(1.0f, ANIM_SPEED * dt);
            anim.current_x += (anim.target_x - anim.current_x) * lerp_t;
            anim.opacity += (anim.target_opacity - anim.opacity) * lerp_t;

            float draw_x = anim.current_x;

            bool is_active_local = (li == pane->active_local_index());
            bool hovered = (mouse.x >= draw_x && mouse.x < draw_x + tw &&
                            mouse.y >= hr.y && mouse.y < hr.y + TAB_H);

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

    for (auto& ph : headers) {
        // Draw header background
        Rect hr = ph.header_rect;
        draw_list->AddRectFilled(
            ImVec2(hr.x, hr.y), ImVec2(hr.x + hr.w, hr.y + hr.h),
            to_col(theme.bg_secondary));
        draw_list->AddLine(
            ImVec2(hr.x, hr.y + hr.h - 1), ImVec2(hr.x + hr.w, hr.y + hr.h - 1),
            to_col(theme.border_subtle), 1.0f);

        for (auto& tr : ph.tabs) {
            bool is_being_dragged = pane_tab_drag_.dragging &&
                                    pane_tab_drag_.dragged_figure_index == tr.figure_index;

            // Skip drawing the tab in its original position if it's being dragged cross-pane
            if (is_being_dragged && pane_tab_drag_.cross_pane) continue;

            // Tab background
            ImU32 bg;
            if (is_being_dragged) {
                bg = to_col(theme.bg_elevated);
            } else if (tr.is_active) {
                bg = to_col(theme.bg_tertiary);
            } else if (tr.is_hovered) {
                bg = to_col(theme.accent_subtle);
            } else {
                bg = to_col(theme.bg_secondary, 0.0f);
            }

            float inset_y = 3.0f;
            ImVec2 tl(tr.x, tr.y + inset_y);
            ImVec2 br(tr.x + tr.w, tr.y + tr.h);
            draw_list->AddRectFilled(tl, br, bg, 4.0f, ImDrawFlags_RoundCornersTop);

            // Active underline
            if (tr.is_active) {
                draw_list->AddLine(
                    ImVec2(tl.x + 3, br.y - 1), ImVec2(br.x - 3, br.y - 1),
                    to_col(theme.accent), 2.0f);
            }

            // Title text
            std::string title = fig_title(tr.figure_index);
            ImVec2 text_sz = ImGui::CalcTextSize(title.c_str());
            ImVec2 text_pos(tr.x + TAB_PAD, tr.y + (tr.h - text_sz.y) * 0.5f);

            draw_list->PushClipRect(ImVec2(tr.x, tr.y), ImVec2(tr.x + tr.w - CLOSE_SZ - 2, tr.y + tr.h), true);
            draw_list->AddText(text_pos,
                tr.is_active ? to_col(theme.text_primary) : to_col(theme.text_secondary),
                title.c_str());
            draw_list->PopClipRect();

            // Close button (show on hover or active, only if pane has >1 figure)
            if ((tr.is_active || tr.is_hovered) && ph.pane->figure_count() > 1) {
                float cx = tr.x + tr.w - CLOSE_SZ * 0.5f - 4.0f;
                float cy = tr.y + tr.h * 0.5f;
                float sz = 3.5f;

                bool close_hovered = (std::abs(mouse.x - cx) < CLOSE_SZ * 0.5f &&
                                      std::abs(mouse.y - cy) < CLOSE_SZ * 0.5f);
                if (close_hovered) {
                    draw_list->AddCircleFilled(ImVec2(cx, cy), CLOSE_SZ * 0.5f,
                        to_col(theme.error, 0.15f));
                }
                ImU32 x_col = close_hovered ? to_col(theme.error) : to_col(theme.text_tertiary);
                draw_list->AddLine(ImVec2(cx - sz, cy - sz), ImVec2(cx + sz, cy + sz), x_col, 1.5f);
                draw_list->AddLine(ImVec2(cx - sz, cy + sz), ImVec2(cx + sz, cy - sz), x_col, 1.5f);

                // Close click
                if (close_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    ph.pane->remove_figure(tr.figure_index);
                    pane_tab_hovered_ = true;
                    continue;
                }
            }

            // Click / drag handling
            if (tr.is_hovered) {
                pane_tab_hovered_ = true;

                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    // Activate this tab
                    for (size_t li = 0; li < ph.pane->figure_indices().size(); ++li) {
                        if (ph.pane->figure_indices()[li] == tr.figure_index) {
                            dock_system_->activate_local_tab(ph.pane->id(), li);
                            break;
                        }
                    }
                    // Start potential drag
                    pane_tab_drag_.dragging = false;  // Will become true after threshold
                    pane_tab_drag_.source_pane_id = ph.pane->id();
                    pane_tab_drag_.dragged_figure_index = tr.figure_index;
                    pane_tab_drag_.drag_start_x = mouse.x;
                    pane_tab_drag_.drag_start_y = mouse.y;
                    pane_tab_drag_.cross_pane = false;
                    pane_tab_drag_.dock_dragging = false;
                }
            }
        }
    }

    // ── Phase 3: Drag update ─────────────────────────────────────────────

    constexpr float DOCK_DRAG_THRESHOLD = 30.0f;  // Vertical distance to trigger dock drag

    if (pane_tab_drag_.dragged_figure_index != SIZE_MAX &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left)) {

        float dx = mouse.x - pane_tab_drag_.drag_start_x;
        float dy = mouse.y - pane_tab_drag_.drag_start_y;
        float dist = std::sqrt(dx * dx + dy * dy);

        if (!pane_tab_drag_.dragging && dist > DRAG_THRESHOLD) {
            pane_tab_drag_.dragging = true;
        }

        if (pane_tab_drag_.dragging) {
            // Check if dragged far enough vertically to enter dock-drag mode
            // (triggers split suggestion overlay via dock system)
            if (!pane_tab_drag_.dock_dragging && std::abs(dy) > DOCK_DRAG_THRESHOLD) {
                // Only enter dock drag if there are multiple figures
                // (need at least 2 to split)
                bool over_any_header = false;
                for (auto& ph : headers) {
                    Rect hr = ph.header_rect;
                    if (mouse.x >= hr.x && mouse.x < hr.x + hr.w &&
                        mouse.y >= hr.y - 10 && mouse.y < hr.y + hr.h + 10) {
                        over_any_header = true;
                        break;
                    }
                }
                if (!over_any_header) {
                    pane_tab_drag_.dock_dragging = true;
                    dock_system_->begin_drag(pane_tab_drag_.dragged_figure_index,
                                             mouse.x, mouse.y);
                }
            }

            // If in dock-drag mode, forward to dock system
            if (pane_tab_drag_.dock_dragging) {
                dock_system_->update_drag(mouse.x, mouse.y);
            }

            // Check if mouse is over a different pane's header
            bool over_source = false;
            for (auto& ph : headers) {
                Rect hr = ph.header_rect;
                if (mouse.x >= hr.x && mouse.x < hr.x + hr.w &&
                    mouse.y >= hr.y && mouse.y < hr.y + hr.h) {
                    if (ph.pane->id() == pane_tab_drag_.source_pane_id) {
                        over_source = true;
                    } else {
                        pane_tab_drag_.cross_pane = true;
                    }
                    break;
                }
            }
            if (!over_source && !pane_tab_drag_.dock_dragging) {
                pane_tab_drag_.cross_pane = true;
            }

            // Draw drag ghost tab
            std::string title = fig_title(pane_tab_drag_.dragged_figure_index);
            ImVec2 text_sz = ImGui::CalcTextSize(title.c_str());
            float ghost_w = std::clamp(text_sz.x + TAB_PAD * 2 + CLOSE_SZ, TAB_MIN_W, TAB_MAX_W);
            float ghost_h = TAB_H;
            float ghost_x = mouse.x - ghost_w * 0.5f;
            float ghost_y = mouse.y - ghost_h * 0.5f;

            // Ghost shadow
            draw_list->AddRectFilled(
                ImVec2(ghost_x + 2, ghost_y + 2),
                ImVec2(ghost_x + ghost_w + 2, ghost_y + ghost_h + 2),
                IM_COL32(0, 0, 0, 40), 6.0f);

            // Ghost background
            draw_list->AddRectFilled(
                ImVec2(ghost_x, ghost_y),
                ImVec2(ghost_x + ghost_w, ghost_y + ghost_h),
                to_col(theme.bg_elevated), 6.0f);
            draw_list->AddRect(
                ImVec2(ghost_x, ghost_y),
                ImVec2(ghost_x + ghost_w, ghost_y + ghost_h),
                to_col(theme.accent, 0.6f), 6.0f, 0, 1.5f);

            // Ghost text
            ImVec2 gtext_pos(ghost_x + TAB_PAD, ghost_y + (ghost_h - text_sz.y) * 0.5f);
            draw_list->AddText(gtext_pos, to_col(theme.text_primary), title.c_str());

            // Draw drop indicator on target pane header
            for (auto& ph : headers) {
                if (ph.pane->id() == pane_tab_drag_.source_pane_id &&
                    ph.pane->figure_count() <= 1) continue;

                Rect hr = ph.header_rect;
                if (mouse.x >= hr.x && mouse.x < hr.x + hr.w &&
                    mouse.y >= hr.y - 10 && mouse.y < hr.y + hr.h + 10) {
                    // Highlight target header
                    draw_list->AddRectFilled(
                        ImVec2(hr.x, hr.y), ImVec2(hr.x + hr.w, hr.y + hr.h),
                        to_col(theme.accent, 0.08f));

                    // Draw insertion line
                    float insert_x = hr.x + 4.0f;
                    for (auto& tr : ph.tabs) {
                        if (mouse.x > tr.x + tr.w * 0.5f) {
                            insert_x = tr.x + tr.w + 1.0f;
                        }
                    }
                    draw_list->AddLine(
                        ImVec2(insert_x, hr.y + 4), ImVec2(insert_x, hr.y + hr.h - 4),
                        to_col(theme.accent), 2.0f);
                }
            }
        }
    }

    // ── Phase 4: Drag end (drop) ─────────────────────────────────────────

    if (pane_tab_drag_.dragged_figure_index != SIZE_MAX &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {

        if (pane_tab_drag_.dragging && pane_tab_drag_.dock_dragging) {
            // Dock-drag mode: let dock system handle the split
            dock_system_->end_drag(mouse.x, mouse.y);
        } else if (pane_tab_drag_.dragging && pane_tab_drag_.cross_pane) {
            // Cross-pane tab move: find target pane under mouse
            for (auto& ph : headers) {
                Rect hr = ph.header_rect;
                if (mouse.x >= hr.x && mouse.x < hr.x + hr.w &&
                    mouse.y >= hr.y - 10 && mouse.y < hr.y + hr.h + 10) {
                    if (ph.pane->id() != pane_tab_drag_.source_pane_id) {
                        dock_system_->move_figure_to_pane(
                            pane_tab_drag_.dragged_figure_index, ph.pane->id());
                    }
                    break;
                }
            }
        }

        // Reset drag state
        pane_tab_drag_.dragging = false;
        pane_tab_drag_.dragged_figure_index = SIZE_MAX;
        pane_tab_drag_.cross_pane = false;
        pane_tab_drag_.dock_dragging = false;
    }

    // Cancel drag on escape
    if (pane_tab_drag_.dragged_figure_index != SIZE_MAX &&
        ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (pane_tab_drag_.dock_dragging) {
            dock_system_->cancel_drag();
        }
        pane_tab_drag_.dragging = false;
        pane_tab_drag_.dragged_figure_index = SIZE_MAX;
        pane_tab_drag_.cross_pane = false;
        pane_tab_drag_.dock_dragging = false;
    }
}

#if PLOTIX_FLOATING_TOOLBAR
void ImGuiIntegration::draw_floating_toolbar() {
    if (!layout_manager_) return;

    float opacity = layout_manager_->floating_toolbar_opacity();
    if (opacity < 0.01f) return;  // Fully hidden, skip drawing

    Rect bounds = layout_manager_->floating_toolbar_rect();

    // Check if mouse is hovering near the toolbar — reveal on hover
    ImVec2 mouse = ImGui::GetIO().MousePos;
    float hover_margin = 30.0f;
    bool mouse_near = (mouse.x >= bounds.x - hover_margin &&
                       mouse.x <= bounds.x + bounds.w + hover_margin &&
                       mouse.y >= bounds.y - hover_margin &&
                       mouse.y <= bounds.y + bounds.h + hover_margin);
    if (mouse_near) {
        layout_manager_->notify_toolbar_activity();
        opacity = layout_manager_->floating_toolbar_opacity();
    }

    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(bounds.w, bounds.h));

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav;

    // Apply opacity to all toolbar colors
    float bg_alpha = 0.95f * opacity;
    float border_alpha = 0.6f * opacity;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 20.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, opacity);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(ui::theme().bg_elevated.r, ui::theme().bg_elevated.g, ui::theme().bg_elevated.b, bg_alpha));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(ui::theme().border_default.r, ui::theme().border_default.g, ui::theme().border_default.b, border_alpha));

    if (ImGui::Begin("##floatingtoolbar", nullptr, flags)) {
        // Handle dragging — drag on empty area of the toolbar to reposition
        if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f)) {
            if (!toolbar_dragging_) {
                // Check we're not clicking a button (only drag from empty space)
                if (!ImGui::IsAnyItemHovered()) {
                    toolbar_dragging_ = true;
                }
            }
        }
        if (toolbar_dragging_) {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImVec2 delta = ImGui::GetIO().MouseDelta;
                layout_manager_->set_floating_toolbar_drag_offset(bounds.x + delta.x, bounds.y + delta.y);
                layout_manager_->notify_toolbar_activity();
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                toolbar_dragging_ = false;
            }
        }

        // Quick access tools
        draw_toolbar_button(ui::icon_str(ui::Icon::ZoomIn), [this]() {
            interaction_mode_ = ToolMode::BoxZoom;
            layout_manager_->notify_toolbar_activity();
        }, "Zoom", interaction_mode_ == ToolMode::BoxZoom);
        ImGui::SameLine();
        draw_toolbar_button(ui::icon_str(ui::Icon::Hand), [this]() {
            interaction_mode_ = ToolMode::Pan;
            layout_manager_->notify_toolbar_activity();
        }, "Pan", interaction_mode_ == ToolMode::Pan);
        ImGui::SameLine();
        draw_toolbar_button(ui::icon_str(ui::Icon::Crosshair), [this]() {
            interaction_mode_ = ToolMode::Select;
            layout_manager_->notify_toolbar_activity();
        }, "Select", interaction_mode_ == ToolMode::Select);
        ImGui::SameLine();
        draw_toolbar_button(ui::icon_str(ui::Icon::Ruler), [this]() {
            layout_manager_->notify_toolbar_activity();
        }, "Measure");

        // Double-click to reset position
        if (ImGui::IsWindowHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered()) {
            layout_manager_->reset_floating_toolbar_position();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
}
#endif

void ImGuiIntegration::draw_plot_text(Figure& figure) {
    if (!layout_manager_) return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const auto& colors = ui::ThemeManager::instance().colors();
    ImU32 tick_col = IM_COL32(
        static_cast<uint8_t>(colors.tick_label.r * 255),
        static_cast<uint8_t>(colors.tick_label.g * 255),
        static_cast<uint8_t>(colors.tick_label.b * 255),
        static_cast<uint8_t>(colors.tick_label.a * 255));
    ImU32 label_col = IM_COL32(
        static_cast<uint8_t>(colors.text_primary.r * 255),
        static_cast<uint8_t>(colors.text_primary.g * 255),
        static_cast<uint8_t>(colors.text_primary.b * 255),
        static_cast<uint8_t>(colors.text_primary.a * 255));
    ImU32 title_col = label_col;

    constexpr float tick_padding = 5.0f;

    for (auto& axes_ptr : figure.axes()) {
        if (!axes_ptr) continue;
        auto& axes = *axes_ptr;
        const auto& vp = axes.viewport();
        auto xlim = axes.x_limits();
        auto ylim = axes.y_limits();

        float x_range = xlim.max - xlim.min;
        float y_range = ylim.max - ylim.min;
        if (x_range == 0.0f) x_range = 1.0f;
        if (y_range == 0.0f) y_range = 1.0f;

        auto data_to_px_x = [&](float dx) -> float {
            return vp.x + (dx - xlim.min) / x_range * vp.w;
        };
        auto data_to_px_y = [&](float dy) -> float {
            return vp.y + (1.0f - (dy - ylim.min) / y_range) * vp.h;
        };

        // --- X tick labels ---
        ImGui::PushFont(font_body_);
        auto x_ticks = axes.compute_x_ticks();
        for (size_t i = 0; i < x_ticks.positions.size(); ++i) {
            float px = data_to_px_x(x_ticks.positions[i]);
            const char* txt = x_ticks.labels[i].c_str();
            ImVec2 sz = ImGui::CalcTextSize(txt);
            dl->AddText(ImVec2(px - sz.x * 0.5f, vp.y + vp.h + tick_padding), tick_col, txt);
        }

        // --- Y tick labels ---
        auto y_ticks = axes.compute_y_ticks();
        for (size_t i = 0; i < y_ticks.positions.size(); ++i) {
            float py = data_to_px_y(y_ticks.positions[i]);
            const char* txt = y_ticks.labels[i].c_str();
            ImVec2 sz = ImGui::CalcTextSize(txt);
            dl->AddText(ImVec2(vp.x - tick_padding - sz.x, py - sz.y * 0.5f), tick_col, txt);
        }
        ImGui::PopFont();

        // --- X axis label ---
        if (!axes.get_xlabel().empty()) {
            ImGui::PushFont(font_menubar_);
            const char* txt = axes.get_xlabel().c_str();
            ImVec2 sz = ImGui::CalcTextSize(txt);
            float cx = vp.x + vp.w * 0.5f;
            float py = vp.y + vp.h + tick_padding + 16.0f + tick_padding;
            dl->AddText(ImVec2(cx - sz.x * 0.5f, py), label_col, txt);
            ImGui::PopFont();
        }

        // --- Y axis label (rotated -90°) ---
        if (!axes.get_ylabel().empty()) {
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
            for (int i = vtx_begin; i < vtx_end; ++i) {
                ImDrawVert& v = dl->VtxBuffer[i];
                float dx = v.pos.x - center_x;
                float dy = v.pos.y - center_y;
                v.pos.x = center_x + dx * cos_a - dy * sin_a;
                v.pos.y = center_y + dx * sin_a + dy * cos_a;
            }

            ImGui::PopFont();
        }

        // --- Title ---
        if (!axes.get_title().empty()) {
            ImGui::PushFont(font_title_);
            const char* txt = axes.get_title().c_str();
            ImVec2 sz = ImGui::CalcTextSize(txt);
            float cx = vp.x + vp.w * 0.5f;
            float py = vp.y - sz.y - tick_padding;
            dl->AddText(ImVec2(cx - sz.x * 0.5f, py), title_col, txt);
            ImGui::PopFont();
        }
    }

    // Legend is drawn by LegendInteraction (in data_interaction.cpp) which
    // supports click-to-toggle visibility and drag-to-reposition.
}

void ImGuiIntegration::draw_theme_settings() {
    // Center the modal window
    ImGuiIO& io = ImGui::GetIO();
    float window_width = 400.0f;
    float window_height = 300.0f;
    ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - window_width) * 0.5f, (io.DisplaySize.y - window_height) * 0.5f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(window_width, window_height));
    
    // Get available themes
    auto& theme_manager = ui::ThemeManager::instance();
    static std::vector<std::string> available_themes;
    if (available_themes.empty()) {
        // This is a simple way to get theme names - in a real implementation
        // we might add a get_available_themes() method to ThemeManager
        available_themes = {"dark", "light", "high_contrast"};
    }
    
    ImGuiWindowFlags flags = 
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;
    
    // Modern modal styling
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui::tokens::SPACE_5, ui::tokens::SPACE_4));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::tokens::RADIUS_LG);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(ui::theme().bg_elevated.r, ui::theme().bg_elevated.g, ui::theme().bg_elevated.b, theme_manager.current().opacity_panel));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(ui::theme().border_default.r, ui::theme().border_default.g, ui::theme().border_default.b, ui::theme().border_default.a));
    
    bool is_open = true;
    if (ImGui::Begin("Theme Settings", &is_open, flags)) {
        ImGui::PushFont(font_heading_);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(ui::theme().text_primary.r, ui::theme().text_primary.g, ui::theme().text_primary.b, ui::theme().text_primary.a));
        ImGui::TextUnformatted("Select Theme");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // Theme selection buttons
        for (const auto& theme_name : available_themes) {
            bool is_current = (theme_manager.current_theme_name() == theme_name);
            
            if (is_current) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(ui::theme().accent_muted.r, ui::theme().accent_muted.g, ui::theme().accent_muted.b, ui::theme().accent_muted.a));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(ui::theme().accent.r, ui::theme().accent.g, ui::theme().accent.b, ui::theme().accent.a));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(ui::theme().bg_tertiary.r, ui::theme().bg_tertiary.g, ui::theme().bg_tertiary.b, ui::theme().bg_tertiary.a));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(ui::theme().text_primary.r, ui::theme().text_primary.g, ui::theme().text_primary.b, ui::theme().text_primary.a));
            }
            
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(ui::theme().accent_subtle.r, ui::theme().accent_subtle.g, ui::theme().accent_subtle.b, ui::theme().accent_subtle.a));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(ui::theme().accent_muted.r, ui::theme().accent_muted.g, ui::theme().accent_muted.b, ui::theme().accent_muted.a));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::tokens::RADIUS_MD);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ui::tokens::SPACE_4, ui::tokens::SPACE_3));
            
            // Capitalize first letter for display
            std::string display_name = theme_name;
            if (!display_name.empty()) {
                display_name[0] = std::toupper(display_name[0]);
                // Replace underscores with spaces
                size_t pos = 0;
                while ((pos = display_name.find('_', pos)) != std::string::npos) {
                    display_name.replace(pos, 1, " ");
                    if (pos + 1 < display_name.length()) {
                        display_name[pos + 1] = std::toupper(display_name[pos + 1]);
                    }
                    pos += 1;
                }
            }
            
            if (ImGui::Button(display_name.c_str(), ImVec2(-1, 0))) {
                theme_manager.set_theme(theme_name);
                PLOTIX_LOG_DEBUG("ui", "Theme changed to: " + theme_name);
            }
            
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(4);
            
            ImGui::Spacing();
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // Close button
        ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - 80.0f);
        if (ImGui::Button("Close", ImVec2(80.0f, 0))) {
            is_open = false;
        }
    }
    
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    
    if (!is_open) {
        show_theme_settings_ = false;
    }
}

} // namespace plotix

#endif // PLOTIX_USE_IMGUI
