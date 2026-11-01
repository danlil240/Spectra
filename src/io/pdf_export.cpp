// pdf_export.cpp — PDF 1.4 serialization of the vector export walk
// (io/vector_scene.cpp), without an external PDF library.
//
// One page per figure; page space is the figure's pixel space (Y down) mapped
// to points at 96 dpi (1 px = 0.75 pt).  Content streams are Flate-compressed
// (stb's zlib compressor).  Text is set in the embedded Inter subset (a
// CIDFontType2 with Identity encoding of glyph ids, FontFile2) with a ToUnicode
// CMap, so it can be selected and searched.  Transparency uses ExtGState
// CA / ca; alpha gradients of stat fills use a luminosity soft mask; markers
// are Form XObjects instanced per point.

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <span>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "io/pdf_export_internal.hpp"
#include "io/text_metrics.hpp"
#include "io/vector_scene.hpp"
#include "io/vector_writer.hpp"
#include "io/zlib_compress.hpp"

namespace spectra
{

namespace
{

constexpr double PT_PER_PX = 0.75;   // CSS pixels at 96 dpi

// ─── Formatting ─────────────────────────────────────────────────────────────

// Fixed-point number with up to `decimals` digits, trailing zeros trimmed.
void put_pdf_num(std::string& out, double v, int decimals = 3)
{
    if (!std::isfinite(v))
        v = 0.0;
    char  buf[64];
    auto  res = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::fixed, decimals);
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

std::string pdf_num(double v, int decimals = 3)
{
    std::string s;
    put_pdf_num(s, v, decimals);
    return s;
}

// Coordinates (pixels): 2 decimals, the precision of the SVG export.
void put_xy(std::string& out, double x, double y)
{
    put_pdf_num(out, x, 2);
    out += ' ';
    put_pdf_num(out, y, 2);
}

float clamp01(float v)
{
    return std::clamp(std::isfinite(v) ? v : 0.0f, 0.0f, 1.0f);
}

int per_mille(float a)
{
    return static_cast<int>(std::lround(clamp01(a) * 1000.0f));
}

std::string rgb_triplet(const Color& c)
{
    std::string s;
    put_pdf_num(s, clamp01(c.r));
    s += ' ';
    put_pdf_num(s, clamp01(c.g));
    s += ' ';
    put_pdf_num(s, clamp01(c.b));
    return s;
}

void put_hex4(std::string& out, uint32_t v)
{
    static constexpr char digits[] = "0123456789ABCDEF";
    for (int shift = 12; shift >= 0; shift -= 4)
        out += digits[(v >> shift) & 0xF];
}

// ─── Path geometry → content operators ──────────────────────────────────────

class OpsSink final : public PathSink
{
   public:
    explicit OpsSink(std::string& out) : out_(out) {}
    void move_to(double x, double y) override
    {
        put_xy(out_, x, y);
        out_ += " m\n";
    }
    void line_to(double x, double y) override
    {
        put_xy(out_, x, y);
        out_ += " l\n";
    }
    void close() override { out_ += "h\n"; }

   private:
    std::string& out_;
};

// Largest |coordinate| of SVG path data.
class ExtentSink final : public PathSink
{
   public:
    void   move_to(double x, double y) override { note(x, y); }
    void   line_to(double x, double y) override { note(x, y); }
    void   close() override {}
    double extent = 0.0;

