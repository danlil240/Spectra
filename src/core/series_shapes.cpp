#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>
#include <spectra/series_shapes.hpp>

namespace spectra
{

static constexpr float PI  = 3.14159265358979f;
static constexpr float NaN = std::numeric_limits<float>::quiet_NaN();

namespace
{

float cross2(float ax, float ay, float bx, float by, float cx, float cy)
{
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

bool point_in_tri(float px, float py, float ax, float ay, float bx, float by, float cx, float cy)
{
    // Assumes CCW triangle (a, b, c). Points on the boundary count as inside so
    // that an ear touching another vertex is rejected.
    return cross2(ax, ay, bx, by, px, py) >= 0.0f && cross2(bx, by, cx, cy, px, py) >= 0.0f
           && cross2(cx, cy, ax, ay, px, py) >= 0.0f;
}

// Ear-clipping triangulation of a simple polygon (convex or concave, either
// winding). Writes vertex-index triples to `out`. For degenerate or
// self-intersecting input where no ear can be found, the remainder is fanned
// so that some fill is still produced.
void triangulate_polygon(const std::vector<float>& x,
                         const std::vector<float>& y,
                         std::vector<size_t>&      out)
{
    out.clear();
    const size_t n = std::min(x.size(), y.size());
    if (n < 3)
        return;

    // Signed area: orient the working index list CCW.
    double area2 = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const size_t j = (i + 1) % n;
        area2 += static_cast<double>(x[i]) * y[j] - static_cast<double>(x[j]) * y[i];
    }

    std::vector<size_t> idx(n);
    for (size_t i = 0; i < n; ++i)
        idx[i] = (area2 >= 0.0) ? i : (n - 1 - i);

    out.reserve((n - 2) * 3);

    size_t guard = 0;
    size_t i     = 0;
    while (idx.size() > 3)
    {
        const size_t m    = idx.size();
        const size_t prev = idx[(i + m - 1) % m];
        const size_t cur  = idx[i % m];
        const size_t next = idx[(i + 1) % m];

        const float turn = cross2(x[prev], y[prev], x[cur], y[cur], x[next], y[next]);
        if (turn == 0.0f)
        {
            // Collinear (or duplicate) vertex: contributes no area, drop it.
            idx.erase(idx.begin() + static_cast<ptrdiff_t>(i % m));
            guard = 0;
            if (i >= idx.size())
                i = 0;
            continue;
        }

        bool is_ear = turn > 0.0f;
        if (is_ear)
        {
            for (size_t k = 0; k < m; ++k)
            {
                const size_t v = idx[k];
                if (v == prev || v == cur || v == next)
                    continue;
                if (point_in_tri(x[v], y[v], x[prev], y[prev], x[cur], y[cur], x[next], y[next]))
                {
                    is_ear = false;
                    break;
                }
            }
        }

        if (is_ear)
        {
            out.push_back(prev);
            out.push_back(cur);
            out.push_back(next);
            idx.erase(idx.begin() + static_cast<ptrdiff_t>(i % m));
            guard = 0;
            if (i >= idx.size())
                i = 0;
            continue;
        }

        i = (i + 1) % m;
        if (++guard > m)
        {
            // No ear found in a full pass (self-intersecting input): fan the
            // remainder so some fill is still produced.
            for (size_t k = 1; k + 1 < idx.size(); ++k)
            {
                out.push_back(idx[0]);
                out.push_back(idx[k]);
                out.push_back(idx[k + 1]);
            }
            return;
        }
    }

    out.push_back(idx[0]);
    out.push_back(idx[1]);
    out.push_back(idx[2]);
}

}   // namespace

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

    // Normalize negative extents to a flipped rect so the rounded-corner radius
    // below stays non-negative (the center, and thus rotation, is unchanged).
    if (w < 0.0f)
    {
        x += w;
        w = -w;
    }
    if (h < 0.0f)
    {
        y += h;
        h = -h;
    }

    float cx = x + w * 0.5f;
    float cy = y + h * 0.5f;

    if (def.corner_radius > 0.0f)
    {
        // Rounded rectangle: approximate corners with arcs
        float         r        = std::min(def.corner_radius, std::min(w * 0.5f, h * 0.5f));
        constexpr int arc_segs = 8;

        // Build vertices of rounded rect
        std::vector<float> vx;
        std::vector<float> vy;
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
        float fcx = cx;
        float fcy = cy;
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
        float x0 = x;
        float y0 = y;
        float x1 = x + w;
        float y1 = y;
        float x2 = x + w;
        float y2 = y + h;
        float x3 = x;
        float y3 = y + h;

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

    if (def.rotation != 0.0f)
    {
        const float cx = (x1 + x2) * 0.5f;
        const float cy = (y1 + y2) * 0.5f;
        rotate_point(x1, y1, cx, cy, def.rotation);
        rotate_point(x2, y2, cx, cy, def.rotation);
    }

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

    if (def.rotation != 0.0f)
    {
        const float cx = (x1 + x2) * 0.5f;
        const float cy = (y1 + y2) * 0.5f;
        rotate_point(x1, y1, cx, cy, def.rotation);
        rotate_point(x2, y2, cx, cy, def.rotation);
    }

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

    // Rotation pivot: vertex average.
    float cx = 0.0f;
    float cy = 0.0f;
    for (size_t i = 0; i < n; ++i)
    {
        cx += px[i];
        cy += py[i];
    }
    cx /= static_cast<float>(n);
    cy /= static_cast<float>(n);

    // Transformed vertices (rotation applied once).
    std::vector<float> vx(px.begin(), px.begin() + static_cast<ptrdiff_t>(n));
    std::vector<float> vy(py.begin(), py.begin() + static_cast<ptrdiff_t>(n));
    if (def.rotation != 0.0f)
    {
        for (size_t i = 0; i < n; ++i)
            rotate_point(vx[i], vy[i], cx, cy, def.rotation);
    }

    // Fill: ear-clipping triangulation (correct for concave simple polygons).
    std::vector<size_t> tris;
    triangulate_polygon(vx, vy, tris);
    for (size_t t = 0; t + 2 < tris.size(); t += 3)
    {
        const size_t a = tris[t];
        const size_t b = tris[t + 1];
        const size_t c = tris[t + 2];
        push_fill_tri(vx[a],
                      vy[a],
                      def.fill_opacity,
                      vx[b],
                      vy[b],
                      def.fill_opacity,
                      vx[c],
                      vy[c],
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
