// svg_export.cpp — vector export of a Figure, bypassing the GPU pipeline.
//
// Mirrors what the Vulkan renderer draws (src/render/render_2d.cpp,
// render_geometry.cpp, render_3d.cpp): theme colours for the chrome, the
// renderer's dash patterns and SDF marker shapes, the CPU-side fill/outline
// geometry of the statistical and shape series, and a painter-sorted
// projection of 3D axes through the axes' camera.

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <span>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/camera.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>
#include <spectra/series_shapes.hpp>
#include <spectra/series_stats.hpp>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ui/overlay/legend_layout.hpp"

namespace spectra
{

namespace
{

// ─── Formatting helpers ─────────────────────────────────────────────────────

// Compact fixed-point number (2 decimals, trailing zeros trimmed).  Pixel
// coordinates need sub-pixel precision regardless of magnitude, so a
// significant-digit format would be wrong for wide figures.
void put_num(std::string& out, double v)
{
    if (!std::isfinite(v))
        v = 0.0;
    char  buf[48];
    auto  res = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::fixed, 2);
    char* end = res.ptr;
    char* dot = std::find(buf, end, '.');
    if (dot != end)
    {
        while (end > dot && end[-1] == '0')
            --end;
        if (end == dot + 1)
            end = dot;
    }
    std::string_view s(buf, static_cast<size_t>(end - buf));
    if (s == "-0")
        s = "0";
    out.append(s);
}

std::string num(double v)
{
    std::string s;
    put_num(s, v);
    return s;
}

float clamp01(float v)
{
    return std::clamp(std::isfinite(v) ? v : 0.0f, 0.0f, 1.0f);
}

// "#rrggbb" — alpha is emitted separately as an *-opacity attribute.
std::string hex(const Color& c)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string           out      = "#";
    for (float ch : {c.r, c.g, c.b})
    {
        const int v = static_cast<int>(std::lround(clamp01(ch) * 255.0f));
        out += digits[(v >> 4) & 0xF];
        out += digits[v & 0xF];
    }
    return out;
}

// ` name="a"` when a is visibly below 1, otherwise nothing.
std::string opacity_attr(const char* name, float a)
{
    a = clamp01(a);
    if (a >= 0.999f)
        return {};
    return std::string(" ") + name + "=\"" + num(std::round(a * 1000.0f) / 1000.0f) + "\"";
}

// XML-escape text for element content or attribute values.  UTF-8 bytes are
// kept as-is (the document is declared UTF-8); control characters that XML
// 1.0 forbids are dropped so the file stays well-formed.
std::string xml_escape(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (char ch : s)
    {
        const auto u = static_cast<unsigned char>(ch);
        switch (ch)
        {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            case '\'':
                out += "&apos;";
                break;
            default:
                if (u < 0x20 && ch != '\t' && ch != '\n' && ch != '\r')
                    break;
                out += ch;
                break;
        }
    }
    return out;
}

// Rough advance width of a UTF-8 string in a proportional sans-serif face.
// Only used for layout decisions (legend width, label overlap), so a
// per-class estimate is sufficient.
float estimate_text_width(std::string_view s, float font_px)
{
    float  em = 0.0f;
    size_t i  = 0;
    while (i < s.size())
    {
        const auto c0  = static_cast<unsigned char>(s[i]);
        uint32_t   cp  = c0;
        size_t     len = 1;
        if (c0 >= 0xF0)
            len = 4;
        else if (c0 >= 0xE0)
            len = 3;
        else if (c0 >= 0xC0)
            len = 2;
        if (len > 1 && i + len <= s.size())
        {
            cp = c0 & (0xFFu >> (len + 1));
            for (size_t k = 1; k < len; ++k)
                cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3Fu);
        }
        i += len;

        if (cp == ' ')
            em += 0.28f;
        else if (cp < 0x80)
        {
            const char c = static_cast<char>(cp);
            if (std::string_view("iljI.,:;!|'`").find(c) != std::string_view::npos)
                em += 0.28f;
            else if (std::string_view("mwMW@%").find(c) != std::string_view::npos)
                em += 0.86f;
            else if (c >= 'A' && c <= 'Z')
                em += 0.66f;
            else if (c >= '0' && c <= '9')
                em += 0.58f;
            else if (std::string_view("()[]{}-/\\ftr").find(c) != std::string_view::npos)
                em += 0.36f;
            else
                em += 0.54f;
        }
        else if (cp >= 0x2E80)
            em += 1.0f;   // CJK and other full-width scripts
        else
            em += 0.62f;
    }
    return em * font_px;
}

// Renderer font sizes (text_renderer.cpp FONT_PIXEL_SIZES).
constexpr float TICK_FONT_PX  = 11.5f;
constexpr float LABEL_FONT_PX = 13.5f;
constexpr float TITLE_FONT_PX = 21.0f;

enum class VAlign
{
    Top,
    Middle,
    Bottom
};

float baseline_offset(VAlign v, float font_px)
{
    switch (v)
    {
        case VAlign::Top:
            return 0.80f * font_px;
        case VAlign::Middle:
            return 0.36f * font_px;
        case VAlign::Bottom:
        default:
            return -0.22f * font_px;
    }
}

// ─── Colormaps ──────────────────────────────────────────────────────────────

// Same three-stop ramps as apply_colormap() in scatter.frag.
Color scatter_colormap(ColormapType cm, float t)
{
    using RGB = std::array<float, 3>;
    t         = clamp01(t);
    auto mix  = [](const RGB& a, const RGB& b, float u)
    { return Color{a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u, a[2] + (b[2] - a[2]) * u}; };
    auto three = [&](const RGB& c0, const RGB& c1, const RGB& c2)
    { return t < 0.5f ? mix(c0, c1, t * 2.0f) : mix(c1, c2, (t - 0.5f) * 2.0f); };
    switch (cm)
    {
        case ColormapType::Viridis:
            return three({0.267f, 0.005f, 0.329f},
                         {0.128f, 0.567f, 0.551f},
                         {0.993f, 0.906f, 0.144f});
        case ColormapType::Plasma:
            return three({0.050f, 0.030f, 0.528f},
                         {0.798f, 0.280f, 0.470f},
                         {0.940f, 0.975f, 0.131f});
        case ColormapType::Inferno:
            return three({0.001f, 0.000f, 0.014f},
                         {0.735f, 0.216f, 0.330f},
                         {0.988f, 0.998f, 0.645f});
        case ColormapType::Magma:
            return three({0.001f, 0.000f, 0.014f},
                         {0.716f, 0.215f, 0.475f},
                         {0.987f, 0.991f, 0.750f});
        case ColormapType::Jet:
            return Color{clamp01(1.5f - std::fabs(4.0f * t - 3.0f)),
                         clamp01(1.5f - std::fabs(4.0f * t - 2.0f)),
                         clamp01(1.5f - std::fabs(4.0f * t - 1.0f))};
        case ColormapType::Coolwarm:
            return three({0.230f, 0.299f, 0.754f},
                         {0.865f, 0.865f, 0.865f},
                         {0.706f, 0.016f, 0.150f});
        case ColormapType::Grayscale:
            return Color{t, t, t};
        case ColormapType::None:
        default:
            return Color{0.5f, 0.5f, 0.5f};
    }
}

// ─── Export context ─────────────────────────────────────────────────────────

struct SvgContext
{
    const SvgStyle&    style;
    std::ostringstream defs;
    std::ostringstream body;
    int                next_id = 0;
    // (marker style, size in 1/100 px) -> symbol id
    std::map<std::pair<int, int>, std::string> marker_ids;

    explicit SvgContext(const SvgStyle& s) : style(s) {}

    std::string new_id(std::string_view prefix)
    {
        return std::string(prefix) + std::to_string(next_id++);
    }

    std::string marker_symbol(MarkerStyle ms, float size);
};

