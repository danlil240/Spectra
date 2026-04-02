#pragma once

#ifdef SPECTRA_USE_IMGUI

    #include <spectra/color.hpp>
    #include <spectra/series.hpp>
    #include <string>
    #include <vector>

struct ImDrawList;
struct ImFont;

namespace spectra::ui
{
class ThemeManager;
}   // namespace spectra::ui

namespace spectra
{

class Axes;

// A persistent text annotation pinned to a data coordinate on the plot.
struct Annotation
{
    float       data_x = 0.0f;           // Data-space X coordinate (anchor point)
    float       data_y = 0.0f;           // Data-space Y coordinate (anchor point)
    std::string text;                    // User-entered annotation text
    Color       color = colors::white;   // Accent color (left bar)
    Color       text_color;              // {0,0,0,0} = use theme text_primary
    const Axes* axes     = nullptr;      // Owning axes (for multi-subplot)
    float       offset_x = 0.0f;         // Screen-space drag offset from anchor (px)
    float       offset_y = -40.0f;       // Default: above the anchor point
    bool        editing  = false;        // True while inline text editing is active
};

// Manages a collection of text annotations on the plot canvas.
// Annotations are placed in Annotate tool mode by clicking on the plot,
// entering text via an inline ImGui input, and persist across pan/zoom.
class AnnotationManager
{
   public:
    AnnotationManager() = default;

    // Set fonts for annotation rendering
    void set_fonts(ImFont* body, ImFont* heading);
    void set_theme_manager(ui::ThemeManager* tm) { theme_mgr_ = tm; }

    // Add a new annotation at the given data coordinates.
    // Returns the index of the newly created annotation.
    size_t add(float data_x, float data_y, const Axes* axes);

    // Remove an annotation by index.
    void remove(size_t index);

    // Remove all annotations belonging to the given axes.
    void remove_for_axes(const Axes* axes);

    // Clear all annotations.
    void clear();

    // Cancel any active editing (e.g. when switching tool modes).
    void cancel_editing();

    // Draw all annotations. Converts data coords to screen coords using viewport/limits.
    // When filter_axes is non-null, only annotations belonging to that axes are drawn.
    // When dl is non-null, draws into the given draw list instead of GetForegroundDrawList().
    void draw(const Rect& viewport,
              float       xlim_min,
              float       xlim_max,
              float       ylim_min,
              float       ylim_max,
              float       opacity     = 1.0f,
              const Axes* filter_axes = nullptr,
              ImDrawList* dl          = nullptr);

    // Hit-test: returns index of annotation near screen position, or -1.
    // When filter_axes is non-null, only annotations belonging to that axes are tested.
    int hit_test(float       screen_x,
                 float       screen_y,
                 const Rect& viewport,
                 float       xlim_min,
                 float       xlim_max,
                 float       ylim_min,
                 float       ylim_max,
                 float       radius_px   = 10.0f,
                 const Axes* filter_axes = nullptr) const;

    // Begin dragging annotation at the given index.
    void begin_drag(size_t index, float screen_x, float screen_y);

    // Update drag position.
    void update_drag(float screen_x, float screen_y);

    // End drag.
    void end_drag();

    // Is a drag currently in progress?
    bool is_dragging() const { return drag_active_; }

    // Check if any annotation is currently being edited.
    bool is_editing() const;

    const std::vector<Annotation>& annotations() const { return annotations_; }
    std::vector<Annotation>&       annotations_mut() { return annotations_; }
    size_t                         count() const { return annotations_.size(); }

   private:
    // Convert data coordinates to screen coordinates
    static void data_to_screen(float       data_x,
                               float       data_y,
                               const Rect& viewport,
                               float       xlim_min,
                               float       xlim_max,
                               float       ylim_min,
                               float       ylim_max,
                               float&      screen_x,
                               float&      screen_y);

    std::vector<Annotation> annotations_;

    // Fonts (not owned)
    ImFont* font_body_    = nullptr;
    ImFont* font_heading_ = nullptr;

    ui::ThemeManager* theme_mgr_ = nullptr;

    // Drag state
    bool   drag_active_         = false;
    size_t drag_index_          = 0;
    float  drag_start_mouse_x_  = 0.0f;
    float  drag_start_mouse_y_  = 0.0f;
    float  drag_start_offset_x_ = 0.0f;
    float  drag_start_offset_y_ = 0.0f;

    // Inline edit buffer
    char edit_buf_[512] = {};
};

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
