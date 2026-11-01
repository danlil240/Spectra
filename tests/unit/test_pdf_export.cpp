// Vector PDF export (io/pdf_export.cpp): document structure, embedded font and
// selectable text, transparency, markers, multi-page output, and — when poppler's
// pdftoppm is installed — a rasterized page compared with the expected colours.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <map>
#include <optional>
#include <spectra/axes3d.hpp>
#include <spectra/custom_series.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>
#include <spectra/series_image.hpp>
#include <spectra/series_shapes.hpp>
#include <spectra/series_stats.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "io/pdf_export_internal.hpp"
#include "io/text_metrics.hpp"

// Declarations only: the implementation is compiled into the library (io/stb_impl.cpp).
#include <stb_image.h>

namespace spectra
{
namespace
{

namespace fs = std::filesystem;

// ─── A tiny PDF reader (enough for what pdf_export writes) ─────────────────

struct PdfObject
{
    std::string dict;   // everything before "stream" (or the whole body)
    std::string data;   // decoded stream data (empty when no stream)
    bool        stream = false;
};

class MiniPdf
{
   public:
    explicit MiniPdf(std::string bytes) : bytes_(std::move(bytes)) { parse(); }

    [[nodiscard]] bool               ok() const { return error_.empty(); }
    [[nodiscard]] const std::string& error() const { return error_; }
    [[nodiscard]] size_t             object_count() const { return objects_.size(); }
    [[nodiscard]] const std::string& trailer() const { return trailer_; }

    [[nodiscard]] const PdfObject* object(int n) const
    {
        auto it = objects_.find(n);
        return it == objects_.end() ? nullptr : &it->second;
    }

    // Objects whose dictionary contains `needle`.
    [[nodiscard]] std::vector<const PdfObject*> find(std::string_view needle) const
    {
        std::vector<const PdfObject*> out;
        for (const auto& [n, o] : objects_)
            if (o.dict.find(needle) != std::string::npos)
                out.push_back(&o);
        return out;
    }

    // Object number referenced as `/key N 0 R` in `dict`, or -1.
    static int ref_of(const std::string& dict, std::string_view key)
    {
        const size_t at = dict.find(std::string(key) + " ");
        if (at == std::string::npos)
            return -1;
        return std::atoi(dict.c_str() + at + key.size() + 1);
    }

   private:
    void fail(std::string why)
    {
        if (error_.empty())
            error_ = std::move(why);
    }

    void parse()
    {
        if (bytes_.rfind("%PDF-1.4\n", 0) != 0)
            return fail("missing %PDF-1.4 header");
        const size_t sx = bytes_.rfind("startxref\n");
        if (sx == std::string::npos || bytes_.find("%%EOF", sx) == std::string::npos)
            return fail("missing startxref / %%EOF");
        const size_t xref = std::strtoull(bytes_.c_str() + sx + 10, nullptr, 10);
        if (bytes_.compare(xref, 5, "xref\n") != 0)
            return fail("startxref does not point at the xref table");
        size_t pos   = xref + 5;
        int    first = 0, count = 0;
        if (std::sscanf(bytes_.c_str() + pos, "%d %d", &first, &count) != 2 || first != 0)
            return fail("bad xref subsection header");
        pos = bytes_.find('\n', pos) + 1;
        for (int i = 0; i < count; ++i, pos += 20)
        {
            const std::string entry = bytes_.substr(pos, 20);
            if (entry.size() != 20 || (entry[18] != ' ' && entry[18] != '\r'))
                return fail("xref entry is not 20 bytes");
            if (i == 0)
            {
                if (entry.compare(0, 18, "0000000000 65535 f") != 0)
                    return fail("bad free entry 0");
                continue;
            }
            if (entry[17] != 'n')
                return fail("unexpected free entry");
            const size_t      off  = std::strtoull(entry.c_str(), nullptr, 10);
            const std::string head = std::to_string(i) + " 0 obj\n";
            if (bytes_.compare(off, head.size(), head) != 0)
                return fail("xref offset of object " + std::to_string(i) + " is wrong");
            const size_t body = off + head.size();
            const size_t end  = bytes_.find("\nendobj\n", body);
            if (end == std::string::npos)
                return fail("unterminated object");
            objects_[i] = read_object(bytes_.substr(body, end - body));
        }
        const size_t t = bytes_.find("trailer\n", pos);
        if (t != pos)
            return fail("trailer does not follow the xref table");
        trailer_ = bytes_.substr(t, sx - t);
        if (trailer_.find("/Size " + std::to_string(count)) == std::string::npos)
            return fail("trailer /Size does not match the xref table");
    }

