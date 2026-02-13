#pragma once

#ifdef PLOTIX_USE_IMGUI

#include <plotix/fwd.hpp>

struct GLFWwindow;
struct ImFont;

namespace plotix {

class VulkanBackend;

class ImGuiIntegration {
public:
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

private:
    void apply_modern_style();
    void load_fonts();

    void draw_icon_bar();
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
    ImFont* font_body_    = nullptr;  // 15px — body text, controls
    ImFont* font_heading_ = nullptr;  // 13px — section headers (uppercase)
    ImFont* font_icon_    = nullptr;  // 20px — icon bar symbols
    ImFont* font_title_   = nullptr;  // 18px — panel title
};

} // namespace plotix

#endif // PLOTIX_USE_IMGUI