   private:
    void note(double x, double y) { extent = std::max({extent, std::fabs(x), std::fabs(y)}); }
};

std::string path_ops(std::string_view d)
{
    std::string out;
    OpsSink     sink(out);
    if (!for_each_path_command(d, sink))
        SPECTRA_LOG_WARN("pdf", "PDF export: malformed path data truncated");
    return out;
}

// Circle as four cubic Béziers.
void circle_ops(std::string& out, double cx, double cy, double r)
{
    constexpr double K = 0.5522847498;
    const double     k = K * r;
    auto             p = [&](double x, double y)
    {
        put_xy(out, x, y);
        out += ' ';
    };
    p(cx + r, cy);
    out += "m\n";
    p(cx + r, cy + k);
    p(cx + k, cy + r);
    p(cx, cy + r);
    out += "c\n";
    p(cx - k, cy + r);
    p(cx - r, cy + k);
    p(cx - r, cy);
    out += "c\n";
    p(cx - r, cy - k);
    p(cx - k, cy - r);
    p(cx, cy - r);
    out += "c\n";
    p(cx + k, cy - r);
    p(cx + r, cy - k);
    p(cx + r, cy);
    out += "c\nh\n";
}

void rect_ops(std::string& out, double x, double y, double w, double h, double rx)
{
    rx = std::min({rx, w * 0.5, h * 0.5});
    if (!(rx > 0.0))
    {
        put_xy(out, x, y);
        out += ' ';
        put_xy(out, w, h);
        out += " re\n";
        return;
    }
    constexpr double K = 0.5522847498;
    const double     k = K * rx;
    auto             p = [&](double px, double py)
    {
        put_xy(out, px, py);
        out += ' ';
    };
    const double x1 = x + w, y1 = y + h;
    p(x + rx, y);
    out += "m\n";
    p(x1 - rx, y);
    out += "l\n";
    p(x1 - rx + k, y);
    p(x1, y + rx - k);
    p(x1, y + rx);
    out += "c\n";
    p(x1, y1 - rx);
    out += "l\n";
    p(x1, y1 - rx + k);
    p(x1 - rx + k, y1);
    p(x1 - rx, y1);
    out += "c\n";
    p(x + rx, y1);
    out += "l\n";
    p(x + rx - k, y1);
    p(x, y1 - rx + k);
    p(x, y1 - rx);
    out += "c\n";
    p(x, y + rx);
    out += "l\n";
    p(x, y + rx - k);
    p(x + rx - k, y);
    p(x + rx, y);
    out += "c\nh\n";
}

void polygon_ops(std::string& out, std::span<const VectorPoint> pts)
{
    for (size_t i = 0; i < pts.size(); ++i)
    {
        put_xy(out, pts[i].x, pts[i].y);
        out += i == 0 ? " m\n" : " l\n";
    }
    if (!pts.empty())
        out += "h\n";
}

// ─── Document objects ───────────────────────────────────────────────────────

// Objects by number (1-based); bodies are filled in any order.
class PdfObjects
{
   public:
    int reserve()
    {
        bodies_.emplace_back();
        return static_cast<int>(bodies_.size());
    }
    void set(int n, std::string body) { bodies_[static_cast<size_t>(n - 1)] = std::move(body); }
    int  add(std::string body)
    {
        const int n = reserve();
        set(n, std::move(body));
        return n;
    }
    [[nodiscard]] size_t             count() const { return bodies_.size(); }
    [[nodiscard]] const std::string& body(size_t i) const { return bodies_[i]; }

   private:
    std::vector<std::string> bodies_;
};

std::string ref(int n)
{
    return std::to_string(n) + " 0 R";
}

// A stream object; `dict` holds the entries other than /Length and /Filter.
std::string stream_object(std::string_view dict, std::string_view data, bool compress = true)
{
    std::vector<uint8_t> z;
    if (compress && !data.empty())
        z = zlib_compress(
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(data.data()), data.size()));
    const bool             flate = !z.empty();
    const std::string_view payload =
        flate ? std::string_view(reinterpret_cast<const char*>(z.data()), z.size()) : data;
    std::string out = "<<";
    out += dict;
    out += " /Length " + std::to_string(payload.size());
    if (flate)
        out += " /Filter /FlateDecode";
    out += " >>\nstream\n";
    out += payload;
    out += "\nendstream";
    return out;
}

// Resources shared by every page and form (one /Resources dictionary).
class PdfResources
{
   public:
    explicit PdfResources(PdfObjects& objs) : objs_(objs), dict_obj_(objs.reserve()) {}

    [[nodiscard]] int dict_obj() const { return dict_obj_; }

    // ExtGState with stroke alpha CA and fill alpha ca (per mille).
    const std::string& alpha_state(int stroke_pm, int fill_pm)
    {
        const auto key = std::make_pair(stroke_pm, fill_pm);
        auto       it  = alpha_.find(key);
        if (it != alpha_.end())
            return it->second;
        const std::string name = "GA" + std::to_string(alpha_.size());
        const int obj = objs_.add("<< /Type /ExtGState /CA " + pdf_num(stroke_pm / 1000.0) + " /ca "
                                  + pdf_num(fill_pm / 1000.0) + " >>");
        ext_gstates_.emplace_back(name, obj);
        return alpha_.emplace(key, name).first->second;
    }

    std::string add_ext_gstate(std::string_view prefix, int obj)
    {
        std::string name = std::string(prefix) + std::to_string(ext_gstates_.size());
        ext_gstates_.emplace_back(name, obj);
        return name;
    }

    std::string add_xobject(std::string_view prefix, int obj)
    {
        std::string name = std::string(prefix) + std::to_string(xobjects_.size());
        xobjects_.emplace_back(name, obj);
        return name;
    }

    std::string add_shading(int obj)
    {
        std::string name = "Sh" + std::to_string(shadings_.size());
        shadings_.emplace_back(name, obj);
        return name;
    }

    // Records that `glyph` was set for `cp` (for the subset and ToUnicode).
    void note_glyph(uint16_t glyph, uint32_t cp)
    {
        glyphs_.emplace(glyph, cp);
        if (seen_cps_.emplace(cp).second)
            codepoints_.push_back(cp);
    }

