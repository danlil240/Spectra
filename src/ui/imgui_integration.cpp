#ifdef PLOTIX_USE_IMGUI

#include "imgui_integration.hpp"
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

void ImGuiIntegration::update_layout(float window_width, float window_height) {
    if (layout_manager_) {
        layout_manager_->update(window_width, window_height);
    }
}

void ImGuiIntegration::new_frame() {
    if (!initialized_) return;
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    
    // Update layout with current window size
    ImGuiIO& io = ImGui::GetIO();
    update_layout(io.DisplaySize.x, io.DisplaySize.y);
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
    
    // Only capture mouse if we're actually interacting with UI elements
    // Not just because ImGui exists
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

// ─── Section Drawing Methods (Legacy - to be replaced by Agent C) ─────────────

static void section_header(const char* text, ImFont* font) {
    ImGui::PushFont(font);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(ui::theme().text_secondary.r, ui::theme().text_secondary.g, ui::theme().text_secondary.b, ui::theme().text_secondary.a));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Spacing();
    ImGui::Spacing();
}

// ─── Figure section ─────────────────────────────────────────────────────────

void ImGuiIntegration::draw_section_figure(Figure& figure) {
    auto& sty = figure.style();

    section_header("BACKGROUND", font_heading_);

    // Enhanced color picker with better layout
    float bg[4] = {sty.background.r, sty.background.g, sty.background.b, sty.background.a};
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12, 8));
    
    if (ImGui::ColorEdit4("##bg", bg,
            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel |
            ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_DisplayHSV)) {
        sty.background = Color{bg[0], bg[1], bg[2], bg[3]};
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Background Color");
    
    ImGui::PopStyleVar(2);

    ImGui::Spacing();
    ImGui::Spacing();
    section_header("MARGINS", font_heading_);

    // Enhanced margin controls with better visual feedback
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 6.0f);
    ImGui::PushItemWidth(-1);
    
    // Top margin
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(ui::theme().bg_tertiary.r, ui::theme().bg_tertiary.g, ui::theme().bg_tertiary.b, ui::theme().bg_tertiary.a));
    ImGui::DragFloat("##mt", &sty.margin_top, 0.5f, 0.0f, 200.0f, "Top  %.0fpx");
    ImGui::PopStyleColor();
    
    // Bottom margin  
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(ui::theme().bg_tertiary.r, ui::theme().bg_tertiary.g, ui::theme().bg_tertiary.b, ui::theme().bg_tertiary.a));
    ImGui::DragFloat("##mb", &sty.margin_bottom, 0.5f, 0.0f, 200.0f, "Bottom  %.0fpx");
    ImGui::PopStyleColor();
    
    // Left margin
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(ui::theme().bg_tertiary.r, ui::theme().bg_tertiary.g, ui::theme().bg_tertiary.b, ui::theme().bg_tertiary.a));
    ImGui::DragFloat("##ml", &sty.margin_left, 0.5f, 0.0f, 200.0f, "Left  %.0fpx");
    ImGui::PopStyleColor();
    
    // Right margin
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(ui::theme().bg_tertiary.r, ui::theme().bg_tertiary.g, ui::theme().bg_tertiary.b, ui::theme().bg_tertiary.a));
    ImGui::DragFloat("##mr", &sty.margin_right, 0.5f, 0.0f, 200.0f, "Right  %.0fpx");
    ImGui::PopStyleColor();
    
    ImGui::PopItemWidth();
    ImGui::PopStyleVar(2);

    ImGui::Spacing();
    ImGui::Spacing();
    section_header("LEGEND", font_heading_);

    // Enhanced legend controls
    auto& leg = figure.legend();
    
    // Custom checkbox with better styling
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(ui::theme().bg_tertiary.r, ui::theme().bg_tertiary.g, ui::theme().bg_tertiary.b, ui::theme().bg_tertiary.a));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(ui::theme().accent.r, ui::theme().accent.g, ui::theme().accent.b, ui::theme().accent.a));
    if (ImGui::Checkbox("Show Legend", &leg.visible)) {
        // Legend visibility changed
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();

    ImGui::Spacing();
    
    // Enhanced position selector
    const char* positions[] = {"Top Right", "Top Left", "Bottom Right", "Bottom Left", "Hidden"};
    int pos = static_cast<int>(leg.position);
    
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
    ImGui::PushItemWidth(-1);
    
    if (ImGui::Combo("##legpos", &pos, positions, 5)) {
        leg.position = static_cast<LegendPosition>(pos);
    }
    
    ImGui::PopItemWidth();
    ImGui::PopStyleVar(2);
    
    // Quick action buttons
    ImGui::Spacing();
    ImGui::Spacing();
    section_header("QUICK ACTIONS", font_heading_);
    
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));
    
    // Reset to defaults button
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(ui::theme().bg_tertiary.r, ui::theme().bg_tertiary.g, ui::theme().bg_tertiary.b, ui::theme().bg_tertiary.a));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(ui::theme().accent_subtle.r, ui::theme().accent_subtle.g, ui::theme().accent_subtle.b, ui::theme().accent_subtle.a));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(ui::theme().text_primary.r, ui::theme().text_primary.g, ui::theme().text_primary.b, ui::theme().text_primary.a));
    
    if (ImGui::Button("Reset to Defaults", ImVec2(-1, 0))) {
        PLOTIX_LOG_DEBUG("ui_button", "Reset to Defaults button clicked");
        // Reset figure style to defaults
        sty.background = Color{1.0f, 1.0f, 1.0f, 1.0f};
        sty.margin_top = 40.0f;
        sty.margin_bottom = 40.0f;
        sty.margin_left = 60.0f;
        sty.margin_right = 20.0f;
        leg.visible = true;
        leg.position = LegendPosition::TopRight;
        PLOTIX_LOG_DEBUG("ui_button", "Figure style reset to defaults completed");
    }
    
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
}

