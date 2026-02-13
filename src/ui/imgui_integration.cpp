#ifdef PLOTIX_USE_IMGUI

#include "imgui_integration.hpp"

#include <plotix/axes.hpp>
#include <plotix/figure.hpp>
#include <plotix/series.hpp>

#include "../render/vulkan/vk_backend.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

// Compressed Inter font data
#include "../../third_party/inter_font.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace plotix {

// ─── Design constants ───────────────────────────────────────────────────────
static constexpr float kIconBarWidth  = 56.0f;
static constexpr float kPanelWidth    = 300.0f;
static constexpr float kIconSize      = 36.0f;
static constexpr float kCornerRadius  = 16.0f;
static constexpr float kMargin        = 12.0f;
static constexpr float kMenubarHeight = 52.0f;

// Enhanced 2026 color palette with better contrast and modern feel
static constexpr ImVec4 kBgSidebar    {0.97f, 0.98f, 1.00f, 0.98f};
static constexpr ImVec4 kBgPanel      {1.00f, 1.00f, 1.00f, 0.98f};
static constexpr ImVec4 kBgMenubar    {0.99f, 0.99f, 1.00f, 0.96f};
static constexpr ImVec4 kTextPrimary  {0.08f, 0.08f, 0.12f, 1.00f};
static constexpr ImVec4 kTextSecondary{0.42f, 0.43f, 0.48f, 1.00f};
static constexpr ImVec4 kTextMenubar  {0.22f, 0.22f, 0.26f, 1.00f};
static constexpr ImVec4 kAccent       {0.12f, 0.38f, 0.78f, 1.00f};
static constexpr ImVec4 kAccentLight  {0.12f, 0.38f, 0.78f, 0.15f};
static constexpr ImVec4 kAccentHover  {0.12f, 0.38f, 0.78f, 0.10f};
static constexpr ImVec4 kDivider      {0.84f, 0.85f, 0.89f, 1.00f};
static constexpr ImVec4 kFrameBg      {0.93f, 0.94f, 0.97f, 1.00f};
static constexpr ImVec4 kFrameHover   {0.87f, 0.88f, 0.92f, 1.00f};
static constexpr ImVec4 kBtnBg        {0.93f, 0.94f, 0.97f, 1.00f};
static constexpr ImVec4 kBtnHover     {0.86f, 0.87f, 0.91f, 1.00f};
static constexpr ImVec4 kTransparent  {0.0f, 0.0f, 0.0f, 0.0f};
static constexpr ImVec4 kMenubarBorder{0.82f, 0.83f, 0.87f, 1.00f};
static constexpr ImVec4 kShadow       {0.0f, 0.0f, 0.0f, 0.08f};
static constexpr ImVec4 kSuccess      {0.12f, 0.68f, 0.42f, 1.00f};
static constexpr ImVec4 kWarning      {0.92f, 0.58f, 0.12f, 1.00f};
static constexpr ImVec4 kError        {0.92f, 0.23f, 0.23f, 1.00f};

// ─── Lifecycle ──────────────────────────────────────────────────────────────

ImGuiIntegration::~ImGuiIntegration() { shutdown(); }

bool ImGuiIntegration::init(VulkanBackend& backend, GLFWwindow* window) {
    if (initialized_) return true;
    if (!window) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

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
    initialized_ = false;
}

void ImGuiIntegration::on_swapchain_recreated(VulkanBackend& backend) {
    if (!initialized_) return;
    ImGui_ImplVulkan_SetMinImageCount(backend.min_image_count());
}