    // Writes the font and the resource dictionary.  Call once, after all pages.
    void finish(bool inter_text)
    {
        std::string dict = "<<";
        if (inter_text && !glyphs_.empty())
            dict += " /Font << /F1 " + ref(write_inter_font()) + " >>";
        else if (!inter_text && used_text_)
            dict += " /Font << /F1 "
                    + ref(objs_.add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
                                    "/Encoding /WinAnsiEncoding >>"))
                    + " >>";
        auto section = [&](const char* key, const std::vector<std::pair<std::string, int>>& v)
        {
            if (v.empty())
                return;
            dict += std::string(" /") + key + " <<";
            for (const auto& [name, obj] : v)
                dict += " /" + name + " " + ref(obj);
            dict += " >>";
        };
        section("ExtGState", ext_gstates_);
        section("XObject", xobjects_);
        section("Shading", shadings_);
        dict += " >>";
        objs_.set(dict_obj_, std::move(dict));
    }

    void note_text_used() { used_text_ = true; }

   private:
    int write_inter_font()
    {
        std::vector<uint8_t> font = inter_font_subset(codepoints_);
        if (font.empty())
        {
            // Glyph ids are those of the full font, so it can stand in for the subset.
            const auto full = embedded_inter_font();
            font.assign(full.begin(), full.end());
        }
        // Subset tag: six capitals derived from the glyph set (stable output).
        uint32_t h = 2166136261u;
        for (const auto& [g, cp] : glyphs_)
        {
            h = (h ^ g) * 16777619u;
            h = (h ^ cp) * 16777619u;
        }
        std::string tag;
        for (int i = 0; i < 6; ++i)
        {
            tag += static_cast<char>('A' + h % 26);
            h /= 26;
        }
        const std::string base = "/" + tag + "+Inter";

        const int file = objs_.add(stream_object(
            " /Length1 " + std::to_string(font.size()),
            std::string_view(reinterpret_cast<const char*>(font.data()), font.size())));

        const InterFontMetrics fm = inter_font_metrics();
        auto      em         = [](float v) { return pdf_num(static_cast<double>(v) * 1000.0, 1); };
        const int descriptor = objs_.add(
            "<< /Type /FontDescriptor /FontName " + base + " /Flags 32 /FontBBox [" + em(fm.bbox[0])
            + " " + em(fm.bbox[1]) + " " + em(fm.bbox[2]) + " " + em(fm.bbox[3])
            + "] /ItalicAngle 0 /Ascent " + em(fm.ascent) + " /Descent " + em(fm.descent)
            + " /CapHeight " + em(0.727f) + " /StemV 80 /FontFile2 " + ref(file) + " >>");

        // Widths of the used glyphs, runs of consecutive ids grouped.
        std::string w = "[";
        for (auto it = glyphs_.begin(); it != glyphs_.end();)
        {
            const uint16_t first = it->first;
            w += " " + std::to_string(first) + " [";
            uint32_t next = first;
            while (it != glyphs_.end() && it->first == next)
            {
                w += " " + pdf_num(inter_glyph_advance_em(it->first) * 1000.0, 2);
                ++it;
                ++next;
            }
            w += " ]";
        }
        w += " ]";
        const int cid =
            objs_.add("<< /Type /Font /Subtype /CIDFontType2 /BaseFont " + base
                      + " /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) "
                        "/Supplement 0 >> /FontDescriptor "
                      + ref(descriptor) + " /CIDToGIDMap /Identity /DW 1000 /W " + w + " >>");

        const int to_unicode = objs_.add(stream_object("", to_unicode_cmap()));
        return objs_.add("<< /Type /Font /Subtype /Type0 /BaseFont " + base
                         + " /Encoding /Identity-H /DescendantFonts [" + ref(cid) + "] /ToUnicode "
                         + ref(to_unicode) + " >>");
    }

    std::string to_unicode_cmap() const
    {
        std::string c = "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n"
                        "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
                        "/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n"
                        "1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n";
        std::vector<std::pair<uint16_t, uint32_t>> entries(glyphs_.begin(), glyphs_.end());
        for (size_t i = 0; i < entries.size(); i += 100)
        {
            const size_t n = std::min<size_t>(100, entries.size() - i);
            c += std::to_string(n) + " beginbfchar\n";
            for (size_t k = i; k < i + n; ++k)
            {
                const auto [g, cp0] = entries[k];
                // .notdef stands for every codepoint Inter lacks.
                const uint32_t cp = (g == 0 || cp0 > 0x10FFFF) ? 0xFFFD : cp0;
                c += "<";
                put_hex4(c, g);
                c += "> <";
                if (cp > 0xFFFF)
                {
                    const uint32_t v = cp - 0x10000;
                    put_hex4(c, 0xD800 + (v >> 10));
                    put_hex4(c, 0xDC00 + (v & 0x3FF));
                }
                else
                    put_hex4(c, cp);
                c += ">\n";
            }
            c += "endbfchar\n";
        }
        c += "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
        return c;
    }

