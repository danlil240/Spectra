// Tests for Axes::text() / Axes::annotate() (data model, layout helpers, SVG export).

#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <string>

#include "core/annotation_layout.hpp"
#include "render/text_renderer.hpp"

namespace spectra
{
namespace
{

size_t count_occurrences(const std::string& haystack, const std::string& needle)
{
    size_t n   = 0;
    size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos)
    {
        ++n;
        pos += needle.size();
    }
    return n;
}

// ── Data model ──────────────────────────────────────────────────────────────

TEST(Annotations, TextAddsAnnotationWithDefaults)
{
    Axes  ax;
    auto& a = ax.text(1.5f, -2.0f, "peak");

    ASSERT_EQ(ax.annotations().size(), 1u);
    EXPECT_EQ(ax.annotations()[0].get(), &a);
    EXPECT_EQ(a.text(), "peak");
    EXPECT_FLOAT_EQ(a.x(), 1.5f);
    EXPECT_FLOAT_EQ(a.y(), -2.0f);
    EXPECT_FALSE(a.color().has_value());   // theme text colour at draw time
    EXPECT_FLOAT_EQ(a.font_size(), 0.0f);
    EXPECT_EQ(a.halign(), TextAnnotation::HAlign::Left);
    EXPECT_EQ(a.valign(), TextAnnotation::VAlign::Bottom);
    EXPECT_FALSE(a.has_arrow());
    EXPECT_TRUE(a.visible());
    EXPECT_TRUE(ax.series().empty());   // annotations are not series
}

TEST(Annotations, FluentSettersApply)
{
    Axes  ax;
    auto& a = ax.text(0.0f, 0.0f, "x")
                  .color(colors::red)
                  .font_size(18.0f)
                  .halign(TextAnnotation::HAlign::Right)
                  .valign(TextAnnotation::VAlign::Top)
                  .text("y")
                  .position(3.0f, 4.0f);

    ASSERT_TRUE(a.color().has_value());
    EXPECT_FLOAT_EQ(a.color()->r, colors::red.r);
    EXPECT_FLOAT_EQ(a.font_size(), 18.0f);
    EXPECT_EQ(a.halign(), TextAnnotation::HAlign::Right);
    EXPECT_EQ(a.valign(), TextAnnotation::VAlign::Top);
    EXPECT_EQ(a.text(), "y");
    EXPECT_FLOAT_EQ(a.x(), 3.0f);
    EXPECT_FLOAT_EQ(a.y(), 4.0f);

    a.font_size(-5.0f);
    EXPECT_FLOAT_EQ(a.font_size(), 0.0f);
}

TEST(Annotations, AnnotateAddsCentredTextWithArrow)
{
    Axes  ax;
    auto& a = ax.annotate("max", 2.0f, 1.0f, 3.0f, 1.5f);

    EXPECT_FLOAT_EQ(a.x(), 3.0f);
    EXPECT_FLOAT_EQ(a.y(), 1.5f);
    ASSERT_TRUE(a.has_arrow());
    EXPECT_FLOAT_EQ(a.arrow_x(), 2.0f);
    EXPECT_FLOAT_EQ(a.arrow_y(), 1.0f);
    EXPECT_EQ(a.halign(), TextAnnotation::HAlign::Center);
    EXPECT_EQ(a.valign(), TextAnnotation::VAlign::Middle);

    a.clear_arrow();
    EXPECT_FALSE(a.has_arrow());
}

TEST(Annotations, ReferencesStayValidAndClearRemovesAll)
{
    Axes  ax;
    auto& first = ax.text(0.0f, 0.0f, "first");
    for (int i = 0; i < 64; ++i)
        ax.text(static_cast<float>(i), 0.0f, "more");
    EXPECT_EQ(first.text(), "first");   // storage is stable across growth
    EXPECT_EQ(ax.annotations().size(), 65u);

    ax.clear_annotations();
    EXPECT_TRUE(ax.annotations().empty());
}

TEST(Annotations, DoNotAffectAutoFit)
{
    Axes               ax;
    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {0.0f, 1.0f};
    ax.line(x, y);
    ax.auto_fit();
    const auto before = ax.x_limits();

    ax.text(100.0f, 100.0f, "far away");
    ax.auto_fit();
    EXPECT_DOUBLE_EQ(ax.x_limits().max, before.max);
}

// ── Layout helpers ──────────────────────────────────────────────────────────

TEST(AnnotationLayout, TextBoxFollowsAlignment)
{
    using annotation_layout::text_box;
    auto b = text_box(100.0f,
                      50.0f,
                      40.0f,
                      10.0f,
                      TextAnnotation::HAlign::Left,
                      TextAnnotation::VAlign::Top);
    EXPECT_FLOAT_EQ(b.x0, 100.0f);
    EXPECT_FLOAT_EQ(b.y0, 50.0f);
    EXPECT_FLOAT_EQ(b.x1, 140.0f);
    EXPECT_FLOAT_EQ(b.y1, 60.0f);

    b = text_box(100.0f,
                 50.0f,
                 40.0f,
                 10.0f,
                 TextAnnotation::HAlign::Center,
                 TextAnnotation::VAlign::Middle);
    EXPECT_FLOAT_EQ(b.x0, 80.0f);
    EXPECT_FLOAT_EQ(b.y0, 45.0f);

    b = text_box(100.0f,
                 50.0f,
                 40.0f,
                 10.0f,
                 TextAnnotation::HAlign::Right,
                 TextAnnotation::VAlign::Bottom);
    EXPECT_FLOAT_EQ(b.x1, 100.0f);
    EXPECT_FLOAT_EQ(b.y1, 50.0f);
}

TEST(AnnotationLayout, ArrowStartsOutsideTextAndEndsAtTarget)
{
    using namespace annotation_layout;
    const Box b{0.0f, 0.0f, 40.0f, 10.0f};   // centre (20, 5)
    Arrow     a;
    ASSERT_TRUE(arrow_geometry(b, 120.0f, 5.0f, a));
    // Leaves the right edge plus the gap, horizontally.
    EXPECT_FLOAT_EQ(a.sx, 40.0f + ARROW_GAP);
    EXPECT_FLOAT_EQ(a.sy, 5.0f);
    EXPECT_FLOAT_EQ(a.tx, 120.0f);
    EXPECT_FLOAT_EQ(a.ty, 5.0f);
    // Head base is ARROW_HEAD_LEN behind the tip, symmetric about the shaft.
    EXPECT_FLOAT_EQ(a.lx, 120.0f - ARROW_HEAD_LEN);
    EXPECT_FLOAT_EQ(a.rx, 120.0f - ARROW_HEAD_LEN);
    EXPECT_FLOAT_EQ(std::fabs(a.ly - a.ry), 2.0f * ARROW_HEAD_HALF_W);
    EXPECT_GT(a.ex, a.sx);
    EXPECT_LT(a.ex, a.tx);
}

TEST(AnnotationLayout, NoArrowWhenTargetUnderText)
{
    using namespace annotation_layout;
    const Box b{0.0f, 0.0f, 40.0f, 10.0f};
    Arrow     a;
    EXPECT_FALSE(arrow_geometry(b, 20.0f, 5.0f, a));
    EXPECT_FALSE(arrow_geometry(b, 42.0f, 5.0f, a));   // inside the gap
    EXPECT_FALSE(arrow_geometry(b, NAN, 5.0f, a));
}

TEST(AnnotationLayout, NearestFontSizeSnapsToPresets)
{
    EXPECT_EQ(TextRenderer::nearest_font_size(0.0f), FontSize::Label);
    EXPECT_EQ(TextRenderer::nearest_font_size(-3.0f), FontSize::Label);
    EXPECT_EQ(TextRenderer::nearest_font_size(10.0f), FontSize::Tick);
    EXPECT_EQ(TextRenderer::nearest_font_size(14.0f), FontSize::Label);
    EXPECT_EQ(TextRenderer::nearest_font_size(24.0f), FontSize::Title);
}

// ── SVG export ──────────────────────────────────────────────────────────────

TEST(AnnotationSvg, TextIsExported)
{
    Figure fig({.width = 640, .height = 480});
    auto&  ax = fig.subplot(1, 1, 1);
    ax.xlim(0.0, 10.0);
    ax.ylim(0.0, 10.0);
    ax.text(5.0f, 5.0f, "hello <world>").color(rgb(1.0f, 0.0f, 0.0f)).font_size(20.0f);
    fig.compute_layout();

    const std::string svg = SvgExporter::to_string(fig);
    EXPECT_NE(svg.find("class=\"annotations\""), std::string::npos);
    EXPECT_NE(svg.find(">hello &lt;world&gt;</text>"), std::string::npos);
    EXPECT_NE(svg.find("font-size=\"20\""), std::string::npos);
    EXPECT_NE(svg.find("fill=\"#ff0000\""), std::string::npos);
    EXPECT_EQ(svg.find("<polygon"), std::string::npos);   // no arrow for text()
}

TEST(AnnotationSvg, AnnotateExportsArrowAndHiddenAreSkipped)
{
    Figure fig({.width = 640, .height = 480});
    auto&  ax = fig.subplot(1, 1, 1);
    ax.xlim(0.0, 10.0);
    ax.ylim(0.0, 10.0);
    ax.annotate("max here", 2.0f, 2.0f, 7.0f, 8.0f);
    ax.text(1.0f, 1.0f, "hidden").visible(false);
    fig.compute_layout();

    const std::string svg = SvgExporter::to_string(fig);
    EXPECT_NE(svg.find(">max here</text>"), std::string::npos);
    EXPECT_NE(svg.find("text-anchor=\"middle\" fill"), std::string::npos);
    EXPECT_EQ(count_occurrences(svg, "<polygon"), 1u);
    EXPECT_EQ(svg.find("hidden"), std::string::npos);
}

TEST(AnnotationSvg, NoAnnotationGroupWithoutAnnotations)
{
    Figure fig({.width = 320, .height = 240});
    fig.subplot(1, 1, 1);
    fig.compute_layout();
    EXPECT_EQ(SvgExporter::to_string(fig).find("annotations"), std::string::npos);
}

}   // namespace
}   // namespace spectra
