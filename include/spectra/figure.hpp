#pragma once

#include <functional>
#include <memory>
#include <spectra/axes.hpp>
#include <spectra/fwd.hpp>
#include <string>
#include <vector>

namespace spectra
{

struct FigureConfig
{
    uint32_t width  = 1280;
    uint32_t height = 720;
};

enum class LegendPosition
{
    TopRight,
    TopLeft,
    BottomRight,
    BottomLeft,
    None,
};

struct LegendConfig
{
    LegendPosition position     = LegendPosition::TopRight;
    bool           visible      = true;
    float          font_size    = 0.0f;   // 0 = use default from font
    Color          bg_color     = {};     // {0,0,0,0} = use theme bg_elevated
    Color          border_color = {};     // {0,0,0,0} = use theme border_subtle
    float          padding      = 8.0f;
};

struct FigureStyle
{
    Color background         = colors::white;
    float margin_top         = 40.0f;
    float margin_bottom      = 60.0f;
    float margin_left        = 70.0f;
    float margin_right       = 20.0f;
    float subplot_hgap       = 40.0f;
    float subplot_vgap       = 50.0f;
    float min_subplot_height = 150.0f;   // Minimum pixel height per subplot row
};

class AnimationBuilder
{
   public:
    explicit AnimationBuilder(Figure& fig);

    AnimationBuilder& fps(float target_fps);
    AnimationBuilder& duration(float seconds);
    AnimationBuilder& on_frame(std::function<void(Frame&)> callback);
    AnimationBuilder& loop(bool enabled);

    void play();
    void record(const std::string& output_path);

   private:
    Figure&                     figure_;
    float                       target_fps_ = 60.0f;
    float                       duration_   = 0.0f;   // 0 = indefinite
    bool                        loop_       = false;
    std::function<void(Frame&)> on_frame_;
};

// Pending export requests (PNG / SVG / video).  Populated by Figure::save_*
// and AnimationBuilder::record(); consumed by the runtime after rendering.
struct FigureExportRequest
{
    std::string png_path;
    uint32_t    png_width  = 0;   // 0 = use figure's native resolution
    uint32_t    png_height = 0;
    std::string svg_path;
    std::string video_path;
};

// Animation state set by AnimationBuilder, driven by WindowRuntime.
struct FigureAnimState
{
    float                       fps      = 60.0f;
    float                       duration = 0.0f;
    bool                        loop     = false;
    std::function<void(Frame&)> on_frame;

    // Per-figure elapsed time so each figure keeps its own timeline
    // position even when it's not the active tab in a split view.
    float time = 0.0f;
};

class Figure
{
   public:
    explicit Figure(const FigureConfig& config = {});

    Axes&   subplot(int rows, int cols, int index);
    Axes3D& subplot3d(int rows, int cols, int index);

    void show();
    void save_png(const std::string& path);
    void save_png(const std::string& path, uint32_t export_width, uint32_t export_height);
    void save_svg(const std::string& path);

    AnimationBuilder animate();

    uint32_t width() const { return config_.width; }
    uint32_t height() const { return config_.height; }
    void     set_size(uint32_t w, uint32_t h)
    {
        config_.width  = w;
        config_.height = h;
    }

    const std::vector<std::unique_ptr<Axes>>& axes() const { return axes_; }
    std::vector<std::unique_ptr<Axes>>&       axes_mut() { return axes_; }

    const std::vector<std::unique_ptr<AxesBase>>& all_axes() const { return all_axes_; }
    std::vector<std::unique_ptr<AxesBase>>&       all_axes_mut() { return all_axes_; }

    FigureStyle&        style() { return style_; }
    const FigureStyle&  style() const { return style_; }
    LegendConfig&       legend() { return legend_; }
    const LegendConfig& legend() const { return legend_; }

    // Layout: called by renderer before drawing
    void compute_layout();

    // Subplot grid info
    int grid_rows() const { return grid_rows_; }
    int grid_cols() const { return grid_cols_; }

    // Scroll state for overflowing subplot grids
    float scroll_offset_y() const { return scroll_offset_y_; }
    void  set_scroll_offset_y(float offset) { scroll_offset_y_ = offset; }
    float content_height() const { return content_height_; }
    void  set_content_height(float h) { content_height_ = h; }
    bool  needs_scroll(float visible_height) const
    {
        return content_height_ > visible_height + 0.5f;
    }

    // Explicitly set grid dimensions (e.g. after removing rows/cols).
    void set_grid(int rows, int cols)
    {
        if (rows >= 1)
            grid_rows_ = rows;
        if (cols >= 1)
            grid_cols_ = cols;
    }

    // Animation property accessors
    float anim_fps() const { return anim_.fps; }
    float anim_duration() const { return anim_.duration; }
    bool  anim_loop() const { return anim_.loop; }
    bool  has_animation() const { return static_cast<bool>(anim_.on_frame); }

    // Export / animation sub-objects (public so runtime subsystems can
    // consume pending requests without requiring friend access).
    FigureExportRequest export_req_;
    FigureAnimState     anim_;

   private:
    friend class AnimationBuilder;
    friend class App;
    friend class FigureManager;
    friend class WindowRuntime;
    friend class SessionRuntime;
    friend class FigureSerializer;
    friend class EmbedSurface;

    FigureConfig                           config_;
    FigureStyle                            style_;
    LegendConfig                           legend_;
    std::vector<std::unique_ptr<Axes>>     axes_;
    std::vector<std::unique_ptr<AxesBase>> all_axes_;
    int                                    grid_rows_ = 1;
    int                                    grid_cols_ = 1;

    // Scroll state for overflow when subplots exceed visible area
    float scroll_offset_y_ = 0.0f;
    float content_height_  = 0.0f;
};

}   // namespace spectra