float luma(const Color& c)
{
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

// Same rule as ui::adapt_series_color_to_theme(): pure black on a dark
// background (or pure white on a light one) is drawn in the theme foreground.
Color theme_series_color(const Series& s, const SvgStyle& st)
{
    Color       c        = s.color();
    const float bg_luma  = luma(st.background);
    const bool  is_black = c.r == 0.0f && c.g == 0.0f && c.b == 0.0f;
    const bool  is_white = c.r == 1.0f && c.g == 1.0f && c.b == 1.0f;
    if ((is_black && bg_luma < 0.35f) || (is_white && bg_luma > 0.65f))
    {
        c.r = st.text_primary.r;
        c.g = st.text_primary.g;
        c.b = st.text_primary.b;
    }
    return c;
}

float series_alpha(const Series& s)
{
    return clamp01(s.color().a * s.opacity());
}

// Scatter without an explicit marker uses the renderer's theme default:
// filled discs on light backgrounds, rings on dark ones.
MarkerStyle default_scatter_marker(MarkerStyle ms, const SvgStyle& st)
{
    if (ms != MarkerStyle::None)
        return ms;
    return luma(st.background) > 0.80f ? MarkerStyle::FilledCircle : MarkerStyle::Circle;
}

// ─── Markers (<symbol> per style+size, instanced with <use>) ────────────────

// Geometry follows the SDFs in scatter.frag.  The shader quad spans
// ±(size/2 + 1) px for UV ±1; outline shapes draw a ring of width
// clamp(4/size, 0.08, 0.25) UV inside the shape boundary.
std::string marker_content(MarkerStyle ms, float size)
{
    const float s  = size * 0.5f + 1.0f;
    const float sw = std::clamp(4.0f / std::max(size, 1e-3f), 0.08f, 0.25f) * s;

    struct P
    {
        float x, y;
    };
    // uv (y-up) polygon -> svg (y-down) points, inset for outlined shapes so
    // the stroke lies inside the shape boundary like the SDF ring.
    auto polygon = [&](const std::vector<P>& uv, bool filled, float inradius_uv)
    {
        const float k = filled ? s : s * std::max(0.1f, 1.0f - sw / (2.0f * inradius_uv * s));
        std::string out = "<polygon points=\"";
        for (size_t i = 0; i < uv.size(); ++i)
        {
            if (i > 0)
                out += ' ';
            put_num(out, uv[i].x * k);
            out += ',';
            put_num(out, -uv[i].y * k);
        }
        out += "\"";
        if (filled)
            out += " fill=\"currentColor\"/>";
        else
            out += " fill=\"none\" stroke=\"currentColor\" stroke-width=\"" + num(sw) + "\"/>";
        return out;
    };
    auto regular = [](int n, float r, float start_deg)
    {
        std::vector<P> pts;
        for (int i = 0; i < n; ++i)
        {
            const float a = (start_deg + 360.0f * static_cast<float>(i) / static_cast<float>(n))
                            * 3.14159265f / 180.0f;
            pts.push_back({r * std::cos(a), r * std::sin(a)});
        }
        return pts;
    };
    // sdf_triangle_up: equilateral, half side 0.75, shifted down by 0.15.
    const std::vector<P> tri_up    = {{-0.75f, -0.583f}, {0.75f, -0.583f}, {0.0f, 0.716f}};
    auto                 transform = [&](auto fn)
    {
        std::vector<P> out;
        for (const auto& p : tri_up)
            out.push_back(fn(p));
        return out;
    };
    constexpr float TRI_IN = 0.433f;

    switch (ms)
    {
        case MarkerStyle::Point:
            return "<circle r=\"" + num(0.55f * s) + "\" fill=\"currentColor\"/>";
        case MarkerStyle::Circle:
            return "<circle r=\"" + num(0.85f * s - sw * 0.5f)
                   + "\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"" + num(sw) + "\"/>";
        case MarkerStyle::Plus:
        case MarkerStyle::Cross:
        {
            std::string d;
            if (ms == MarkerStyle::Plus)
            {
                const float a = 0.75f * s;
                d = "M" + num(-a) + " 0H" + num(a) + "M0 " + num(-a) + "V" + num(a);
            }
            else
            {
                const float a = 0.5303f * s;
                d = "M" + num(-a) + " " + num(-a) + "L" + num(a) + " " + num(a) + "M" + num(-a)
                    + " " + num(a) + "L" + num(a) + " " + num(-a);
            }
            return "<path d=\"" + d + "\" fill=\"none\" stroke=\"currentColor\" stroke-width=\""
                   + num(0.3f * s) + "\"/>";
        }
        case MarkerStyle::Star:
        {
            // sdStar(r = 0.9, n = 5, m = 3): inner radius 0.4227.
            std::vector<P> pts;
            for (int i = 0; i < 10; ++i)
            {
                const float r = (i % 2 == 0) ? 0.9f : 0.4227f;
                const float a = (90.0f + 36.0f * static_cast<float>(i)) * 3.14159265f / 180.0f;
                pts.push_back({r * std::cos(a), r * std::sin(a)});
            }
            return polygon(pts, true, 0.4f);
        }
        case MarkerStyle::Square:
        case MarkerStyle::FilledSquare:
            return polygon({{-0.7f, -0.7f}, {0.7f, -0.7f}, {0.7f, 0.7f}, {-0.7f, 0.7f}},
                           ms == MarkerStyle::FilledSquare,
                           0.7f);
        case MarkerStyle::Diamond:
        case MarkerStyle::FilledDiamond:
            return polygon({{0.8485f, 0.0f}, {0.0f, 0.8485f}, {-0.8485f, 0.0f}, {0.0f, -0.8485f}},
                           ms == MarkerStyle::FilledDiamond,
                           0.6f);
        case MarkerStyle::TriangleUp:
        case MarkerStyle::FilledTriangleUp:
            return polygon(tri_up, ms == MarkerStyle::FilledTriangleUp, TRI_IN);
        case MarkerStyle::TriangleDown:
            return polygon(transform([](P p) { return P{p.x, -p.y}; }), false, TRI_IN);
        case MarkerStyle::TriangleLeft:
            return polygon(transform([](P p) { return P{-p.y, p.x}; }), false, TRI_IN);
        case MarkerStyle::TriangleRight:
            return polygon(transform([](P p) { return P{p.y, -p.x}; }), false, TRI_IN);
        case MarkerStyle::Pentagon:
            return polygon(regular(5, 0.8653f, 90.0f), false, 0.7f);
        case MarkerStyle::Hexagon:
            return polygon(regular(6, 0.866f, 0.0f), false, 0.75f);
        case MarkerStyle::FilledCircle:
        case MarkerStyle::None:
        default:
            return "<circle r=\"" + num(0.85f * s) + "\" fill=\"currentColor\"/>";
    }
}

std::string SvgContext::marker_symbol(MarkerStyle ms, float size)
{
    if (!(size > 0.0f) || !std::isfinite(size))
        size = 6.0f;
    const auto key =
        std::make_pair(static_cast<int>(ms), static_cast<int>(std::lround(size * 100.0f)));
    auto it = marker_ids.find(key);
    if (it != marker_ids.end())
        return it->second;
    std::string id = "mk" + std::to_string(marker_ids.size());
    defs << "    <symbol id=\"" << id << "\" overflow=\"visible\">" << marker_content(ms, size)
         << "</symbol>\n";
    marker_ids.emplace(key, id);
    return id;
}

// ─── 2D mapping ─────────────────────────────────────────────────────────────

// Data -> SVG pixel coordinates.  SVG is Y-down, data is Y-up.
struct DataToSvg
{
    float  vp_x, vp_y, vp_w, vp_h;
    double x_min, x_max, y_min, y_max;

    [[nodiscard]] double map_x(double data_x) const
    {
        double range = x_max - x_min;
        if (range == 0.0)
            range = 1.0;
        return vp_x + (data_x - x_min) / range * vp_w;
    }

    [[nodiscard]] double map_y(double data_y) const
    {
        double range = y_max - y_min;
        if (range == 0.0)
            range = 1.0;
        return vp_y + (1.0 - (data_y - y_min) / range) * vp_h;
    }
};

// Clip segment a-b to the rectangle [x0,x1]x[y0,y1] (Liang-Barsky).
// Returns false when nothing is inside; t0/t1 give the visible parameter range.
bool clip_segment(double ax, double ay, double bx, double by, const double r[4], double& t0, double& t1)
{
    t0               = 0.0;
    t1               = 1.0;
    const double dx  = bx - ax;
    const double dy  = by - ay;
    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {ax - r[0], r[1] - ax, ay - r[2], r[3] - ay};
    for (int i = 0; i < 4; ++i)
    {
        if (p[i] == 0.0)
        {
            if (q[i] < 0.0)
                return false;
            continue;
        }
        const double t = q[i] / p[i];
        if (p[i] < 0.0)
            t0 = std::max(t0, t);
        else
            t1 = std::min(t1, t);
        if (t0 > t1)
            return false;
    }
    return true;
}

// Path data for a polyline, broken at non-finite samples (NaN gaps).  Segments
// are clipped to the plot area plus a generous margin so reference lines
// spanning +-1e12 (hline/vline) or far-off data keep sane coordinates.
std::string polyline_path(std::span<const float> x,
                          std::span<const float> y,
                          const DataToSvg&       m,
                          double                 x_offset)
{
    const double margin  = std::max(m.vp_w, m.vp_h) + 50.0;
    const double rect[4] = {m.vp_x - margin, m.vp_x + m.vp_w + margin, m.vp_y - margin,
                            m.vp_y + m.vp_h + margin};
    std::string  d;
    const size_t n       = std::min(x.size(), y.size());
    bool         pen     = false;   // path currently ends exactly at the previous sample
    bool         have    = false;   // previous sample is valid
    double       px = 0, py = 0;
    auto         put_pt  = [&](char cmd, double ux, double uy)
    {
        d += cmd;
        put_num(d, ux);
        d += ' ';
        put_num(d, uy);
    };
    for (size_t i = 0; i < n; ++i)
    {
        if (!std::isfinite(x[i]) || !std::isfinite(y[i]))
        {
            pen  = false;
            have = false;
            continue;
        }
        const double cx = m.map_x(x_offset + x[i]);
        const double cy = m.map_y(y[i]);
        if (have)
        {
            double t0 = 0, t1 = 1;
            if (clip_segment(px, py, cx, cy, rect, t0, t1))
            {
                if (!pen || t0 > 0.0)
                    put_pt('M', px + (cx - px) * t0, py + (cy - py) * t0);
                put_pt(' ', px + (cx - px) * t1, py + (cy - py) * t1);
                pen = t1 >= 1.0;
            }
            else
                pen = false;
        }
        px   = cx;
        py   = cy;
        have = true;
    }
    return d;
}

// stroke-dasharray matching get_dash_pattern() (flat-ended dashes in line.frag).
std::string dash_array(LineStyle ls, float width)
{
    if (ls == LineStyle::Solid || ls == LineStyle::None)
        return {};
    const DashPattern dp = get_dash_pattern(ls, width);
    std::string       out;
    for (int i = 0; i < dp.count && i < 8; ++i)
    {
        if (i > 0)
            out += ',';
        put_num(out, dp.segments[i]);
    }
    return out;
}

// line.frag keeps full coverage out to width/2 and feathers over one more
// pixel, so the drawn line covers about width + 1 px.
constexpr float LINE_AA_EXTRA_PX = 1.0f;

void emit_stroke_path(SvgContext&        c,
                      const std::string& d,
                      const char*        cls,
                      const Color&       col,
                      float              alpha,
                      float              width,
                      LineStyle          ls = LineStyle::Solid)
{
    if (d.empty() || !(width > 0.0f))
        return;
    const std::string dash = dash_array(ls, width);
    c.body << "      <path class=\"" << cls << "\" d=\"" << d << "\" fill=\"none\" stroke=\""
           << hex(col) << "\" stroke-width=\"" << num(width + LINE_AA_EXTRA_PX) << "\""
           << opacity_attr("stroke-opacity", alpha) << " stroke-linejoin=\"round\"";
    if (dash.empty())
        c.body << " stroke-linecap=\"round\"";
    else
        c.body << " stroke-linecap=\"butt\" stroke-dasharray=\"" << dash << "\"";
    c.body << "/>\n";
}

// Instanced markers.  `colors`, when non-empty, gives a per-point colour.
void emit_markers(SvgContext&               c,
                  std::span<const float>    x,
                  std::span<const float>    y,
                  const DataToSvg&          m,
                  double                    x_offset,
                  MarkerStyle               ms,
                  float                     size,
                  const Color&              col,
                  float                     alpha,
                  const std::vector<Color>& colors = {})
{
    const size_t n = std::min(x.size(), y.size());
    if (n == 0 || ms == MarkerStyle::None)
        return;
    const std::string id     = c.marker_symbol(ms, size);
    const float       margin = size + 2.0f;
    c.body << "      <g class=\"markers\" color=\"" << hex(col) << "\""
           << opacity_attr("fill-opacity", alpha) << opacity_attr("stroke-opacity", alpha)
           << ">\n";
    for (size_t i = 0; i < n; ++i)
    {
        if (!std::isfinite(x[i]) || !std::isfinite(y[i]))
            continue;
        const double px = m.map_x(x_offset + x[i]);
        const double py = m.map_y(y[i]);
        if (px < m.vp_x - margin || px > m.vp_x + m.vp_w + margin || py < m.vp_y - margin
            || py > m.vp_y + m.vp_h + margin)
            continue;
        c.body << "        <use xlink:href=\"#" << id << "\" x=\"" << num(px) << "\" y=\""
               << num(py) << "\"";
        if (i < colors.size())
            c.body << " color=\"" << hex(colors[i]) << "\"";
        c.body << "/>\n";
    }
    c.body << "      </g>\n";
}

// Filled triangle list with per-vertex alpha (interleaved x, y, alpha), as
// produced by the stat/shape series for the stat_fill pipeline.  Consecutive
// triangles whose alpha is the same affine function of position are merged
// into one <path>; a varying alpha becomes a linear gradient.  Merging also
// avoids anti-aliasing seams between the triangles of one shape.
void emit_fill(SvgContext&            c,
               std::span<const float> verts,
               const DataToSvg&       m,
               double                 x_offset,
               const Color&           col,
               float                  alpha_scale,
               const char*            cls)
{
    const size_t tri_count = verts.size() / 9;
    if (tri_count == 0 || !(alpha_scale > 0.0f))
        return;

    struct Group
    {
        double      gx = 0, gy = 0, c0 = 0;   // alpha(p) = c0 + gx*px + gy*py
        double      min_x = 1e300, min_y = 1e300, max_x = -1e300, max_y = -1e300;
        std::string d;
    };
    Group              g;
    bool               open   = false;
    int                groups = 0;
    std::ostringstream out;

    auto flush = [&]()
    {
        if (!open || g.d.empty())
            return;
        open = false;
        ++groups;
        const double glen   = std::hypot(g.gx, g.gy);
        const double extent = std::hypot(g.max_x - g.min_x, g.max_y - g.min_y);
        out << "      <path class=\"" << cls << "\" d=\"" << g.d << "\"";
        if (glen * extent < 0.004)
        {
            const double cx = 0.5 * (g.min_x + g.max_x);
            const double cy = 0.5 * (g.min_y + g.max_y);
            const auto   a  = static_cast<float>(g.c0 + g.gx * cx + g.gy * cy);
            out << " fill=\"" << hex(col) << "\""
                   << opacity_attr("fill-opacity", clamp01(a) * alpha_scale) << "/>\n";
            return;
        }
        // Gradient along the alpha slope, spanning the group's bounding box.
        const double dx = g.gx / glen;
        const double dy = g.gy / glen;
        double       t0 = 1e300;
        double       t1 = -1e300;
        for (double px : {g.min_x, g.max_x})
            for (double py : {g.min_y, g.max_y})
            {
                const double t = px * dx + py * dy;
                t0             = std::min(t0, t);
                t1             = std::max(t1, t);
            }
        const auto        a0 = static_cast<float>(g.c0 + glen * t0);
        const auto        a1 = static_cast<float>(g.c0 + glen * t1);
        const std::string id = c.new_id("fg");
        c.defs << "    <linearGradient id=\"" << id << "\" gradientUnits=\"userSpaceOnUse\" x1=\""
               << num(dx * t0) << "\" y1=\"" << num(dy * t0) << "\" x2=\"" << num(dx * t1)
               << "\" y2=\"" << num(dy * t1) << "\"><stop offset=\"0\" stop-color=\"" << hex(col)
               << "\" stop-opacity=\"" << num(clamp01(a0) * alpha_scale)
               << "\"/><stop offset=\"1\" stop-color=\"" << hex(col) << "\" stop-opacity=\""
               << num(clamp01(a1) * alpha_scale) << "\"/></linearGradient>\n";
        out << " fill=\"url(#" << id << ")\"/>\n";
    };

    for (size_t t = 0; t < tri_count; ++t)
    {
        double px[3], py[3], pa[3];
        bool   ok = true;
        for (size_t k = 0; k < 3; ++k)
        {
            const float* v = verts.data() + t * 9 + k * 3;
            if (!std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[2]))
                ok = false;
            px[k] = m.map_x(x_offset + v[0]);
            py[k] = m.map_y(v[1]);
            pa[k] = v[2];
        }
        if (!ok)
            continue;
        const double e1x = px[1] - px[0], e1y = py[1] - py[0];
        const double e2x = px[2] - px[0], e2y = py[2] - py[0];
        const double det = e1x * e2y - e1y * e2x;
        if (std::fabs(det) < 1e-9)
            continue;
        const double da1 = pa[1] - pa[0], da2 = pa[2] - pa[0];
        const double gx  = (da1 * e2y - da2 * e1y) / det;
        const double gy  = (e1x * da2 - e2x * da1) / det;
        const double c0  = pa[0] - gx * px[0] - gy * py[0];

        if (open)
        {
            bool same = true;
            for (int k = 0; k < 3 && same; ++k)
                same = std::fabs(g.c0 + g.gx * px[k] + g.gy * py[k] - pa[k]) < 0.004;
            if (!same)
                flush();
        }
        if (!open)
        {
            g    = Group{};
            g.gx = gx;
            g.gy = gy;
            g.c0 = c0;
            open = true;
        }
        // Consistent winding so overlapping triangles never cancel under nonzero fill.
        int order[3] = {0, 1, 2};
        if (det < 0.0)
            std::swap(order[1], order[2]);
        for (int k = 0; k < 3; ++k)
        {
            const int i = order[k];
            g.d += (k == 0) ? 'M' : 'L';
            put_num(g.d, px[i]);
            g.d += ' ';
            put_num(g.d, py[i]);
            g.min_x = std::min(g.min_x, px[i]);
            g.max_x = std::max(g.max_x, px[i]);
            g.min_y = std::min(g.min_y, py[i]);
            g.max_y = std::max(g.max_y, py[i]);
        }
        g.d += 'Z';
    }
    flush();
    // Separate semi-transparent paths sharing edges show anti-aliasing seams;
    // the outline stroke covers the outer edge, so render multi-part fills crisp.
    if (groups > 1)
        c.body << "      <g shape-rendering=\"crispEdges\">\n" << out.str() << "      </g>\n";
    else
        c.body << out.str();
}