    PdfObject read_object(const std::string& body)
    {
        PdfObject    o;
        const size_t s = body.find(">>\nstream\n");
        if (s == std::string::npos)
        {
            o.dict = body;
            return o;
        }
        o.stream           = true;
        o.dict             = body.substr(0, s + 2);
        const size_t begin = s + 10;
        const size_t length =
            static_cast<size_t>(std::atoll(o.dict.c_str() + o.dict.find("/Length ") + 8));
        if (begin + length + 10 > body.size()
            || body.compare(begin + length, 10, "\nendstream") != 0)
        {
            fail("stream /Length does not match its data");
            return o;
        }
        std::string raw = body.substr(begin, length);
        if (o.dict.find("/FlateDecode") != std::string::npos)
        {
            int   out_len = 0;
            char* out = stbi_zlib_decode_malloc(raw.data(), static_cast<int>(raw.size()), &out_len);
            if (!out)
            {
                fail("stream does not inflate");
                return o;
            }
            o.data.assign(out, static_cast<size_t>(out_len));
            std::free(out);
        }
        else
            o.data = std::move(raw);
        return o;
    }

    std::string              bytes_;
    std::string              error_;
    std::string              trailer_;
    std::map<int, PdfObject> objects_;
};

// glyph id -> UTF-8 text, from a ToUnicode CMap's bfchar entries.
std::map<uint32_t, std::string> parse_to_unicode(const std::string& cmap)
{
    std::map<uint32_t, std::string> out;
    bool                            in_bfchar = false;
    size_t                          pos       = 0;
    while (pos < cmap.size())
    {
        size_t eol = cmap.find('\n', pos);
        if (eol == std::string::npos)
            eol = cmap.size();
        const std::string line = cmap.substr(pos, eol - pos);
        pos                    = eol + 1;
        if (line.find("beginbfchar") != std::string::npos)
        {
            in_bfchar = true;
            continue;
        }
        if (line == "endbfchar")
            in_bfchar = false;
        // "<gggg> <uuuu[uuuu]>"
        if (!in_bfchar || line.size() < 13 || line[0] != '<' || line[5] != '>' || line[7] != '<'
            || line.back() != '>')
            continue;
        const std::string a = line.substr(1, 4);
        const std::string b = line.substr(8, line.size() - 9);
        if (b.empty() || b.size() % 4 != 0)
            continue;
        std::vector<uint32_t> units;
        for (size_t i = 0; i < b.size(); i += 4)
            units.push_back(static_cast<uint32_t>(std::stoul(b.substr(i, 4), nullptr, 16)));
        uint32_t cp = units[0];
        if (units.size() == 2)
            cp = 0x10000 + ((units[0] - 0xD800) << 10) + (units[1] - 0xDC00);
        std::string utf8;
        if (cp < 0x80)
            utf8 += static_cast<char>(cp);
        else if (cp < 0x800)
        {
            utf8 += static_cast<char>(0xC0 | (cp >> 6));
            utf8 += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            utf8 += static_cast<char>(0xE0 | (cp >> 12));
            utf8 += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            utf8 += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            utf8 += static_cast<char>(0xF0 | (cp >> 18));
            utf8 += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            utf8 += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            utf8 += static_cast<char>(0x80 | (cp & 0x3F));
        }
        out[static_cast<uint32_t>(std::stoul(a, nullptr, 16))] = utf8;
    }
    return out;
}

// Every `<hex> Tj` string of the content streams, decoded through the ToUnicode map.
std::vector<std::string> extract_text(const MiniPdf& pdf)
{
    const auto fonts = pdf.find("/ToUnicode");
    if (fonts.empty())
        return {};
    const PdfObject* cmap = pdf.object(MiniPdf::ref_of(fonts[0]->dict, "/ToUnicode"));
    if (!cmap)
        return {};
    const auto               map = parse_to_unicode(cmap->data);
    std::vector<std::string> out;
    for (const PdfObject* o : pdf.find("<< /Length"))
    {
        const std::string& d   = o->data;
        size_t             pos = 0;
        while ((pos = d.find("> Tj", pos)) != std::string::npos)
        {
            const size_t open = d.rfind('<', pos);
            std::string  text;
            for (size_t i = open + 1; i + 4 <= pos; i += 4)
            {
                auto it = map.find(static_cast<uint32_t>(std::stoul(d.substr(i, 4), nullptr, 16)));
                text += it == map.end() ? "?" : it->second;
            }
            out.push_back(text);
            pos += 4;
        }
    }
    return out;
}

// With SPECTRA_TEST_EVIDENCE_DIR set, keeps the exported document for inspection.
const std::string& keep_evidence(const std::string& name, const std::string& pdf)
{
    if (const char* dir = std::getenv("SPECTRA_TEST_EVIDENCE_DIR"))
    {
        std::ofstream out(fs::path(dir) / name, std::ios::binary);
        out << pdf;
    }
    return pdf;
}

bool contains(const std::vector<std::string>& v, const std::string& s)
{
    return std::find(v.begin(), v.end(), s) != v.end();
}

int count_of(const std::string& haystack, std::string_view needle)
{
    int    n   = 0;
    size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos)
    {
        ++n;
        pos += needle.size();
    }
    return n;
}

std::string all_content(const MiniPdf& pdf)
{
    std::string out;
    for (const PdfObject* page : pdf.find("/Type /Page "))
        if (const PdfObject* c = pdf.object(MiniPdf::ref_of(page->dict, "/Contents")))
            out += c->data;
    return out;
}

Figure make_line_figure()
{
    Figure             fig({.width = 800, .height = 600});
    auto&              ax = fig.subplot(1, 1, 1);
    std::vector<float> x  = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f};
    std::vector<float> y  = {0.0f, 1.0f, 0.5f, 1.5f, 1.0f};
    ax.line(x, y).label("test-line").color(rgb(1.0f, 0.0f, 0.0f)).width(4.0f);
    ax.title("Test Title");
    ax.xlabel("X Label");
    ax.ylabel("Y Label");
    fig.compute_layout();
    return fig;
}