void ImGuiIntegration::new_frame() {
    if (!initialized_) return;
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiIntegration::build_ui(Figure& figure) {
    if (!initialized_) return;

    float dt = ImGui::GetIO().DeltaTime;
    float target = panel_open_ ? 1.0f : 0.0f;
    panel_anim_ += (target - panel_anim_) * std::min(1.0f, 10.0f * dt);
    if (std::abs(panel_anim_ - target) < 0.002f) panel_anim_ = target;

    draw_menubar();
    draw_icon_bar();
    if (panel_anim_ > 0.002f) draw_panel(figure);
}

void ImGuiIntegration::render(VulkanBackend& backend) {
    if (!initialized_) return;
    ImGui::Render();
    auto* dd = ImGui::GetDrawData();
    if (dd) ImGui_ImplVulkan_RenderDrawData(dd, backend.current_command_buffer());
}

bool ImGuiIntegration::wants_capture_mouse() const {
    return initialized_ && ImGui::GetIO().WantCaptureMouse;
}
bool ImGuiIntegration::wants_capture_keyboard() const {
    return initialized_ && ImGui::GetIO().WantCaptureKeyboard;
}

// ─── Fonts ──────────────────────────────────────────────────────────────────

void ImGuiIntegration::load_fonts() {
    ImGuiIO& io = ImGui::GetIO();

    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;  // we own the static data

    cfg.SizePixels = 0;
    font_body_ = io.Fonts->AddFontFromMemoryCompressedTTF(
        InterFont_compressed_data, InterFont_compressed_size, 16.0f, &cfg);

    font_heading_ = io.Fonts->AddFontFromMemoryCompressedTTF(
        InterFont_compressed_data, InterFont_compressed_size, 12.5f, &cfg);

    font_icon_ = io.Fonts->AddFontFromMemoryCompressedTTF(
        InterFont_compressed_data, InterFont_compressed_size, 20.0f, &cfg);

    font_title_ = io.Fonts->AddFontFromMemoryCompressedTTF(
        InterFont_compressed_data, InterFont_compressed_size, 18.0f, &cfg);

    font_menubar_ = io.Fonts->AddFontFromMemoryCompressedTTF(
        InterFont_compressed_data, InterFont_compressed_size, 15.0f, &cfg);

    io.FontDefault = font_body_;
}

// ─── Style ──────────────────────────────────────────────────────────────────

void ImGuiIntegration::apply_modern_style() {
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowPadding     = ImVec2(18, 16);
    s.FramePadding      = ImVec2(12, 8);
    s.ItemSpacing       = ImVec2(12, 8);
    s.ItemInnerSpacing  = ImVec2(10, 6);
    s.ScrollbarSize     = 10.0f;
    s.GrabMinSize       = 10.0f;

    s.WindowRounding    = kCornerRadius;
    s.ChildRounding     = 12.0f;
    s.FrameRounding     = 10.0f;
    s.PopupRounding     = 12.0f;
    s.ScrollbarRounding = 6.0f;
    s.GrabRounding      = 6.0f;
    s.TabRounding       = 10.0f;

    s.WindowBorderSize  = 1.0f;
    s.FrameBorderSize   = 1.0f;
    s.PopupBorderSize   = 1.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]             = kBgPanel;
    c[ImGuiCol_PopupBg]              = kBgPanel;
    c[ImGuiCol_Border]               = kDivider;

    c[ImGuiCol_FrameBg]              = kFrameBg;
    c[ImGuiCol_FrameBgHovered]       = kFrameHover;
    c[ImGuiCol_FrameBgActive]        = {0.86f, 0.87f, 0.90f, 1.0f};

    c[ImGuiCol_Header]               = kAccentLight;
    c[ImGuiCol_HeaderHovered]        = kAccentHover;
    c[ImGuiCol_HeaderActive]         = kAccentLight;

    c[ImGuiCol_Button]               = kBtnBg;
    c[ImGuiCol_ButtonHovered]        = kBtnHover;
    c[ImGuiCol_ButtonActive]         = {0.82f, 0.83f, 0.87f, 1.0f};

    c[ImGuiCol_SliderGrab]           = kAccent;
    c[ImGuiCol_SliderGrabActive]     = {0.20f, 0.42f, 0.78f, 1.0f};

    c[ImGuiCol_CheckMark]            = kAccent;

    c[ImGuiCol_ScrollbarBg]          = kTransparent;
    c[ImGuiCol_ScrollbarGrab]        = {0.80f, 0.81f, 0.84f, 1.0f};
    c[ImGuiCol_ScrollbarGrabHovered] = {0.70f, 0.71f, 0.74f, 1.0f};
    c[ImGuiCol_ScrollbarGrabActive]  = kAccent;

    c[ImGuiCol_Separator]            = kDivider;
    c[ImGuiCol_Text]                 = kTextPrimary;
    c[ImGuiCol_TextDisabled]         = kTextSecondary;

    c[ImGuiCol_Tab]                  = kTransparent;
    c[ImGuiCol_TabHovered]           = kAccentHover;
    c[ImGuiCol_TabSelected]          = kAccentLight;
    c[ImGuiCol_TabSelectedOverline]  = kAccent;
}

