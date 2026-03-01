#include <cassert>
#include <spectra/figure.hpp>
#include <stdexcept>

#include "axes3d.hpp"
#include "layout.hpp"

namespace spectra
{

// --- AnimationBuilder ---

AnimationBuilder::AnimationBuilder(Figure& fig) : figure_(fig) {}

AnimationBuilder& AnimationBuilder::fps(float target_fps)
{
    target_fps_ = target_fps;
    return *this;
}

AnimationBuilder& AnimationBuilder::duration(float seconds)
{
    duration_ = seconds;
    return *this;
}

AnimationBuilder& AnimationBuilder::on_frame(std::function<void(Frame&)> callback)
{
    on_frame_ = std::move(callback);
    return *this;
}

AnimationBuilder& AnimationBuilder::loop(bool enabled)
{
    loop_ = enabled;
    return *this;
}

void AnimationBuilder::play()
{
    figure_.anim_fps_      = target_fps_;
    figure_.anim_duration_ = duration_;
    figure_.anim_loop_     = loop_;
    figure_.anim_on_frame_ = on_frame_;
    // Actual playback is driven by App (Agent 4).
}

void AnimationBuilder::record(const std::string& output_path)
{
    figure_.anim_fps_          = target_fps_;
    figure_.anim_duration_     = duration_;
    figure_.anim_loop_         = false;
    figure_.anim_on_frame_     = on_frame_;
    figure_.video_record_path_ = output_path;
}

// --- Figure ---

Figure::Figure(const FigureConfig& config) : config_(config) {}

Axes& Figure::subplot(int rows, int cols, int index)
{
    if (rows <= 0 || cols <= 0 || index < 1 || index > rows * cols)
    {
        throw std::out_of_range("subplot index out of range");
    }

    // Update grid dimensions to the maximum seen
    if (rows > grid_rows_)
        grid_rows_ = rows;
    if (cols > grid_cols_)
        grid_cols_ = cols;

    // Ensure we have enough axes slots (1-based index)
    size_t idx = static_cast<size_t>(index - 1);
    if (idx >= axes_.size())
    {
        axes_.resize(idx + 1);
    }

    if (!axes_[idx])
    {
        axes_[idx] = std::make_unique<Axes>();
    }

    return *axes_[idx];
}

Axes3D& Figure::subplot3d(int rows, int cols, int index)
{
    if (rows <= 0 || cols <= 0 || index < 1 || index > rows * cols)
    {
        throw std::out_of_range("subplot3d index out of range");
    }

    if (rows > grid_rows_)
        grid_rows_ = rows;
    if (cols > grid_cols_)
        grid_cols_ = cols;

    size_t idx = static_cast<size_t>(index - 1);
    if (idx >= all_axes_.size())
    {
        all_axes_.resize(idx + 1);
    }

    if (!all_axes_[idx])
    {
        all_axes_[idx] = std::make_unique<Axes3D>();
    }

    return *static_cast<Axes3D*>(all_axes_[idx].get());
}

void Figure::show()
{
    // Driven by App (Agent 4). This is a placeholder that computes layout.
    compute_layout();
}

void Figure::save_png(const std::string& path)
{
    png_export_path_   = path;
    png_export_width_  = 0;
    png_export_height_ = 0;
    compute_layout();
}

void Figure::save_png(const std::string& path, uint32_t export_width, uint32_t export_height)
{
    png_export_path_   = path;
    png_export_width_  = export_width;
    png_export_height_ = export_height;
    compute_layout();
}

void Figure::save_svg(const std::string& path)
{
    svg_export_path_ = path;
    compute_layout();
}

AnimationBuilder Figure::animate()
{
    return AnimationBuilder(*this);
}

void Figure::compute_layout()
{
    // Use the figure's own style margins (matching what the Spectra app uses
    // via ImGui layout) instead of the default Margins from layout.hpp.
    Margins fig_margins;
    fig_margins.left   = style_.margin_left;
    fig_margins.right  = style_.margin_right;
    fig_margins.top    = style_.margin_top;
    fig_margins.bottom = style_.margin_bottom;

    auto rects = compute_subplot_layout(static_cast<float>(config_.width),
                                        static_cast<float>(config_.height),
                                        grid_rows_,
                                        grid_cols_,
                                        fig_margins);

    // Assign viewport rects to 2D axes
    for (size_t i = 0; i < axes_.size() && i < rects.size(); ++i)
    {
        if (axes_[i])
        {
            axes_[i]->set_viewport(rects[i]);
        }
    }

    // Assign viewport rects to all axes (including 3D)
    for (size_t i = 0; i < all_axes_.size() && i < rects.size(); ++i)
    {
        if (all_axes_[i])
        {
            all_axes_[i]->set_viewport(rects[i]);
        }
    }
}

}   // namespace spectra
