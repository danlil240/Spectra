#include <cmath>
#include <limits>
#include <spectra/series_shapes.hpp>

namespace spectra
{

static constexpr float PI  = 3.14159265358979f;
static constexpr float NaN = std::numeric_limits<float>::quiet_NaN();

// ── Shape creation ──────────────────────────────────────────────────────────

ShapeSeries& ShapeSeries::rect(float x, float y, float w, float h)
{
    ShapeDef def;
    def.type      = ShapeDef::Type::Rect;
    def.params[0] = x;
    def.params[1] = y;
    def.params[2] = w;
    def.params[3] = h;
    shapes_.push_back(std::move(def));
    dirty_ = true;
    return *this;
}

ShapeSeries& ShapeSeries::circle(float cx, float cy, float r)
{
    ShapeDef def;
    def.type      = ShapeDef::Type::Circle;
    def.params[0] = cx;
    def.params[1] = cy;
    def.params[2] = r;
    shapes_.push_back(std::move(def));
    dirty_ = true;
    return *this;
}

ShapeSeries& ShapeSeries::ellipse(float cx, float cy, float rx, float ry)
{
    ShapeDef def;
    def.type      = ShapeDef::Type::Ellipse;
    def.params[0] = cx;
    def.params[1] = cy;
    def.params[2] = rx;
    def.params[3] = ry;
    shapes_.push_back(std::move(def));
    dirty_ = true;
    return *this;
}

ShapeSeries& ShapeSeries::line(float x1, float y1, float x2, float y2)
{
    ShapeDef def;
    def.type      = ShapeDef::Type::Line;
    def.params[0] = x1;
    def.params[1] = y1;
    def.params[2] = x2;
    def.params[3] = y2;
    shapes_.push_back(std::move(def));
    dirty_ = true;
    return *this;
}

ShapeSeries& ShapeSeries::arrow(float x1, float y1, float x2, float y2)
{
    ShapeDef def;
    def.type      = ShapeDef::Type::Arrow;
    def.params[0] = x1;
    def.params[1] = y1;
    def.params[2] = x2;
    def.params[3] = y2;
    shapes_.push_back(std::move(def));
    dirty_ = true;
    return *this;
}

ShapeSeries& ShapeSeries::ring(float cx, float cy, float outer_r, float inner_r)
{
    ShapeDef def;
    def.type      = ShapeDef::Type::Ring;
    def.params[0] = cx;
    def.params[1] = cy;
    def.params[2] = outer_r;
    def.params[3] = inner_r;
    shapes_.push_back(std::move(def));
    dirty_ = true;
    return *this;
}

ShapeSeries& ShapeSeries::polygon(std::span<const float> x, std::span<const float> y)
{
    ShapeDef def;
    def.type   = ShapeDef::Type::Polygon;
    def.poly_x = std::vector<float>(x.begin(), x.end());
    def.poly_y = std::vector<float>(y.begin(), y.end());
    shapes_.push_back(std::move(def));
    dirty_ = true;
    return *this;
}

ShapeSeries& ShapeSeries::text(float x, float y, const std::string& content)
{
    ShapeDef def;
    def.type         = ShapeDef::Type::Text;
    def.params[0]    = x;
    def.params[1]    = y;
    def.text_content = content;
    shapes_.push_back(std::move(def));
    dirty_ = true;
    return *this;
}

// ── Per-shape style modifiers ───────────────────────────────────────────────

ShapeSeries& ShapeSeries::fill_color(const Color& c)
{
    if (!shapes_.empty())
    {
        shapes_.back().fill_color = c;
        dirty_                    = true;
    }
    return *this;
}

ShapeSeries& ShapeSeries::line_color(const Color& c)
{
    if (!shapes_.empty())
    {
        shapes_.back().line_color = c;
        dirty_                    = true;
    }
    return *this;
}

ShapeSeries& ShapeSeries::fill_opacity(float opacity)
{
    if (!shapes_.empty())
    {
        shapes_.back().fill_opacity = opacity;
        dirty_                      = true;
    }
    return *this;
}

ShapeSeries& ShapeSeries::line_width(float w)
{
    if (!shapes_.empty())
    {
        shapes_.back().line_width = w;
        dirty_                    = true;
    }
    return *this;
}

ShapeSeries& ShapeSeries::corner_radius(float r)
{
    if (!shapes_.empty())
    {
        shapes_.back().corner_radius = r;
        dirty_                       = true;
    }
    return *this;
}

ShapeSeries& ShapeSeries::rotation(float radians)
{
    if (!shapes_.empty())
    {
        shapes_.back().rotation = radians;
        dirty_                  = true;
    }
    return *this;
}

ShapeSeries& ShapeSeries::segments(int n)
{
    if (!shapes_.empty())
    {
        shapes_.back().segments = n;
        dirty_                  = true;
    }
    return *this;
}

ShapeSeries& ShapeSeries::arrow_head(float length_frac, float width)
{
    if (!shapes_.empty())
    {
        shapes_.back().arrow_head_frac  = length_frac;
        shapes_.back().arrow_head_width = width;
        dirty_                          = true;
    }
    return *this;
}

ShapeSeries& ShapeSeries::text_align(int align)
{
    if (!shapes_.empty())
    {
        shapes_.back().text_align = align;
        dirty_                    = true;
    }
    return *this;
}

ShapeSeries& ShapeSeries::text_valign(int valign)
{
    if (!shapes_.empty())
    {
        shapes_.back().text_valign = valign;
        dirty_                     = true;
    }
    return *this;
}

// ── Bulk operations ─────────────────────────────────────────────────────────

ShapeSeries& ShapeSeries::clear_shapes()
{
    shapes_.clear();
    line_x_.clear();
    line_y_.clear();
    fill_verts_.clear();
    text_annotations_.clear();
    dirty_ = true;
    return *this;
}

// ── Geometry helpers ────────────────────────────────────────────────────────

void ShapeSeries::rotate_point(float& x, float& y, float cx, float cy, float angle)
{
    float dx    = x - cx;
    float dy    = y - cy;
    float cos_a = std::cos(angle);
    float sin_a = std::sin(angle);
    x           = cx + dx * cos_a - dy * sin_a;
    y           = cy + dx * sin_a + dy * cos_a;
}

void ShapeSeries::push_nan_break()
{
    line_x_.push_back(NaN);
    line_y_.push_back(NaN);
}

void ShapeSeries::push_fill_tri(float x0,
                                float y0,
                                float a0,
                                float x1,
                                float y1,
                                float a1,
                                float x2,
                                float y2,
                                float a2)
{
    fill_verts_.push_back(x0);
    fill_verts_.push_back(y0);
    fill_verts_.push_back(a0);
    fill_verts_.push_back(x1);
    fill_verts_.push_back(y1);
    fill_verts_.push_back(a1);
    fill_verts_.push_back(x2);
    fill_verts_.push_back(y2);
    fill_verts_.push_back(a2);
}

void ShapeSeries::push_outline_seg(float x0, float y0, float x1, float y1)
{
    line_x_.push_back(x0);
    line_y_.push_back(y0);
    line_x_.push_back(x1);
    line_y_.push_back(y1);
}

// ── Geometry generation ─────────────────────────────────────────────────────

void ShapeSeries::generate_rect(const ShapeDef& def)
{
    float x = def.params[0];
    float y = def.params[1];
    float w = def.params[2];
    float h = def.params[3];

    float cx = x + w * 0.5f;
    float cy = y + h * 0.5f;

    if (def.corner_radius > 0.0f)
    {
        // Rounded rectangle: approximate corners with arcs
        float         r        = std::min(def.corner_radius, std::min(w * 0.5f, h * 0.5f));
        constexpr int arc_segs = 8;

        // Build vertices of rounded rect
        std::vector<float> vx, vy;
        vx.reserve(4 * (arc_segs + 1));
        vy.reserve(4 * (arc_segs + 1));

        // Corner centers
        float corners[4][2] = {
            {x + w - r, y + h - r},   // bottom-right
            {x + r, y + h - r},       // bottom-left
            {x + r, y + r},           // top-left
            {x + w - r, y + r},       // top-right
        };
        float start_angles[4] = {0.0f, PI * 0.5f, PI, PI * 1.5f};

        for (int c = 0; c < 4; ++c)
        {
            for (int i = 0; i <= arc_segs; ++i)
            {
                float angle = start_angles[c] + (PI * 0.5f) * static_cast<float>(i) / arc_segs;
                float px    = corners[c][0] + r * std::cos(angle);
                float py    = corners[c][1] + r * std::sin(angle);
                if (def.rotation != 0.0f)
                    rotate_point(px, py, cx, cy, def.rotation);
                vx.push_back(px);
                vy.push_back(py);
            }
        }

        size_t n = vx.size();

        // Fill: fan from center
        float fcx = cx, fcy = cy;
        if (def.rotation != 0.0f)
            rotate_point(fcx, fcy, cx, cy, def.rotation);
        for (size_t i = 0; i < n; ++i)
        {
            size_t j = (i + 1) % n;
            push_fill_tri(fcx,
                          fcy,
                          def.fill_opacity,
                          vx[i],
                          vy[i],
                          def.fill_opacity,
                          vx[j],
                          vy[j],
                          def.fill_opacity);
        }

        // Outline
        if (!line_x_.empty())
            push_nan_break();
        for (size_t i = 0; i < n; ++i)
        {
            size_t j = (i + 1) % n;
            push_outline_seg(vx[i], vy[i], vx[j], vy[j]);
        }
    }
    else
    {
        // Simple rectangle
        float x0 = x, y0 = y;
        float x1 = x + w, y1 = y;
        float x2 = x + w, y2 = y + h;
        float x3 = x, y3 = y + h;

        if (def.rotation != 0.0f)
        {
            rotate_point(x0, y0, cx, cy, def.rotation);
            rotate_point(x1, y1, cx, cy, def.rotation);
            rotate_point(x2, y2, cx, cy, def.rotation);
            rotate_point(x3, y3, cx, cy, def.rotation);
        }

        // Fill: two triangles
        push_fill_tri(x0, y0, def.fill_opacity, x1, y1, def.fill_opacity, x2, y2, def.fill_opacity);
        push_fill_tri(x0, y0, def.fill_opacity, x2, y2, def.fill_opacity, x3, y3, def.fill_opacity);

        // Outline: four edges
        if (!line_x_.empty())
            push_nan_break();
        push_outline_seg(x0, y0, x1, y1);
        push_outline_seg(x1, y1, x2, y2);
        push_outline_seg(x2, y2, x3, y3);
        push_outline_seg(x3, y3, x0, y0);
    }
}

void ShapeSeries::generate_circle(const ShapeDef& def)
{
    float cx   = def.params[0];
    float cy   = def.params[1];
    float r    = def.params[2];
    int   segs = def.segments;

    // Fill: fan from center
    for (int i = 0; i < segs; ++i)
    {
        float a0  = 2.0f * PI * static_cast<float>(i) / segs;
        float a1  = 2.0f * PI * static_cast<float>(i + 1) / segs;
        float px0 = cx + r * std::cos(a0);
        float py0 = cy + r * std::sin(a0);
        float px1 = cx + r * std::cos(a1);
        float py1 = cy + r * std::sin(a1);
        push_fill_tri(cx,
                      cy,
                      def.fill_opacity,
                      px0,
                      py0,
                      def.fill_opacity,
                      px1,
                      py1,
                      def.fill_opacity);
    }

    // Outline
    if (!line_x_.empty())
        push_nan_break();
    for (int i = 0; i < segs; ++i)
    {
        float a0  = 2.0f * PI * static_cast<float>(i) / segs;
        float a1  = 2.0f * PI * static_cast<float>(i + 1) / segs;
        float px0 = cx + r * std::cos(a0);
        float py0 = cy + r * std::sin(a0);
        float px1 = cx + r * std::cos(a1);
        float py1 = cy + r * std::sin(a1);
        push_outline_seg(px0, py0, px1, py1);
    }
}

void ShapeSeries::generate_ellipse(const ShapeDef& def)
{
    float cx   = def.params[0];
    float cy   = def.params[1];
    float rx   = def.params[2];
    float ry   = def.params[3];
    int   segs = def.segments;

    // Fill: fan from center
    for (int i = 0; i < segs; ++i)
    {
        float a0  = 2.0f * PI * static_cast<float>(i) / segs;
        float a1  = 2.0f * PI * static_cast<float>(i + 1) / segs;
        float px0 = cx + rx * std::cos(a0);
        float py0 = cy + ry * std::sin(a0);
        float px1 = cx + rx * std::cos(a1);
        float py1 = cy + ry * std::sin(a1);

        if (def.rotation != 0.0f)
        {
            rotate_point(px0, py0, cx, cy, def.rotation);
            rotate_point(px1, py1, cx, cy, def.rotation);
        }

        push_fill_tri(cx,
                      cy,
                      def.fill_opacity,
                      px0,
                      py0,
                      def.fill_opacity,
                      px1,
                      py1,
                      def.fill_opacity);
    }

    // Outline
    if (!line_x_.empty())
        push_nan_break();
    for (int i = 0; i < segs; ++i)
    {
        float a0  = 2.0f * PI * static_cast<float>(i) / segs;
        float a1  = 2.0f * PI * static_cast<float>(i + 1) / segs;
        float px0 = cx + rx * std::cos(a0);
        float py0 = cy + ry * std::sin(a0);
        float px1 = cx + rx * std::cos(a1);
        float py1 = cy + ry * std::sin(a1);

        if (def.rotation != 0.0f)
        {
            rotate_point(px0, py0, cx, cy, def.rotation);
            rotate_point(px1, py1, cx, cy, def.rotation);
        }

        push_outline_seg(px0, py0, px1, py1);
    }
}

void ShapeSeries::generate_line(const ShapeDef& def)
{
    float x1 = def.params[0];
    float y1 = def.params[1];
    float x2 = def.params[2];
    float y2 = def.params[3];

    // Lines have no fill, only outline
    if (!line_x_.empty())
        push_nan_break();
    push_outline_seg(x1, y1, x2, y2);
}

void ShapeSeries::generate_arrow(const ShapeDef& def)
{
    float x1 = def.params[0];
    float y1 = def.params[1];
    float x2 = def.params[2];
    float y2 = def.params[3];

    float dx  = x2 - x1;
    float dy  = y2 - y1;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-8f)
        return;