// ─── 2D series ──────────────────────────────────────────────────────────────

// The renderer regenerates stat/shape geometry at upload time.  An export that
// runs before (or without) a render must not read stale geometry, so dirty
// series are exported from a rebuilt copy (ShapeSeries only builds lazily).
template <typename T>
const T* fresh_geometry(const T* s, std::unique_ptr<T>& holder, bool always = false)
{
    if (!s || !(always || s->is_dirty()))
        return s;
    holder = std::make_unique<T>(*s);
    holder->rebuild_geometry();
    return holder.get();
}

void emit_series_2d(SvgContext& c, const Series& s, const DataToSvg& m)
{
    const SvgStyle& st    = c.style;
    const Color     col   = theme_series_color(s, st);
    const float     alpha = series_alpha(s);
    const double    xoff  = s.x_offset();
    const auto&     style = s.plot_style();

    // Fill + outline series (render_2d.cpp: per-type fill alpha / outline width).
    auto fill_and_outline = [&](std::span<const float> fill,
                                std::span<const float> lx,
                                std::span<const float> ly,
                                float                  fill_alpha,
                                float                  outline_w,
                                const char*            cls)
    {
        emit_fill(c, fill, m, xoff, col, alpha * fill_alpha, cls);
        if (lx.size() > 1)
            emit_stroke_path(c, polyline_path(lx, ly, m, xoff), cls, col, alpha, outline_w);
    };

    if (auto* ls = dynamic_cast<const LineSeries*>(&s))
    {
        if (style.has_line() && ls->point_count() >= 2)
            emit_stroke_path(c,
                             polyline_path(ls->x_data(), ls->y_data(), m, xoff),
                             "line",
                             col,
                             alpha,
                             ls->width(),
                             style.line_style);
        if (style.has_marker())
            emit_markers(c,
                         ls->x_data(),
                         ls->y_data(),
                         m,
                         xoff,
                         style.marker_style,
                         style.marker_size,
                         col,
                         alpha);
    }
    else if (auto* sc = dynamic_cast<const ScatterSeries*>(&s))
    {
        std::vector<Color> per_point;
        if (sc->has_colormap())
        {
            const auto  vals  = sc->color_values_data();
            const float lo    = sc->colormap_min();
            const float range = sc->colormap_max() - lo;
            per_point.reserve(vals.size());
            for (float v : vals)
            {
                const float t = std::fabs(range) > 1e-20f ? (v - lo) / range : 0.5f;
                per_point.push_back(std::isfinite(v) ? scatter_colormap(sc->colormap(), t) : col);
            }
        }
        emit_markers(c,
                     sc->x_data(),
                     sc->y_data(),
                     m,
                     xoff,
                     default_scatter_marker(style.marker_style, st),
                     sc->size(),
                     col,
                     alpha,
                     per_point);
    }
    else if (auto* bp_src = dynamic_cast<const BoxPlotSeries*>(&s))
    {
        std::unique_ptr<BoxPlotSeries> bp_copy;
        const BoxPlotSeries*          bp = fresh_geometry(bp_src, bp_copy);
        fill_and_outline(bp->fill_verts(), bp->x_data(), bp->y_data(), 0.45f, 1.5f, "box");
        if (bp->outlier_count() > 0)
            emit_markers(c,
                         bp->outlier_x(),
                         bp->outlier_y(),
                         m,
                         xoff,
                         MarkerStyle::Circle,
                         5.0f,
                         col,
                         alpha);
    }
    else if (auto* vn_src = dynamic_cast<const ViolinSeries*>(&s))
    {
        std::unique_ptr<ViolinSeries> vn_copy;
        const ViolinSeries*          vn = fresh_geometry(vn_src, vn_copy);
        fill_and_outline(vn->fill_verts(), vn->x_data(), vn->y_data(), 0.40f, 1.5f, "violin");
    }
    else if (auto* hs_src = dynamic_cast<const HistogramSeries*>(&s))
    {
        std::unique_ptr<HistogramSeries> hs_copy;
        const HistogramSeries*          hs = fresh_geometry(hs_src, hs_copy);
        fill_and_outline(hs->fill_verts(), hs->x_data(), hs->y_data(), 0.65f, 1.0f, "histogram");
    }
    else if (auto* bs_src = dynamic_cast<const BarSeries*>(&s))
    {
        std::unique_ptr<BarSeries> bs_copy;
        const BarSeries*          bs = fresh_geometry(bs_src, bs_copy);
        fill_and_outline(bs->fill_verts(), bs->x_data(), bs->y_data(), 0.75f, 1.5f, "bar");
    }
    else if (auto* band_src = dynamic_cast<const BandSeries*>(&s))
    {
        std::unique_ptr<BandSeries> band_copy;
        const BandSeries*          band = fresh_geometry(band_src, band_copy);
        emit_fill(c, band->fill_verts(), m, xoff, col, alpha * band->fill_opacity(), "band");
        if (band->show_edges() && band->edge_width() > 0.0f && band->point_count() > 1)
            emit_stroke_path(c,
                             polyline_path(band->x_data(), band->y_data(), m, xoff),
                             "band",
                             col,
                             alpha,
                             band->edge_width());
    }
    else if (auto* stem_src = dynamic_cast<const StemSeries*>(&s))
    {
        std::unique_ptr<StemSeries> stem_copy;
        const StemSeries*          stem = fresh_geometry(stem_src, stem_copy);
        if (stem->point_count() > 1)
        {
            emit_stroke_path(c,
                             polyline_path(stem->x_data(), stem->y_data(), m, xoff),
                             "stem",
                             col,
                             alpha,
                             stem->stem_width());
            if (stem->show_heads())
            {
                // Geometry is (x, baseline), (x, y), (NaN, NaN) per stem; heads sit on the tips.
                std::vector<float> hx, hy;
                const auto         sx = stem->x_data();
                const auto         sy = stem->y_data();
                for (size_t i = 1; i < sx.size() && i < sy.size(); i += 3)
                {
                    hx.push_back(sx[i]);
                    hy.push_back(sy[i]);
                }
                emit_markers(c,
                             hx,
                             hy,
                             m,
                             xoff,
                             MarkerStyle::FilledCircle,
                             style.marker_size > 0.0f ? style.marker_size : 6.0f,
                             col,
                             alpha);
            }
        }
    }
    else if (auto* sh_src = dynamic_cast<const ShapeSeries*>(&s))
    {
        std::unique_ptr<ShapeSeries> sh_copy;
        const ShapeSeries*          sh = fresh_geometry(sh_src, sh_copy, true);
        fill_and_outline(sh->fill_verts(), sh->x_data(), sh->y_data(), 0.50f, 1.5f, "shape");
    }
}

