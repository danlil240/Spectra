#pragma once

#ifdef PLOTIX_USE_IMGUI

#include <plotix/fwd.hpp>
#include "input.hpp"
#include "layout_manager.hpp"
#include "inspector.hpp"
#include "selection_context.hpp"
#include <functional>
#include <vector>
#include <string>
#include <memory>

struct GLFWwindow;
struct ImFont;

namespace plotix {

class DataInteraction;
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
    void update_layout(float window_width, float window_height, float dt = 0.0f);

    bool wants_capture_mouse() const;
    bool wants_capture_keyboard() const;
    
    // Interaction state getters
    bool should_reset_view() const { return reset_view_; }
    void clear_reset_view() { reset_view_ = false; }
    ToolMode get_interaction_mode() const { return interaction_mode_; }
    
    // Status bar data setters (called by app loop with real data)
    void set_cursor_data(float x, float y) { cursor_data_x_ = x; cursor_data_y_ = y; }
    void set_zoom_level(float zoom) { zoom_level_ = zoom; }
    void set_gpu_time(float ms) { gpu_time_ms_ = ms; }

    // Data interaction layer (owned externally by App)
    void set_data_interaction(DataInteraction* di) { data_interaction_ = di; }
    DataInteraction* data_interaction() const { return data_interaction_; }

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
    // Legacy methods (to be removed after full migration)
    void draw_menubar();
    void draw_icon_bar();
    void draw_panel(Figure& figure);

    bool initialized_ = false;
    std::unique_ptr<LayoutManager> layout_manager_;

    // Inspector system (Agent C)
    ui::Inspector inspector_;
    ui::SelectionContext selection_ctx_;

    // Panel state
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
    
    // Status bar data
    float cursor_data_x_ = 0.0f;
    float cursor_data_y_ = 0.0f;
    float zoom_level_ = 1.0f;
    float gpu_time_ms_ = 0.0f;

    // Data interaction layer (not owned)
    DataInteraction* data_interaction_ = nullptr;
};

} // namespace plotix

#endif // PLOTIX_USE_IMGUI
