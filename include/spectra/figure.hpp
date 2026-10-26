#pragma once

#include <functional>
#include <memory>
#include <spectra/axes.hpp>
#include <spectra/fwd.hpp>
#include <string>
#include <vector>

namespace spectra
{

/// @brief Initial size of a Figure in pixels.
struct FigureConfig
{
    /// Figure width in pixels.
    uint32_t width = 1500;
    /// Figure height in pixels.
    uint32_t height = 900;
};

/// @brief Where the legend is drawn inside each axes.
enum class LegendPosition
{
    TopRight,
    TopLeft,
    BottomRight,
    BottomLeft,
    None,
};

/// @brief Legend placement and appearance for a Figure.
struct LegendConfig
{
    LegendPosition position     = LegendPosition::TopRight;
    bool           visible      = true;
    float          font_size    = 0.0f;   ///< 0 = use default from font
    Color          bg_color     = {};     ///< {0,0,0,0} = use theme bg_elevated
    Color          border_color = {};     ///< {0,0,0,0} = use theme border_subtle
    float          padding      = 8.0f;
};

/// @brief Figure background colour, outer margins and subplot spacing (pixels).
struct FigureStyle
{
    Color background         = colors::white;
    float margin_top         = 40.0f;
    float margin_bottom      = 60.0f;
    float margin_left        = 70.0f;
    float margin_right       = 20.0f;
    float subplot_hgap       = 40.0f;
    float subplot_vgap       = 50.0f;
    float min_subplot_height = 150.0f;   ///< Minimum pixel height per subplot row
};

/// @brief Fluent builder that configures and starts a per-frame animation on a Figure.
///
/// Obtain one from Figure::animate():
/// @code
/// fig.animate().fps(60).on_frame([&](spectra::Frame& f) { /* update data */ }).play();
/// @endcode
class AnimationBuilder
{
   public:
    /// @brief Create a builder bound to @p fig.
    explicit AnimationBuilder(Figure& fig);

    /// @brief Target frame rate in frames per second (default 60).
    AnimationBuilder& fps(float target_fps);
    /// @brief Animation length in seconds; 0 runs until the window closes.
    AnimationBuilder& duration(float seconds);
    /// @brief Callback invoked once per frame before rendering.
    AnimationBuilder& on_frame(std::function<void(Frame&)> callback);
    /// @brief Restart the timeline when @ref duration is reached.
    AnimationBuilder& loop(bool enabled);

    /// @brief Start the animation in an interactive window.
    void play();
    /// @brief Render the animation offscreen and encode it to @p output_path (e.g. MP4).
    void record(const std::string& output_path);

   private:
    Figure&                     figure_;
    float                       target_fps_ = 60.0f;
    float                       duration_   = 0.0f;   ///< 0 = indefinite
    bool                        loop_       = false;
    std::function<void(Frame&)> on_frame_;
};

/// @brief Pending export requests (PNG / SVG / video).  Populated by Figure::save_*
/// and AnimationBuilder::record(); consumed by the runtime after rendering.
struct FigureExportRequest
{
    std::string png_path;
    uint32_t    png_width         = 0;   ///< 0 = use figure's native resolution
    uint32_t    png_height        = 0;
    bool        copy_to_clipboard = false;
    std::string svg_path;
    std::string video_path;
};

/// @brief Animation state set by AnimationBuilder, driven by WindowRuntime.
struct FigureAnimState
{
    float                       fps            = 60.0f;
    float                       duration       = 0.0f;
    bool                        loop           = false;
    bool                        live_streaming = false;
    std::function<void(Frame&)> on_frame;

    /// Per-figure elapsed time so each figure keeps its own timeline
    /// position even when it's not the active tab in a split view.
    float time = 0.0f;
};

/// @brief A canvas holding a grid of 2D and 3D axes; the top-level object you plot into.
///
/// Create figures through App::figure() (or spectra::figure() in the Easy API), add axes with
/// subplot() / subplot3d(), then display with App::run() or export with save_png().
class Figure
{
   public:
    /// @brief Construct a figure with the given size.
    explicit Figure(const FigureConfig& config = {});

    /// @brief Get or create the 2D axes at 1-based @p index in a @p rows x @p cols grid.
    Axes& subplot(int rows, int cols, int index);
    /// @brief Get or create the 3D axes at 1-based @p index in a @p rows x @p cols grid.
    Axes3D& subplot3d(int rows, int cols, int index);

