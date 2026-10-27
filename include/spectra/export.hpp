#pragma once

#include <cstdint>
#include <spectra/color.hpp>
#include <string>
#include <vector>

namespace spectra
{

// Forward declarations for SVG export
class Figure;

class ImageExporter
{
   public:
    static bool write_png(const std::string& path,
                          const uint8_t*     rgba_data,
                          uint32_t           width,
                          uint32_t           height);

    static std::vector<uint8_t> write_png_to_memory(const uint8_t* rgba_data,
                                                    uint32_t       width,
                                                    uint32_t       height);
};

// Colours of the non-data parts of an SVG export (background, grid, axes,
// text, legend).  The defaults match the built-in "light" theme; the app fills
// this from the active theme so an export looks like the on-screen plot.
struct SvgStyle
{
    Color       background     = {0.969f, 0.980f, 1.000f, 1.0f};   ///< figure + plot area
    Color       grid_major     = {0.32f, 0.40f, 0.48f, 0.24f};
    Color       grid_minor     = {0.32f, 0.40f, 0.48f, 0.10f};
    Color       axis_line      = {0.08f, 0.16f, 0.23f, 0.78f};   ///< border, tick marks, 3D box
    Color       tick_label     = {0.125f, 0.196f, 0.278f, 1.0f};
    Color       text_primary   = {0.063f, 0.125f, 0.200f, 1.0f};   ///< titles; 'k'/'w' series remap
    Color       text_secondary = {0.200f, 0.282f, 0.373f, 1.0f};   ///< axis labels, legend text
    Color       legend_bg      = {0.973f, 0.984f, 1.000f, 1.0f};
    Color       legend_border  = {0.61f, 0.72f, 0.82f, 0.44f};
    std::string font_family    = "Inter, 'Helvetica Neue', Arial, sans-serif";
};

class SvgExporter
{
   public:
    // Write a Figure to an SVG file. Traverses Figure→Axes→Series hierarchy
    // and emits SVG elements directly, bypassing the GPU pipeline.
    // 3D axes are projected with their camera and painter-sorted.
    static bool write_svg(const std::string& path, const Figure& figure);
    static bool write_svg(const std::string& path, const Figure& figure, const SvgStyle& style);

    // Write SVG to a string instead of a file.
    static std::string to_string(const Figure& figure);
    static std::string to_string(const Figure& figure, const SvgStyle& style);
};

#ifdef SPECTRA_USE_FFMPEG
class VideoExporter
{
   public:
    struct Config
    {
        std::string output_path;
        uint32_t    width   = 1280;
        uint32_t    height  = 720;
        float       fps     = 60.0f;
        std::string codec   = "libx264";
        std::string pix_fmt = "yuv420p";
    };

    explicit VideoExporter(const Config& config);
    ~VideoExporter();

    VideoExporter(const VideoExporter&)            = delete;
    VideoExporter& operator=(const VideoExporter&) = delete;

    bool write_frame(const uint8_t* rgba_data);
    void finish();

    bool is_open() const { return pipe_ != nullptr; }

   private:
    Config config_;
    FILE*  pipe_ = nullptr;
};
#endif   // SPECTRA_USE_FFMPEG

}   // namespace spectra
