#ifdef PLOTIX_USE_IMGUI

#include "imgui_integration.hpp"
#include "data_interaction.hpp"
#include "theme.hpp"
#include "design_tokens.hpp"
#include "icons.hpp"

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
    float target = panel_open_ ? 1.0f : 0.0f;
    panel_anim_ += (target - panel_anim_) * std::min(1.0f, 10.0f * dt);
    if (std::abs(panel_anim_ - target) < 0.002f) panel_anim_ = target;

    // Draw all zones using layout manager
    draw_command_bar();
    draw_nav_rail();
    draw_canvas(figure);
    if (layout_manager_->is_inspector_visible()) {
        draw_inspector(figure);
    }
    draw_status_bar();
    draw_floating_toolbar();

    // Draw data interaction overlays (tooltip, crosshair, markers) on top of everything
    if (data_interaction_) {
        ImGuiIO& io = ImGui::GetIO();
        data_interaction_->draw_overlays(io.DisplaySize.x, io.DisplaySize.y);
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
        
        // Mouse mode buttons
        draw_toolbar_button(ui::icon_str(ui::Icon::Hand), [this]() { 
            PLOTIX_LOG_DEBUG("ui_button", "Pan mode button clicked");
            interaction_mode_ = ToolMode::Pan; 
            PLOTIX_LOG_DEBUG("ui_button", "Tool mode set to Pan");
        }, "Pan Mode", interaction_mode_ == ToolMode::Pan);
        
        ImGui::SameLine();
        
        draw_toolbar_button(ui::icon_str(ui::Icon::ZoomIn), [this]() { 
            PLOTIX_LOG_DEBUG("ui_button", "Box zoom mode button clicked");
            interaction_mode_ = ToolMode::BoxZoom; 
            PLOTIX_LOG_DEBUG("ui_button", "Tool mode set to BoxZoom");
        }, "Box Zoom Mode", interaction_mode_ == ToolMode::BoxZoom);
        
        ImGui::SameLine();
        
        // Subtle separator
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(ui::theme().border_default.r, ui::theme().border_default.g, ui::theme().border_default.b, ui::theme().border_default.a));
        ImGui::Separator();
        ImGui::PopStyleColor();
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
            MenuItem("Theme Settings", []() { /* TODO: Theme settings */ }),
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

    float rail_w = bounds.w;
    float btn_size = 32.0f;

    // Use minimal padding so ImGui doesn't inflate the window beyond bounds.w
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, ui::tokens::SPACE_3));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(ui::theme().bg_secondary.r, ui::theme().bg_secondary.g, ui::theme().bg_secondary.b, ui::theme().bg_secondary.a));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(ui::theme().border_default.r, ui::theme().border_default.g, ui::theme().border_default.b, ui::theme().border_default.a));

    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(bounds.w, bounds.h), ImGuiCond_Always);

    if (ImGui::Begin("##navrail", nullptr, flags)) {
        // Manually center icons: offset cursor to center the button in the rail
        float pad_x = std::max(0.0f, (rail_w - btn_size) * 0.5f);
        float pad_y = ui::tokens::SPACE_3;

        auto nav_btn = [&](ui::Icon icon, const char* tooltip, Section section) {
            ImGui::SetCursorPosX(pad_x);
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

        ImGui::SetCursorPosY(pad_y);
        nav_btn(ui::Icon::ScatterChart, "Figures", Section::Figure);
        nav_btn(ui::Icon::ChartLine, "Series", Section::Series);
        nav_btn(ui::Icon::Axes, "Axes", Section::Axes);

        // Push bottom icons: reserve space for 2 buttons + spacing
        float bottom_space = btn_size * 2.0f + ui::tokens::SPACE_3;
        float bottom_y = ImGui::GetWindowHeight() - bottom_space - pad_y;
        if (ImGui::GetCursorPosY() < bottom_y) {
            ImGui::SetCursorPosY(bottom_y);
        }

        // Settings at bottom
        ImGui::SetCursorPosX(pad_x);
        icon_button(ui::icon_str(ui::Icon::Settings), false, font_icon_, btn_size);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", "Settings");
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
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
                    // Keep existing series selection if valid, else default to figure
                    if (selection_ctx_.type != ui::SelectionType::Series) {
                        selection_ctx_.select_figure(&figure);
                    }
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

void ImGuiIntegration::draw_floating_toolbar() {
    if (!layout_manager_) return;
    
    Rect bounds = layout_manager_->floating_toolbar_rect();
    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y));
    ImGui::SetNextWindowSize(ImVec2(bounds.w, bounds.h));
    
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;
    
    // Solid elevated background with rounded pill shape
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 20.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(ui::theme().bg_elevated.r, ui::theme().bg_elevated.g, ui::theme().bg_elevated.b, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(ui::theme().border_default.r, ui::theme().border_default.g, ui::theme().border_default.b, 0.6f));
    
    if (ImGui::Begin("##floatingtoolbar", nullptr, flags)) {
        // Quick access tools
        draw_toolbar_button(ui::icon_str(ui::Icon::ZoomIn), [this]() { interaction_mode_ = ToolMode::BoxZoom; }, "Zoom", interaction_mode_ == ToolMode::BoxZoom);
        ImGui::SameLine();
        draw_toolbar_button(ui::icon_str(ui::Icon::Hand), [this]() { interaction_mode_ = ToolMode::Pan; }, "Pan", interaction_mode_ == ToolMode::Pan);
        ImGui::SameLine();
        draw_toolbar_button(ui::icon_str(ui::Icon::Ruler), []() { /* TODO: Measure mode */ }, "Measure");
        ImGui::SameLine();
        draw_toolbar_button(ui::icon_str(ui::Icon::Crosshair), []() { /* TODO: Crosshair */ }, "Crosshair");
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

} // namespace plotix

#endif // PLOTIX_USE_IMGUI