// ─── Structure ─────────────────────────────────────────────────────────────

TEST(PdfExport, DocumentStructureIsValid)
{
    auto          fig = make_line_figure();
    const MiniPdf pdf(keep_evidence("pdf_line.pdf", PdfExporter::to_string(fig)));
    ASSERT_TRUE(pdf.ok()) << pdf.error();

    const PdfObject* catalog = pdf.object(MiniPdf::ref_of(pdf.trailer(), "/Root"));
    ASSERT_NE(catalog, nullptr);
    EXPECT_NE(catalog->dict.find("/Type /Catalog"), std::string::npos);
    const PdfObject* pages = pdf.object(MiniPdf::ref_of(catalog->dict, "/Pages"));
    ASSERT_NE(pages, nullptr);
    EXPECT_NE(pages->dict.find("/Count 1"), std::string::npos);

    const auto page = pdf.find("/Type /Page ");
    ASSERT_EQ(page.size(), 1u);
    // 800 x 600 px at 96 dpi.
    EXPECT_NE(page[0]->dict.find("/MediaBox [0 0 600 450]"), std::string::npos);
    const PdfObject* content = pdf.object(MiniPdf::ref_of(page[0]->dict, "/Contents"));
    ASSERT_NE(content, nullptr);
    EXPECT_NE(content->dict.find("/FlateDecode"), std::string::npos);
    // Pixel space flipped into points, then the line series in red.
    EXPECT_EQ(content->data.rfind("0.75 0 0 -0.75 0 450 cm\n", 0), 0u);
    EXPECT_NE(content->data.find("1 0 0 RG"), std::string::npos);
    EXPECT_NE(content->data.find("5 w"), std::string::npos);   // width 4 + 1 px AA
    EXPECT_NE(content->data.find("W n"), std::string::npos);   // plot-area clip
}

TEST(PdfExport, OutputIsDeterministic)
{
    auto fig = make_line_figure();
    EXPECT_EQ(PdfExporter::to_string(fig), PdfExporter::to_string(fig));
}