    PdfObjects&                                objs_;
    int                                        dict_obj_;
    std::map<std::pair<int, int>, std::string> alpha_;
    std::vector<std::pair<std::string, int>>   ext_gstates_;
    std::vector<std::pair<std::string, int>>   xobjects_;
    std::vector<std::pair<std::string, int>>   shadings_;
    std::map<uint16_t, uint32_t>               glyphs_;
    std::vector<uint32_t>                      codepoints_;
    std::set<uint32_t>                         seen_cps_;
    bool                                       used_text_ = false;
};

// ─── Page writer ────────────────────────────────────────────────────────────

// Style after SVG inheritance; defaults are SVG's initial values.
struct Resolved
{
    Color              color          = {0.0f, 0.0f, 0.0f, 1.0f};
    VectorPaint        fill           = VectorPaint::rgb({0.0f, 0.0f, 0.0f, 1.0f});
    float              fill_opacity   = 1.0f;
    VectorPaint        stroke         = VectorPaint::none();
    float              stroke_width   = 1.0f;
    float              stroke_opacity = 1.0f;
    VectorLineJoin     join           = VectorLineJoin::Unset;   // miter
    VectorLineCap      cap            = VectorLineCap::Unset;    // butt
    std::vector<float> dash;
};

// `s` over `parent`.  Opacities of 1 mean "not specified" (the SVG writer omits
// them), so they inherit like every unset field.
Resolved resolve(const Resolved& parent, const VectorStyle& s)
{
    Resolved r = parent;
    if (s.color)
        r.color = *s.color;
    if (s.fill.kind != PaintKind::Unset)
        r.fill = s.fill;
    if (s.fill_opacity < 0.999f)
        r.fill_opacity = s.fill_opacity;
    if (s.stroke.kind != PaintKind::Unset)
        r.stroke = s.stroke;
    if (s.stroke_width >= 0.0f)
        r.stroke_width = s.stroke_width;
    if (s.stroke_opacity < 0.999f)
        r.stroke_opacity = s.stroke_opacity;
    if (s.join != VectorLineJoin::Unset)
        r.join = s.join;
    if (s.cap != VectorLineCap::Unset)
        r.cap = s.cap;
    if (!s.dash.empty())
        r.dash = s.dash;
    return r;
}

bool paints(const VectorPaint& p)
{
    return p.kind == PaintKind::Rgb || p.kind == PaintKind::CurrentColor
           || p.kind == PaintKind::Gradient;
}

Color paint_color(const VectorPaint& p, const Resolved& r)
{
    return p.kind == PaintKind::Rgb ? p.color : r.color;
}

class PdfPageWriter final : public VectorWriter
{
   public:
    PdfPageWriter(PdfObjects& objs, PdfResources& res, double width_px, double height_px)
        : objs_(objs), res_(res), width_px_(width_px), height_px_(height_px)
    {
        styles_.emplace_back();
        // Pixel space (Y down) -> points (Y up); SVG's miter limit.
        put_pdf_num(out_, PT_PER_PX, 2);
        out_ += " 0 0 ";
        put_pdf_num(out_, -PT_PER_PX, 2);
        out_ += " 0 ";
        put_pdf_num(out_, height_px * PT_PER_PX, 2);
        out_ += " cm\n4 M\n";
    }

    void background(const Color& c)
    {
        set_fill(c);
        rect_ops(out_, 0.0, 0.0, width_px_, height_px_, 0.0);
        out_ += "f\n";
    }

    [[nodiscard]] const std::string& content() const { return out_; }

    int define_clip_rect(double x, double y, double w, double h) override
    {
        const int id = next_id_++;
        clips_[id]   = {x, y, w, h};
        return id;
    }

    int define_linear_gradient(double       x1,
                               double       y1,
                               double       x2,
                               double       y2,
                               const Color& color,
                               float        a0,
                               float        a1) override
    {
        // Alpha ramp as a luminosity soft mask: a gray axial shading from a0 to a1.
        const int id = next_id_++;
        const int shading =
            objs_.add("<< /ShadingType 2 /ColorSpace /DeviceGray /Coords [" + pdf_num(x1, 2) + " "
                      + pdf_num(y1, 2) + " " + pdf_num(x2, 2) + " " + pdf_num(y2, 2)
                      + "] /Function << /FunctionType 2 /Domain [0 1] /C0 [" + pdf_num(clamp01(a0))
                      + "] /C1 [" + pdf_num(clamp01(a1)) + "] /N 1 >> /Extend [true true] >>");
        const std::string sh_name = res_.add_shading(shading);
        const int         form    = objs_.add(stream_object(
            " /Type /XObject /Subtype /Form /BBox [0 0 " + pdf_num(width_px_, 2) + " "
                + pdf_num(height_px_, 2)
                + "] /Group << /Type /Group /S /Transparency /CS /DeviceGray >> /Resources "
                + ref(res_.dict_obj()),
            "/" + sh_name + " sh\n"));
        const int         gs = objs_.add("<< /Type /ExtGState /ca 1 /CA 1 /SMask << /Type /Mask /S "
                                         "/Luminosity /G "
                                 + ref(form) + " >> >>");
        gradients_[id]       = {res_.add_ext_gstate("GM", gs), color};
        return id;
    }