// ─── Text ───────────────────────────────────────────────────────────────────

void emit_text(std::ostringstream& o,
               std::string_view    text,
               double              x,
               double              y,
               float               font_px,
               const Color&        col,
               const char*         anchor,
               VAlign              valign,
               std::string_view    extra = {})
{
    if (text.empty())
        return;
    o << "    <text x=\"" << num(x) << "\" y=\"" << num(y + baseline_offset(valign, font_px))
      << "\" font-size=\"" << num(font_px) << "\" fill=\"" << hex(col) << "\""
      << opacity_attr("fill-opacity", col.a);
    if (std::string_view(anchor) != "start")
        o << " text-anchor=\"" << anchor << "\"";
    o << extra << ">" << xml_escape(text) << "</text>\n";
}

// ─── Legend ─────────────────────────────────────────────────────────────────

enum class SwatchKind
{
    Line,
    Marker,
    Fill,
    Stem
};

struct LegendEntry
{
    std::string label;
    SwatchKind  kind;
    Color       color;
    float       alpha;
    float       fill_alpha  = 1.0f;
    LineStyle   line_style  = LineStyle::Solid;
    float       line_width  = 2.0f;
    MarkerStyle marker      = MarkerStyle::None;
    float       marker_size = 6.0f;
};

LegendEntry legend_entry_for(const Series& s, const SvgStyle& st)
{
    LegendEntry e{s.label(), SwatchKind::Fill, theme_series_color(s, st), series_alpha(s)};
    const auto& style = s.plot_style();
    if (auto* ls = dynamic_cast<const LineSeries*>(&s))
    {
        e.kind        = style.has_line() ? SwatchKind::Line : SwatchKind::Marker;
        e.line_style  = style.line_style;
        e.line_width  = ls->width();
        e.marker      = style.marker_style;
        e.marker_size = style.marker_size;
    }
    else if (auto* l3 = dynamic_cast<const LineSeries3D*>(&s))
    {
        e.kind       = SwatchKind::Line;
        e.line_width = l3->width();
    }
    else if (auto* sc = dynamic_cast<const ScatterSeries*>(&s))
    {
        e.kind        = SwatchKind::Marker;
        e.marker      = default_scatter_marker(style.marker_style, st);
        e.marker_size = sc->size();
        if (sc->has_colormap())
            e.color = scatter_colormap(sc->colormap(), 0.5f);
    }
    else if (auto* s3 = dynamic_cast<const ScatterSeries3D*>(&s))
    {
        e.kind = SwatchKind::Marker;
        e.marker =
            s3->marker_style() == MarkerStyle::None ? MarkerStyle::Circle : s3->marker_style();
        e.marker_size = s3->size();
    }
    else if (dynamic_cast<const StemSeries*>(&s))
        e.kind = SwatchKind::Stem;
    else if (auto* band = dynamic_cast<const BandSeries*>(&s))
        e.fill_alpha = band->fill_opacity();
    else if (auto* surf = dynamic_cast<const SurfaceSeries*>(&s))
    {
        if (surf->colormap_type() != ColormapType::None)
            e.color = SurfaceSeries::sample_colormap(surf->colormap_type(), 0.5f);
        if (surf->wireframe())
        {
            e.kind       = SwatchKind::Line;
            e.line_width = 1.5f;
        }
    }
    else if (dynamic_cast<const BarSeries*>(&s))
        e.fill_alpha = 0.75f;
    else if (dynamic_cast<const HistogramSeries*>(&s))
        e.fill_alpha = 0.65f;
    else if (dynamic_cast<const BoxPlotSeries*>(&s))
        e.fill_alpha = 0.45f;
    else if (dynamic_cast<const ViolinSeries*>(&s))
        e.fill_alpha = 0.40f;
    else if (dynamic_cast<const ShapeSeries*>(&s))
        e.fill_alpha = 0.50f;
    return e;
}

void emit_legend(SvgContext& c, const AxesBase& axes, const Rect& vp, const LegendConfig& cfg)
{
    if (!cfg.visible || cfg.position == LegendPosition::None)
        return;
    const SvgStyle&          st = c.style;
    std::vector<LegendEntry> entries;
    for (const auto& s : axes.series())
    {
        // Hidden series are not drawn, so they get no legend entry either.
        if (!s || !s->visible() || s->label().empty() || !s->show_in_legend())
            continue;
        entries.push_back(legend_entry_for(*s, st));
    }
    if (entries.empty())
        return;

    const float     font_px  = cfg.font_size > 0.0f ? cfg.font_size : 12.5f;
    const float     pad_x    = cfg.padding + 2.0f;
    const float     pad_y    = cfg.padding;
    const float     row_h    = font_px + 6.0f;
    constexpr float swatch_w = 22.0f;
    constexpr float gap      = 6.0f;
    constexpr float margin   = 12.0f;
    constexpr float col_gap  = 14.0f;

    float max_label_w = 0.0f;
    for (const auto& e : entries)
        max_label_w = std::max(max_label_w, estimate_text_width(e.label, font_px));
    const float col_w = swatch_w + gap + max_label_w;
    const auto  grid  = legend_layout::compute_grid(static_cast<int>(entries.size()),
                                                  col_w,
                                                  row_h,
                                                  pad_x,
                                                  pad_y,
                                                  col_gap,
                                                  vp.w - 2.0f * margin,
                                                  vp.h - 2.0f * margin);
    const float w     = grid.width;
    // A static export cannot scroll: size the box for every row.
    const float h = 2.0f * pad_y + static_cast<float>(grid.rows) * row_h;

    LegendPosition pos = cfg.position;
    if (pos == LegendPosition::Best)
    {
        if (auto* a2 = dynamic_cast<const Axes*>(&axes))
            pos = legend_layout::choose_best_corner(*a2, vp, w, h, margin, LegendPosition::None);
        else
            pos = LegendPosition::TopRight;
    }
    const Rect r = legend_layout::detail::corner_rect(pos, vp, w, h, margin);

    // Same "unset" sentinel as LegendInteraction: rgb all zero -> theme colour.
    auto        unset  = [](const Color& col) { return col.r == 0.0f && col.g == 0.0f && col.b == 0.0f; };
    const Color bg     = unset(cfg.bg_color) ? st.legend_bg : cfg.bg_color;
    const Color border = unset(cfg.border_color) ? st.legend_border : cfg.border_color;
    c.body << "    <g class=\"legend\">\n";
    c.body << "      <rect x=\"" << num(r.x) << "\" y=\"" << num(r.y) << "\" width=\"" << num(w)
           << "\" height=\"" << num(h) << "\" rx=\"6\" fill=\"" << hex(bg) << "\""
           << opacity_attr("fill-opacity", bg.a * 0.92f) << " stroke=\"" << hex(border) << "\""
           << opacity_attr("stroke-opacity", border.a) << " stroke-width=\"1\"/>\n";

    for (size_t i = 0; i < entries.size(); ++i)
    {
        const auto& e   = entries[i];
        const int   idx = static_cast<int>(i);
        const int   col = grid.rows > 0 ? idx / grid.rows : 0;
        const int   row = grid.rows > 0 ? idx % grid.rows : idx;
        const float ex  = r.x + pad_x + static_cast<float>(col) * (col_w + col_gap);
        const float ey  = r.y + pad_y + (static_cast<float>(row) + 0.5f) * row_h;
        const float cx  = ex + swatch_w * 0.5f;

        switch (e.kind)
        {
            case SwatchKind::Line:
            {
                const float       lw   = std::clamp(e.line_width, 1.0f, 3.0f);
                const std::string dash = dash_array(e.line_style, std::min(lw, 1.5f));
                c.body << "      <line x1=\"" << num(ex) << "\" y1=\"" << num(ey) << "\" x2=\""
                       << num(ex + swatch_w) << "\" y2=\"" << num(ey) << "\" stroke=\""
                       << hex(e.color) << "\" stroke-width=\"" << num(lw) << "\""
                       << opacity_attr("stroke-opacity", e.alpha);
                if (!dash.empty())
                    c.body << " stroke-dasharray=\"" << dash << "\"";
                c.body << "/>\n";
                if (e.marker != MarkerStyle::None)
                    c.body << "      <use xlink:href=\"#"
                           << c.marker_symbol(e.marker, std::min(e.marker_size, 10.0f))
                           << "\" x=\"" << num(cx) << "\" y=\"" << num(ey) << "\" color=\""
                           << hex(e.color) << "\"" << opacity_attr("opacity", e.alpha) << "/>\n";
                break;
            }
            case SwatchKind::Marker:
                c.body << "      <use xlink:href=\"#"
                       << c.marker_symbol(e.marker, std::clamp(e.marker_size, 4.0f, 10.0f))
                       << "\" x=\"" << num(cx) << "\" y=\"" << num(ey) << "\" color=\""
                       << hex(e.color) << "\"" << opacity_attr("opacity", e.alpha) << "/>\n";
                break;
            case SwatchKind::Stem:
                c.body << "      <line x1=\"" << num(cx) << "\" y1=\"" << num(ey + 6.0f)
                       << "\" x2=\"" << num(cx) << "\" y2=\"" << num(ey - 3.0f) << "\" stroke=\""
                       << hex(e.color) << "\" stroke-width=\"1.5\""
                       << opacity_attr("stroke-opacity", e.alpha) << "/>\n";
                c.body << "      <circle cx=\"" << num(cx) << "\" cy=\"" << num(ey - 3.0f)
                       << "\" r=\"3\" fill=\"" << hex(e.color) << "\""
                       << opacity_attr("fill-opacity", e.alpha) << "/>\n";
                break;
            case SwatchKind::Fill:
                c.body << "      <rect class=\"swatch-fill\" x=\"" << num(ex + 3.0f) << "\" y=\""
                       << num(ey - 5.0f) << "\" width=\"" << num(swatch_w - 6.0f)
                       << "\" height=\"10\" rx=\"2\" fill=\"" << hex(e.color) << "\""
                       << opacity_attr("fill-opacity", e.alpha * std::max(e.fill_alpha, 0.35f))
                       << " stroke=\"" << hex(e.color) << "\" stroke-width=\"1\""
                       << opacity_attr("stroke-opacity", e.alpha) << "/>\n";
                break;
        }
        emit_text(c.body,
                  e.label,
                  ex + swatch_w + gap,
                  ey,
                  font_px,
                  st.text_secondary,
                  "start",
                  VAlign::Middle);
    }
    c.body << "    </g>\n";
}

