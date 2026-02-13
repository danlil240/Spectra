#pragma once

#ifdef PLOTIX_USE_IMGUI

#include <plotix/fwd.hpp>
#include "input.hpp"
#include "layout_manager.hpp"
#include <functional>
#include <vector>
#include <string>
#include <memory>

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
    
    // Layout management
    LayoutManager& get_layout_manager() { return *layout_manager_; }
    void update_layout(float window_width, float window_height);

    bool wants_capture_mouse() const;
    bool wants_capture_keyboard() const;
    
    // Interaction state getters
    bool should_reset_view() const { return reset_view_; }
    void clear_reset_view() { reset_view_ = false; }
    ToolMode get_interaction_mode() const { return interaction_mode_; }

private:
    void apply_modern_style();
    void load_fonts();

    void draw_command_bar();
    void draw_nav_rail();
    void draw_canvas(Figure& figure);
    void draw_inspector(Figure& figure);
    void draw_status_bar();
    void draw_floating_toolbar();
    
    void draw_toolbar_button(const char* icon, std::function<void()> callback, const char* tooltip, bool is_active = false);
    void draw_menubar_menu(const char* label, const std::vector<MenuItem>& items);
    void draw_section_figure(Figure& figure);
    void draw_section_series(Figure& figure);
    void draw_section_axes(Figure& figure);
    
    // Legacy methods (to be removed after Agent C migration)
    void draw_menubar();
    void draw_icon_bar();
    void draw_panel(Figure& figure);

    bool initialized_ = false;
    std::unique_ptr<LayoutManager> layout_manager_;

    // Panel state (legacy, will be replaced by inspector)
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
    
    // Interaction state
    bool reset_view_ = false;
    ToolMode interaction_mode_ = ToolMode::Pan;
};

} // namespace plotix

#endif // PLOTIX_USE_IMGUI