    void define_marker(int id, std::span<const MarkerPart> parts) override
    {
        markers_[id].assign(parts.begin(), parts.end());
    }

    void begin_group(const VectorGroup& g) override
    {
        styles_.push_back(resolve(styles_.back(), g.style));
        const auto it      = clips_.find(g.clip);
        const bool clipped = it != clips_.end();
        clipped_.push_back(clipped);
        if (clipped)
        {
            push_state();
            const auto& r = it->second;
            rect_ops(out_, r[0], r[1], r[2], r[3], 0.0);
            out_ += "W n\n";
        }
    }

    void end_group() override
    {
        if (clipped_.empty())
            return;
        if (clipped_.back())
            pop_state();
        clipped_.pop_back();
        styles_.pop_back();
    }

    void path(int, std::string_view d, const VectorStyle& s) override
    {
        paint(path_ops(d), s, true);
    }

    void line(int, double x1, double y1, double x2, double y2, const VectorStyle& s) override
    {
        std::string g;
        put_xy(g, x1, y1);
        g += " m\n";
        put_xy(g, x2, y2);
        g += " l\n";
        paint(g, s, false);
    }

    void polygon(int, std::span<const VectorPoint> pts, const VectorStyle& s) override
    {
        std::string g;
        polygon_ops(g, pts);
        paint(g, s, true);
    }

    void circle(int, double cx, double cy, double r, const VectorStyle& s) override
    {
        std::string g;
        circle_ops(g, cx, cy, r);
        paint(g, s, true);
    }

    void rect(int, double x, double y, double w, double h, double rx, const VectorStyle& s) override
    {
        std::string g;
        rect_ops(g, x, y, w, h, rx);
        paint(g, s, true);
    }

    void use_marker(int, int marker, double x, double y, const VectorStyle& s) override
    {
        const auto mk = markers_.find(marker);
        if (mk == markers_.end() || !std::isfinite(x) || !std::isfinite(y))
            return;
        const Resolved r  = resolve(styles_.back(), s);
        const float    op = clamp01(s.opacity);
        const int      sa = per_mille(r.stroke_opacity * op);
        const int      fa = per_mille(r.fill_opacity * op);
        set_fill(r.color);
        set_stroke(r.color);
        set_alpha(sa, fa);
        out_ += "q 1 0 0 1 ";
        put_xy(out_, x, y);
        out_ += " cm /" + marker_form(marker, mk->second, sa, fa) + " Do Q\n";
    }

    void image(int,
               double                   x,
               double                   y,
               double                   w,
               double                   h,
               std::span<const uint8_t> rgba,
               uint32_t                 px_w,
               uint32_t                 px_h,
               bool                     pixelated,
               float                    opacity,
               std::string_view) override
    {
        const size_t n = static_cast<size_t>(px_w) * px_h;
        if (n == 0 || rgba.size() < n * 4 || !(w > 0.0) || !(h > 0.0))
            return;
        std::string rgb(n * 3, '\0');
        std::string alpha(n, '\0');
        bool        opaque = true;
        for (size_t i = 0; i < n; ++i)
        {
            rgb[i * 3 + 0] = static_cast<char>(rgba[i * 4 + 0]);
            rgb[i * 3 + 1] = static_cast<char>(rgba[i * 4 + 1]);
            rgb[i * 3 + 2] = static_cast<char>(rgba[i * 4 + 2]);
            alpha[i]       = static_cast<char>(rgba[i * 4 + 3]);
            opaque         = opaque && rgba[i * 4 + 3] == 255;
        }
        const std::string size = " /Width " + std::to_string(px_w) + " /Height "
                                 + std::to_string(px_h) + " /BitsPerComponent 8";
        const char* interp = pixelated ? " /Interpolate false" : " /Interpolate true";
        std::string dict   = " /Type /XObject /Subtype /Image" + size + " /ColorSpace /DeviceRGB";
        dict += interp;
        if (!opaque)
            dict += " /SMask "
                    + ref(objs_.add(stream_object(" /Type /XObject /Subtype /Image" + size
                                                      + " /ColorSpace /DeviceGray" + interp,
                                                  alpha)));
        const std::string name = res_.add_xobject("Im", objs_.add(stream_object(dict, rgb)));

        // Image space's unit square, row 0 at the top: [w 0 0 -h x y+h] in Y-down pixels.
        push_state();
        set_alpha(state_.stroke_alpha, per_mille(clamp01(opacity)));
        put_pdf_num(out_, w, 3);
        out_ += " 0 0 ";
        put_pdf_num(out_, -h, 3);
        out_ += ' ';
        put_xy(out_, x, y + h);
        out_ += " cm /" + name + " Do\n";
        pop_state();
    }

