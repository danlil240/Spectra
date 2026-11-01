// svg_export.cpp — SVG serialization of the vector export walk
// (io/vector_scene.cpp).  SvgWriter turns VectorWriter calls into SVG
// elements: marker symbols instanced with <use>, clip paths and alpha
// gradients in <defs>, text pinned to the Inter advances the layout used
// (textLength), and optionally an embedded Inter subset.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include <fstream>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "io/font_subset.hpp"
#include "io/svg_export_internal.hpp"
#include "io/text_metrics.hpp"
#include "io/vector_scene.hpp"
#include "io/vector_writer.hpp"

namespace spectra
{

namespace
{

std::string num(double v)
{
    return vector_num(v);
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

// Text safe inside an XML comment ("--" is not allowed there).
std::string comment_text(std::string_view s)
{
    std::string out = xml_escape(s);
    for (size_t at = out.find("--"); at != std::string::npos; at = out.find("--", at))
        out.replace(at, 2, "-\xE2\x80\x90");   // "-" + U+2010 hyphen
    if (!out.empty() && out.back() == '-')
        out.back() = '_';
    return out;
}

const char* anchor_name(VectorAnchor a)
{
    switch (a)
    {
        case VectorAnchor::Middle:
            return "middle";
        case VectorAnchor::End:
            return "end";
        case VectorAnchor::Start:
            break;
    }
    return "start";
}

class SvgWriter final : public VectorWriter
{
   public:
    explicit SvgWriter(const SvgStyle& style) : style_(style) {}

    int define_clip_rect(double x, double y, double w, double h) override
    {
        const int id = next_id_++;
        defs_ += "    <clipPath id=\"clip" + std::to_string(id) + "\"><rect x=\"" + num(x)
                 + "\" y=\"" + num(y) + "\" width=\"" + num(w) + "\" height=\"" + num(h)
                 + "\"/></clipPath>\n";
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
        const int         id = next_id_++;
        const std::string h  = hex(color);
        defs_ += "    <linearGradient id=\"fg" + std::to_string(id)
                 + "\" gradientUnits=\"userSpaceOnUse\" x1=\"" + num(x1) + "\" y1=\"" + num(y1)
                 + "\" x2=\"" + num(x2) + "\" y2=\"" + num(y2)
                 + "\"><stop offset=\"0\" stop-color=\"" + h + "\" stop-opacity=\"" + num(a0)
                 + "\"/><stop offset=\"1\" stop-color=\"" + h + "\" stop-opacity=\"" + num(a1)
                 + "\"/></linearGradient>\n";
        return id;
    }

    void define_marker(int id, std::span<const MarkerPart> parts) override
    {
        defs_ += "    <symbol id=\"mk" + std::to_string(id) + "\" overflow=\"visible\">";
        for (const auto& p : parts)
        {
            switch (p.kind)
            {
                case MarkerPart::Kind::Circle:
                    defs_ += "<circle r=\"" + num(p.radius) + "\"";
                    break;
                case MarkerPart::Kind::Polygon:
                    defs_ += "<polygon points=\"";
                    for (size_t i = 0; i < p.points.size(); ++i)
                    {
                        if (i > 0)
                            defs_ += ' ';
                        vector_put_num(defs_, p.points[i].x);
                        defs_ += ',';
                        vector_put_num(defs_, p.points[i].y);
                    }
                    defs_ += "\"";
                    break;
                case MarkerPart::Kind::Path:
                    defs_ += "<path d=\"" + p.d + "\"";
                    break;
            }
            if (p.rim)
                defs_ += std::string(" fill=\"none\" stroke=\"")
                         + (p.rim_white ? "#ffffff" : "#000000") + "\" stroke-width=\""
                         + num(p.stroke_width) + "\" opacity=\"" + num(p.rim_opacity) + "\"/>";
            else if (p.filled)
                defs_ += " fill=\"currentColor\"/>";
            else
                defs_ += " fill=\"none\" stroke=\"currentColor\" stroke-width=\""
                         + num(p.stroke_width) + "\"/>";
        }
        defs_ += "</symbol>\n";
    }

    void begin_group(const VectorGroup& g) override
    {
        indent(g.indent);
        body_ += "<g";
        class_attr(g.style.cls);
        if (!g.data_type.empty())
            body_ += " data-type=\"" + xml_escape(g.data_type) + "\"";
        if (g.clip >= 0)
            body_ += " clip-path=\"url(#clip" + std::to_string(g.clip) + ")\"";
        style_attrs(g.style);
        body_ += ">";
        if (g.tooltip)
            body_ += "<title>" + xml_escape(*g.tooltip) + "</title>";
        body_ += "\n";
        group_indents_.push_back(g.indent);
    }

    void end_group() override
    {
        if (group_indents_.empty())
            return;
        indent(group_indents_.back());
        group_indents_.pop_back();
        body_ += "</g>\n";
    }

    void path(int ind, std::string_view d, const VectorStyle& s) override
    {
        indent(ind);
        body_ += "<path";
        class_attr(s.cls);
        body_ += " d=\"";
        body_ += d;
        body_ += "\"";
        finish_element(s);
    }

    void line(int ind, double x1, double y1, double x2, double y2, const VectorStyle& s) override
    {
        indent(ind);
        body_ += "<line";
        class_attr(s.cls);
        body_ += " x1=\"" + num(x1) + "\" y1=\"" + num(y1) + "\" x2=\"" + num(x2) + "\" y2=\""
                 + num(y2) + "\"";
        finish_element(s);
    }

    void polygon(int ind, std::span<const VectorPoint> pts, const VectorStyle& s) override
    {
        indent(ind);
        body_ += "<polygon";
        class_attr(s.cls);
        body_ += " points=\"";
        for (size_t i = 0; i < pts.size(); ++i)
        {
            if (i > 0)
                body_ += ' ';
            vector_put_num(body_, pts[i].x);
            body_ += ',';
            vector_put_num(body_, pts[i].y);
        }
        body_ += "\"";
        finish_element(s);
    }

    void circle(int ind, double cx, double cy, double r, const VectorStyle& s) override
    {
        indent(ind);
        body_ += "<circle";
        class_attr(s.cls);
        body_ += " cx=\"" + num(cx) + "\" cy=\"" + num(cy) + "\" r=\"" + num(r) + "\"";
        finish_element(s);
    }

    void rect(int ind, double x, double y, double w, double h, double rx, const VectorStyle& s)
        override
    {
        indent(ind);
        body_ += "<rect";
        class_attr(s.cls);
        body_ += " x=\"" + num(x) + "\" y=\"" + num(y) + "\" width=\"" + num(w) + "\" height=\""
                 + num(h) + "\"";
        if (rx > 0.0)
            body_ += " rx=\"" + num(rx) + "\"";
        finish_element(s);
    }

    void use_marker(int ind, int marker, double x, double y, const VectorStyle& s) override
    {
        indent(ind);
        body_ += "<use xlink:href=\"#mk" + std::to_string(marker) + "\" x=\"" + num(x) + "\" y=\""
                 + num(y) + "\"";
        finish_element(s);
    }

    void image(int                      ind,
               double                   x,
               double                   y,
               double                   w,
               double                   h,
               std::span<const uint8_t> rgba,
               uint32_t                 px_w,
               uint32_t                 px_h,
               bool                     pixelated,
               float                    opacity,
               std::string_view         cls) override
    {
        if (rgba.size() < static_cast<size_t>(px_w) * px_h * 4)
            return;
        const auto png = ImageExporter::write_png_to_memory(rgba.data(), px_w, px_h);
        if (png.empty())
            return;
        indent(ind);
        body_ += "<image";
        class_attr(cls);
        body_ += " x=\"" + num(x) + "\" y=\"" + num(y) + "\" width=\"" + num(w) + "\" height=\""
                 + num(h) + "\" preserveAspectRatio=\"none\"" + opacity_attr("opacity", opacity);
        if (pixelated)
            body_ += " style=\"image-rendering:pixelated\"";
        body_ += " xlink:href=\"data:image/png;base64," + base64_encode(png) + "\"/>\n";
    }

    void text(const VectorText& t) override
    {
        indent(t.indent);
        body_ += "<text";
        class_attr(t.cls);
        body_ +=
            " x=\"" + num(t.x) + "\" y=\"" + num(t.y) + "\" font-size=\"" + num(t.font_px) + "\"";
        // Annotation text always names its anchor, before the fill.
        if (!t.cls.empty())
            body_ += std::string(" text-anchor=\"") + anchor_name(t.anchor) + "\"";
        body_ += " fill=\"" + hex(t.color) + "\"" + opacity_attr("fill-opacity", t.color.a);
        if (t.cls.empty() && t.anchor != VectorAnchor::Start)
            body_ += std::string(" text-anchor=\"") + anchor_name(t.anchor) + "\"";
        text_length_attr(t.text, t.font_px);
        if (t.medium_weight)
            body_ += " font-weight=\"500\"";
        if (t.rotated)
            body_ += " transform=\"rotate(" + num(t.rot_deg) + " " + num(t.rot_x) + " "
                     + num(t.rot_y) + ")\"";
        body_ += ">" + xml_escape(t.text) + "</text>\n";
    }

    void note_skipped_series(std::string_view type_name,
                             std::string_view label,
                             std::string_view reason) override
    {
        body_ += "      <!-- plugin series '" + comment_text(type_name) + "'";
        if (!label.empty())
            body_ += " \"" + comment_text(label) + "\"";
        body_ += " not exported: " + comment_text(reason) + " -->\n";
    }

    [[nodiscard]] const char* format_name() const override { return "SVG"; }

    // The complete document for a w x h figure.
    [[nodiscard]] std::string document(uint32_t w, uint32_t h) const
    {
        std::string svg;
        svg.reserve(body_.size() + defs_.size() + 512);
        svg += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        svg += "<svg xmlns=\"http://www.w3.org/2000/svg\" "
               "xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"";
        svg += std::to_string(w) + "\" height=\"" + std::to_string(h) + "\" viewBox=\"0 0 "
               + std::to_string(w) + " " + std::to_string(h) + "\" font-family=\""
               + xml_escape(style_.font_family) + "\">\n";
        std::string defs = defs_;
        if (style_.embed_font && !codepoints_.empty())
        {
            // The used glyphs of Inter as "Inter": an @font-face family shadows an
            // installed font of the same name, so every viewer sets the same face.
            const std::vector<uint8_t> font = inter_font_subset(codepoints_);
            if (font.empty())
                SPECTRA_LOG_WARN("svg", "SVG export: Inter is unavailable, font not embedded");
            else
                defs =
                    "    <style>@font-face{font-family:'Inter';font-style:normal;font-weight:400;"
                    "src:url(data:font/ttf;base64,"
                    + base64_encode(font) + ") format('truetype');}</style>\n" + defs;
        }
        if (!defs.empty())
            svg += "  <defs>\n" + defs + "  </defs>\n";
        svg += "  <rect class=\"background\" width=\"100%\" height=\"100%\" fill=\""
               + hex(style_.background) + "\"/>\n";
        svg += body_;
        svg += "</svg>\n";
        return svg;
    }

   private:
    void indent(int n) { body_.append(static_cast<size_t>(std::max(n, 0)), ' '); }

    void class_attr(std::string_view cls)
    {
        if (cls.empty())
            return;
        body_ += " class=\"";
        body_ += cls;
        body_ += "\"";
    }

    static std::string paint(const VectorPaint& p)
    {
        switch (p.kind)
        {
            case PaintKind::None:
                return "none";
            case PaintKind::Rgb:
                return hex(p.color);
            case PaintKind::CurrentColor:
                return "currentColor";
            case PaintKind::Gradient:
                return "url(#fg" + std::to_string(p.gradient) + ")";
            case PaintKind::Unset:
                break;
        }
        return {};
    }

    // Presentation attributes in the exporter's fixed order.
    void style_attrs(const VectorStyle& s)
    {
        if (s.color)
            body_ += " color=\"" + hex(*s.color) + "\"";
        if (s.fill.kind != PaintKind::Unset)
            body_ += " fill=\"" + paint(s.fill) + "\"";
        body_ += opacity_attr("fill-opacity", s.fill_opacity);
        if (s.stroke.kind != PaintKind::Unset)
            body_ += " stroke=\"" + paint(s.stroke) + "\"";
        const std::string width =
            s.stroke_width >= 0.0f ? " stroke-width=\"" + num(s.stroke_width) + "\"" : "";
        if (s.opacity_before_width)
            body_ += opacity_attr("stroke-opacity", s.stroke_opacity) + width;
        else
            body_ += width + opacity_attr("stroke-opacity", s.stroke_opacity);
        if (s.join == VectorLineJoin::Round)
            body_ += " stroke-linejoin=\"round\"";
        if (s.cap == VectorLineCap::Round)
            body_ += " stroke-linecap=\"round\"";
        else if (s.cap == VectorLineCap::Butt)
            body_ += " stroke-linecap=\"butt\"";
        if (!s.dash.empty())
        {
            body_ += " stroke-dasharray=\"";
            for (size_t i = 0; i < s.dash.size(); ++i)
            {
                if (i > 0)
                    body_ += ',';
                vector_put_num(body_, s.dash[i]);
            }
            body_ += "\"";
        }
        body_ += opacity_attr("opacity", s.opacity);
        if (s.crisp_edges)
            body_ += " shape-rendering=\"crispEdges\"";
    }

    void finish_element(const VectorStyle& s)
    {
        style_attrs(s);
        body_ += "/>\n";
    }

    void note_text(std::string_view text)
    {
        for (const uint32_t cp : utf8_codepoints(text))
        {
            if (cp < 128)
            {
                if (ascii_seen_[cp])
                    continue;
                ascii_seen_[cp] = true;
            }
            codepoints_.push_back(cp);
        }
    }

    // ` textLength=".." lengthAdjust="spacingAndGlyphs"` pinning `text` to the Inter
    // advance the layout used, so fallback fonts cannot overflow fitted space.
    // Nothing for text Inter does not fully cover: its width is only an estimate.
    void text_length_attr(std::string_view text, float font_px)
    {
        note_text(text);
        if (!style_.fit_text_length || !inter_covers(text))
            return;
        const float w = inter_text_width(text, font_px);
        if (w > 0.0f)
            body_ += " textLength=\"" + num(w) + "\" lengthAdjust=\"spacingAndGlyphs\"";
    }

    const SvgStyle&       style_;
    std::string           defs_;
    std::string           body_;
    int                   next_id_ = 0;
    std::vector<int>      group_indents_;
    std::vector<uint32_t> codepoints_;   // every codepoint of emitted <text> content
    std::array<bool, 128> ascii_seen_{};
};

}   // anonymous namespace

// ─── SvgExporter ────────────────────────────────────────────────────────────

std::string SvgExporter::to_string(const Figure& figure)
{
    return to_string(figure, default_svg_style_for(figure));
}

std::string SvgExporter::to_string(const Figure& figure, const SvgStyle& style)
{
    return svg_export_to_string(figure, style, SvgExportHost{});
}

std::string svg_export_to_string(const Figure&        figure,
                                 const SvgStyle&      style,
                                 const SvgExportHost& host)
{
    SvgWriter writer(style);
    draw_vector_scene(figure, style, host, writer);
    return writer.document(figure.width(), figure.height());
}

bool SvgExporter::write_svg(const std::string& path, const Figure& figure)
{
    return write_svg(path, figure, default_svg_style_for(figure));
}

bool SvgExporter::write_svg(const std::string& path, const Figure& figure, const SvgStyle& style)
{
    return svg_export_write(path, figure, style, SvgExportHost{});
}

bool svg_export_write(const std::string&   path,
                      const Figure&        figure,
                      const SvgStyle&      style,
                      const SvgExportHost& host)
{
    const std::string content = svg_export_to_string(figure, style, host);

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open())
        return false;

    file << content;
    return file.good();
}

}   // namespace spectra