TEST(PdfExport, EmptyFigureIsAValidPage)
{
    Figure fig({.width = 320, .height = 200});
    fig.compute_layout();
    const MiniPdf pdf(PdfExporter::to_string(fig));
    ASSERT_TRUE(pdf.ok()) << pdf.error();
    EXPECT_EQ(pdf.find("/Type /Page ").size(), 1u);
    EXPECT_TRUE(pdf.find("/FontFile2").empty());   // no text, no font
}

TEST(PdfExport, ImageSeriesBecomesImageXObject)
{
    Figure             fig({.width = 400, .height = 300});
    auto&              ax = fig.subplot(1, 1, 1);
    std::vector<float> v  = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, std::nanf("")};   // 3 x 2
    auto&              im = ax.imshow(v, 3, 2);
    fig.compute_layout();
    const MiniPdf pdf(keep_evidence("pdf_image.pdf", PdfExporter::to_string(fig)));
    ASSERT_TRUE(pdf.ok()) << pdf.error();

    // One pixel per cell, unsmoothed; the NaN cell is transparent, so a soft mask.
    const PdfObject* rgb = nullptr;
    for (const PdfObject* o : pdf.find("/ColorSpace /DeviceRGB"))
        if (o->dict.find("/Subtype /Image") != std::string::npos)
            rgb = o;
    ASSERT_NE(rgb, nullptr);
    EXPECT_NE(rgb->dict.find("/Width 3 /Height 2"), std::string::npos);
    EXPECT_NE(rgb->dict.find("/Interpolate false"), std::string::npos);
    ASSERT_EQ(rgb->data.size(), 3u * 2u * 3u);
    const PdfObject* mask = pdf.object(MiniPdf::ref_of(rgb->dict, "/SMask"));
    ASSERT_NE(mask, nullptr);
    ASSERT_EQ(mask->data.size(), 6u);
    EXPECT_EQ(static_cast<uint8_t>(mask->data[0]), 255);
    EXPECT_EQ(static_cast<uint8_t>(mask->data[5]), 0);   // NaN

    // Origin Upper: image row 0 is grid row 0.
    const Color want = im.color_for_value(v[1]);
    EXPECT_NEAR(static_cast<uint8_t>(rgb->data[3]) / 255.0f, want.r, 1.0f / 255.0f);
    EXPECT_NEAR(static_cast<uint8_t>(rgb->data[4]) / 255.0f, want.g, 1.0f / 255.0f);
    EXPECT_NEAR(static_cast<uint8_t>(rgb->data[5]) / 255.0f, want.b, 1.0f / 255.0f);

    // Drawn through the page resources.
    const auto page = pdf.find("/Type /Page ");
    ASSERT_EQ(page.size(), 1u);
    const PdfObject* content = pdf.object(MiniPdf::ref_of(page[0]->dict, "/Contents"));
    ASSERT_NE(content, nullptr);
    EXPECT_NE(content->data.find(" cm /Im"), std::string::npos);
}