    void text(const VectorText& t) override
    {
        if (t.text.empty() || !(t.font_px > 0.0f))
            return;
        res_.note_text_used();
        std::string shown;
        if (inter_metrics_available())
        {
            shown = "<";
            for (const uint32_t cp : utf8_codepoints(t.text))
            {
                const uint16_t g = inter_glyph_index(cp);
                res_.note_glyph(g, cp);
                put_hex4(shown, g);
            }
            shown += ">";
        }
        else
        {
            // Helvetica fallback (no embedded font): ASCII only.
            shown = "(";
            for (const uint32_t cp : utf8_codepoints(t.text))
            {
                const char ch = cp >= 0x20 && cp < 0x7F ? static_cast<char>(cp) : '?';
                if (ch == '(' || ch == ')' || ch == '\\')
                    shown += '\\';
                shown += ch;
            }
            shown += ")";
        }

        const double width = inter_text_width(t.text, t.font_px);
        const double off   = t.anchor == VectorAnchor::Middle ? width * 0.5
                             : t.anchor == VectorAnchor::End  ? width
                                                              : 0.0;
        const double theta = t.rotated ? t.rot_deg * 3.14159265358979323846 / 180.0 : 0.0;
        const double c     = std::cos(theta);
        const double s     = std::sin(theta);
        double       x     = t.x - off;
        double       y     = t.y;
        if (t.rotated)
        {
            const double dx = x - t.rot_x;
            const double dy = y - t.rot_y;
            x               = t.rot_x + c * dx - s * dy;
            y               = t.rot_y + s * dx + c * dy;
        }

        set_fill(t.color);
        if (t.medium_weight)
        {
            // Inter Medium is not embedded: thicken Regular with a thin stroke.
            set_stroke(t.color);
            set_line(t.font_px * 0.015f, VectorLineCap::Unset, VectorLineJoin::Round, {});
            set_alpha(per_mille(t.color.a), per_mille(t.color.a));
        }
        else
            set_alpha(state_.stroke_alpha, per_mille(t.color.a));
        out_ += "BT\n";
        if (t.medium_weight)
            out_ += "2 Tr\n";
        out_ += "/F1 ";
        put_pdf_num(out_, t.font_px, 3);
        out_ += " Tf\n";
        for (const double v : {c, s, s, -c})
        {
            put_pdf_num(out_, v, 4);
            out_ += ' ';
        }
        put_xy(out_, x, y);
        out_ += " Tm\n" + shown + " Tj\n";
        if (t.medium_weight)
            out_ += "0 Tr\n";
        out_ += "ET\n";
    }

    void note_skipped_series(std::string_view, std::string_view, std::string_view) override {}

    [[nodiscard]] const char* format_name() const override { return "PDF"; }

   private:
    // Graphics state the content stream last set (to skip redundant operators).
    struct GState
    {
        std::string fill         = "0 0 0";
        std::string stroke       = "0 0 0";
        int         stroke_alpha = 1000;
        int         fill_alpha   = 1000;
        std::string width        = "1";
        int         cap          = 0;
        int         join         = 0;
        std::string dash         = "[] 0";
    };

    void push_state()
    {
        out_ += "q\n";
        saved_.push_back(state_);
    }

    void pop_state()
    {
        out_ += "Q\n";
        if (!saved_.empty())
        {
            state_ = saved_.back();
            saved_.pop_back();
        }
    }

    void set_fill(const Color& c)
    {
        std::string v = rgb_triplet(c);
        if (v == state_.fill)
            return;
        out_ += v + " rg\n";
        state_.fill = std::move(v);
    }

    void set_stroke(const Color& c)
    {
        std::string v = rgb_triplet(c);
        if (v == state_.stroke)
            return;
        out_ += v + " RG\n";
        state_.stroke = std::move(v);
    }

    void set_alpha(int stroke_pm, int fill_pm)
    {
        if (stroke_pm == state_.stroke_alpha && fill_pm == state_.fill_alpha)
            return;
        out_ += "/" + res_.alpha_state(stroke_pm, fill_pm) + " gs\n";
        state_.stroke_alpha = stroke_pm;
        state_.fill_alpha   = fill_pm;
    }

