#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace plotix {

class Figure;

// Serializable workspace state: captures all figures, series configurations,
// panel states, zoom levels, and theme for save/load.
// Format: JSON text file with .plotix extension.
struct WorkspaceData {
    // File format version for migration support
    static constexpr uint32_t FORMAT_VERSION = 2;

    struct AxisState {
        float x_min = 0.0f, x_max = 1.0f;
        float y_min = 0.0f, y_max = 1.0f;
        bool auto_fit = true;
        bool grid_visible = true;
        std::string x_label;
        std::string y_label;
        std::string title;
    };

    struct SeriesState {
        std::string name;
        std::string type;  // "line" or "scatter"
        float color_r = 1.0f, color_g = 1.0f, color_b = 1.0f, color_a = 1.0f;
        float line_width = 2.0f;
        float marker_size = 6.0f;
        bool visible = true;
        // Data is NOT saved (too large); only metadata
        size_t point_count = 0;
        // Series-level opacity (for legend interaction fade)
        float opacity = 1.0f;
        // Plot style (line style + marker style)
        int line_style = 1;    // LineStyle enum value (default Solid)
        int marker_style = 0;  // MarkerStyle enum value (default None)
    };

    struct FigureState {
        std::string title;
        uint32_t width = 1280;
        uint32_t height = 720;
        int grid_rows = 1;
        int grid_cols = 1;
        bool is_modified = false;
        std::string custom_tab_title;  // Tab title from FigureManager
        std::vector<AxisState> axes;
        std::vector<SeriesState> series;
    };

    struct PanelState {
        bool inspector_visible = true;
        float inspector_width = 320.0f;
        bool nav_rail_expanded = false;
    };

    struct InteractionState {
        bool crosshair_enabled = false;
        bool tooltip_enabled = true;
        // Persistent data markers (screen-independent, stored in data coords)
        struct MarkerEntry {
            float data_x = 0.0f;
            float data_y = 0.0f;
            std::string series_label;
            size_t point_index = 0;
        };
        std::vector<MarkerEntry> markers;
    };

    uint32_t version = FORMAT_VERSION;
    std::string theme_name = "dark";
    size_t active_figure_index = 0;
    PanelState panels;
    InteractionState interaction;
    std::vector<FigureState> figures;

    // Undo history metadata (not the actual actions — just count for UI display)
    size_t undo_count = 0;
    size_t redo_count = 0;

    // Dock/split view state (serialized JSON from DockSystem)
    std::string dock_state;
};

// Workspace save/load operations.
// All methods are static — no persistent state needed.
class Workspace {
public:
    // Save workspace state to a .plotix file. Returns true on success.
    static bool save(const std::string& path, const WorkspaceData& data);

    // Load workspace state from a .plotix file. Returns true on success.
    static bool load(const std::string& path, WorkspaceData& data);

    // Capture current application state into WorkspaceData.
    // figures: all open figures. active_index: currently active figure.
    static WorkspaceData capture(const std::vector<Figure*>& figures,
                                 size_t active_index,
                                 const std::string& theme_name,
                                 bool inspector_visible,
                                 float inspector_width,
                                 bool nav_rail_expanded);

    // Apply loaded workspace data to figures (restores axis limits, etc.).
    // Returns false if data is incompatible.
    static bool apply(const WorkspaceData& data,
                      std::vector<Figure*>& figures);

    // Get default workspace file path (in user config directory).
    static std::string default_path();

    // Auto-save path (for crash recovery).
    static std::string autosave_path();

    // Autosave: save workspace to autosave_path() if enough time has elapsed.
    // Returns true if an autosave was performed.
    // interval_seconds: minimum seconds between autosaves (default 60).
    static bool maybe_autosave(const WorkspaceData& data, float interval_seconds = 60.0f);

    // Check if an autosave file exists (for crash recovery prompt).
    static bool has_autosave();

    // Delete the autosave file.
    static void clear_autosave();

private:
    // JSON serialization helpers (minimal, no external dependency)
    static std::string serialize_json(const WorkspaceData& data);
    static bool deserialize_json(const std::string& json, WorkspaceData& data);

    // Simple JSON value parser
    static std::string read_string_value(const std::string& json, const std::string& key);
    static double read_number_value(const std::string& json, const std::string& key, double default_val = 0.0);
    static bool read_bool_value(const std::string& json, const std::string& key, bool default_val = false);
};

} // namespace plotix