    float nx = dx / len;   // unit direction
    float ny = dy / len;
    float px = -ny;   // perpendicular
    float py = nx;

    float head_len = len * def.arrow_head_frac;
    float head_w   = def.arrow_head_width;

    // Shaft line (from start to base of arrowhead)
    float shaft_end_x = x2 - nx * head_len;
    float shaft_end_y = y2 - ny * head_len;

    if (!line_x_.empty())
        push_nan_break();
    push_outline_seg(x1, y1, shaft_end_x, shaft_end_y);

    // Arrowhead: filled triangle
    float left_x  = shaft_end_x + px * head_w;
    float left_y  = shaft_end_y + py * head_w;
    float right_x = shaft_end_x - px * head_w;
    float right_y = shaft_end_y - py * head_w;

    push_fill_tri(x2,
                  y2,
                  def.fill_opacity,
                  left_x,
                  left_y,
                  def.fill_opacity,
                  right_x,
                  right_y,
                  def.fill_opacity);

    // Arrowhead outline
    push_outline_seg(x2, y2, left_x, left_y);
    push_outline_seg(left_x, left_y, right_x, right_y);
    push_outline_seg(right_x, right_y, x2, y2);
}

void ShapeSeries::generate_ring(const ShapeDef& def)
{
    float cx      = def.params[0];
    float cy      = def.params[1];
    float outer_r = def.params[2];
    float inner_r = def.params[3];
    int   segs    = def.segments;

    // Fill: ring as a series of quads (two triangles each)
    for (int i = 0; i < segs; ++i)
    {
        float a0 = 2.0f * PI * static_cast<float>(i) / segs;
        float a1 = 2.0f * PI * static_cast<float>(i + 1) / segs;

        float ox0 = cx + outer_r * std::cos(a0);
        float oy0 = cy + outer_r * std::sin(a0);
        float ox1 = cx + outer_r * std::cos(a1);
        float oy1 = cy + outer_r * std::sin(a1);
        float ix0 = cx + inner_r * std::cos(a0);
        float iy0 = cy + inner_r * std::sin(a0);
        float ix1 = cx + inner_r * std::cos(a1);
        float iy1 = cy + inner_r * std::sin(a1);

        push_fill_tri(ox0,
                      oy0,
                      def.fill_opacity,
                      ox1,
                      oy1,
                      def.fill_opacity,
                      ix0,
                      iy0,
                      def.fill_opacity);
        push_fill_tri(ix0,
                      iy0,
                      def.fill_opacity,
                      ox1,
                      oy1,
                      def.fill_opacity,
                      ix1,
                      iy1,
                      def.fill_opacity);
    }

    // Outline: outer circle + inner circle
    if (!line_x_.empty())
        push_nan_break();
    for (int i = 0; i < segs; ++i)
    {
        float a0  = 2.0f * PI * static_cast<float>(i) / segs;
        float a1  = 2.0f * PI * static_cast<float>(i + 1) / segs;
        float px0 = cx + outer_r * std::cos(a0);
        float py0 = cy + outer_r * std::sin(a0);
        float px1 = cx + outer_r * std::cos(a1);
        float py1 = cy + outer_r * std::sin(a1);
        push_outline_seg(px0, py0, px1, py1);
    }
    push_nan_break();
    for (int i = 0; i < segs; ++i)
    {
        float a0  = 2.0f * PI * static_cast<float>(i) / segs;
        float a1  = 2.0f * PI * static_cast<float>(i + 1) / segs;
        float px0 = cx + inner_r * std::cos(a0);
        float py0 = cy + inner_r * std::sin(a0);
        float px1 = cx + inner_r * std::cos(a1);
        float py1 = cy + inner_r * std::sin(a1);
        push_outline_seg(px0, py0, px1, py1);
    }
}

