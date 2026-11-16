// AxisStyle text sizes / colours (tick_size, label_size, title_size, tick_color,
// label_color) and the UI text scale: resolved metrics, layout margins, SVG/PDF
// output, workspace persistence and inspector undo.  The GPU text path is
// covered by test_text_scaling (label `gpu`).

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <regex>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <string>
#include <vector>

#include "core/axis_text_style.hpp"
#include "core/figure_layout.hpp"
#include "core/layout.hpp"
#include "ui/commands/undo_manager.hpp"
#include "ui/commands/undoable_property.hpp"
#include "ui/workspace/figure_serializer.hpp"

namespace spectra
{
namespace
{

Axes& plot_axes(Figure& fig, int rows = 1, int cols = 1, int index = 1)
{
    auto& ax = fig.subplot(rows, cols, index);
    ax.plot(std::vector<float>{0, 1, 2, 3}, std::vector<float>{0, 1, 0.5f, 2});
    ax.xlim(0.0, 3.0);
    ax.ylim(0.0, 2.0);
    ax.title("Title");
    ax.xlabel("time (s)");
    ax.ylabel("value");
    return ax;
}

// font-size of the first <text> element whose content is `text`.
float svg_font_size(const std::string& svg, const std::string& text)
{
    const std::regex re("<text[^>]*font-size=\"([0-9.]+)\"[^>]*>" + text + "</text>");
    std::smatch      m;
    if (!std::regex_search(svg, m, re))
        return -1.0f;
    return std::stof(m[1].str());
}

// fill of the first <text> element whose content is `text`.
std::string svg_fill(const std::string& svg, const std::string& text)
{
    const std::regex re("<text[^>]*fill=\"(#[0-9a-f]{6})\"[^>]*>" + text + "</text>");
    std::smatch      m;
    if (!std::regex_search(svg, m, re))
        return {};
    return m[1].str();
}

}   // namespace

// ─── Resolved metrics ───────────────────────────────────────────────────────

TEST(AxisTextStyle, DefaultsResolveToTheThemeSizes)
{
    const AxisStyle def;
    EXPECT_EQ(def.tick_size, 0.0f);
    EXPECT_EQ(def.label_size, THEME_LABEL_FONT_PX);
    EXPECT_EQ(def.title_size, THEME_TITLE_FONT_PX);
    EXPECT_FALSE(axis_color_is_set(def.tick_color));
    EXPECT_FALSE(axis_color_is_set(def.label_color));

    const AxisTextMetrics m = axis_text_metrics(def);
    EXPECT_EQ(m.scale, 1.0f);
    EXPECT_EQ(m.tick_px, 13.0f);
    EXPECT_EQ(m.label_px, 14.0f);
    EXPECT_EQ(m.title_px, 18.0f);
    EXPECT_EQ(m.tick_length, def.tick_length);
    EXPECT_EQ(m.font_px(LabelFont::Tick), 13.0f);
    EXPECT_EQ(m.font_px(LabelFont::Label), 14.0f);
    EXPECT_EQ(m.font_px(LabelFont::Title), 18.0f);
}

TEST(AxisTextStyle, CustomSizesAndScaleMultiply)
{
    AxisStyle s;
    s.tick_size             = 10.0f;
    s.label_size            = 30.0f;
    s.title_size            = 40.0f;
    s.tick_length           = 6.0f;
    const AxisTextMetrics m = axis_text_metrics(s, 1.5f);
    EXPECT_FLOAT_EQ(m.tick_px, 15.0f);
    EXPECT_FLOAT_EQ(m.label_px, 45.0f);
    EXPECT_FLOAT_EQ(m.title_px, 60.0f);
    EXPECT_FLOAT_EQ(m.tick_length, 9.0f);
    EXPECT_FLOAT_EQ(m.px(4.0f), 6.0f);

    // Unset / invalid values fall back to the theme; huge ones are clamped.
    s.label_size            = -3.0f;
    s.title_size            = 1e6f;
    const AxisTextMetrics c = axis_text_metrics(s, 0.0f);
    EXPECT_EQ(c.scale, 1.0f);
    EXPECT_EQ(c.label_px, THEME_LABEL_FONT_PX);
    EXPECT_EQ(c.title_px, AXIS_FONT_MAX_PX);
}

TEST(AxisTextStyle, TitleFitNeverExceedsTheTitleSize)
{
    AxisStyle s;
    s.title_size            = 18.0f;
    s.label_size            = 48.0f;
    const AxisTextMetrics m = axis_text_metrics(s);
    EXPECT_FLOAT_EQ(m.title_font_px(LabelFont::Title), 18.0f);
    EXPECT_FLOAT_EQ(m.title_font_px(LabelFont::Label), 18.0f);   // capped, not 48
    EXPECT_FLOAT_EQ(m.font_px(LabelFont::Label), 48.0f);         // x/y labels unchanged

    // An over-long title in a 100 px budget is ellipsized at 18 px, not at the
    // (larger) label size.
    const LabelWidthFn measure = [&m](const std::string& t, LabelFont f)
    { return static_cast<float>(t.size()) * 0.6f * m.title_font_px(f); };
    const FittedLabel fit =
        fit_axes_title("A very long plot title that cannot fit", 100.0f, 0.0f, measure);
    EXPECT_TRUE(fit.truncated);
    EXPECT_FLOAT_EQ(m.title_font_px(fit.font) * fit.scale, 18.0f);
    EXPECT_LE(measure(fit.text, fit.font) * fit.scale, 100.0f);

    // A label size below the title size still lets the title shrink smoothly.
    s.label_size             = 12.0f;
    const AxisTextMetrics m2 = axis_text_metrics(s);
    EXPECT_FLOAT_EQ(m2.title_font_px(LabelFont::Label), 12.0f);
}

// ─── Layout ─────────────────────────────────────────────────────────────────

TEST(AxisTextStyleLayout, DefaultStyleKeepsThePlainCell)
{
    Figure fig({.width = 800, .height = 600});
    auto&  ax = plot_axes(fig);
    fig.compute_layout();
    const auto& st = fig.style();
    EXPECT_FLOAT_EQ(ax.viewport().y, st.margin_top);
    EXPECT_FLOAT_EQ(ax.viewport().y + ax.viewport().h, 600.0f - st.margin_bottom);
    EXPECT_EQ(ax.text_scale(), 1.0f);
}

TEST(AxisTextStyleLayout, MarginsGrowWithFontSize)
{
    Figure fig({.width = 800, .height = 600});
    auto&  ax = plot_axes(fig);
    fig.compute_layout();
    const Rect plain = ax.viewport();

    ax.axis_style().label_size = 30.0f;
    ax.axis_style().title_size = 40.0f;
    ax.axis_style().tick_size  = 24.0f;
    fig.compute_layout();
    const Rect big = ax.viewport();

    // Room above for the 40 px title, below for 24 px ticks + 30 px label, and
    // left for the wider tick labels + taller rotated label.
    const AxisTextMetrics m = axis_text_metrics(ax);
    EXPECT_GE(big.y + 0.5f, top_label_extent(ax, m));
    EXPECT_GE(600.0f - (big.y + big.h) + 0.5f, bottom_label_extent(ax, m));
    EXPECT_GT(big.y, plain.y);
    EXPECT_LT(big.y + big.h, plain.y + plain.h);
    EXPECT_GT(big.x, plain.x);
    EXPECT_GE(big.x + 0.5f, left_label_extent(ax));
    // The right edge does not move.
    EXPECT_FLOAT_EQ(big.x + big.w, plain.x + plain.w);
}

TEST(AxisTextStyleLayout, ExtentsGrowMonotonicallyWithFontSize)
{
    Figure fig({.width = 800, .height = 600});
    auto&  ax = plot_axes(fig);
    fig.compute_layout();
    float last_bottom = 0.0f, last_top = 0.0f, last_left = 0.0f;
    for (float px : {10.0f, 14.0f, 20.0f, 28.0f})
    {
        ax.axis_style().label_size = px;
        ax.axis_style().title_size = px;
        ax.axis_style().tick_size  = px;
        const AxisTextMetrics m    = axis_text_metrics(ax);
        const float           b    = bottom_label_extent(ax, m);
        const float           t    = top_label_extent(ax, m);
        const float           l    = left_label_extent(ax);
        EXPECT_GT(b, last_bottom) << px;
        EXPECT_GT(t, last_top) << px;
        EXPECT_GT(l, last_left) << px;
        last_bottom = b;
        last_top    = t;
        last_left   = l;
    }
}

TEST(AxisTextStyleLayout, TextScaleIsStoredAndGrowsTheMargins)
{
    Figure        fig({.width = 800, .height = 600});
    auto&         ax  = plot_axes(fig, 1, 2, 1);
    auto&         ax3 = fig.subplot3d(1, 2, 2);
    const Margins margins{60.0f, 40.0f, 50.0f, 40.0f};
    const auto    rects = compute_subplot_layout(800.0f, 600.0f, 1, 2, margins);

    apply_subplot_layout(fig, rects, margins, 1.0f);
    const Rect plain = ax.viewport();
    EXPECT_EQ(ax.text_scale(), 1.0f);

    apply_subplot_layout(fig, rects, margins, 2.0f);
    EXPECT_EQ(ax.text_scale(), 2.0f);
    EXPECT_EQ(ax3.text_scale(), 2.0f);
    EXPECT_EQ(axis_text_metrics(ax).tick_px, 26.0f);
    EXPECT_GT(ax.viewport().y, plain.y);
    EXPECT_LT(ax.viewport().y + ax.viewport().h, plain.y + plain.h);

    // Exports (Figure::compute_layout) lay out at scale 1 again.
    fig.compute_layout();
    EXPECT_EQ(ax.text_scale(), 1.0f);
    EXPECT_EQ(ax3.text_scale(), 1.0f);
}

TEST(AxisTextStyleLayout, RowSharesTheGrowth)
{
    Figure fig({.width = 900, .height = 400});
    auto&  left                  = plot_axes(fig, 1, 2, 1);
    auto&  right                 = plot_axes(fig, 1, 2, 2);
    left.axis_style().title_size = 36.0f;
    fig.compute_layout();
    // Both axes of the row keep the same top and bottom edges.
    EXPECT_FLOAT_EQ(left.viewport().y, right.viewport().y);
    EXPECT_FLOAT_EQ(left.viewport().h, right.viewport().h);
    EXPECT_GT(left.viewport().y, fig.style().margin_top);
}

TEST(AxisTextStyleLayout, BottomMarginHoldsTheXOffsetWithoutXLabel)
{
    Figure fig({.width = 800, .height = 900});
    auto&  ax = fig.subplot(1, 1, 1);
    ax.plot(std::vector<float>{0, 1}, std::vector<float>{0, 1});
    ax.xlim(1720000000.0, 1720000001.2);   // epoch seconds: "+1.72e9"-style offset
    ax.ylim(0.0, 1.0);
    ax.axis_style().tick_size = 48.0f;
    fig.compute_layout();
    ASSERT_TRUE(ax.xlabel().empty());
    ASSERT_FALSE(ax.compute_x_ticks().offset_label.empty());

    const AxisTextMetrics m = axis_text_metrics(ax);
    // Tick marks + tick labels + the offset row under them (same rows as the renderer).
    const float with_offset = m.tick_length + m.px(5.0f) + m.tick_px * PLOT_TEXT_LINE_FACTOR
                              + m.px(4.0f) + m.tick_px * PLOT_TEXT_LINE_FACTOR + m.px(6.0f);
    EXPECT_GE(bottom_label_extent(ax, m) + 0.5f, with_offset);
    EXPECT_GE(900.0f - (ax.viewport().y + ax.viewport().h) + 0.5f, with_offset);

    // Without an offset (small limits) the row is not reserved.
    ax.xlim(0.0, 10.0);
    EXPECT_LT(bottom_label_extent(ax, m), with_offset - m.tick_px);
}

// ─── SVG / PDF ──────────────────────────────────────────────────────────────

TEST(AxisTextStyleSvg, DefaultSizesAndThemeColours)
{
    Figure fig({.width = 800, .height = 600});
    plot_axes(fig);
    fig.compute_layout();
    const std::string svg = SvgExporter::to_string(fig);
    EXPECT_FLOAT_EQ(svg_font_size(svg, "time \\(s\\)"), 14.0f);
    EXPECT_FLOAT_EQ(svg_font_size(svg, "Title"), 18.0f);
    EXPECT_FLOAT_EQ(svg_font_size(svg, "1\\.0"), 13.0f);
    EXPECT_NE(svg_fill(svg, "time \\(s\\)"), "#ff0000");
}

TEST(AxisTextStyleSvg, SizesAndColoursReachTheText)
{
    Figure fig({.width = 800, .height = 600});
    auto&  ax                   = plot_axes(fig);
    ax.axis_style().label_size  = 30.0f;
    ax.axis_style().title_size  = 40.0f;
    ax.axis_style().tick_size   = 20.0f;
    ax.axis_style().tick_color  = Color{0.0f, 0.0f, 1.0f, 1.0f};
    ax.axis_style().label_color = Color{1.0f, 0.0f, 0.0f, 1.0f};
    fig.compute_layout();
    const std::string svg = SvgExporter::to_string(fig);

    EXPECT_FLOAT_EQ(svg_font_size(svg, "time \\(s\\)"), 30.0f);
    EXPECT_FLOAT_EQ(svg_font_size(svg, "value"), 30.0f);
    EXPECT_FLOAT_EQ(svg_font_size(svg, "Title"), 40.0f);
    EXPECT_FLOAT_EQ(svg_font_size(svg, "1\\.0"), 20.0f);
    EXPECT_EQ(svg_fill(svg, "time \\(s\\)"), "#ff0000");
    EXPECT_EQ(svg_fill(svg, "value"), "#ff0000");
    EXPECT_EQ(svg_fill(svg, "1\\.0"), "#0000ff");
    EXPECT_NE(svg_fill(svg, "Title"), "#ff0000");   // the title keeps the theme colour
}

TEST(AxisTextStyleSvg, ThreeDTickSizeAndColour)
{
    Figure fig({.width = 800, .height = 600});
    auto&  ax = fig.subplot3d(1, 1, 1);
    ax.xlim(0.0, 1.0);
    ax.ylim(0.0, 1.0);
    ax.zlim(0.0, 1.0);
    ax.title("Cube");
    ax.axis_style().tick_size  = 22.0f;
    ax.axis_style().title_size = 30.0f;
    ax.axis_style().tick_color = Color{0.0f, 1.0f, 0.0f, 1.0f};
    fig.compute_layout();
    const std::string svg = SvgExporter::to_string(fig);
    EXPECT_FLOAT_EQ(svg_font_size(svg, "Cube"), 30.0f);
    EXPECT_NE(svg.find("font-size=\"22\" fill=\"#00ff00\""), std::string::npos);
}

// ─── Persistence ────────────────────────────────────────────────────────────

class AxisTextStylePersist : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        path_ = (std::filesystem::temp_directory_path() / "spectra_axis_text_style.spf").string();
    }
    void        TearDown() override { std::remove(path_.c_str()); }
    std::string path_;
};

