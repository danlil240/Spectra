#pragma once

// Format-neutral drawing interface of the vector exporters.
//
// The scene walk (io/vector_scene.cpp) turns a Figure into calls on a
// VectorWriter; SvgWriter (io/svg_export.cpp) serializes them as SVG elements
// and PdfWriter (io/pdf_export.cpp) as PDF content streams.
//
// Conventions:
//  - Coordinates are figure pixels, origin top-left, Y down (the SVG user space).
//  - Path geometry is SVG path data restricted to M/L/H/V/Z (absolute and
//    relative, implicit lineto after M/L, space or comma separated).  The walk
//    builds it once; for_each_path_command() decodes it for non-SVG writers.
//  - Styles follow SVG semantics: a group's style is inherited by everything
//    drawn inside it, an element's style overrides it, and `opacity` multiplies
//    (it is not inherited).  Unset fields inherit.
//  - Fields that only shape the SVG source (class names, indentation, attribute
//    order) are carried along so SVG output stays stable; other writers ignore them.

#include <cstdint>
#include <optional>
#include <spectra/color.hpp>
#include <spectra/plot_style.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace spectra
{

enum class PaintKind : uint8_t
{
    Unset,          // inherit
    None,           // fill="none"
    Rgb,            // explicit colour (alpha ignored: opacity is separate)
    CurrentColor,   // the inherited `color`
    Gradient,       // a define_linear_gradient() handle
};

struct VectorPaint
{
    PaintKind kind     = PaintKind::Unset;
    Color     color    = {};
    int       gradient = -1;

    static VectorPaint none() { return {PaintKind::None, {}, -1}; }
    static VectorPaint rgb(const Color& c) { return {PaintKind::Rgb, c, -1}; }
    static VectorPaint current() { return {PaintKind::CurrentColor, {}, -1}; }
    static VectorPaint gradient_ref(int id) { return {PaintKind::Gradient, {}, id}; }
};

enum class VectorLineCap : uint8_t
{
    Unset,
    Butt,
    Round,
};

enum class VectorLineJoin : uint8_t
{
    Unset,
    Round,
};

struct VectorStyle
{
    std::string_view     cls;     // SVG class attribute (semantic role)
    std::optional<Color> color;   // currentColor (rgb only)
    VectorPaint          fill;
    float                fill_opacity = 1.0f;
    VectorPaint          stroke;
    float                stroke_width   = -1.0f;   // < 0: unset
    float                stroke_opacity = 1.0f;
    VectorLineJoin       join           = VectorLineJoin::Unset;
    VectorLineCap        cap            = VectorLineCap::Unset;
    std::vector<float>   dash;                  // empty: solid
    float                opacity     = 1.0f;    // element opacity (multiplies)
    bool                 crisp_edges = false;   // rendering hint
    // SVG attribute order: stroke-opacity before stroke-width.
    bool opacity_before_width = false;
};

struct VectorGroup
{
    int                             indent = 0;   // SVG source indentation (spaces)
    VectorStyle                     style;
    std::string_view                data_type;   // SVG data-type attribute
    int                             clip = -1;   // define_clip_rect() handle
    std::optional<std::string_view> tooltip;     // hover text (SVG <title>)
};

enum class VectorAnchor : uint8_t
{
    Start,
    Middle,
    End,
};

struct VectorText
{
    int              indent = 4;
    std::string_view text;
    double           x       = 0.0;
    double           y       = 0.0;   // baseline
    float            font_px = 0.0f;
    Color            color;   // fill colour; alpha = fill opacity
    VectorAnchor     anchor = VectorAnchor::Start;
    std::string_view cls;   // non-empty: annotation text (SVG attribute order differs)
    bool             medium_weight = false;   // font-weight 500 (titles)
    // Rotation (degrees, SVG sense: positive = clockwise on screen) about (rot_x, rot_y).
    bool   rotated = false;
    double rot_deg = 0.0;
    double rot_x   = 0.0;
    double rot_y   = 0.0;
};

struct VectorPoint
{
    double x, y;
};

// One part of a marker symbol, centred on the marker position (pixels, Y down).
// Parts paint in currentColor unless `rim` is set.
struct MarkerPart
{
    enum class Kind : uint8_t
    {
        Circle,
        Polygon,
        Path,   // stroked SVG path data (plus / cross)
    };
    Kind                     kind         = Kind::Circle;
    bool                     filled       = true;   // fill, else stroke with stroke_width
    float                    radius       = 0.0f;
    float                    stroke_width = 0.0f;
    std::vector<VectorPoint> points;
    std::string              d;
    // Rim of the default scatter dot: stroked in white (or black) at rim_opacity.
    bool  rim         = false;
    bool  rim_white   = true;
    float rim_opacity = 1.0f;
};

class VectorWriter
{
   public:
    virtual ~VectorWriter() = default;

    // Definitions; the returned handles are referenced by later calls.
    virtual int define_clip_rect(double x, double y, double w, double h) = 0;
    // Gradient of `color` from alpha a0 at (x1, y1) to a1 at (x2, y2), padded.
    virtual int  define_linear_gradient(double       x1,
                                        double       y1,
                                        double       x2,
                                        double       y2,
                                        const Color& color,
                                        float        a0,
                                        float        a1)                         = 0;
    virtual void define_marker(int id, std::span<const MarkerPart> parts) = 0;

    virtual void begin_group(const VectorGroup& g) = 0;
    virtual void end_group()                       = 0;

    virtual void path(int indent, std::string_view d, const VectorStyle& s)                  = 0;
    virtual void line(int                indent,
                      double             x1,
                      double             y1,
                      double             x2,
                      double             y2,
                      const VectorStyle& s)                                                  = 0;
    virtual void polygon(int indent, std::span<const VectorPoint> pts, const VectorStyle& s) = 0;
    virtual void circle(int indent, double cx, double cy, double r, const VectorStyle& s)    = 0;
    virtual void
    rect(int indent, double x, double y, double w, double h, double rx, const VectorStyle& s) = 0;
    // An instance of a define_marker() symbol at (x, y).
    virtual void use_marker(int indent, int marker, double x, double y, const VectorStyle& s) = 0;
    virtual void text(const VectorText& t)                                                    = 0;
    // A plugin series that could not be exported (SVG: an XML comment in its place).
    virtual void note_skipped_series(std::string_view type_name,
                                     std::string_view label,
                                     std::string_view reason) = 0;

    // Format name for log messages ("SVG", "PDF").
    [[nodiscard]] virtual const char* format_name() const = 0;
};

// Decodes SVG path data (the subset described above) into absolute moveto /
// lineto / closepath calls.  Returns false on malformed data (calls made so far stand).
struct PathSink
{
    virtual ~PathSink()                      = default;
    virtual void move_to(double x, double y) = 0;
    virtual void line_to(double x, double y) = 0;
    virtual void close()                     = 0;
};
bool for_each_path_command(std::string_view d, PathSink& sink);

// Compact fixed-point number (2 decimals, trailing zeros trimmed, "-0" -> "0",
// non-finite -> "0"): the number format of SVG attributes and path data.
void        vector_put_num(std::string& out, double v);
std::string vector_num(double v);

}   // namespace spectra