void ShapeSeries::generate_polygon(const ShapeDef& def)
{
    const auto& px = def.poly_x;
    const auto& py = def.poly_y;
    size_t      n  = std::min(px.size(), py.size());
    if (n < 3)
        return;

    // Compute centroid for fan triangulation
    float cx = 0.0f, cy = 0.0f;
    for (size_t i = 0; i < n; ++i)
    {
        cx += px[i];
        cy += py[i];
    }
    cx /= static_cast<float>(n);
    cy /= static_cast<float>(n);

    // Fill: fan from centroid
    for (size_t i = 0; i < n; ++i)
    {
        size_t j   = (i + 1) % n;
        float  vx0 = px[i], vy0 = py[i];
        float  vx1 = px[j], vy1 = py[j];

        if (def.rotation != 0.0f)
        {
            rotate_point(vx0, vy0, cx, cy, def.rotation);
            rotate_point(vx1, vy1, cx, cy, def.rotation);
        }

        float rcx = cx, rcy = cy;
        if (def.rotation != 0.0f)
            rotate_point(rcx, rcy, cx, cy, def.rotation);

        push_fill_tri(rcx,
                      rcy,
                      def.fill_opacity,
                      vx0,
                      vy0,
                      def.fill_opacity,
                      vx1,
                      vy1,
                      def.fill_opacity);
    }

    // Outline
    if (!line_x_.empty())
        push_nan_break();
    for (size_t i = 0; i < n; ++i)
    {
        size_t j   = (i + 1) % n;
        float  vx0 = px[i], vy0 = py[i];
        float  vx1 = px[j], vy1 = py[j];

        if (def.rotation != 0.0f)
        {
            float pcx = cx, pcy = cy;
            rotate_point(vx0, vy0, pcx, pcy, def.rotation);
            rotate_point(vx1, vy1, pcx, pcy, def.rotation);
        }

        push_outline_seg(vx0, vy0, vx1, vy1);
    }
}

