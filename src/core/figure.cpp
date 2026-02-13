#include <plotix/figure.hpp>

#include "layout.hpp"

#include <cassert>
#include <stdexcept>

namespace plotix {

// --- AnimationBuilder ---

AnimationBuilder::AnimationBuilder(Figure& fig)
    : figure_(fig) {}

AnimationBuilder& AnimationBuilder::fps(float target_fps) {
    target_fps_ = target_fps;
    return *this;
}

AnimationBuilder& AnimationBuilder::duration(float seconds) {
    duration_ = seconds;
    return *this;
}

AnimationBuilder& AnimationBuilder::on_frame(std::function<void(Frame&)> callback) {
    on_frame_ = std::move(callback);
    return *this;
}

AnimationBuilder& AnimationBuilder::loop(bool enabled) {
    loop_ = enabled;
    return *this;
}

void AnimationBuilder::play() {
    figure_.anim_fps_      = target_fps_;
    figure_.anim_duration_ = duration_;
    figure_.anim_loop_     = loop_;
    figure_.anim_on_frame_ = on_frame_;
    // Actual playback is driven by App (Agent 4).
}

void AnimationBuilder::record(const std::string& output_path) {
    figure_.anim_fps_      = target_fps_;
    figure_.anim_duration_ = duration_;
    figure_.anim_loop_     = false;
    figure_.anim_on_frame_ = on_frame_;
    figure_.video_record_path_ = output_path;
}

// --- Figure ---

Figure::Figure(const FigureConfig& config)
    : config_(config) {}

Axes& Figure::subplot(int rows, int cols, int index) {
    if (rows <= 0 || cols <= 0 || index < 1 || index > rows * cols) {
        throw std::out_of_range("subplot index out of range");
    }

    // Update grid dimensions to the maximum seen
    if (rows > grid_rows_) grid_rows_ = rows;
    if (cols > grid_cols_) grid_cols_ = cols;

    // Ensure we have enough axes slots (1-based index)
    size_t idx = static_cast<size_t>(index - 1);
    if (idx >= axes_.size()) {
        axes_.resize(idx + 1);
    }

    if (!axes_[idx]) {
        axes_[idx] = std::make_unique<Axes>();
    }

    return *axes_[idx];
}

void Figure::show() {
    // Driven by App (Agent 4). This is a placeholder that computes layout.
    compute_layout();
}

void Figure::save_png(const std::string& path) {
    png_export_path_ = path;
    png_export_width_  = 0;
    png_export_height_ = 0;
    compute_layout();
}

void Figure::save_png(const std::string& path, uint32_t export_width, uint32_t export_height) {
    png_export_path_   = path;
    png_export_width_  = export_width;
    png_export_height_ = export_height;
    compute_layout();
}

void Figure::save_svg(const std::string& path) {
    svg_export_path_ = path;
    compute_layout();
}

AnimationBuilder Figure::animate() {
    return AnimationBuilder(*this);
}

void Figure::compute_layout() {
    auto rects = compute_subplot_layout(
        static_cast<float>(config_.width),
        static_cast<float>(config_.height),
        grid_rows_, grid_cols_);

    // Assign viewport rects to each axes
    for (size_t i = 0; i < axes_.size() && i < rects.size(); ++i) {
        if (axes_[i]) {
            axes_[i]->set_viewport(rects[i]);
        }
    }

    // Auto-fit limits for axes that don't have explicit limits
    for (auto& ax : axes_) {
        if (ax) {
            // Limits are computed lazily in x_limits()/y_limits(),
            // but we can trigger auto_fit here if needed.
        }
    }
}

} // namespace plotix