TEST_F(AxisTextStylePersist, RoundTripsSizesAndColours)
{
    Figure src({800, 600});
    auto&  ax                   = plot_axes(src);
    ax.axis_style().tick_size   = 11.0f;
    ax.axis_style().label_size  = 22.0f;
    ax.axis_style().title_size  = 33.0f;
    ax.axis_style().tick_color  = Color{0.1f, 0.2f, 0.3f, 1.0f};
    ax.axis_style().label_color = Color{0.4f, 0.5f, 0.6f, 0.5f};
    auto& ax3                   = src.subplot3d(2, 1, 2);
    ax3.axis_style().title_size = 27.0f;
    ASSERT_TRUE(FigureSerializer::save(path_, src));

    Figure dst({800, 600});
    ASSERT_TRUE(FigureSerializer::load(path_, dst));
    ASSERT_FALSE(dst.axes().empty());
    const AxisStyle& as = dst.axes()[0]->axis_style();
    EXPECT_EQ(as.tick_size, 11.0f);
    EXPECT_EQ(as.label_size, 22.0f);
    EXPECT_EQ(as.title_size, 33.0f);
    EXPECT_FLOAT_EQ(as.tick_color.b, 0.3f);
    EXPECT_FLOAT_EQ(as.label_color.a, 0.5f);

    bool found3d = false;
    for (const auto& a : dst.all_axes())
        if (auto* a3 = dynamic_cast<Axes3D*>(a.get()))
        {
            found3d = true;
            EXPECT_EQ(a3->axis_style().title_size, 27.0f);
        }
    EXPECT_TRUE(found3d);
}

