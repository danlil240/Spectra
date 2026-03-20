#pragma once

// ─── Shape Series ───────────────────────────────────────────────────────────
//
// Renders 2D geometric shapes (rectangles, circles, ellipses, lines, arrows,
// polygons, rings) on a plot.  Each shape is defined in data-space coordinates
// and rendered as filled triangles + outline segments using the same GPU
// pipelines as statistical series (stat_fill + line).
//
// Easy API:
//   auto& sh = spectra::shapes();
//   sh.rect(1, 2, 3, 4).circle(5, 5, 1.5).arrow(0, 0, 3, 3);
//
// Object API:
//   auto& sh = ax.shapes();
//   sh.rect(x, y, w, h).fill_opacity(0.3f).circle(cx, cy, r);
//
// Animation:
//   spectra::on_update([&](float dt, float t) {
//       sh.clear_shapes();
//       sh.circle(cos(t)*2, sin(t)*2, 0.5f);
//   });
//
// ─────────────────────────────────────────────────────────────────────────────

#include <cmath>
#include <span>
#include <spectra/color.hpp>
#include <spectra/fwd.hpp>
#include <spectra/series.hpp>
#include <string>
#include <vector>

namespace spectra
{

// Individual shape definition stored before geometry rebuild.
struct ShapeDef
{
    enum class Type
    {
        Rect,
        Circle,
        Ellipse,
        Line,
        Arrow,
        Polygon,
        Ring,
        Text,
    };

    Type  type = Type::Rect;
    Color fill_color{0.0f, 0.0f, 0.0f, 0.0f};   // zero-alpha = use series color
    Color line_color{0.0f, 0.0f, 0.0f, 0.0f};   // zero-alpha = use series color
    float fill_opacity  = 0.4f;
    float line_width    = 1.5f;
    float corner_radius = 0.0f;   // for rounded rects

    // Geometry params (meaning depends on type):
    //   Rect:    x, y, w, h
    //   Circle:  cx, cy, r, (unused)
    //   Ellipse: cx, cy, rx, ry
    //   Line:    x1, y1, x2, y2
    //   Arrow:   x1, y1, x2, y2  (with arrowhead)
    //   Ring:    cx, cy, outer_r, inner_r
    //   Polygon: stored in poly_x / poly_y
    float params[4] = {};

    // Polygon vertices (only used when type == Polygon)
    std::vector<float> poly_x;
    std::vector<float> poly_y;

    // Rotation in radians (around shape center)
    float rotation = 0.0f;

    // Number of segments for circles/ellipses/rings
    int segments = 64;

    // Arrow head size as fraction of arrow length
    float arrow_head_frac  = 0.15f;
    float arrow_head_width = 0.08f;

    // Text params (only used when type == Text)
    std::string text_content;
    float       text_font_size = 0.0f;   // 0 = auto (FontSize::Label)
    int         text_align     = 0;      // 0=Left, 1=Center, 2=Right
    int         text_valign    = 1;      // 0=Top, 1=Middle, 2=Bottom
};

class ShapeSeries : public Series
{
   public:
    ShapeSeries() = default;

    // ── Shape creation (fluent, returns *this for chaining) ──

    // Add a rectangle at (x, y) with size (w, h).
    ShapeSeries& rect(float x, float y, float w, float h);

    // Add a circle at (cx, cy) with radius r.
    ShapeSeries& circle(float cx, float cy, float r);

    // Add an ellipse at (cx, cy) with radii (rx, ry).
    ShapeSeries& ellipse(float cx, float cy, float rx, float ry);

    // Add a line from (x1, y1) to (x2, y2).
    ShapeSeries& line(float x1, float y1, float x2, float y2);

    // Add an arrow from (x1, y1) to (x2, y2).
    ShapeSeries& arrow(float x1, float y1, float x2, float y2);

    // Add a ring (annulus) at (cx, cy) with outer/inner radii.
    ShapeSeries& ring(float cx, float cy, float outer_r, float inner_r);