// ─── 2D axes ────────────────────────────────────────────────────────────────

std::string clip_rect(SvgContext& c, const Rect& vp)
{
    const std::string id = c.new_id("clip");
    c.defs << "    <clipPath id=\"" << id << "\"><rect x=\"" << num(vp.x) << "\" y=\""
           << num(vp.y) << "\" width=\"" << num(vp.w) << "\" height=\"" << num(vp.h)
           << "\"/></clipPath>\n";
    return id;
}

// Minor grid values: 4 lines between major ticks, extended into the margins
// (render_geometry.cpp).
std::vector<double> minor_grid_values(const std::vector<double>& t, double lo, double hi)
{
    constexpr int       SUBDIV     = 5;
    constexpr int       MAX_MARGIN = 4 * SUBDIV;
    std::vector<double> out;
    const size_t        n = t.size();
    for (size_t i = 0; i + 1 < n; ++i)
    {
        const double step = (t[i + 1] - t[i]) / SUBDIV;
        for (int s = 1; s < SUBDIV; ++s)
            out.push_back(t[i] + step * s);
    }
    if (n >= 2)
    {
        const double step0 = (t[1] - t[0]) / SUBDIV;
        for (int k = 1; std::isfinite(step0) && step0 > 0 && k <= MAX_MARGIN; ++k)
        {
            const double v = t[0] - step0 * k;
            if (v < lo)
                break;
            out.push_back(v);
        }
        const double step1 = (t[n - 1] - t[n - 2]) / SUBDIV;
        for (int k = 1; std::isfinite(step1) && step1 > 0 && k <= MAX_MARGIN; ++k)
        {
            const double v = t[n - 1] + step1 * k;
            if (v > hi)
                break;
            out.push_back(v);
        }
    }
    return out;
}

void emit_grid_2d(SvgContext& c, const Axes& axes, const DataToSvg& m)
{
    if (!axes.grid_enabled())
        return;
    const auto&  as      = axes.axis_style();
    const auto   x_ticks = axes.compute_x_ticks();
    const auto   y_ticks = axes.compute_y_ticks();
    const double top     = m.vp_y;
    const double bottom  = m.vp_y + m.vp_h;
    const double left    = m.vp_x;
    const double right   = m.vp_x + m.vp_w;

    auto emit_lines = [&](const std::vector<double>& xs,
                          const std::vector<double>& ys,
                          const Color&               col,
                          float                      w)
    {
        std::string d;
        for (double v : xs)
        {
            const double px = m.map_x(v);
            if (px < left - 0.5 || px > right + 0.5)
                continue;
            d += 'M';
            put_num(d, px);
            d += ' ';
            put_num(d, top);
            d += 'V';
            put_num(d, bottom);
        }
        for (double v : ys)
        {
            const double py = m.map_y(v);
            if (py < top - 0.5 || py > bottom + 0.5)
                continue;
            d += 'M';
            put_num(d, left);
            d += ' ';
            put_num(d, py);
            d += 'H';
            put_num(d, right);
        }
        if (d.empty())
            return;
        c.body << "      <path d=\"" << d << "\" fill=\"none\" stroke=\"" << hex(col)
               << "\" stroke-width=\"" << num(w) << "\"" << opacity_attr("stroke-opacity", col.a)
               << "/>\n";
    };

    c.body << "    <g class=\"grid\">\n";
    emit_lines(minor_grid_values(x_ticks.positions, m.x_min, m.x_max),
               minor_grid_values(y_ticks.positions, m.y_min, m.y_max),
               c.style.grid_minor,
               std::max(1.0f, as.grid_width * 0.5f));
    emit_lines(x_ticks.positions,
               y_ticks.positions,
               as.grid_color.a > 0.0f ? as.grid_color : c.style.grid_major,
               as.grid_width);
    c.body << "    </g>\n";
}

void emit_axes_2d(SvgContext& c, const Axes& axes, const LegendConfig& legend)
{
    const Rect vp = axes.viewport();
    if (!(vp.w >= 1.0f) || !(vp.h >= 1.0f))
        return;
    const SvgStyle& st   = c.style;
    const auto      xlim = axes.x_limits();
    const auto      ylim = axes.y_limits();
    const DataToSvg m{vp.x, vp.y, vp.w, vp.h, xlim.min, xlim.max, ylim.min, ylim.max};

    c.body << "  <g class=\"axes\">\n";
    emit_grid_2d(c, axes, m);

    // Series data, clipped to the plot area.
    const std::string clip = clip_rect(c, vp);
    c.body << "    <g class=\"series\" clip-path=\"url(#" << clip << ")\">\n";
    for (const auto& sp : axes.series())
    {
        // Match the renderer: hidden series are not drawn.
        if (sp && sp->visible())
            emit_series_2d(c, *sp, m);
    }
    c.body << "    </g>\n";

    // Border and tick marks (axis_line colour, render_geometry.cpp).
    const auto& as      = axes.axis_style();
    const float tl      = as.tick_length;
    const auto  x_ticks = axes.compute_x_ticks();
    const auto  y_ticks = axes.compute_y_ticks();
    {
        std::string d;
        if (axes.border_enabled())
        {
            d += 'M';
            put_num(d, vp.x);
            d += ' ';
            put_num(d, vp.y);
            d += 'H';
            put_num(d, vp.x + vp.w);
            d += 'V';
            put_num(d, vp.y + vp.h);
            d += 'H';
            put_num(d, vp.x);
            d += 'Z';
        }
        if (tl > 0.0f)
        {
            for (double p : x_ticks.positions)
            {
                const double px = m.map_x(p);
                if (px < vp.x - 0.5 || px > vp.x + vp.w + 0.5)
                    continue;
                d += 'M';
                put_num(d, px);
                d += ' ';
                put_num(d, vp.y + vp.h);
                d += 'v';
                put_num(d, tl);
            }
            for (double p : y_ticks.positions)
            {
                const double py = m.map_y(p);
                if (py < vp.y - 0.5 || py > vp.y + vp.h + 0.5)
                    continue;
                d += 'M';
                put_num(d, vp.x);
                d += ' ';
                put_num(d, py);
                d += 'h';
                put_num(d, -tl);
            }
        }
        if (!d.empty())
            c.body << "    <path class=\"axis\" d=\"" << d << "\" fill=\"none\" stroke=\""
                   << hex(st.axis_line) << "\" stroke-width=\"1\""
                   << opacity_attr("stroke-opacity", st.axis_line.a) << "/>\n";
    }

    // Tick labels, skipping ones that would overlap (as the renderer does).
    constexpr float tick_padding     = 5.0f;
    float           max_y_tick_width = 0.0f;
    c.body << "    <g class=\"tick-labels\">\n";
    {
        float last_right = -1e30f;
        for (size_t i = 0; i < x_ticks.positions.size() && i < x_ticks.labels.size(); ++i)
        {
            const auto  px = static_cast<float>(m.map_x(x_ticks.positions[i]));
            const float w  = estimate_text_width(x_ticks.labels[i], TICK_FONT_PX);
            if (i > 0 && px - w * 0.5f < last_right + 6.0f)
                continue;
            emit_text(c.body,
                      x_ticks.labels[i],
                      px,
                      vp.y + vp.h + tl + tick_padding,
                      TICK_FONT_PX,
                      st.tick_label,
                      "middle",
                      VAlign::Top);
            last_right = px + w * 0.5f;
        }
    }
    {
        float     last_bottom = -1e30f;
        const int n =
            static_cast<int>(std::min(y_ticks.positions.size(), y_ticks.labels.size()));
        for (int i = n - 1; i >= 0; --i)
        {
            const auto idx = static_cast<size_t>(i);
            const auto py  = static_cast<float>(m.map_y(y_ticks.positions[idx]));
            if (py - TICK_FONT_PX * 0.5f < last_bottom + 4.0f && i < n - 1)
                continue;
            emit_text(c.body,
                      y_ticks.labels[idx],
                      vp.x - tl - tick_padding,
                      py,
                      TICK_FONT_PX,
                      st.tick_label,
                      "end",
                      VAlign::Middle);
            last_bottom      = py + TICK_FONT_PX * 0.5f;
            max_y_tick_width = std::max(max_y_tick_width,
                                        estimate_text_width(y_ticks.labels[idx], TICK_FONT_PX));
        }
    }
    c.body << "    </g>\n";

    // Axis labels and title.
    if (!axes.get_xlabel().empty())
        emit_text(c.body,
                  axes.get_xlabel(),
                  vp.x + vp.w * 0.5f,
                  vp.y + vp.h + tick_padding + 16.0f + tick_padding,
                  LABEL_FONT_PX,
                  st.text_secondary,
                  "middle",
                  VAlign::Top);
    if (!axes.get_ylabel().empty())
    {
        const float cx = vp.x - tl - tick_padding - max_y_tick_width - 6.0f - LABEL_FONT_PX * 0.5f;
        const float cy = vp.y + vp.h * 0.5f;
        emit_text(c.body,
                  axes.get_ylabel(),
                  cx,
                  cy,
                  LABEL_FONT_PX,
                  st.text_secondary,
                  "middle",
                  VAlign::Middle,
                  " transform=\"rotate(-90 " + num(cx) + " " + num(cy) + ")\"");
    }
    if (!axes.get_title().empty())
        emit_text(c.body,
                  axes.get_title(),
                  vp.x + vp.w * 0.5f,
                  std::max(2.0f, vp.y - TITLE_FONT_PX - tick_padding),
                  TITLE_FONT_PX,
                  st.text_primary,
                  "middle",
                  VAlign::Top,
                  " font-weight=\"500\"");

    // ShapeSeries text annotations (data-space labels).
    static constexpr const char* anchors[] = {"start", "middle", "end"};
    static constexpr VAlign      valigns[] = {VAlign::Top, VAlign::Middle, VAlign::Bottom};
    for (const auto& sp : axes.series())
    {
        auto* shape_src = dynamic_cast<const ShapeSeries*>(sp.get());
        if (!shape_src || !shape_src->visible())
            continue;
        std::unique_ptr<ShapeSeries> shape_copy;
        const ShapeSeries*           shape = fresh_geometry(shape_src, shape_copy, true);
        const Color sc = theme_series_color(*shape, st);
        for (const auto& ann : shape->text_annotations())
        {
            Color tc = ann.text_color.a > 0.01f ? ann.text_color : sc;
            tc.a *= shape->opacity();
            emit_text(c.body,
                      ann.content,
                      m.map_x(shape->x_offset() + ann.x),
                      m.map_y(ann.y),
                      ann.font_size > 0.0f ? ann.font_size : LABEL_FONT_PX,
                      tc,
                      anchors[std::clamp(ann.align, 0, 2)],
                      valigns[std::clamp(ann.valign, 0, 2)]);
        }
    }

    emit_legend(c, axes, vp, legend);
    c.body << "  </g>\n";
}

