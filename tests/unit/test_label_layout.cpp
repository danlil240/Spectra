// Tick offset / multiplier annotations ("+1.72e9", "×10⁻⁷") share their rows with the
// X axis label (bottom) and the axes title (top).  These tests check that the centred
// label is laid out beside the annotation, never over it, in the shared layout helper
// (used by the GPU text path) and in the SVG exporter.

#include <algorithm>
#include <cstdint>
#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "core/label_layout.hpp"
#include "render/text_fit.hpp"
#include "render/text_renderer.hpp"

using namespace spectra;

namespace
{

// ─── Helper-level tests (GPU path) ──────────────────────────────────────────

// Fake metrics: every codepoint has a fixed width depending on the font size.
float fake_measure(const std::string& s, FontSize fs)
{
    const float glyph = fs == FontSize::Title ? 10.0f : fs == FontSize::Label ? 8.0f : 6.0f;
    size_t      n     = 0;
    for (size_t i = 0; i < s.size();)
    {
        uint32_t cp = 0;
        i += TextRenderer::decode_utf8(s, i, cp);
        ++n;
    }
    return static_cast<float>(n) * glyph;
}

struct Interval
{
    float lo;
    float hi;
};

bool disjoint(Interval a, Interval b, float gap)
{
    return a.hi + gap <= b.lo + 1e-3f || b.hi + gap <= a.lo + 1e-3f;
}

// Real offset labels produced by the tick generator for a range of axis limits.
std::vector<std::string> real_offset_labels()
{
    struct Range
    {
        double lo, hi;
    };
    const Range ranges[] = {
        {1720000000.0, 1720000001.2},     // positive epoch offset
        {-1720000001.2, -1720000000.0},   // negative offset
        {0.0, 3.0e-7},                    // scientific multiplier ×10⁻⁷
        {0.0, 4.0e9},                     // scientific multiplier ×10⁹
        {1.0e7 - 0.02, 1.0e7 + 0.02},     // offset with a small baseline variation
    };
    std::vector<std::string> out;
    Figure                   fig({.width = 640, .height = 480});
    auto&                    ax = fig.subplot(1, 1, 1);
    for (const auto& r : ranges)
    {
        ax.xlim(r.lo, r.hi);
        out.push_back(ax.compute_x_ticks().offset_label);
    }
    return out;
}

TEST(LabelLayout, NoReservationKeepsFullWidthAndCentre)
{
    const CenteredLabelSlot slot = centered_label_slot(100.0f, 400.0f, 0.0f, 0.0f);
    EXPECT_FLOAT_EQ(slot.left, 100.0f);
    EXPECT_FLOAT_EQ(slot.right, 500.0f);
    EXPECT_FLOAT_EQ(slot.max_width, 400.0f);
    EXPECT_FLOAT_EQ(centered_label_x(100.0f, 400.0f, slot, 120.0f), 300.0f);
    EXPECT_FLOAT_EQ(offset_annotation_reserve(0.0f), 0.0f);
}

TEST(LabelLayout, ShortLabelStaysCentredNextToOffset)
{
    const float             reserve = offset_annotation_reserve(60.0f);
    const CenteredLabelSlot slot    = centered_label_slot(0.0f, 400.0f, 0.0f, reserve);
    EXPECT_FLOAT_EQ(slot.max_width, 400.0f - 60.0f - OFFSET_LABEL_GAP);
    EXPECT_FLOAT_EQ(centered_label_x(0.0f, 400.0f, slot, 100.0f), 200.0f);
}

TEST(LabelLayout, WideLabelShiftsAwayFromOffset)
{
    const float             reserve = offset_annotation_reserve(60.0f);
    const CenteredLabelSlot slot    = centered_label_slot(0.0f, 400.0f, 0.0f, reserve);
    const float             cx      = centered_label_x(0.0f, 400.0f, slot, 300.0f);
    EXPECT_LT(cx, 200.0f);
    EXPECT_LE(cx + 150.0f, 400.0f - reserve + 1e-3f);
    EXPECT_GE(cx - 150.0f, 0.0f - 1e-3f);

    // Same on the left (Y offset vs title).
    const CenteredLabelSlot left_slot = centered_label_slot(0.0f, 400.0f, reserve, 0.0f);
    const float             tx        = centered_label_x(0.0f, 400.0f, left_slot, 300.0f);
    EXPECT_GT(tx, 200.0f);
    EXPECT_GE(tx - 150.0f, reserve - 1e-3f);
}

TEST(LabelLayout, ReservationWiderThanPanelYieldsEmptySlot)
{
    const CenteredLabelSlot slot = centered_label_slot(10.0f, 50.0f, 0.0f, 80.0f);
    EXPECT_FLOAT_EQ(slot.max_width, 0.0f);
    const FittedText fit = fit_text_to_width("Acquisition timestamp",
                                             slot.max_width,
                                             FontSize::Label,
                                             FontSize::Label,
                                             fake_measure);
    EXPECT_TRUE(fit.text.empty());
}

// The X label (right offset) and the title (left Y offset) never intersect their row's
// offset annotation, for real offset strings, long and short labels and narrow panels.
TEST(LabelLayout, LabelAndOffsetBoundsAreDisjoint)
{
    const std::vector<std::string> offsets = real_offset_labels();
    for (const auto& o : offsets)
        ASSERT_FALSE(o.empty());

    const std::string labels[] = {
        "t (s)",
        "Acquisition timestamp measured in seconds since the Unix epoch",
        "Δt — μs since trigger, calibrated against the reference clock",
    };
    const float widths[] = {60.0f, 120.0f, 220.0f, 480.0f, 1200.0f};

    for (const auto& offset : offsets)
        for (const auto& label : labels)
            for (float w : widths)
                for (bool title : {false, true})
                {
                    SCOPED_TRACE(offset + " | " + label + " | w=" + std::to_string(w)
                                 + (title ? " | title" : " | xlabel"));
                    const float      x0    = 37.0f;
                    const float      off_w = fake_measure(offset, FontSize::Tick);
                    const float      res   = offset_annotation_reserve(off_w);
                    const auto       slot  = title ? centered_label_slot(x0, w, res, 0.0f)
                                                   : centered_label_slot(x0, w, 0.0f, res);
                    const FontSize   fs    = title ? FontSize::Title : FontSize::Label;
                    const FittedText fit =
                        fit_text_to_width(label, slot.max_width, fs, FontSize::Label, fake_measure);
                    if (fit.text.empty())
                        continue;
                    const float    lw = fake_measure(fit.text, fit.size);
                    const float    cx = centered_label_x(x0, w, slot, lw);
                    const Interval lbl{cx - lw * 0.5f, cx + lw * 0.5f};
                    const Interval off =
                        title ? Interval{x0, x0 + off_w} : Interval{x0 + w - off_w, x0 + w};
                    EXPECT_TRUE(disjoint(lbl, off, OFFSET_LABEL_GAP - 1e-3f))
                        << "label [" << lbl.lo << ", " << lbl.hi << "] offset [" << off.lo << ", "
                        << off.hi << "]";
                    EXPECT_GE(lbl.lo, x0 - 1e-3f);
                    EXPECT_LE(lbl.hi, x0 + w + 1e-3f);
                }
}

// ─── SVG path ───────────────────────────────────────────────────────────────

// Mirror of the SVG exporter's estimate_text_width() (svg_export.cpp), which decides
// layout there.  Kept in sync by hand; the test only needs the same estimate.
float svg_estimate(std::string_view s, float font_px)
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
            em += 1.0f;
        else
            em += 0.62f;
    }
    return em * font_px;
}