    /// @brief Compute the subplot layout. Figures are displayed when App::run() runs.
    void show();
    /// @brief Request a PNG export at native resolution.
    ///
    /// Export calls only validate and queue the request; the file is written by
    /// the runtime after the figure is rendered (during / at the end of
    /// App::run()).  They return false — and queue nothing — when the path is
    /// empty, is a directory, or its parent directory is missing or not
    /// writable.  Later render/write failures are reported through the logger.
    bool save_png(const std::string& path);
    /// @brief Request a PNG export at the given resolution (see save_png()).
    bool save_png(const std::string& path, uint32_t export_width, uint32_t export_height);
    /// @brief Request an SVG vector export (see save_png() for the return value).
    bool save_svg(const std::string& path);
    /// @brief Copy a rendered image of the figure to the system clipboard.
    void copy_to_clipboard();

    /// @brief Start configuring an animation for this figure.
    AnimationBuilder animate();

    /// @brief Figure width in pixels.
    uint32_t width() const { return config_.width; }
    /// @brief Figure height in pixels.
    uint32_t height() const { return config_.height; }
    /// @brief Resize the figure (pixels).
    void set_size(uint32_t w, uint32_t h)
    {
        config_.width  = w;
        config_.height = h;
    }

    /// Tab/window title set by user code (e.g. spectra::tab("Name")).
    /// When non-empty, the UI uses this as the tab/window display title
    /// instead of the auto-generated "Figure N".
    const std::string& tab_title() const { return tab_title_; }
    void               set_tab_title(std::string title) { tab_title_ = std::move(title); }

    /// @brief All 2D axes, in creation order.
    const std::vector<std::unique_ptr<Axes>>& axes() const { return axes_; }
    std::vector<std::unique_ptr<Axes>>&       axes_mut() { return axes_; }

    /// @brief All axes, including 3D ones.
    const std::vector<std::unique_ptr<AxesBase>>& all_axes() const { return all_axes_; }
    std::vector<std::unique_ptr<AxesBase>>&       all_axes_mut() { return all_axes_; }

    /// Return the 2D Axes at `index` if it exists, else nullptr.
    /// Checks axes() (2D) first, then all_axes() (3D/generic) with a dynamic_cast.
    Axes* get_axes(size_t index) const
    {
        if (index < axes_.size() && axes_[index])
            return axes_[index].get();
        if (index < all_axes_.size() && all_axes_[index])
            return dynamic_cast<Axes*>(all_axes_[index].get());
        return nullptr;
    }

    /// Iterate all axes (2D and 3D) and call fn(AxesBase*) for each non-null entry.
    template <typename Fn>
    void for_each_axes(Fn&& fn) const
    {
        for (auto& a : axes_)
            if (a)
                fn(static_cast<AxesBase*>(a.get()));
        for (auto& a : all_axes_)
            if (a)
                fn(a.get());
    }

    /// @brief Mutable figure style (background, margins, gaps).
    FigureStyle&       style() { return style_; }
    const FigureStyle& style() const { return style_; }
    /// @brief Mutable legend configuration.
    LegendConfig&       legend() { return legend_; }
    const LegendConfig& legend() const { return legend_; }

    // Layout: called by renderer before drawing
    /// @brief Recompute axes viewports from the subplot grid (called by the renderer).
    void compute_layout();

    // Subplot grid info
    /// @brief Number of subplot grid rows.
    int grid_rows() const { return grid_rows_; }
    /// @brief Number of subplot grid columns.
    int grid_cols() const { return grid_cols_; }

    /// Scroll state for overflowing subplot grids
    float scroll_offset_y() const { return scroll_offset_y_; }
    void  set_scroll_offset_y(float offset) { scroll_offset_y_ = offset; }
    float content_height() const { return content_height_; }
    void  set_content_height(float h) { content_height_ = h; }
    bool  needs_scroll(float visible_height) const
    {
        return content_height_ > visible_height + 0.5f;
    }

    /// Explicitly set grid dimensions (e.g. after removing rows/cols).
    void set_grid(int rows, int cols)
    {
        if (rows >= 1)
            grid_rows_ = rows;
        if (cols >= 1)
            grid_cols_ = cols;
    }

    // Animation property accessors
    /// @brief Target animation frame rate.
    float anim_fps() const { return anim_.fps; }
    float anim_duration() const { return anim_.duration; }
    bool  anim_loop() const { return anim_.loop; }
    /// @brief True if a frame callback or live streaming is active.
    bool has_animation() const { return static_cast<bool>(anim_.on_frame) || anim_.live_streaming; }

    /// Export / animation sub-objects (public so runtime subsystems can
    /// consume pending requests without requiring friend access).
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

    /// Scroll state for overflow when subplots exceed visible area
    float       scroll_offset_y_ = 0.0f;
    float       content_height_  = 0.0f;
    std::string tab_title_;
};

}   // namespace spectra
