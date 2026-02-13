#pragma once

#ifdef PLOTIX_USE_IMGUI

#include <plotix/fwd.hpp>
#include "input.hpp"
#include <functional>
#include <vector>
#include <string>

struct GLFWwindow;
struct ImFont;

namespace plotix {

class VulkanBackend;

class ImGuiIntegration {
public:
    struct MenuItem {
        std::string label;
        std::function<void()> callback;
        MenuItem(const std::string& l, std::function<void()> cb = nullptr) 
            : label(l), callback(cb) {}
    };

    ImGuiIntegration() = default;
    ~ImGuiIntegration();

    ImGuiIntegration(const ImGuiIntegration&) = delete;
    ImGuiIntegration& operator=(const ImGuiIntegration&) = delete;

    bool init(VulkanBackend& backend, GLFWwindow* window);
    void shutdown();

    void new_frame();
    void build_ui(Figure& figure);
    void render(VulkanBackend& backend);

    void on_swapchain_recreated(VulkanBackend& backend);

    bool wants_capture_mouse() const;
    bool wants_capture_keyboard() const;
    
    // Interaction state getters
    bool should_reset_view() const { return reset_view_; }
    void clear_reset_view() { reset_view_ = false; }
    InteractionMode get_interaction_mode() const { return interaction_mode_; }

private:
    void apply_modern_style();
    void load_fonts();

    void draw_icon_bar();
    void draw_menubar();
    void draw_menubar_menu(const char* label, const std::vector<MenuItem>& items);
    void draw_toolbar_button(const char* icon, std::function<void()> callback, const char* tooltip);
    void draw_panel(Figure& figure);
    void draw_section_figure(Figure& figure);
    void draw_section_series(Figure& figure);
    void draw_section_axes(Figure& figure);

    bool initialized_ = false;
    bool panel_open_   = false;

    enum class Section { Figure, Series, Axes };
    Section active_section_ = Section::Figure;

    float panel_anim_ = 0.0f;

    // Fonts at different sizes
    ImFont* font_body_    = nullptr;  // 16px — body text, controls
    ImFont* font_heading_ = nullptr;  // 12.5px — section headers (uppercase)
    ImFont* font_icon_    = nullptr;  // 20px — icon bar symbols
    ImFont* font_title_   = nullptr;  // 18px — panel title
    ImFont* font_menubar_ = nullptr;  // 15px — menubar items
    
    // Menubar state
    float menubar_height_ = 52.0f;
    
    // Interaction state
    bool reset_view_ = false;
    InteractionMode interaction_mode_ = InteractionMode::Pan;
};

} // namespace plotix

#endif // PLOTIX_USE_IMGUI