// ─── 3D axes ────────────────────────────────────────────────────────────────

// Screen-space primitive for painter's-algorithm sorting.
struct Prim3D
{
    enum Kind : uint8_t
    {
        Triangle,
        Segment,
        Marker
    };
    float    depth = 0.0f;   // NDC depth, larger = farther
    uint32_t group = 0;      // index into the style groups
    Kind     kind  = Segment;
    float    p[6]{};
    Color    color;              // triangle fill / per-segment stroke
    bool     has_color = false;
};

// Style shared by consecutive primitives of one series.
struct Group3D
{
    std::string open_tag;
    std::string symbol;              // marker symbol id (markers)
    bool        seam_stroke = false;   // stroke triangles in their fill colour
};

struct Projector3D
{
    mat4 mvp;
    mat4 model;
    Rect vp;

    // Data-space point -> screen pixel + NDC depth.  False if behind the camera.
    bool project(double x, double y, double z, float& sx, float& sy, float& depth) const
    {
        const auto  fx = static_cast<float>(x);
        const auto  fy = static_cast<float>(y);
        const auto  fz = static_cast<float>(z);
        const auto& m  = mvp.m;
        const float cx = m[0] * fx + m[4] * fy + m[8] * fz + m[12];
        const float cy = m[1] * fx + m[5] * fy + m[9] * fz + m[13];
        const float cz = m[2] * fx + m[6] * fy + m[10] * fz + m[14];
        const float cw = m[3] * fx + m[7] * fy + m[11] * fz + m[15];
        if (!(cw > 0.001f) || !std::isfinite(cx) || !std::isfinite(cy))
            return false;
        sx    = vp.x + (cx / cw + 1.0f) * 0.5f * vp.w;
        sy    = vp.y + (cy / cw + 1.0f) * 0.5f * vp.h;
        depth = cz / cw;
        return true;
    }

    // Data-space point -> normalized box ("world") space.
    [[nodiscard]] vec3 to_world(const vec3& p) const
    {
        const vec4 w = mat4_mul_vec4(model, vec4{p.x, p.y, p.z, 1.0});
        return {w.x, w.y, w.z};
    }

    [[nodiscard]] vec3 model_dir(const vec3& d) const
    {
        const auto& m = model.m;
        return vec3_normalize({m[0] * d.x + m[4] * d.y + m[8] * d.z,
                               m[1] * d.x + m[5] * d.y + m[9] * d.z,
                               m[2] * d.x + m[6] * d.y + m[10] * d.z});
    }
};

// Flat-shaded colour of one triangle, following surface3d.frag.
struct Lighting3D
{
    vec3  cam_pos;
    vec3  light;
    vec3  fill_light;
    float ambient   = 0.15f;
    float specular  = 0.3f;
    float shininess = 32.0f;

    [[nodiscard]] Color shade(const Color& base, vec3 w0, vec3 w1, vec3 w2) const
    {
        vec3 n = vec3_cross(w1 - w0, w2 - w0);
        if (vec3_length_sq(n) < 1e-20)
            return base;
        n                = vec3_normalize(n);
        const vec3 centr = (w0 + w1 + w2) * (1.0 / 3.0);
        const vec3 v     = vec3_normalize(cam_pos - centr);
        if (vec3_dot(n, v) < 0.0)
            n = n * -1.0;
        const double diffuse = std::max(vec3_dot(n, light), 0.0) * 0.65;
        const double fill    = std::max(vec3_dot(n, fill_light), 0.0) * 0.15;
        const vec3   h       = vec3_normalize(light + v);
        const double spec =
            std::pow(std::max(vec3_dot(n, h), 0.0), static_cast<double>(shininess)) * specular;
        const double lit = ambient + diffuse + fill;
        return Color{clamp01(static_cast<float>(base.r * lit + spec)),
                     clamp01(static_cast<float>(base.g * lit + spec)),
                     clamp01(static_cast<float>(base.b * lit + spec)),
                     base.a};
    }
};

struct Box2
{
    float x0, y0, x1, y1;
};

bool overlaps_any(const Box2& r, const std::vector<Box2>& placed, float gap)
{
    for (const auto& o : placed)
        if (r.x0 < o.x1 + gap && r.x1 + gap > o.x0 && r.y0 < o.y1 + gap && r.y1 + gap > o.y0)
            return true;
    return false;
}