    void set_line(float                     width,
                  VectorLineCap             cap,
                  VectorLineJoin            join,
                  const std::vector<float>& dash)
    {
        std::string w = pdf_num(width, 3);
        if (w != state_.width)
        {
            out_ += w + " w\n";
            state_.width = std::move(w);
        }
        const int c = cap == VectorLineCap::Round ? 1 : 0;
        if (c != state_.cap)
        {
            out_ += std::to_string(c) + " J\n";
            state_.cap = c;
        }
        const int j = join == VectorLineJoin::Round ? 1 : 0;
        if (j != state_.join)
        {
            out_ += std::to_string(j) + " j\n";
            state_.join = j;
        }
        std::string d = "[";
        for (size_t i = 0; i < dash.size(); ++i)
        {
            if (i > 0)
                d += ' ';
            put_pdf_num(d, dash[i], 2);
        }
        d += "] 0";
        if (d != state_.dash)
        {
            out_ += d + " d\n";
            state_.dash = std::move(d);
        }
    }

    // Fills and/or strokes `geom` (path construction operators) with `s` over the
    // current group style.  Lines (`can_fill` false) are never filled, as in SVG.
    void paint(const std::string& geom, const VectorStyle& s, bool can_fill)
    {
        if (geom.empty())
            return;
        const Resolved r    = resolve(styles_.back(), s);
        const float    op   = clamp01(s.opacity);
        bool           fill = can_fill && paints(r.fill);
        const bool     stroke =
            paints(r.stroke) && r.stroke.kind != PaintKind::Gradient && r.stroke_width > 0.0f;
        if (fill && r.fill.kind == PaintKind::Gradient)
        {
            fill          = false;
            const auto gi = gradients_.find(r.fill.gradient);
            if (gi != gradients_.end())
            {
                push_state();
                out_ += "/" + gi->second.gs + " gs\n";
                // The mask carries the alpha; ca adds the element opacity on top.
                state_.stroke_alpha = state_.fill_alpha = 1000;   // the mask state sets both
                set_fill(gi->second.color);
                set_alpha(1000, per_mille(op));
                out_ += geom + "f\n";
                pop_state();
            }
        }
        if (!fill && !stroke)
            return;
        if (fill)
            set_fill(paint_color(r.fill, r));
        if (stroke)
        {
            set_stroke(paint_color(r.stroke, r));
            set_line(r.stroke_width, r.cap, r.join, r.dash);
        }
        set_alpha(stroke ? per_mille(r.stroke_opacity * op) : state_.stroke_alpha,
                  fill ? per_mille(r.fill_opacity * op) : state_.fill_alpha);
        out_ += geom;
        out_ += fill && stroke ? "B\n" : fill ? "f\n" : "S\n";
    }

    // Form XObject drawing marker `id` around the origin in the current colour.
    // Parts with their own alpha (the default dot's rim) are baked per alpha.
    std::string marker_form(int id, const std::vector<MarkerPart>& parts, int sa, int fa)
    {
        bool has_rim = false;
        for (const auto& p : parts)
            has_rim = has_rim || p.rim;
        const auto key = std::make_tuple(id, has_rim ? sa : -1, has_rim ? fa : -1);
        auto       it  = forms_.find(key);
        if (it != forms_.end())
            return it->second;

        std::string body      = "0 J 0 j [] 0 d\n";
        double      extent    = 0.0;
        double      max_width = 0.0;
        for (const auto& p : parts)
        {
            std::string g;
            switch (p.kind)
            {
                case MarkerPart::Kind::Circle:
                    circle_ops(g, 0.0, 0.0, p.radius);
                    extent = std::max(extent, static_cast<double>(p.radius));
                    break;
                case MarkerPart::Kind::Polygon:
                    polygon_ops(g, p.points);
                    for (const auto& q : p.points)
                        extent = std::max({extent, std::fabs(q.x), std::fabs(q.y)});
                    break;
                case MarkerPart::Kind::Path:
                {
                    g = path_ops(p.d);
                    ExtentSink e;
                    for_each_path_command(p.d, e);
                    extent = std::max(extent, e.extent);
                    break;
                }
            }
            if (p.rim)
            {
                body += std::string(p.rim_white ? "1 1 1" : "0 0 0") + " RG\n/"
                        + res_.alpha_state(per_mille(sa / 1000.0f * p.rim_opacity), fa) + " gs\n"
                        + pdf_num(p.stroke_width) + " w\n" + g + "S\n";
            }
            else if (p.filled)
                body += g + "f\n";
            else
                body += pdf_num(p.stroke_width) + " w\n" + g + "S\n";
            if (!p.filled)
                max_width = std::max(max_width, static_cast<double>(p.stroke_width));
        }
        const std::string b = pdf_num(extent + max_width * 0.5 + 1.0, 2);
        const int         form =
            objs_.add(stream_object(" /Type /XObject /Subtype /Form /BBox [-" + b + " -" + b + " "
                                        + b + " " + b + "] /Resources " + ref(res_.dict_obj()),
                                    body));
        return forms_.emplace(key, res_.add_xobject("Mk", form)).first->second;
    }

