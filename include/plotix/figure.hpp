#pragma once

#include <plotix/axes.hpp>
#include <plotix/fwd.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace plotix {

struct FigureConfig {
    uint32_t width  = 1280;
    uint32_t height = 720;
};

class AnimationBuilder {
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
    float   target_fps_ = 60.0f;
    float   duration_   = 0.0f;  // 0 = indefinite
    bool    loop_       = false;
    std::function<void(Frame&)> on_frame_;
};

class Figure {
public:
    explicit Figure(const FigureConfig& config = {});

    Axes& subplot(int rows, int cols, int index);

    void show();
    void save_png(const std::string& path);

    AnimationBuilder animate();

    uint32_t width()  const { return config_.width; }
    uint32_t height() const { return config_.height; }

    const std::vector<std::unique_ptr<Axes>>& axes() const { return axes_; }

    // Layout: called by renderer before drawing
    void compute_layout();

    // Subplot grid info
    int grid_rows() const { return grid_rows_; }
    int grid_cols() const { return grid_cols_; }

private:
    friend class AnimationBuilder;
    friend class App;

    FigureConfig config_;
    std::vector<std::unique_ptr<Axes>> axes_;
    int grid_rows_ = 1;
    int grid_cols_ = 1;

    // Animation state (set by AnimationBuilder)
    float   anim_fps_      = 60.0f;
    float   anim_duration_ = 0.0f;
    bool    anim_loop_     = false;
    std::function<void(Frame&)> anim_on_frame_;
};

} // namespace plotix