void emit_axes_3d(SvgContext& c, const Axes3D& axes, const LegendConfig& legend)
{
    const Rect vp = axes.viewport();
    if (!(vp.w >= 1.0f) || !(vp.h >= 1.0f))
        return;
    const SvgStyle& st = c.style;

    // Like the renderer, pick up orbit parameters assigned directly on the camera.
    Camera cam = axes.camera();
    cam.apply_orbit_changes();
    const float aspect = vp.w / std::max(vp.h, 1.0f);
    Projector3D pj;
    pj.model = axes.data_to_normalized_matrix();
    pj.mvp   = mat4_mul(cam.projection_matrix(aspect), mat4_mul(cam.view_matrix(), pj.model));
    pj.vp    = vp;

    const auto   xlim  = axes.x_limits();
    const auto   ylim  = axes.y_limits();
    const auto   zlim  = axes.z_limits();
    const double lo[3] = {xlim.min, ylim.min, zlim.min};
    const double hi[3] = {xlim.max, ylim.max, zlim.max};

    c.body << "  <g class=\"axes axes3d\">\n";
    const std::string clip = clip_rect(c, vp);
    c.body << "    <g clip-path=\"url(#" << clip << ")\">\n";

    // ── Grid planes at the min corners (an overlay behind the series) ──
    if (axes.grid_enabled())
    {
        const auto   gp      = static_cast<int>(axes.grid_planes());
        const auto   xt      = axes.compute_x_ticks().positions;
        const auto   yt      = axes.compute_y_ticks().positions;
        const auto   zt      = axes.compute_z_ticks().positions;
        const Color& g       = st.grid_major;
        const Color  blended = {g.r * 0.7f + 0.3f, g.g * 0.7f + 0.3f, g.b * 0.7f + 0.3f, 0.35f};
        std::string  d;
        auto         seg = [&](vec3 a, vec3 b)
        {
            float ax = 0, ay = 0, ad = 0, bx = 0, by = 0, bd = 0;
            if (!pj.project(a.x, a.y, a.z, ax, ay, ad) || !pj.project(b.x, b.y, b.z, bx, by, bd))
                return;
            d += 'M';
            put_num(d, ax);
            d += ' ';
            put_num(d, ay);
            d += 'L';
            put_num(d, bx);
            d += ' ';
            put_num(d, by);
        };
        if (gp & static_cast<int>(Axes3D::GridPlane::XY))
        {
            for (double x : xt)
                seg({x, lo[1], lo[2]}, {x, hi[1], lo[2]});
            for (double y : yt)
                seg({lo[0], y, lo[2]}, {hi[0], y, lo[2]});
        }
        if (gp & static_cast<int>(Axes3D::GridPlane::XZ))
        {
            for (double x : xt)
                seg({x, lo[1], lo[2]}, {x, lo[1], hi[2]});
            for (double z : zt)
                seg({lo[0], lo[1], z}, {hi[0], lo[1], z});
        }
        if (gp & static_cast<int>(Axes3D::GridPlane::YZ))
        {
            for (double y : yt)
                seg({lo[0], y, lo[2]}, {lo[0], y, hi[2]});
            for (double z : zt)
                seg({lo[0], lo[1], z}, {lo[0], hi[1], z});
        }
        if (!d.empty())
            c.body << "      <path class=\"grid\" d=\"" << d << "\" fill=\"none\" stroke=\""
                   << hex(blended) << "\" stroke-width=\"1\""
                   << opacity_attr("stroke-opacity", blended.a) << "/>\n";
    }

    // ── Painter-sorted primitives: bounding box edges + series ──
    std::vector<Prim3D>  prims;
    std::vector<Group3D> groups;

    auto add_segment = [&](uint32_t group, const vec3& a, const vec3& b, const Color* col)
    {
        Prim3D p;
        float  d0 = 0, d1 = 0;
        if (!pj.project(a.x, a.y, a.z, p.p[0], p.p[1], d0)
            || !pj.project(b.x, b.y, b.z, p.p[2], p.p[3], d1))
            return;
        p.depth = 0.5f * (d0 + d1);
        p.group = group;
        p.kind  = Prim3D::Segment;
        if (col)
        {
            p.color     = *col;
            p.has_color = true;
        }
        prims.push_back(p);
    };
    auto add_triangle = [&](uint32_t group, const std::array<vec3, 3>& v, const Color& col)
    {
        Prim3D p;
        float  dsum = 0;
        for (size_t k = 0; k < 3; ++k)
        {
            float d = 0;
            if (!pj.project(v[k].x, v[k].y, v[k].z, p.p[2 * k], p.p[2 * k + 1], d))
                return;
            dsum += d;
        }
        p.depth     = dsum / 3.0f;
        p.group     = group;
        p.kind      = Prim3D::Triangle;
        p.color     = col;
        p.has_color = true;
        prims.push_back(p);
    };

    if (axes.show_bounding_box())
    {
        const auto gi = static_cast<uint32_t>(groups.size());
        groups.push_back({"<g class=\"box\" fill=\"none\" stroke=\"" + hex(st.axis_line)
                              + "\" stroke-width=\"1.2\""
                              + opacity_attr("stroke-opacity", st.axis_line.a) + ">",
                          {},
                          false});
        for (int a = 0; a < 3; ++a)
        {
            const int b  = (a + 1) % 3;
            const int cc = (a + 2) % 3;
            for (int k = 0; k < 4; ++k)
            {
                double p0[3], p1[3];
                p0[a]  = lo[a];
                p1[a]  = hi[a];
                p0[b]  = p1[b]  = (k & 1) ? hi[b] : lo[b];
                p0[cc] = p1[cc] = (k & 2) ? hi[cc] : lo[cc];
                add_segment(gi, {p0[0], p0[1], p0[2]}, {p1[0], p1[1], p1[2]}, nullptr);
            }
        }
    }

    // Lighting (a zero light_dir selects the shader default), in normalized box space.
    vec3 ld = axes.lighting_enabled() ? axes.light_dir() : vec3{0, 0, 0};
    if (vec3_dot(ld, ld) < 0.001)
        ld = {0.5, 0.7, 1.0};
    const Lighting3D base_light{cam.position,
                                pj.model_dir(ld),
                                pj.model_dir({-0.3, -0.5, 0.4})};
    auto             material = [&](float amb, float spec, float shin)
    {
        Lighting3D l = base_light;
        l.ambient    = amb > 0.001f ? amb : 0.15f;
        l.specular   = spec > 0.001f ? spec : 0.3f;
        l.shininess  = shin > 1.0f ? shin : 32.0f;
        return l;
    };

    // surface3d.frag discards fragments outside the normalized box.
    constexpr double BOX        = Axes3D::box_half_size() + 1e-3;
    auto             inside_box = [&](const vec3& w)
    { return std::fabs(w.x) <= BOX && std::fabs(w.y) <= BOX && std::fabs(w.z) <= BOX; };
    auto finite3 = [](const vec3& v)
    { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };

    for (const auto& sp : axes.series())
    {
        if (!sp || !sp->visible())
            continue;
        const Series& s     = *sp;
        const Color   col   = theme_series_color(s, st);
        const float   alpha = series_alpha(s);
        const auto    gi    = static_cast<uint32_t>(groups.size());

        if (auto* l3 = dynamic_cast<const LineSeries3D*>(&s))
        {
            groups.push_back({"<g class=\"line3d\" fill=\"none\" stroke=\"" + hex(col)
                                  + "\" stroke-width=\"" + num(l3->width() + LINE_AA_EXTRA_PX) + "\""
                                  + opacity_attr("stroke-opacity", alpha)
                                  + " stroke-linecap=\"round\">",
                              {},
                              false});
            const auto x = l3->x_data(), y = l3->y_data(), z = l3->z_data();
            for (size_t i = 0; i + 1 < l3->point_count(); ++i)
            {
                const vec3 a{x[i], y[i], z[i]};
                const vec3 b{x[i + 1], y[i + 1], z[i + 1]};
                if (finite3(a) && finite3(b))
                    add_segment(gi, a, b, nullptr);
            }
        }
        else if (auto* s3 = dynamic_cast<const ScatterSeries3D*>(&s))
        {
            const MarkerStyle ms =
                s3->marker_style() == MarkerStyle::None ? MarkerStyle::Circle : s3->marker_style();
            groups.push_back({"<g class=\"scatter3d\" color=\"" + hex(col) + "\""
                                  + opacity_attr("fill-opacity", alpha)
                                  + opacity_attr("stroke-opacity", alpha) + ">",
                              c.marker_symbol(ms, s3->size()),
                              false});
            const auto x = s3->x_data(), y = s3->y_data(), z = s3->z_data();
            for (size_t i = 0; i < s3->point_count(); ++i)
            {
                Prim3D p;
                if (!finite3({x[i], y[i], z[i]})
                    || !pj.project(x[i], y[i], z[i], p.p[0], p.p[1], p.depth))
                    continue;
                p.group = gi;
                p.kind  = Prim3D::Marker;
                prims.push_back(p);
            }
        }
        else if (auto* surf = dynamic_cast<const SurfaceSeries*>(&s))
        {
            const int  rows = surf->rows();
            const int  cols = surf->cols();
            const auto xg   = surf->x_grid();
            const auto yg   = surf->y_grid();
            const auto zv   = surf->z_values();
            const auto ur   = static_cast<size_t>(std::max(rows, 0));
            const auto uc   = static_cast<size_t>(std::max(cols, 0));
            if (ur < 2 || uc < 2 || xg.size() < uc || yg.size() < ur || zv.size() < ur * uc)
                continue;
            const ColormapType cm = surf->colormap_type();
            // Colormap t follows surface3d.frag: normalized-box Z over [-3, 3].
            auto cmap = [&](double wz)
            {
                if (cm == ColormapType::None)
                    return col;
                constexpr double hs = Axes3D::box_half_size();
                return SurfaceSeries::sample_colormap(cm, static_cast<float>((wz + hs) / (2 * hs)));
            };
            auto vtx = [&](size_t i, size_t j) { return vec3{xg[j], yg[i], zv[i * uc + j]}; };
            if (surf->wireframe())
            {
                groups.push_back({"<g class=\"surface-wire\" fill=\"none\" stroke=\"" + hex(col)
                                      + "\" stroke-width=\"1\""
                                      + opacity_attr("stroke-opacity", alpha) + ">",
                                  {},
                                  false});
                auto wire = [&](const vec3& a, const vec3& b)
                {
                    if (!finite3(a) || !finite3(b))
                        return;
                    const vec3 wa = pj.to_world(a);
                    const vec3 wb = pj.to_world(b);
                    if (!inside_box(wa) || !inside_box(wb))
                        return;
                    const Color cc = cmap(0.5 * (wa.z + wb.z));
                    add_segment(gi, a, b, cm == ColormapType::None ? nullptr : &cc);
                };
                for (size_t i = 0; i < ur; ++i)
                    for (size_t j = 0; j < uc; ++j)
                    {
                        if (j + 1 < uc)
                            wire(vtx(i, j), vtx(i, j + 1));
                        if (i + 1 < ur)
                            wire(vtx(i, j), vtx(i + 1, j));
                    }
            }
            else
            {
                const bool opaque = alpha >= 0.999f;
                groups.push_back(
                    {std::string("<g class=\"surface\"") + opacity_attr("fill-opacity", alpha)
                         + (opaque ? " stroke-width=\"0.6\" stroke-linejoin=\"round\"" : "") + ">",
                     {},
                     opaque});
                const Lighting3D lt =
                    material(surf->ambient(), surf->specular(), surf->shininess());
                static constexpr std::array<std::array<int, 3>, 2> tris = {
                    {{0, 1, 2}, {0, 2, 3}}};
                for (size_t i = 0; i + 1 < ur; ++i)
                    for (size_t j = 0; j + 1 < uc; ++j)
                    {
                        const vec3 q[4] = {vtx(i, j), vtx(i, j + 1), vtx(i + 1, j + 1),
                                           vtx(i + 1, j)};
                        for (const auto& tri : tris)
                        {
                            std::array<vec3, 3> dv{q[tri[0]], q[tri[1]], q[tri[2]]};
                            std::array<vec3, 3> wv;
                            bool                ok = true;
                            for (size_t k = 0; k < 3 && ok; ++k)
                            {
                                wv[k] = pj.to_world(dv[k]);
                                ok    = finite3(dv[k]) && inside_box(wv[k]);
                            }
                            if (!ok)
                                continue;
                            const Color base = cmap((wv[0].z + wv[1].z + wv[2].z) / 3.0);
                            add_triangle(gi, dv, lt.shade(base, wv[0], wv[1], wv[2]));
                        }
                    }
            }
        }
        else if (auto* mesh = dynamic_cast<const MeshSeries*>(&s))
        {
            const auto v      = mesh->vertices();
            const auto idx    = mesh->indices();
            const bool opaque = alpha >= 0.999f;
            groups.push_back(
                {std::string("<g class=\"mesh\"") + opacity_attr("fill-opacity", alpha)
                     + (opaque ? " stroke-width=\"0.6\" stroke-linejoin=\"round\"" : "") + ">",
                 {},
                 opaque});
            const Lighting3D lt = material(mesh->ambient(), mesh->specular(), mesh->shininess());
            const size_t     nv = v.size() / 6;   // {x, y, z, nx, ny, nz} per vertex
            for (size_t t = 0; t + 2 < idx.size(); t += 3)
            {
                std::array<vec3, 3> dv, wv;
                bool                ok = true;
                for (size_t k = 0; k < 3 && ok; ++k)
                {
                    const size_t vi = idx[t + k];
                    ok              = vi < nv;
                    if (ok)
                    {
                        dv[k] = {v[vi * 6], v[vi * 6 + 1], v[vi * 6 + 2]};
                        wv[k] = pj.to_world(dv[k]);
                        ok    = finite3(dv[k]);
                    }
                }
                if (ok)
                    add_triangle(gi, dv, lt.shade(col, wv[0], wv[1], wv[2]));
            }
        }
        // ShapeSeries3D and plugin series are not exported (documented limitation).
    }

    // Far to near.
    std::stable_sort(prims.begin(),
                     prims.end(),
                     [](const Prim3D& a, const Prim3D& b) { return a.depth > b.depth; });

    uint32_t current = UINT32_MAX;
    for (const auto& p : prims)
    {
        const Group3D& g = groups[p.group];
        if (p.group != current)
        {
            if (current != UINT32_MAX)
                c.body << "      </g>\n";
            c.body << "      " << g.open_tag << "\n";
            current = p.group;
        }
        switch (p.kind)
        {
            case Prim3D::Triangle:
            {
                const std::string h = hex(p.color);
                c.body << "        <path d=\"M" << num(p.p[0]) << ' ' << num(p.p[1]) << 'L'
                       << num(p.p[2]) << ' ' << num(p.p[3]) << 'L' << num(p.p[4]) << ' '
                       << num(p.p[5]) << "Z\" fill=\"" << h << "\"";
                if (g.seam_stroke)
                    c.body << " stroke=\"" << h << "\"";
                c.body << "/>\n";
                break;
            }
            case Prim3D::Segment:
                c.body << "        <line x1=\"" << num(p.p[0]) << "\" y1=\"" << num(p.p[1])
                       << "\" x2=\"" << num(p.p[2]) << "\" y2=\"" << num(p.p[3]) << "\"";
                if (p.has_color)
                    c.body << " stroke=\"" << hex(p.color) << "\"";
                c.body << "/>\n";
                break;
            case Prim3D::Marker:
                c.body << "        <use xlink:href=\"#" << g.symbol << "\" x=\"" << num(p.p[0])
                       << "\" y=\"" << num(p.p[1]) << "\"/>\n";
                break;
        }
    }
    if (current != UINT32_MAX)
        c.body << "      </g>\n";
    c.body << "    </g>\n";   // end clipped group

    // ── Labels: title first, then axis arrows, then tick labels without overlap ──
    std::vector<Box2> placed;
    if (!axes.get_title().empty())
    {
        const float tw = estimate_text_width(axes.get_title(), TITLE_FONT_PX);
        const float cx = vp.x + vp.w * 0.5f;
        const float ty = vp.y + 2.0f;
        placed.push_back({cx - tw * 0.5f, ty, cx + tw * 0.5f, ty + TITLE_FONT_PX});
        emit_text(c.body,
                  axes.get_title(),
                  cx,
                  ty,
                  TITLE_FONT_PX,
                  st.text_primary,
                  "middle",
                  VAlign::Top,
                  " font-weight=\"500\"");
    }

    // Axis arrows (X red, Y green, Z blue) leaving the max corners (render_arrows).
    struct Arrow
    {
        vec3               a, b;
        Color              color;
        const char*        fallback;
        const std::string& label;
    };
    const double len[3]    = {(hi[0] - lo[0]) * 0.18, (hi[1] - lo[1]) * 0.18, (hi[2] - lo[2]) * 0.18};
    const Arrow  arrows[3] = {
        {{hi[0], lo[1], lo[2]},
          {hi[0] + len[0], lo[1], lo[2]},
          {0.90f, 0.27f, 0.27f, 0.86f},
          "X",
          axes.get_xlabel()},
        {{lo[0], hi[1], lo[2]},
          {lo[0], hi[1] + len[1], lo[2]},
          {0.27f, 0.78f, 0.27f, 0.86f},
          "Y",
          axes.get_ylabel()},
        {{lo[0], lo[1], hi[2]},
          {lo[0], lo[1], hi[2] + len[2]},
          {0.31f, 0.51f, 1.00f, 0.86f},
          "Z",
          axes.get_zlabel()},
    };
    for (const auto& a : arrows)
    {
        float ax = 0, ay = 0, ad = 0, bx = 0, by = 0, bd = 0;
        if (!pj.project(a.a.x, a.a.y, a.a.z, ax, ay, ad)
            || !pj.project(a.b.x, a.b.y, a.b.z, bx, by, bd))
            continue;
        const float dx = bx - ax;
        const float dy = by - ay;
        const float dl = std::sqrt(dx * dx + dy * dy);
        c.body << "    <line class=\"axis-arrow\" x1=\"" << num(ax) << "\" y1=\"" << num(ay)
               << "\" x2=\"" << num(bx) << "\" y2=\"" << num(by) << "\" stroke=\"" << hex(a.color)
               << "\" stroke-width=\"3\"" << opacity_attr("stroke-opacity", a.color.a)
               << " stroke-linecap=\"round\"/>\n";
        if (dl > 1.0f)
        {
            const float ux = dx / dl;
            const float uy = dy / dl;
            const float hl = std::min(9.0f, dl * 0.5f);
            c.body << "    <polygon points=\"" << num(bx + ux * hl * 0.6f) << ','
                   << num(by + uy * hl * 0.6f) << ' '
                   << num(bx - ux * hl * 0.4f - uy * hl * 0.45f) << ','
                   << num(by - uy * hl * 0.4f + ux * hl * 0.45f) << ' '
                   << num(bx - ux * hl * 0.4f + uy * hl * 0.45f) << ','
                   << num(by - uy * hl * 0.4f - ux * hl * 0.45f) << "\" fill=\"" << hex(a.color)
                   << "\"" << opacity_attr("fill-opacity", a.color.a) << "/>\n";
        }
        const std::string lbl = a.label.empty() ? a.fallback : a.label;
        float             lx  = bx + (dl > 1.0f ? dx / dl * 8.0f : 8.0f);
        float             ly  = by + (dl > 1.0f ? dy / dl * 8.0f : 0.0f);
        const float       lw  = estimate_text_width(lbl, LABEL_FONT_PX);
        const float       hh  = LABEL_FONT_PX * 0.5f;
        Box2              r{lx, ly - hh, lx + lw, ly + hh};
        if (overlaps_any(r, placed, 2.0f))
        {
            lx = bx + 8.0f;
            for (const auto& o : placed)
            {
                const Box2 mv{lx, ly - hh, lx + lw, ly + hh};
                if (mv.x0 < o.x1 && mv.x1 > o.x0 && mv.y0 < o.y1 && mv.y1 > o.y0)
                    ly += o.y1 - mv.y0 + 2.0f;
            }
            r = {lx, ly - hh, lx + lw, ly + hh};
        }
        placed.push_back(r);
        emit_text(c.body, lbl, lx, ly, LABEL_FONT_PX, a.color, "start", VAlign::Middle);
    }

    // Tick labels: same anchors as render_geometry.cpp (horizontal axes along
    // the bottom edge nearest the camera, the vertical axis on the leftmost edge).
    {
        const vec3   view_dir  = vec3_normalize(cam.target - cam.position);
        const int    up_idx    = cam.up_axis == Camera::UpAxis::Z ? 2 : 1;
        const int    h_a       = 0;
        const int    h_b       = up_idx == 2 ? 1 : 2;
        const double cam_p[3]  = {cam.position.x, cam.position.y, cam.position.z};
        const double view_c[3] = {view_dir.x, view_dir.y, view_dir.z};
        double       tick_off[3];
        for (int i = 0; i < 3; ++i)
            tick_off[i] = (hi[i] - lo[i]) * 0.04;
        auto near_edge = [&](int axis)
        { return cam_p[axis] >= 0.0 ? hi[axis] + tick_off[axis] : lo[axis] - tick_off[axis]; };
        double vert_corner[3] = {lo[0], lo[1], lo[2]};
        {
            float best = 1e30f;
            for (int ci = 0; ci < 4; ++ci)
            {
                double p[3];
                p[up_idx] = lo[up_idx];
                p[h_a]    = (ci & 1) ? hi[h_a] : lo[h_a];
                p[h_b]    = (ci & 2) ? hi[h_b] : lo[h_b];
                float sx = 0, sy = 0, d = 0;
                if (pj.project(p[0], p[1], p[2], sx, sy, d) && sx < best)
                {
                    best             = sx;
                    vert_corner[h_a] = p[h_a];
                    vert_corner[h_b] = p[h_b];
                }
            }
        }
        c.body << "    <g class=\"tick-labels\">\n";
        for (int axis = 0; axis < 3; ++axis)
        {
            // Looking straight down an axis collapses its ticks onto a point.
            if (std::fabs(view_c[axis]) > 0.98)
                continue;
            const auto ticks    = axis == 0   ? axes.compute_x_ticks()
                                  : axis == 1 ? axes.compute_y_ticks()
                                              : axes.compute_z_ticks();
            const bool vertical = axis == up_idx;
            float      last_x = 0, last_y = 0;
            bool       has_last = false;
            for (size_t i = 0; i < ticks.positions.size() && i < ticks.labels.size(); ++i)
            {
                double p[3];
                p[axis] = ticks.positions[i];
                if (vertical)
                {
                    p[h_a] = vert_corner[h_a];
                    p[h_b] = vert_corner[h_b];
                }
                else
                {
                    const int other = 3 - axis - up_idx;
                    p[other]        = near_edge(other);
                    p[up_idx]       = lo[up_idx];
                }
                float sx = 0, sy = 0, d = 0;
                if (!pj.project(p[0], p[1], p[2], sx, sy, d))
                    continue;
                if (has_last && std::hypot(sx - last_x, sy - last_y) < 18.0f)
                    continue;
                last_x               = sx;
                last_y               = sy;
                has_last             = true;
                const std::string& t = ticks.labels[i];
                const float        w = estimate_text_width(t, TICK_FONT_PX);
                const float        h = TICK_FONT_PX * 0.5f;
                const Box2         r = vertical ? Box2{sx - 5.0f - w, sy - h, sx - 5.0f, sy + h}
                                                : Box2{sx - w * 0.5f, sy, sx + w * 0.5f, sy + 2 * h};
                if (overlaps_any(r, placed, 2.0f))
                    continue;
                placed.push_back(r);
                if (vertical)
                    emit_text(c.body,
                              t,
                              sx - 5.0f,
                              sy,
                              TICK_FONT_PX,
                              st.tick_label,
                              "end",
                              VAlign::Middle);
                else
                    emit_text(c.body, t, sx, sy, TICK_FONT_PX, st.tick_label, "middle", VAlign::Top);
            }
        }
        c.body << "    </g>\n";
    }

    emit_legend(c, axes, vp, legend);
    c.body << "  </g>\n";
}

}   // anonymous namespace

