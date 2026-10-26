#include <cassert>
#include <filesystem>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>
#include <stdexcept>
#include <system_error>

#include "axes3d.hpp"
#include "layout.hpp"

#ifdef _WIN32
    #include <io.h>
#else
    #include <unistd.h>
#endif

namespace spectra
{

namespace
{

// Validate that `path` names a file we can plausibly create: non-empty, not an
// existing directory, and its parent directory exists and is writable.
bool export_path_is_writable(const std::string& path, const char* what)
{
    namespace fs = std::filesystem;
    if (path.empty())
    {
        SPECTRA_LOG_ERROR("export", "{} export: empty output path", what);
        return false;
    }

    std::error_code ec;
    const fs::path  p(path);
    if (fs::is_directory(p, ec))
    {
        SPECTRA_LOG_ERROR("export", "{} export: '{}' is a directory", what, path);
        return false;
    }

    ec.clear();
    fs::path dir = p.parent_path();
    if (dir.empty())
        dir = fs::current_path(ec);
    if (ec || !fs::is_directory(dir, ec))
    {
        SPECTRA_LOG_ERROR("export", "{} export: directory '{}' does not exist", what, dir.string());
        return false;
    }

#ifdef _WIN32
    const bool writable = ::_access(dir.string().c_str(), 2) == 0;
#else
    const bool writable = ::access(dir.c_str(), W_OK) == 0;
#endif
    if (!writable)
    {
        SPECTRA_LOG_ERROR("export",
                          "{} export: directory '{}' is not writable",
                          what,
                          dir.string());
        return false;
    }
    return true;
}

}   // namespace

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
    figure_.anim_.fps      = target_fps_;
    figure_.anim_.duration = duration_;
    figure_.anim_.loop     = loop_;
    figure_.anim_.on_frame = on_frame_;
    // Actual playback is driven by App (Agent 4).
}

void AnimationBuilder::record(const std::string& output_path)
{
    figure_.anim_.fps              = target_fps_;
    figure_.anim_.duration         = duration_;
    figure_.anim_.loop             = false;
    figure_.anim_.on_frame         = on_frame_;
    figure_.export_req_.video_path = output_path;
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
    auto idx = static_cast<size_t>(index - 1);
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

    auto idx = static_cast<size_t>(index - 1);
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

bool Figure::save_png(const std::string& path)
{
    return save_png(path, 0, 0);
}

bool Figure::save_png(const std::string& path, uint32_t export_width, uint32_t export_height)
{
    if (!export_path_is_writable(path, "PNG"))
        return false;
    export_req_.png_path   = path;
    export_req_.png_width  = export_width;
    export_req_.png_height = export_height;
    compute_layout();
    return true;
}

bool Figure::save_svg(const std::string& path)
{
    if (!export_path_is_writable(path, "SVG"))
        return false;
    export_req_.svg_path = path;
    compute_layout();
    return true;
}

void Figure::copy_to_clipboard()
{
    export_req_.copy_to_clipboard = true;
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
