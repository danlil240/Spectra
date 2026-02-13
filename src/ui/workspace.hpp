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
    static constexpr uint32_t FORMAT_VERSION = 1;

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
    };

    struct FigureState {
        std::string title;
        uint32_t width = 1280;
        uint32_t height = 720;
        int grid_rows = 1;
        int grid_cols = 1;
        std::vector<AxisState> axes;
        std::vector<SeriesState> series;
    };

    struct PanelState {
        bool inspector_visible = true;
        float inspector_width = 320.0f;
        bool nav_rail_expanded = false;
    };

    uint32_t version = FORMAT_VERSION;
    std::string theme_name = "dark";
    size_t active_figure_index = 0;
    PanelState panels;
    std::vector<FigureState> figures;
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