// ─── Series section ─────────────────────────────────────────────────────────

void ImGuiIntegration::draw_section_series(Figure& figure) {
    int id = 0;
    for (auto& ax : figure.axes_mut()) {
        if (!ax) continue;
        for (auto& s : ax->series_mut()) {
            if (!s) continue;
            ImGui::PushID(id++);

            const char* name = s->label().empty() ? "Unnamed" : s->label().c_str();
            float col[4] = {s->color().r, s->color().g, s->color().b, s->color().a};

            // Color swatch + label row
            if (ImGui::ColorEdit4("##c", col,
                    ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
                s->set_color(Color{col[0], col[1], col[2], col[3]});
            }
            ImGui::SameLine();

            bool vis = s->visible();
            if (ImGui::Checkbox(name, &vis)) {
                s->visible(vis);
            }

            // Type-specific slider
            ImGui::PushItemWidth(-1);
            if (auto* line = dynamic_cast<LineSeries*>(s.get())) {
                float w = line->width();
                if (ImGui::SliderFloat("##lw", &w, 0.5f, 12.0f, "Width %.1f")) {
                    line->width(w);
                }
            }
            if (auto* scatter = dynamic_cast<ScatterSeries*>(s.get())) {
                float sz = scatter->size();
                if (ImGui::SliderFloat("##ps", &sz, 0.5f, 30.0f, "Size %.1f")) {
                    scatter->size(sz);
                }
            }
            ImGui::PopItemWidth();

            ImGui::Spacing();
            ImGui::PopID();
        }
    }
}

// ─── Axes section ───────────────────────────────────────────────────────────

void ImGuiIntegration::draw_section_axes(Figure& figure) {
    int idx = 0;
    for (auto& ax : figure.axes_mut()) {
        if (!ax) { idx++; continue; }
        ImGui::PushID(idx);

        if (idx > 0) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }

        char hdr[32];
        std::snprintf(hdr, sizeof(hdr), "AXES %d", idx + 1);
        section_header(hdr, font_heading_);

        bool grid = ax->grid_enabled();
        if (ImGui::Checkbox("Grid", &grid)) ax->set_grid_enabled(grid);
        ImGui::SameLine();
        bool border = ax->border_enabled();
        if (ImGui::Checkbox("Border", &border)) ax->set_border_enabled(border);

        auto xlim = ax->x_limits();
        auto ylim = ax->y_limits();
        float xr[2] = {xlim.min, xlim.max};
        float yr[2] = {ylim.min, ylim.max};

        ImGui::PushItemWidth(-1);
        if (ImGui::DragFloat2("##xl", xr, 0.01f, 0.0f, 0.0f, "X  %.3f"))
            ax->xlim(xr[0], xr[1]);
        if (ImGui::DragFloat2("##yl", yr, 0.01f, 0.0f, 0.0f, "Y  %.3f"))
            ax->ylim(yr[0], yr[1]);
        ImGui::PopItemWidth();

        if (ImGui::Button("Auto-fit", ImVec2(-1, 0))) {
            PLOTIX_LOG_DEBUG("ui_button", "Auto-fit button clicked for axis " + std::to_string(idx));
            ax->auto_fit();
            PLOTIX_LOG_DEBUG("ui_button", "Auto-fit completed for axis " + std::to_string(idx));
        }

        const char* modes[] = {"Fit", "Tight", "Padded", "Manual"};
        int mode = static_cast<int>(ax->get_autoscale_mode());
        ImGui::PushItemWidth(-1);
        if (ImGui::Combo("##as", &mode, modes, 4))
            ax->autoscale_mode(static_cast<AutoscaleMode>(mode));
        ImGui::PopItemWidth();

        auto& as = ax->axis_style();
        float gc[4] = {as.grid_color.r, as.grid_color.g, as.grid_color.b, as.grid_color.a};
        if (ImGui::ColorEdit4("##gc", gc,
                ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
            as.grid_color = Color{gc[0], gc[1], gc[2], gc[3]};
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("Grid Color");

        ImGui::PopID();
        idx++;
    }
}

// ─── New Layout-Based Drawing Methods ─────────────────────────────────────────

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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28, 16));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::tokens::RADIUS_MD);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(32, 0));
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
                layout_manager_->set_inspector_visible(!layout_manager_->is_inspector_visible()); 
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
    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y));
    ImGui::SetNextWindowSize(ImVec2(bounds.w, bounds.h));
    
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui::tokens::SPACE_4, ui::tokens::SPACE_4));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::tokens::RADIUS_MD);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, ui::tokens::SPACE_2));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(ui::theme().bg_secondary.r, ui::theme().bg_secondary.g, ui::theme().bg_secondary.b, ui::ThemeManager::instance().current().opacity_panel));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(ui::theme().border_default.r, ui::theme().border_default.g, ui::theme().border_default.b, ui::theme().border_default.a));

    if (ImGui::Begin("##navrail", nullptr, flags)) {
        // Navigation icons (simplified for now)
        auto nav_btn = [&](ui::Icon icon, const char* tooltip, Section section) {
            bool is_active = panel_open_ && active_section_ == section;
            if (icon_button(ui::icon_str(icon), is_active, font_icon_, ui::tokens::ICON_LG)) {
                if (is_active) {
                    panel_open_ = false;
                } else {
                    active_section_ = section;
                    panel_open_ = true;
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tooltip);
            }
        };

        nav_btn(ui::Icon::ScatterChart, "Figures", Section::Figure);
        nav_btn(ui::Icon::ChartLine, "Series", Section::Series);
        nav_btn(ui::Icon::Axes, "Axes", Section::Axes);
        nav_btn(ui::Icon::Wrench, "Tools", Section::Axes);

        // Push remaining icons to bottom
        float bottom_y = ImGui::GetWindowHeight() - ui::tokens::ICON_LG - ImGui::GetStyle().WindowPadding.y;
        if (ImGui::GetCursorPosY() < bottom_y) {
            ImGui::SetCursorPosY(bottom_y);
        }

        // Settings at bottom
        icon_button(ui::icon_str(ui::Icon::Settings), false, font_icon_, ui::tokens::ICON_LG);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", "Settings");
        }
        icon_button(ui::icon_str(ui::Icon::Help), false, font_icon_, ui::tokens::ICON_LG);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", "Help");
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
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
    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y));
    ImGui::SetNextWindowSize(ImVec2(bounds.w, bounds.h));
    
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 20));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::tokens::RADIUS_MD);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(ui::theme().bg_secondary.r, ui::theme().bg_secondary.g, ui::theme().bg_secondary.b, ui::theme().bg_secondary.a));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(ui::theme().border_default.r, ui::theme().border_default.g, ui::theme().border_default.b, ui::theme().border_default.a));

    if (ImGui::Begin("##inspector", nullptr, flags)) {
        ImGui::SetCursorScreenPos(ImVec2(bounds.x - 3.0f, bounds.y));
        ImGui::InvisibleButton("##inspector_resize", ImVec2(6.0f, bounds.h));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        if (ImGui::IsItemActive()) {
            float right_edge = bounds.x + bounds.w;
            float new_width = right_edge - ImGui::GetIO().MousePos.x;
            layout_manager_->set_inspector_width(new_width);
        }

        // Inspector header
        ImGui::PushFont(font_title_);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(ui::theme().text_primary.r, ui::theme().text_primary.g, ui::theme().text_primary.b, ui::theme().text_primary.a));
        ImGui::TextUnformatted("Inspector");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        
        ImGui::Separator();
        
        // For now, use the legacy panel drawing logic
        // This will be replaced by Agent C's inspector system
        if (panel_open_) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, panel_anim_);
            
            switch (active_section_) {
                case Section::Figure:
                    draw_section_figure(figure);
                    break;
                case Section::Series:
                    draw_section_series(figure);
                    break;
                case Section::Axes:
                    draw_section_axes(figure);
                    break;
            }
            
            ImGui::PopStyleVar();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
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
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(ui::theme().bg_secondary.r, ui::theme().bg_secondary.g, ui::theme().bg_secondary.b, ui::theme().bg_secondary.a));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(ui::theme().border_default.r, ui::theme().border_default.g, ui::theme().border_default.b, ui::theme().border_default.a));
    
    if (ImGui::Begin("##statusbar", nullptr, flags)) {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::PushFont(font_heading_);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(ui::theme().text_secondary.r, ui::theme().text_secondary.g, ui::theme().text_secondary.b, ui::theme().text_secondary.a));
        
        // Left side: cursor coordinates (placeholder)
        ImGui::TextUnformatted("X: 0.000  Y: 0.000");
        
        ImGui::SameLine(0.0f, ImGui::GetContentRegionAvail().x - 100.0f);
        
        // Right side: performance info
        char fps[16];
        std::snprintf(fps, sizeof(fps), "%d fps", static_cast<int>(io.Framerate));
        ImGui::TextUnformatted(fps);
        
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
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
    
    // Frosted glass effect
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 20.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(ui::theme().bg_elevated.r, ui::theme().bg_elevated.g, ui::theme().bg_elevated.b, ui::ThemeManager::instance().current().opacity_panel));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(ui::theme().border_default.r, ui::theme().border_default.g, ui::theme().border_default.b, ui::theme().border_default.a));
    
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
    ImGui::PopStyleVar(2);
}

} // namespace plotix

#endif // PLOTIX_USE_IMGUI