// ── Rebuild geometry ────────────────────────────────────────────────────────

void ShapeSeries::rebuild_geometry()
{
    line_x_.clear();
    line_y_.clear();
    fill_verts_.clear();
    text_annotations_.clear();

    for (const auto& def : shapes_)
    {
        switch (def.type)
        {
            case ShapeDef::Type::Rect:
                generate_rect(def);
                break;
            case ShapeDef::Type::Circle:
                generate_circle(def);
                break;
            case ShapeDef::Type::Ellipse:
                generate_ellipse(def);
                break;
            case ShapeDef::Type::Line:
                generate_line(def);
                break;
            case ShapeDef::Type::Arrow:
                generate_arrow(def);
                break;
            case ShapeDef::Type::Ring:
                generate_ring(def);
                break;
            case ShapeDef::Type::Polygon:
                generate_polygon(def);
                break;
            case ShapeDef::Type::Text:
            {
                TextAnnotation ann;
                ann.x          = def.params[0];
                ann.y          = def.params[1];
                ann.content    = def.text_content;
                ann.font_size  = def.text_font_size;
                ann.align      = def.text_align;
                ann.valign     = def.text_valign;
                ann.text_color = def.line_color;   // use line_color for text
                text_annotations_.push_back(std::move(ann));
                break;
            }
        }
    }
}

}   // namespace spectra