    // Add a polygon from vertex arrays.
    ShapeSeries& polygon(std::span<const float> x, std::span<const float> y);

    // Add a text annotation at data-space coordinates (x, y).
    ShapeSeries& text(float x, float y, const std::string& content);

    // ── Per-shape style modifiers (apply to the last added shape) ──

    ShapeSeries& fill_color(const Color& c);
    ShapeSeries& line_color(const Color& c);
    ShapeSeries& fill_opacity(float opacity);
    ShapeSeries& line_width(float w);
    ShapeSeries& corner_radius(float r);
    ShapeSeries& rotation(float radians);
    ShapeSeries& segments(int n);
    ShapeSeries& arrow_head(float length_frac, float width);
    ShapeSeries& text_align(int align);       // 0=Left, 1=Center, 2=Right
    ShapeSeries& text_valign(int valign);     // 0=Top, 1=Middle, 2=Bottom

    // ── Bulk operations ──

    // Remove all shapes (useful before rebuilding in animation callbacks).
    ShapeSeries& clear_shapes();

    // Number of shapes.
    size_t shape_count() const { return shapes_.size(); }

    // Access the shape definitions.
    const std::vector<ShapeDef>& shapes() const { return shapes_; }

    // Access text annotations (for renderer to draw via TextRenderer).
    // Each entry: {data_x, data_y, content, font_size, align, valign, color}.
    struct TextAnnotation
    {
        float       x;
        float       y;
        std::string content;
        float       font_size;   // 0 = auto
        int         align;       // 0=Left, 1=Center, 2=Right
        int         valign;      // 0=Top, 1=Middle, 2=Bottom
        Color       text_color;  // zero-alpha = use series color
    };
    const std::vector<TextAnnotation>& text_annotations() const { return text_annotations_; }

    // ── Geometry access (for renderer) ──

    // Access outline geometry (line segments with NaN breaks)
    std::span<const float> x_data() const { return line_x_; }
    std::span<const float> y_data() const { return line_y_; }
    size_t                 point_count() const { return line_x_.size(); }

    // Access fill geometry (interleaved x,y,alpha per vertex)
    std::span<const float> fill_verts() const { return fill_verts_; }
    size_t                 fill_vertex_count() const { return fill_verts_.size() / 3; }

    // Rebuild geometry from shape definitions.
    void rebuild_geometry();

    void record_commands(Renderer& renderer) override;

    // Fluent setters (covariant return)
    using Series::color;
    using Series::label;
    using Series::opacity;
    ShapeSeries& label(const std::string& lbl)
    {
        Series::label(lbl);
        return *this;
    }
    ShapeSeries& color(const Color& c)
    {
        Series::color(c);
        return *this;
    }
    ShapeSeries& opacity(float o)
    {
        Series::opacity(o);
        return *this;
    }

   private:
    // Shape definitions
    std::vector<ShapeDef> shapes_;

    // Text annotations (populated during rebuild_geometry from Text-type shapes)
    std::vector<TextAnnotation> text_annotations_;

    // Generated geometry
    std::vector<float> line_x_;
    std::vector<float> line_y_;
    std::vector<float> fill_verts_;   // interleaved {x, y, alpha} per vertex

    // Geometry generation helpers
    void generate_rect(const ShapeDef& def);
    void generate_circle(const ShapeDef& def);
    void generate_ellipse(const ShapeDef& def);
    void generate_line(const ShapeDef& def);
    void generate_arrow(const ShapeDef& def);
    void generate_ring(const ShapeDef& def);
    void generate_polygon(const ShapeDef& def);

    // Push a NaN break into the outline (segment separator).
    void push_nan_break();

    // Push a filled triangle into fill_verts_.
    void push_fill_tri(float x0, float y0, float a0,
                       float x1, float y1, float a1,
                       float x2, float y2, float a2);

    // Push an outline segment (two points).
    void push_outline_seg(float x0, float y0, float x1, float y1);

    // Apply rotation transform around center.
    static void rotate_point(float& x, float& y, float cx, float cy, float angle);
};

}   // namespace spectra