    struct Gradient
    {
        std::string gs;
        Color       color;
    };

    PdfObjects&                                      objs_;
    PdfResources&                                    res_;
    double                                           width_px_;
    double                                           height_px_;
    std::string                                      out_;
    GState                                           state_;
    std::vector<GState>                              saved_;
    std::vector<Resolved>                            styles_;
    std::vector<bool>                                clipped_;
    int                                              next_id_ = 0;
    std::map<int, std::array<double, 4>>             clips_;
    std::map<int, Gradient>                          gradients_;
    std::map<int, std::vector<MarkerPart>>           markers_;
    std::map<std::tuple<int, int, int>, std::string> forms_;
};

}   // anonymous namespace

std::string pdf_export_to_string(std::span<const PdfPage> pages, const SvgExportHost& host)
{
    PdfObjects   objs;
    const int    catalog = objs.reserve();
    const int    tree    = objs.reserve();
    const int    info    = objs.add("<< /Producer (Spectra) >>");
    PdfResources res(objs);

    std::vector<int> kids;
    for (const auto& page : pages)
    {
        if (!page.figure)
            continue;
        const Figure& fig = *page.figure;
        const double  w   = std::max(1u, fig.width());
        const double  h   = std::max(1u, fig.height());
        PdfPageWriter writer(objs, res, w, h);
        writer.background(page.style.background);
        draw_vector_scene(fig, page.style, host, writer);
        const int contents = objs.add(stream_object("", writer.content()));
        kids.push_back(objs.add("<< /Type /Page /Parent " + ref(tree) + " /MediaBox [0 0 "
                                + pdf_num(w * PT_PER_PX, 2) + " " + pdf_num(h * PT_PER_PX, 2)
                                + "] /Resources " + ref(res.dict_obj()) + " /Contents "
                                + ref(contents)
                                + " /Group << /Type /Group /S /Transparency /CS /DeviceRGB >> >>"));
    }
    res.finish(inter_metrics_available());

    std::string kid_refs;
    for (const int k : kids)
        kid_refs += (kid_refs.empty() ? "" : " ") + ref(k);
    objs.set(
        tree,
        "<< /Type /Pages /Kids [" + kid_refs + "] /Count " + std::to_string(kids.size()) + " >>");
    objs.set(catalog, "<< /Type /Catalog /Pages " + ref(tree) + " >>");

    // Header (with a binary comment so transfers treat the file as binary),
    // objects, cross-reference table and trailer.
    std::string         pdf = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
    std::vector<size_t> offsets;
    offsets.reserve(objs.count());
    for (size_t i = 0; i < objs.count(); ++i)
    {
        offsets.push_back(pdf.size());
        pdf += std::to_string(i + 1) + " 0 obj\n";
        pdf += objs.body(i);
        pdf += "\nendobj\n";
    }
    const size_t xref = pdf.size();
    pdf += "xref\n0 " + std::to_string(objs.count() + 1) + "\n0000000000 65535 f \n";
    for (const size_t off : offsets)
    {
        char entry[24];
        std::snprintf(entry, sizeof(entry), "%010zu 00000 n \n", off);
        pdf += entry;
    }
    pdf += "trailer\n<< /Size " + std::to_string(objs.count() + 1) + " /Root " + ref(catalog)
           + " /Info " + ref(info) + " >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    return pdf;
}

bool pdf_export_write(const std::string&       path,
                      std::span<const PdfPage> pages,
                      const SvgExportHost&     host)
{
    const std::string content = pdf_export_to_string(pages, host);

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open())
        return false;
    file << content;
    return file.good();
}

// ─── PdfExporter ────────────────────────────────────────────────────────────

bool PdfExporter::write_pdf(const std::string& path, const Figure& figure)
{
    return write_pdf(path, figure, default_svg_style_for(figure));
}

bool PdfExporter::write_pdf(const std::string& path, const Figure& figure, const SvgStyle& style)
{
    const PdfPage page{&figure, style};
    return pdf_export_write(path, std::span<const PdfPage>(&page, 1), SvgExportHost{});
}

bool PdfExporter::write_pdf(const std::string& path, std::span<const Figure* const> figures)
{
    std::vector<PdfPage> pages;
    pages.reserve(figures.size());
    for (const Figure* f : figures)
        if (f)
            pages.push_back({f, default_svg_style_for(*f)});
    return pdf_export_write(path, pages, SvgExportHost{});
}

std::string PdfExporter::to_string(const Figure& figure)
{
    return to_string(figure, default_svg_style_for(figure));
}

std::string PdfExporter::to_string(const Figure& figure, const SvgStyle& style)
{
    const PdfPage page{&figure, style};
    return pdf_export_to_string(std::span<const PdfPage>(&page, 1), SvgExportHost{});
}

}   // namespace spectra