struct SvgText
{
    float       x       = 0.0f;
    float       font_px = 0.0f;
    std::string anchor  = "start";
    std::string content;
};

float attr_float(const std::string& tag, const char* name)
{
    const std::string key = std::string(" ") + name + "=\"";
    const size_t      p   = tag.find(key);
    return p == std::string::npos ? 0.0f : std::stof(tag.substr(p + key.size()));
}

std::vector<SvgText> parse_texts(const std::string& svg)
{
    std::vector<SvgText> out;
    size_t               pos = 0;
    while ((pos = svg.find("<text ", pos)) != std::string::npos)
    {
        const size_t gt  = svg.find('>', pos);
        const size_t end = svg.find("</text>", gt);
        if (gt == std::string::npos || end == std::string::npos)
            break;
        const std::string tag = svg.substr(pos, gt - pos);
        SvgText           t;
        t.x            = attr_float(tag, "x");
        t.font_px      = attr_float(tag, "font-size");
        const size_t a = tag.find(" text-anchor=\"");
        if (a != std::string::npos)
            t.anchor = tag.substr(a + 14, tag.find('"', a + 14) - (a + 14));
        t.content = svg.substr(gt + 1, end - gt - 1);
        out.push_back(t);
        pos = end;
    }
    return out;
}

Interval svg_bounds(const SvgText& t)
{
    const float w = svg_estimate(t.content, t.font_px);
    if (t.anchor == "middle")
        return {t.x - w * 0.5f, t.x + w * 0.5f};
    if (t.anchor == "end")
        return {t.x - w, t.x};
    return {t.x, t.x + w};
}

const SvgText* find_text(const std::vector<SvgText>& texts, std::string_view content)
{
    for (const auto& t : texts)
        if (t.content == content)
            return &t;
    return nullptr;
}

// Text whose content is `full` or an ellipsized ("..." or U+2026) prefix of it.
const SvgText* find_label(const std::vector<SvgText>& texts, const std::string& full)
{
    for (const auto& t : texts)
    {
        if (t.content == full)
            return &t;
        for (std::string_view ell : {std::string_view("..."), LABEL_ELLIPSIS})
        {
            const size_t n = ell.size();
            if (t.content.size() > n && t.content.compare(t.content.size() - n, n, ell) == 0
                && full.compare(0, t.content.size() - n, t.content, 0, t.content.size() - n) == 0)
                return &t;
        }
    }
    return nullptr;
}