// ─── Inspector undo ─────────────────────────────────────────────────────────

TEST(AxisTextStyleUndo, OneStepRestoresTheWholeStyle)
{
    Figure      fig({800, 600});
    auto&       ax = plot_axes(fig);
    UndoManager undo;
    const auto  before = ax.axis_style();
    AxisStyle   after  = before;
    after.label_size   = 26.0f;
    after.tick_color   = Color{1.0f, 0.0f, 0.0f, 1.0f};
    undoable_axis_style(&undo, ax, before, after);
    EXPECT_EQ(ax.axis_style().label_size, 26.0f);
    ASSERT_EQ(undo.undo_count(), 1u);

    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(ax.axis_style().label_size, before.label_size);
    EXPECT_FALSE(axis_color_is_set(ax.axis_style().tick_color));
    ASSERT_TRUE(undo.redo());
    EXPECT_EQ(ax.axis_style().label_size, 26.0f);
    EXPECT_EQ(ax.axis_style().tick_color.r, 1.0f);

    // No-op edits push nothing.
    undoable_axis_style(&undo, ax, ax.axis_style(), ax.axis_style());
    EXPECT_EQ(undo.undo_count(), 1u);
}

TEST(AxisTextStyleSvg, LongTitleKeepsTheTitleSizeWhenLabelsAreLarger)
{
    const std::string long_title = "An extremely long title that cannot possibly fit here";
    {
        Figure fig({.width = 220, .height = 400});
        auto&  ax = plot_axes(fig);
        ax.title(long_title);
        ax.axis_style().title_size = 18.0f;
        ax.axis_style().label_size = 48.0f;
        fig.compute_layout();
        const std::string svg = SvgExporter::to_string(fig);
        // Ellipsized title ("An …"), drawn at 18 px rather than at the 48 px label size.
        EXPECT_FLOAT_EQ(svg_font_size(svg, "An[^<]*\xE2\x80\xA6"), 18.0f);
    }
    {
        Figure fig({.width = 220, .height = 400});
        auto&  ax = fig.subplot3d(1, 1, 1);
        ax.xlim(0.0, 1.0);
        ax.ylim(0.0, 1.0);
        ax.zlim(0.0, 1.0);
        ax.title(long_title);
        ax.axis_style().title_size = 18.0f;
        ax.axis_style().label_size = 48.0f;
        fig.compute_layout();
        const std::string svg = SvgExporter::to_string(fig);
        EXPECT_FLOAT_EQ(svg_font_size(svg, "An[^<]*\xE2\x80\xA6"), 18.0f);
    }
}

}   // namespace spectra
