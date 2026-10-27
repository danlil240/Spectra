// Issue #105: 3D ticks come from the shared tick generator, lighting can be
// switched off, and 3D labels may use the margins of their subplot cell.

#include <gtest/gtest.h>
#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>

#include "core/axes3d.hpp"
#include "core/axis_ticks.hpp"
#include "render/renderer.hpp"

using namespace spectra;

namespace
{

void expect_same_ticks(const TickResult& a, const TickResult& b)
{
    EXPECT_EQ(a.positions, b.positions);
    EXPECT_EQ(a.labels, b.labels);
    EXPECT_EQ(a.offset_label, b.offset_label);
}

LinearTickOptions axes3d_tick_options()
{
    LinearTickOptions opt;
    opt.target_px = Y_TICK_TARGET_PX;
    opt.axis_px   = 5.0 * Y_TICK_TARGET_PX;
    return opt;
}

}   // namespace

TEST(Axes3DTicks, UseSharedGenerator)
{
    Axes3D ax;
    ax.xlim(-2.0, 2.0);
    ax.ylim(0.0, 0.003);
    ax.zlim(-1.0e9 - 5.0, -1.0e9 + 5.0);
    const auto opt = axes3d_tick_options();
    expect_same_ticks(ax.compute_x_ticks(), generate_linear_ticks(-2.0, 2.0, opt));
    expect_same_ticks(ax.compute_y_ticks(), generate_linear_ticks(0.0, 0.003, opt));
    expect_same_ticks(ax.compute_z_ticks(), generate_linear_ticks(-1.0e9 - 5.0, -1.0e9 + 5.0, opt));
}

TEST(Axes3DTicks, AboutFiveStepsWithSharedPrecision)
{
    Axes3D ax;
    ax.xlim(-1.0, 1.0);
    const TickResult t = ax.compute_x_ticks();
    ASSERT_GE(t.positions.size(), 4u);
    EXPECT_LE(t.positions.size(), 7u);
    // One precision per axis, as on 2D axes ("-1.0", "-0.5", "0.0", ...).
    for (const auto& l : t.labels)
        EXPECT_NE(l.find('.'), std::string::npos) << l;
}

TEST(Axes3DTicks, LargeOffsetMovesToOffsetLabel)
{
    Axes3D ax;
    ax.xlim(1.0e9, 1.0e9 + 10.0);
    const TickResult t = ax.compute_x_ticks();
    EXPECT_FALSE(t.offset_label.empty());
    for (const auto& l : t.labels)
        EXPECT_LE(l.size(), 4u) << l;
}

TEST(Axes3DTicks, ArrowLabelCarriesTickOffset)
{
    EXPECT_EQ(axes3d_arrow_label("", "X", ""), "X");
    EXPECT_EQ(axes3d_arrow_label("Time", "X", ""), "Time");
    EXPECT_EQ(axes3d_arrow_label("", "Z", "+1e9"), "Z  +1e9");
    EXPECT_EQ(axes3d_arrow_label("Depth", "Z", "+1e9"), "Depth  +1e9");
}

TEST(Axes3DLighting, ShaderLightDirIsZeroOnlyWhenDisabled)
{
    Axes3D ax;
    ax.light_dir(1.0f, 0.5f, 0.25f);
    vec3 ld = shader_light_dir(ax);
    EXPECT_FLOAT_EQ(static_cast<float>(ld.x), 1.0f);
    EXPECT_FLOAT_EQ(static_cast<float>(ld.y), 0.5f);
    EXPECT_FLOAT_EQ(static_cast<float>(ld.z), 0.25f);

    // A zero direction while enabled must not read as "lighting off".
    ax.light_dir(0.0f, 0.0f, 0.0f);
    ld = shader_light_dir(ax);
    EXPECT_GT(vec3_length_sq(ld), 0.1);

    ax.light_dir(1.0f, 1.0f, 1.0f);
    ax.lighting_enabled(false);
    ld = shader_light_dir(ax);
    EXPECT_EQ(vec3_length_sq(ld), 0.0);
}

TEST(Axes3DCellRect, GrowsIntoFigureMarginsWithinTheFigure)
{
    FigureStyle style;   // margins: left 70, right 20, top 40, bottom 60
    const Rect  vp{70.0f, 40.0f, 550.0f, 380.0f};
    const Rect  cell = axes3d_cell_rect(vp, style, 640.0f, 480.0f);
    EXPECT_FLOAT_EQ(cell.x, 0.0f);
    EXPECT_FLOAT_EQ(cell.y, 0.0f);
    EXPECT_FLOAT_EQ(cell.w, 640.0f);
    EXPECT_FLOAT_EQ(cell.h, 480.0f);

    // Clamped to the figure and to a quarter of a small viewport.
    const Rect small{300.0f, 300.0f, 80.0f, 40.0f};
    const Rect c2 = axes3d_cell_rect(small, style, 1000.0f, 1000.0f);
    EXPECT_FLOAT_EQ(c2.x, 280.0f);
    EXPECT_FLOAT_EQ(c2.y, 290.0f);
    EXPECT_FLOAT_EQ(c2.w, 120.0f);
    EXPECT_FLOAT_EQ(c2.h, 60.0f);
}
