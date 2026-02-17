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
    uint32_t width = 1280;
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
    LegendPosition position = LegendPosition::TopRight;
    bool visible = true;
    float font_size = 12.0f;
    Color bg_color = {1.0f, 1.0f, 1.0f, 0.85f};
    Color border_color = {0.7f, 0.7f, 0.7f, 1.0f};
    float padding = 8.0f;
};

struct FigureStyle
{
    Color background = colors::white;
    float margin_top = 40.0f;
    float margin_bottom = 60.0f;
    float margin_left = 70.0f;
    float margin_right = 20.0f;
    float subplot_hgap = 40.0f;
    float subplot_vgap = 50.0f;
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
    Figure& figure_;
    float target_fps_ = 60.0f;
    float duration_ = 0.0f;  // 0 = indefinite
    bool loop_ = false;
    std::function<void(Frame&)> on_frame_;
};

class Figure
{
   public:
    explicit Figure(const FigureConfig& config = {});

    Axes& subplot(int rows, int cols, int index);
    Axes3D& subplot3d(int rows, int cols, int index);

    void show();
    void save_png(const std::string& path);
    void save_png(const std::string& path, uint32_t export_width, uint32_t export_height);
    void save_svg(const std::string& path);

    AnimationBuilder animate();

    uint32_t width() const { return config_.width; }
    uint32_t height() const { return config_.height; }

    const std::vector<std::unique_ptr<Axes>>& axes() const { return axes_; }
    std::vector<std::unique_ptr<Axes>>& axes_mut() { return axes_; }

    const std::vector<std::unique_ptr<AxesBase>>& all_axes() const { return all_axes_; }
    std::vector<std::unique_ptr<AxesBase>>& all_axes_mut() { return all_axes_; }

    FigureStyle& style() { return style_; }
    const FigureStyle& style() const { return style_; }
    LegendConfig& legend() { return legend_; }
    const LegendConfig& legend() const { return legend_; }

    // Layout: called by renderer before drawing
    void compute_layout();

    // Subplot grid info
    int grid_rows() const { return grid_rows_; }
    int grid_cols() const { return grid_cols_; }

   private:
    friend class AnimationBuilder;
    friend class App;
    friend class WindowRuntime;
    friend class SessionRuntime;

    FigureConfig config_;
    FigureStyle style_;
    LegendConfig legend_;
    std::vector<std::unique_ptr<Axes>> axes_;
    std::vector<std::unique_ptr<AxesBase>> all_axes_;
    int grid_rows_ = 1;
    int grid_cols_ = 1;

    // Pending PNG export path (set by save_png, executed after render)
    std::string png_export_path_;
    uint32_t png_export_width_ = 0;  // 0 = use figure's native resolution
    uint32_t png_export_height_ = 0;

    // Pending SVG export path (set by save_svg, executed after layout)
    std::string svg_export_path_;

    // Pending video recording path (set by AnimationBuilder::record())
    std::string video_record_path_;

    // Animation state (set by AnimationBuilder)
    float anim_fps_ = 60.0f;
    float anim_duration_ = 0.0f;
    bool anim_loop_ = false;
    std::function<void(Frame&)> anim_on_frame_;
};

}  // namespace spectra