TEST(PdfExport, WriteToFile)
{
    auto           fig  = make_line_figure();
    const fs::path path = fs::temp_directory_path() / "spectra_test_pdf_export.pdf";
    ASSERT_TRUE(PdfExporter::write_pdf(path.string(), fig));
    std::string bytes;
    {
        // Closed before remove(): Windows cannot delete a file that is still open.
        std::ifstream in(path, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    EXPECT_EQ(bytes, PdfExporter::to_string(fig));
    fs::remove(path);
    EXPECT_FALSE(PdfExporter::write_pdf("/nonexistent-dir/x.pdf", fig));
}

// ─── Text ──────────────────────────────────────────────────────────────────

TEST(PdfExport, FontIsAnEmbeddedSubsetWithToUnicode)
{
    if (!inter_metrics_available())
        GTEST_SKIP() << "build has no embedded Inter font";
    auto          fig = make_line_figure();
    const MiniPdf pdf(PdfExporter::to_string(fig));
    ASSERT_TRUE(pdf.ok()) << pdf.error();

    const auto type0 = pdf.find("/Subtype /Type0");
    ASSERT_EQ(type0.size(), 1u);
    EXPECT_NE(type0[0]->dict.find("/Encoding /Identity-H"), std::string::npos);
    EXPECT_NE(type0[0]->dict.find("+Inter"), std::string::npos);
    ASSERT_GE(MiniPdf::ref_of(type0[0]->dict, "/ToUnicode"), 1);

    const auto cid = pdf.find("/Subtype /CIDFontType2");
    ASSERT_EQ(cid.size(), 1u);
    EXPECT_NE(cid[0]->dict.find("/CIDToGIDMap /Identity"), std::string::npos);
    EXPECT_NE(cid[0]->dict.find("/W ["), std::string::npos);

    const auto descriptor = pdf.find("/FontFile2");
    ASSERT_EQ(descriptor.size(), 1u);
    const PdfObject* file = pdf.object(MiniPdf::ref_of(descriptor[0]->dict, "/FontFile2"));
    ASSERT_NE(file, nullptr);
    ASSERT_TRUE(file->stream);
    // A real TrueType file, smaller than the full face.
    ASSERT_GE(file->data.size(), 12u);
    EXPECT_EQ(file->data.compare(0, 4, std::string("\0\1\0\0", 4)), 0);
    EXPECT_LT(file->data.size(), embedded_inter_font().size());
    EXPECT_NE(file->dict.find("/Length1 " + std::to_string(file->data.size())), std::string::npos);
}

TEST(PdfExport, TextIsSelectableThroughToUnicode)
{
    if (!inter_metrics_available())
        GTEST_SKIP() << "build has no embedded Inter font";
    auto fig = make_line_figure();
    fig.axes()[0]->title("Spannung \xC3\xBC\xC2\xB0"
                         "C \xE2\x80\x94 \xF0\x9F\x98\x80");
    fig.compute_layout();
    const MiniPdf pdf(keep_evidence("pdf_text.pdf", PdfExporter::to_string(fig)));
    ASSERT_TRUE(pdf.ok()) << pdf.error();
    const auto text = extract_text(pdf);
    EXPECT_TRUE(contains(text, "X Label"));
    EXPECT_TRUE(contains(text, "Y Label"));
    EXPECT_TRUE(contains(text, "test-line"));
    EXPECT_TRUE(contains(text, "0.5")) << "tick label";
    // Non-ASCII survives; a codepoint Inter lacks maps to U+FFFD (it is set as .notdef).
    EXPECT_TRUE(contains(text,
                         "Spannung \xC3\xBC\xC2\xB0"
                         "C \xE2\x80\x94 \xEF\xBF\xBD"));
}

TEST(PdfExport, RotatedYLabelAndMediumTitle)
{
    auto          fig = make_line_figure();
    const MiniPdf pdf(PdfExporter::to_string(fig));
    ASSERT_TRUE(pdf.ok()) << pdf.error();
    const std::string c = all_content(pdf);
    // The Y label runs bottom-to-top: text matrix [0 -1 -1 0 x y].
    EXPECT_NE(c.find("Tf\n0 -1 -1 0 "), std::string::npos);
    // The title is thickened with fill+stroke rendering.
    EXPECT_NE(c.find("2 Tr"), std::string::npos);
}

// ─── Transparency, markers, fills ──────────────────────────────────────────

TEST(PdfExport, TransparencyUsesExtGState)
{
    Figure             fig({.width = 640, .height = 480});
    std::vector<float> x = {0.0f, 1.0f, 2.0f}, y = {1.0f, 3.0f, 2.0f};
    fig.subplot(1, 1, 1).line(x, y).color(rgb(0.0f, 0.0f, 1.0f)).opacity(0.5f);
    fig.compute_layout();
    const MiniPdf pdf(PdfExporter::to_string(fig));
    ASSERT_TRUE(pdf.ok()) << pdf.error();
    EXPECT_FALSE(pdf.find("/Type /ExtGState /CA 0.5 ").empty());
    EXPECT_NE(all_content(pdf).find(" gs\n"), std::string::npos);
}

TEST(PdfExport, MarkersAreInstancedFormXObjects)
{
    Figure             fig({.width = 640, .height = 480});
    std::vector<float> x = {1.0f, 2.0f, 3.0f, 4.0f}, y = {1.0f, 4.0f, 2.0f, 3.0f};
    fig.subplot(1, 1, 1).scatter(x, y).color(rgb(0.0f, 0.5f, 0.0f));
    fig.compute_layout();
    const MiniPdf pdf(keep_evidence("pdf_markers.pdf", PdfExporter::to_string(fig)));
    ASSERT_TRUE(pdf.ok()) << pdf.error();
    const auto forms = pdf.find("/Subtype /Form /BBox");
    ASSERT_EQ(forms.size(), 1u);
    EXPECT_NE(forms[0]->data.find(" c\n"), std::string::npos);   // a Bézier circle
    EXPECT_EQ(count_of(all_content(pdf), " Do Q"), 4);
}

TEST(PdfExport, BarAlphaGradientBecomesASoftMask)
{
    Figure             fig({.width = 640, .height = 480});
    std::vector<float> x = {1.0f, 2.0f, 3.0f}, h = {3.0f, 5.0f, 2.0f};
    fig.subplot(1, 1, 1).bar(x, h);
    fig.compute_layout();
    const std::string svg = SvgExporter::to_string(fig);
    const MiniPdf     pdf(keep_evidence("pdf_bars.pdf", PdfExporter::to_string(fig)));
    ASSERT_TRUE(pdf.ok()) << pdf.error();
    // One soft mask per SVG gradient, each painting an axial gray shading.
    const int gradients = count_of(svg, "<linearGradient");
    EXPECT_EQ(static_cast<int>(pdf.find("/SMask << /Type /Mask /S /Luminosity").size()), gradients);
    EXPECT_EQ(static_cast<int>(pdf.find("/ShadingType 2 /ColorSpace /DeviceGray").size()),
              gradients);
}

TEST(PdfExport, StatisticalAnd3DSeriesAreDrawn)
{
    Figure             fig({.width = 1200, .height = 800});
    std::vector<float> vals(200);
    for (size_t i = 0; i < vals.size(); ++i)
        vals[i] = std::sin(static_cast<float>(i) * 0.37f) * 10.0f + 50.0f;
    fig.subplot(2, 2, 1).histogram(vals, 20);
    fig.subplot(2, 2, 2).box_plot().add_box(1.0f, vals);
    fig.subplot(2, 2, 3).violin().add_violin(1.0f, vals);
    auto&              ax3 = fig.subplot3d(2, 2, 4);
    std::vector<float> g   = {-1.0f, 0.0f, 1.0f};
    std::vector<float> zz  = {0.0f, 0.2f, 0.0f, 0.2f, 1.0f, 0.2f, 0.0f, 0.2f, 0.0f};
    ax3.surface(g, g, zz).colormap(ColormapType::Viridis);
    fig.compute_layout();
    const MiniPdf pdf(keep_evidence("pdf_stats_3d.pdf", PdfExporter::to_string(fig)));
    ASSERT_TRUE(pdf.ok()) << pdf.error();
    const std::string c = all_content(pdf);
    // Filled stat shapes plus the 8 opaque surface triangles (fill + seam stroke).
    EXPECT_GE(count_of(c, "\nf\n") + count_of(c, "\nB\n"), 4 + 8);
    EXPECT_GE(count_of(c, "\nB\n"), 8);
}

// ─── Plugin series, multi-page, API ────────────────────────────────────────

TEST(PdfExport, PluginSeriesIsDrawnThroughTheHostHook)
{
    Figure      fig({.width = 640, .height = 480});
    auto&       ax   = fig.subplot(1, 1, 1);
    auto        cs   = std::make_unique<CustomSeries>("grid--cells");
    const float blob = 0.0f;
    cs->set_data(&blob, sizeof(blob), 1);
    ax.series_mut().push_back(std::move(cs));
    ax.xlim(0.0, 4.0);
    ax.ylim(0.0, 2.0);
    fig.compute_layout();

    SvgExportHost host;
    host.custom_series = [](const CustomSeries&, const float*, float, CustomVectorSink& sink)
    {
        const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
        sink.fill_rect(0.0f, 0.0f, 2.0f, 1.0f, red);
        return static_cast<const char*>(nullptr);
    };
    const PdfPage page{&fig, SvgStyle{}};
    const MiniPdf pdf(pdf_export_to_string(std::span<const PdfPage>(&page, 1), host));
    ASSERT_TRUE(pdf.ok()) << pdf.error();
    const std::string c = all_content(pdf);
    EXPECT_NE(c.find("1 0 0 rg"), std::string::npos);
    EXPECT_NE(c.find(" re\nf\n"), std::string::npos);
}

TEST(PdfExport, OnePagePerFigure)
{
    Figure             a({.width = 800, .height = 600});
    Figure             b({.width = 400, .height = 400});
    Figure             c({.width = 1280, .height = 720});
    std::vector<float> x = {0.0f, 1.0f}, y = {0.0f, 1.0f};
    a.subplot(1, 1, 1).line(x, y);
    b.subplot(1, 1, 1).scatter(x, y);
    c.subplot(1, 1, 1).title("third");
    for (Figure* f : {&a, &b, &c})
        f->compute_layout();
    const Figure*  figs[] = {&a, &b, &c};
    const fs::path path   = fs::temp_directory_path() / "spectra_test_pdf_pages.pdf";
    ASSERT_TRUE(PdfExporter::write_pdf(path.string(), figs));
    std::string bytes;
    {
        std::ifstream in(path, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    fs::remove(path);
    const MiniPdf pdf(std::move(bytes));
    ASSERT_TRUE(pdf.ok()) << pdf.error();
    EXPECT_EQ(pdf.find("/Type /Page ").size(), 3u);
    EXPECT_EQ(pdf.find("/Type /Pages /Kids").size(), 1u);
    EXPECT_FALSE(pdf.find("/Count 3").empty());
    EXPECT_EQ(pdf.find("/MediaBox [0 0 300 300]").size(), 1u);
    EXPECT_EQ(pdf.find("/MediaBox [0 0 960 540]").size(), 1u);
    // One font shared by every page.
    EXPECT_LE(pdf.find("/Subtype /Type0").size(), 1u);
}

TEST(PdfExport, SavePdfQueuesARequest)
{
    Figure         fig({.width = 320, .height = 240});
    const fs::path path = fs::temp_directory_path() / "spectra_test_save.pdf";
    ASSERT_TRUE(fig.save_pdf(path.string(), {.theme = "light"}));
    EXPECT_EQ(fig.export_req_.pdf_path, path.string());
    EXPECT_EQ(fig.export_req_.pdf_theme, "light");
    EXPECT_FALSE(fig.save_pdf("/nonexistent-dir/x.pdf"));
}

// ─── Rasterized check (poppler) ────────────────────────────────────────────

std::optional<std::string> find_pdftoppm()
{
    for (const char* p :
         {"/usr/bin/pdftoppm", "/usr/local/bin/pdftoppm", "/opt/homebrew/bin/pdftoppm"})
        if (fs::exists(p))
            return std::string(p);
    return std::nullopt;
}

TEST(PdfExport, RasterizedPageShowsBackgroundAndSeries)
{
    const auto pdftoppm = find_pdftoppm();
    if (!pdftoppm)
        GTEST_SKIP() << "pdftoppm (poppler-utils) not installed";
    auto           fig = make_line_figure();
    const fs::path dir = fs::temp_directory_path() / "spectra_test_pdf_raster";
    fs::create_directories(dir);
    const fs::path pdf = dir / "line.pdf";
    ASSERT_TRUE(PdfExporter::write_pdf(pdf.string(), fig));
    // 96 dpi: one PDF pixel per figure pixel.
    const std::string cmd = *pdftoppm + " -r 96 -png -singlefile \"" + pdf.string() + "\" \""
                            + (dir / "line").string() + "\"";
    ASSERT_EQ(std::system(cmd.c_str()), 0) << cmd;
    int      w = 0, h = 0, n = 0;
    uint8_t* px = stbi_load((dir / "line.png").string().c_str(), &w, &h, &n, 3);
    ASSERT_NE(px, nullptr);
    EXPECT_EQ(w, 800);
    EXPECT_EQ(h, 600);
    // Corner = light theme background (#f7faff); some strongly red pixels = the series.
    const SvgStyle st;
    EXPECT_NEAR(px[0], std::lround(st.background.r * 255.0f), 2);
    EXPECT_NEAR(px[1], std::lround(st.background.g * 255.0f), 2);
    EXPECT_NEAR(px[2], std::lround(st.background.b * 255.0f), 2);
    int red = 0, dark = 0;
    for (int i = 0; i < w * h; ++i)
    {
        const uint8_t* p = px + 3 * i;
        if (p[0] > 200 && p[1] < 60 && p[2] < 60)
            ++red;
        if (p[0] < 90 && p[1] < 90 && p[2] < 110)
            ++dark;   // text and axis ink
    }
    stbi_image_free(px);
    fs::remove_all(dir);
    EXPECT_GT(red, 1000);
    EXPECT_GT(dark, 200);
}

}   // namespace
}   // namespace spectra