// ─── SvgExporter ────────────────────────────────────────────────────────────

std::string SvgExporter::to_string(const Figure& figure)
{
    return to_string(figure, SvgStyle{});
}

std::string SvgExporter::to_string(const Figure& figure, const SvgStyle& style)
{
    const uint32_t w = figure.width();
    const uint32_t h = figure.height();

    SvgContext c(style);

    // The caller computes the layout (Figure::compute_layout() is non-const);
    // each axes' viewport is used as-is.
    for (const auto& axes_ptr : figure.axes())
    {
        if (axes_ptr)
            emit_axes_2d(c, *axes_ptr, figure.legend());
    }
    for (const auto& base : figure.all_axes())
    {
        if (auto* a3 = dynamic_cast<const Axes3D*>(base.get()))
            emit_axes_3d(c, *a3, figure.legend());
    }

    std::ostringstream svg;
    svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
           "xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\""
        << w << "\" height=\"" << h << "\" viewBox=\"0 0 " << w << " " << h
        << "\" font-family=\"" << xml_escape(style.font_family) << "\">\n";
    const std::string defs = c.defs.str();
    if (!defs.empty())
        svg << "  <defs>\n" << defs << "  </defs>\n";
    svg << "  <rect class=\"background\" width=\"100%\" height=\"100%\" fill=\""
        << hex(style.background) << "\"/>\n";
    svg << c.body.str();
    svg << "</svg>\n";
    return svg.str();
}

bool SvgExporter::write_svg(const std::string& path, const Figure& figure)
{
    return write_svg(path, figure, SvgStyle{});
}

bool SvgExporter::write_svg(const std::string& path, const Figure& figure, const SvgStyle& style)
{
    const std::string content = to_string(figure, style);

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open())
        return false;

    file << content;
    return file.good();
}

}   // namespace spectra