TEST(LabelLayoutSvg, OffsetsAndLabelsDoNotOverlap)
{
    struct Case
    {
        double xlo, xhi, ylo, yhi;
    };
    const Case cases[] = {
        {1720000000.0, 1720000001.2, 1.0e7 - 0.02, 1.0e7 + 0.02},       // positive offsets
        {-1720000001.2, -1720000000.0, -1.0e7 - 0.02, -1.0e7 + 0.02},   // negative offsets
        {0.0, 3.0e-7, 0.0, 4.0e9},                                      // multipliers
    };
    const std::string xlabel = "Acquisition timestamp measured in seconds since the Unix epoch";
    const std::string title  = "Epoch offsets with long axis labels and a calibrated baseline";

    for (uint32_t fig_w : {640u, 1000u})
        for (const auto& c : cases)
        {
            SCOPED_TRACE("fig_w=" + std::to_string(fig_w) + " x=[" + std::to_string(c.xlo) + ", "
                         + std::to_string(c.xhi) + "]");
            Figure fig({.width = fig_w, .height = 480});
            Axes*  axes[4];
            for (int i = 0; i < 4; ++i)
            {
                auto& ax = fig.subplot(2, 2, i + 1);
                ax.xlim(c.xlo, c.xhi);
                ax.ylim(c.ylo, c.yhi);
                ax.xlabel(xlabel);
                ax.title(title);
                axes[i] = &ax;
            }
            fig.compute_layout();
            const auto texts = parse_texts(SvgExporter::to_string(fig));

            const Axes& ax       = *axes[2];   // lower-left panel
            const auto& vp       = ax.viewport();
            const auto  x_offset = ax.compute_x_ticks().offset_label;
            const auto  y_offset = ax.compute_y_ticks().offset_label;
            ASSERT_FALSE(x_offset.empty());
            ASSERT_FALSE(y_offset.empty());

            // All four panels share the strings; check every occurrence on each row.
            int checked_x = 0;
            int checked_t = 0;
            for (const auto& t : texts)
            {
                if (t.content != x_offset && t.content != y_offset)
                    continue;
                const Interval off = svg_bounds(t);
                for (const auto& l : texts)
                {
                    const bool is_xlabel = find_label({l}, xlabel) != nullptr;
                    const bool is_title  = find_label({l}, title) != nullptr;
                    if (!is_xlabel && !is_title)
                        continue;
                    const Interval lbl = svg_bounds(l);
                    // Only pairs within the same panel (horizontal overlap of spans).
                    if (lbl.hi < off.lo - vp.w || lbl.lo > off.hi + vp.w)
                        continue;
                    if (t.content == x_offset && is_xlabel && t.anchor == "end"
                        && std::abs(t.x - l.x) < vp.w)
                    {
                        EXPECT_TRUE(disjoint(lbl, off, 1.0f))
                            << "x label [" << lbl.lo << ", " << lbl.hi << "] vs offset [" << off.lo
                            << ", " << off.hi << "]";
                        ++checked_x;
                    }
                    if (t.content == y_offset && is_title && t.anchor == "start"
                        && std::abs(t.x - l.x) < vp.w)
                    {
                        EXPECT_TRUE(disjoint(lbl, off, 1.0f))
                            << "title [" << lbl.lo << ", " << lbl.hi << "] vs offset [" << off.lo
                            << ", " << off.hi << "]";
                        ++checked_t;
                    }
                }
            }
            EXPECT_GE(checked_x, 4);
            EXPECT_GE(checked_t, 4);

            // The lower-left labels stay inside their axes span.
            const SvgText* xl = nullptr;
            for (const auto& t : texts)
                if (find_label({t}, xlabel) && t.x >= vp.x && t.x <= vp.x + vp.w)
                    xl = &t;
            ASSERT_NE(xl, nullptr);
            const Interval b = svg_bounds(*xl);
            EXPECT_GE(b.lo, vp.x - 1.0f);
            EXPECT_LE(b.hi, vp.x + vp.w + 1.0f);
        }
}

TEST(LabelLayoutSvg, NoOffsetKeepsLabelCentred)
{
    Figure fig({.width = 800, .height = 600});
    auto&  ax = fig.subplot(1, 1, 1);
    ax.xlim(0.0, 10.0);
    ax.ylim(0.0, 1.0);
    ax.xlabel("Time (s)");
    ax.title("Response");
    fig.compute_layout();
    ASSERT_TRUE(ax.compute_x_ticks().offset_label.empty());
    const auto     texts = parse_texts(SvgExporter::to_string(fig));
    const SvgText* xl    = find_text(texts, "Time (s)");
    const SvgText* tt    = find_text(texts, "Response");
    ASSERT_NE(xl, nullptr);
    ASSERT_NE(tt, nullptr);
    const auto& vp = ax.viewport();
    EXPECT_NEAR(xl->x, vp.x + vp.w * 0.5f, 0.01f);
    EXPECT_NEAR(tt->x, vp.x + vp.w * 0.5f, 0.01f);
}

}   // namespace