// ─── Icon sidebar ───────────────────────────────────────────────────────────

// Helper: draw a clickable icon button with enhanced visual feedback
static bool icon_button(const char* label, bool active, ImFont* font,
                        float size, const ImVec4& accent) {
    ImGui::PushFont(font);

    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(accent.x, accent.y, accent.z, 0.15f));
        ImGui::PushStyleColor(ImGuiCol_Text, accent);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, accent);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, kTransparent);
        ImGui::PushStyleColor(ImGuiCol_Text, kTextSecondary);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, kTransparent);
    }
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(accent.x, accent.y, accent.z, 0.12f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(accent.x, accent.y, accent.z, 0.20f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 8));

    bool clicked = ImGui::Button(label, ImVec2(size, size));

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(5);
    ImGui::PopFont();
    return clicked;
}

void ImGuiIntegration::draw_icon_bar() {
    ImGuiIO& io = ImGui::GetIO();
    float bar_y = kMenubarHeight + kMargin;
    float bar_h = io.DisplaySize.y - bar_y - kMargin;

    ImGui::SetNextWindowPos(ImVec2(kMargin, bar_y));
    ImGui::SetNextWindowSize(ImVec2(kIconBarWidth, bar_h));

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 20));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, kCornerRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 8));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kBgSidebar);
    ImGui::PushStyleColor(ImGuiCol_Border, kDivider);

    if (ImGui::Begin("##iconbar", nullptr, flags)) {
        // Section icons
        auto section_btn = [&](const char* icon, Section sec) {
            bool active = panel_open_ && (active_section_ == sec);
            if (icon_button(icon, active, font_icon_, kIconSize, kAccent)) {
                if (panel_open_ && active_section_ == sec)
                    panel_open_ = false;
                else {
                    panel_open_ = true;
                    active_section_ = sec;
                }
            }
        };

        // Using simple ASCII labels that look clean with Inter
        section_btn("Fig", Section::Figure);
        section_btn("Ser", Section::Series);
        section_btn("Ax",  Section::Axes);

        // Push remaining icons to bottom
        float bottom_y = ImGui::GetWindowHeight() - kIconSize - ImGui::GetStyle().WindowPadding.y;
        if (ImGui::GetCursorPosY() < bottom_y) {
            ImGui::SetCursorPosY(bottom_y);
        }

        // FPS at bottom of icon bar
        ImGui::PushFont(font_heading_);
        ImGui::PushStyleColor(ImGuiCol_Text, kTextSecondary);
        char fps[16];
        std::snprintf(fps, sizeof(fps), "%d", static_cast<int>(io.Framerate));
        float tw = ImGui::CalcTextSize(fps).x;
        ImGui::SetCursorPosX((kIconBarWidth - tw) * 0.5f - ImGui::GetStyle().WindowPadding.x * 0.5f);
        ImGui::TextUnformatted(fps);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

// ─── Modern Menubar ───────────────────────────────────────────────────────────

void ImGuiIntegration::draw_menubar() {
    ImGuiIO& io = ImGui::GetIO();
    float menubar_width = io.DisplaySize.x - kMargin * 2;
    
    ImGui::SetNextWindowPos(ImVec2(kMargin, kMargin));
    ImGui::SetNextWindowSize(ImVec2(menubar_width, kMenubarHeight));
    
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;
    
    // Enhanced styling for 2026 modern look
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28, 16));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, kCornerRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(32, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kBgMenubar);
    ImGui::PushStyleColor(ImGuiCol_Border, kMenubarBorder);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    
    if (ImGui::Begin("##menubar", nullptr, flags)) {
        // App title/brand on the left with enhanced accent and icon
        ImGui::PushFont(font_title_);
        ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
        ImGui::TextUnformatted("◆ Plotix");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        
        ImGui::SameLine();
        ImGui::SetCursorPosX(130.0f); // Fixed position after title
        
        // Subtle separator
        ImGui::PushStyleColor(ImGuiCol_Separator, kDivider);
        ImGui::Separator();
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::SetCursorPosX(150.0f); // Fixed position after separator
        
        // File menu with enhanced icons and better descriptions
        draw_menubar_menu("File", {
            MenuItem("� Export PNG", []() { /* TODO: Export functionality */ }),
            MenuItem("� Export SVG", []() { /* TODO: Export functionality */ }),
            MenuItem("🎬 Export Video", []() { /* TODO: Video export functionality */ }),
            MenuItem("", nullptr), // Separator
            MenuItem("🚪 Exit", []() { /* TODO: Exit functionality */ })
        });
        
        ImGui::SameLine();
        
        // View menu with better options
        draw_menubar_menu("View", {
            MenuItem("🎛️ Toggle Panel", [this]() { panel_open_ = !panel_open_; }),
            MenuItem("� Zoom to Fit", []() { /* TODO: Zoom to fit functionality */ }),
            MenuItem("�� Reset View", []() { /* TODO: Reset view functionality */ }),
            MenuItem("📐 Toggle Grid", []() { /* TODO: Grid toggle functionality */ })
        });
        
        ImGui::SameLine();
        
        // Tools menu with enhanced functionality
        draw_menubar_menu("Tools", {
            MenuItem("📸 Screenshot", []() { /* TODO: Screenshot functionality */ }),
            MenuItem("⚡ Performance Monitor", []() { /* TODO: Performance monitor */ }),
            MenuItem("🎨 Theme Settings", []() { /* TODO: Theme settings */ }),
            MenuItem("🔧 Preferences", []() { /* TODO: Preferences dialog */ })
        });
        
        // Push status info to the right with enhanced formatting
        ImGui::SameLine(0.0f, ImGui::GetContentRegionAvail().x - 220.0f);
        
        // Enhanced status info with better icons and formatting
        ImGui::PushFont(font_menubar_);
        ImGui::PushStyleColor(ImGuiCol_Text, kTextSecondary);
        
        char status[128];
        std::snprintf(status, sizeof(status), "🖥️ %d×%d | ⚡ %.0f FPS | 📊 GPU", 
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

// Helper for drawing dropdown menus
void ImGuiIntegration::draw_menubar_menu(const char* label, const std::vector<MenuItem>& items) {
    ImGui::PushFont(font_menubar_);
    ImGui::PushStyleColor(ImGuiCol_Text, kTextMenubar);
    ImGui::PushStyleColor(ImGuiCol_Button, kTransparent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccentLight);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    
    if (ImGui::Button(label)) {
        ImGui::OpenPopup(label);
    }
    
    // Enhanced popup styling for modern look
    if (ImGui::BeginPopup(label)) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 8));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 4));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, kBgPanel);
        ImGui::PushStyleColor(ImGuiCol_Border, kMenubarBorder);
        
        for (const auto& item : items) {
            if (item.label.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Separator, kDivider);
                ImGui::Separator();
                ImGui::PopStyleColor();
            } else {
                // Enhanced menu item styling
                ImGui::PushStyleColor(ImGuiCol_Text, kTextPrimary);
                ImGui::PushStyleColor(ImGuiCol_Header, kTransparent);
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, kAccentHover);
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, kAccentLight);
                
                if (ImGui::MenuItem(item.label.c_str())) {
                    if (item.callback) item.callback();
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

// ─── Expandable panel ───────────────────────────────────────────────────────

static void section_header(const char* text, ImFont* font) {
    ImGui::PushFont(font);
    ImGui::PushStyleColor(ImGuiCol_Text, kTextSecondary);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Spacing();
    ImGui::Spacing();
}

void ImGuiIntegration::draw_panel(Figure& figure) {
    ImGuiIO& io = ImGui::GetIO();
    float panel_y = kMenubarHeight + kMargin;
    float panel_h = io.DisplaySize.y - panel_y - kMargin;

    // Enhanced slide animation with easing
    float final_x = kMargin + kIconBarWidth + 8.0f;
    float start_x = final_x - kPanelWidth * 0.4f;
    float eased_anim = panel_anim_ * panel_anim_ * (3.0f - 2.0f * panel_anim_); // Smooth step
    float cur_x = start_x + (final_x - start_x) * eased_anim;

    ImGui::SetNextWindowPos(ImVec2(cur_x, panel_y));
    ImGui::SetNextWindowSize(ImVec2(kPanelWidth, panel_h));

    // Enhanced fade effects
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, panel_anim_);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 20));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, kCornerRadius);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kBgPanel);
    ImGui::PushStyleColor(ImGuiCol_Border, kDivider);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;

    if (ImGui::Begin("##panel", nullptr, flags)) {
        // Enhanced title with subtle accent
        const char* titles[] = {"Figure", "Series", "Axes"};
        ImGui::PushFont(font_title_);
        ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
        ImGui::TextUnformatted(titles[static_cast<int>(active_section_)]);
        ImGui::PopStyleColor();
        ImGui::PopFont();

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Separator, kDivider);
        ImGui::Separator();
        ImGui::PopStyleColor();
        ImGui::Spacing();

        // Content
        switch (active_section_) {
            case Section::Figure: draw_section_figure(figure); break;
            case Section::Series: draw_section_series(figure); break;
            case Section::Axes:   draw_section_axes(figure);   break;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
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
    ImGui::PushStyleColor(ImGuiCol_FrameBg, kFrameBg);
    ImGui::DragFloat("##mt", &sty.margin_top, 0.5f, 0.0f, 200.0f, "Top  %.0fpx");
    ImGui::PopStyleColor();
    
    // Bottom margin  
    ImGui::PushStyleColor(ImGuiCol_FrameBg, kFrameBg);
    ImGui::DragFloat("##mb", &sty.margin_bottom, 0.5f, 0.0f, 200.0f, "Bottom  %.0fpx");
    ImGui::PopStyleColor();
    
    // Left margin
    ImGui::PushStyleColor(ImGuiCol_FrameBg, kFrameBg);
    ImGui::DragFloat("##ml", &sty.margin_left, 0.5f, 0.0f, 200.0f, "Left  %.0fpx");
    ImGui::PopStyleColor();
    
    // Right margin
    ImGui::PushStyleColor(ImGuiCol_FrameBg, kFrameBg);
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
    ImGui::PushStyleColor(ImGuiCol_FrameBg, kFrameBg);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, kAccent);
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
    ImGui::PushStyleColor(ImGuiCol_Button, kBtnBg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kBtnHover);
    ImGui::PushStyleColor(ImGuiCol_Text, kTextPrimary);
    
    if (ImGui::Button("Reset to Defaults", ImVec2(-1, 0))) {
        // Reset figure style to defaults
        sty.background = Color{1.0f, 1.0f, 1.0f, 1.0f};
        sty.margin_top = 40.0f;
        sty.margin_bottom = 40.0f;
        sty.margin_left = 60.0f;
        sty.margin_right = 20.0f;
        leg.visible = true;
        leg.position = LegendPosition::TopRight;
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

        if (ImGui::Button("Auto-fit", ImVec2(-1, 0)))
            ax->auto_fit();

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

} // namespace plotix

#endif // PLOTIX_USE_IMGUI
