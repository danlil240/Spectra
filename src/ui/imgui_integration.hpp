#pragma once

#ifdef PLOTIX_USE_IMGUI

#include <plotix/fwd.hpp>
#include "input.hpp"
#include "layout_manager.hpp"
#include "inspector.hpp"
#include "selection_context.hpp"
#include "dock_system.hpp"
#include <functional>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>

struct GLFWwindow;
struct ImFont;

namespace plotix {

class BoxZoomOverlay;
class CommandPalette;
class CommandRegistry;
class DataInteraction;
class DockSystem;
class ShortcutManager;
class UndoManager;
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
    bool is_tab_interacting() const { return pane_tab_hovered_ || pane_tab_drag_.dragging; }
    
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

    // Box zoom overlay (Agent B Week 7, owned externally by App)
    void set_box_zoom_overlay(BoxZoomOverlay* bzo) { box_zoom_overlay_ = bzo; }
    BoxZoomOverlay* box_zoom_overlay() const { return box_zoom_overlay_; }

    // Command palette & productivity (Agent F, owned externally by App)
    void set_command_palette(CommandPalette* cp) { command_palette_ = cp; }
    void set_command_registry(CommandRegistry* cr) { command_registry_ = cr; }
    void set_shortcut_manager(ShortcutManager* sm) { shortcut_manager_ = sm; }
    void set_undo_manager(UndoManager* um) { undo_manager_ = um; }
    CommandPalette* command_palette() const { return command_palette_; }
    CommandRegistry* command_registry() const { return command_registry_; }
    ShortcutManager* shortcut_manager() const { return shortcut_manager_; }
    UndoManager* undo_manager() const { return undo_manager_; }

    // Dock system (Agent A Week 9, owned externally by App)
    void set_dock_system(DockSystem* ds) { dock_system_ = ds; }
    DockSystem* dock_system() const { return dock_system_; }

private:
    void apply_modern_style();
    void load_fonts();

    void draw_command_bar();
    void draw_nav_rail();
    void draw_canvas(Figure& figure);
    void draw_inspector(Figure& figure);
    void draw_status_bar();
    void draw_split_view_splitters();
    void draw_pane_tab_headers();
#if PLOTIX_FLOATING_TOOLBAR
    void draw_floating_toolbar();
#endif
    void draw_theme_settings();
    
    void draw_plot_text(Figure& figure);
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

    // Box zoom overlay (Agent B Week 7, not owned)
    BoxZoomOverlay* box_zoom_overlay_ = nullptr;

    // Command palette & productivity (Agent F, not owned)
    CommandPalette* command_palette_ = nullptr;
    CommandRegistry* command_registry_ = nullptr;
    ShortcutManager* shortcut_manager_ = nullptr;
    UndoManager* undo_manager_ = nullptr;

    // Dock system (Agent A Week 9, not owned)
    DockSystem* dock_system_ = nullptr;

    // Per-pane tab drag state
    struct PaneTabDragState {
        bool dragging = false;
        uint32_t source_pane_id = 0;
        size_t dragged_figure_index = SIZE_MAX;
        float drag_start_x = 0.0f;
        float drag_start_y = 0.0f;
        bool cross_pane = false;    // Dragged outside source pane header
        bool dock_dragging = false; // Dragged far enough vertically → dock system handles split
    };
    PaneTabDragState pane_tab_drag_;
    bool pane_tab_hovered_ = false;  // True when mouse is over a pane tab header

    // Animated tab positions: keyed by (pane_id, figure_index) so each
    // pane gets independent animation state (prevents shaking after splits)
    struct PaneTabAnim {
        float current_x = 0.0f;
        float target_x = 0.0f;
        float opacity = 1.0f;
        float target_opacity = 1.0f;
    };
    struct PaneTabAnimKey {
        uint32_t pane_id;
        size_t fig_idx;
        bool operator==(const PaneTabAnimKey& o) const {
            return pane_id == o.pane_id && fig_idx == o.fig_idx;
        }
    };
    struct PaneTabAnimKeyHash {
        size_t operator()(const PaneTabAnimKey& k) const {
            return std::hash<uint64_t>()(
                (static_cast<uint64_t>(k.pane_id) << 32) | (k.fig_idx & 0xFFFFFFFF));
        }
    };
    std::unordered_map<PaneTabAnimKey, PaneTabAnim, PaneTabAnimKeyHash> pane_tab_anims_;

    // Tab insertion gap animation state (for drag-between-tabs)
    struct InsertionGap {
        uint32_t target_pane_id = 0;   // Pane where the gap is shown
        size_t insert_after_idx = SIZE_MAX;  // Insert after this tab index (SIZE_MAX = before first)
        float current_gap = 0.0f;      // Animated gap width
        float target_gap = 0.0f;       // Target gap width
    };
    InsertionGap insertion_gap_;

    // Non-split drag-to-split state
    struct TabDragSplitState {
        bool active = false;           // True when a main tab bar tab is being dock-dragged
        DropZone suggested_zone = DropZone::None;
        float overlay_opacity = 0.0f;  // Animated overlay opacity
    };
    TabDragSplitState tab_drag_split_;

    // Figure title lookup callback (set by App)
    std::function<std::string(size_t)> get_figure_title_;
public:
    void set_figure_title_callback(std::function<std::string(size_t)> cb) { get_figure_title_ = std::move(cb); }
private:
    
    // Theme settings window state
    bool show_theme_settings_ = false;
    
#if PLOTIX_FLOATING_TOOLBAR
    // Floating toolbar drag state
    bool toolbar_dragging_ = false;
#endif
};

} // namespace plotix

#endif // PLOTIX_USE_IMGUI
