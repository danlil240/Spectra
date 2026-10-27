// Round-2 visual follow-ups (r2-b): explicit black legend background, the
// high-contrast grid over fills, the app UI scale, the SDL3 event wait and
// the back-to-front triangle sort of translucent 3D shapes.  The ImGui side
// of the UI scale is tested in test_theme.cpp (which has imgui.h).

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/math3d.hpp>
#include <string>
#include <vector>

#include "render/shape3d_sort.hpp"
#include "ui/settings/settings_store.hpp"
#include "ui/theme/theme.hpp"
#include "ui/window/event_wait.hpp"
#include "ui/workspace/figure_serializer.hpp"

using namespace spectra;

namespace
{

std::filesystem::path temp_file(const std::string& stem, const std::string& ext)
{
    return std::filesystem::temp_directory_path()
           / (stem + "_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + ext);
}

Figure make_legend_figure()
{
    Figure             fig({.width = 400, .height = 300});
    auto&              ax = fig.subplot(1, 1, 1);
    std::vector<float> x  = {0.0f, 1.0f, 2.0f};
    std::vector<float> y  = {0.0f, 1.0f, 0.5f};
    ax.line(x, y).label("a");
    fig.compute_layout();
    return fig;
}

}   // namespace

// ─── Legend background: opaque black is expressible ─────────────────────────

TEST(LegendBackground, BlackIsUnsetUnlessFlagged)
{
    LegendConfig lc;
    EXPECT_FALSE(lc.uses_bg_color());
    EXPECT_FALSE(lc.uses_border_color());

    lc.bg_color = Color{0.0f, 0.0f, 0.0f, 1.0f};
    EXPECT_FALSE(lc.uses_bg_color());   // old sentinel behaviour kept
    lc.has_bg_color = true;
    EXPECT_TRUE(lc.uses_bg_color());

    LegendConfig red;
    red.bg_color = Color{1.0f, 0.0f, 0.0f, 1.0f};
    EXPECT_TRUE(red.uses_bg_color());   // non-black still works without the flag
    red.border_color     = Color{0.0f, 0.0f, 0.0f, 1.0f};
    red.has_border_color = true;
    EXPECT_TRUE(red.uses_border_color());
}

TEST(LegendBackground, SvgDrawsExplicitBlack)
{
    auto       fig    = make_legend_figure();
    const auto legend = [](const std::string& svg)
    {
        const auto g = svg.find("<g class=\"legend\">");
        return g == std::string::npos ? std::string() : svg.substr(g, 400);
    };

    const std::string themed = legend(SvgExporter::to_string(fig));
    ASSERT_FALSE(themed.empty());
    EXPECT_EQ(themed.find("fill=\"#000000\""), std::string::npos);

    fig.legend().bg_color     = Color{0.0f, 0.0f, 0.0f, 1.0f};
    fig.legend().has_bg_color = true;
    const std::string black   = legend(SvgExporter::to_string(fig));
    EXPECT_NE(black.find("fill=\"#000000\""), std::string::npos) << black;
}

TEST(LegendBackground, SerializerRoundTripsFlags)
{
    const auto path               = temp_file("spectra_legend_flags", ".spectra");
    Figure     src                = make_legend_figure();
    src.legend().bg_color         = Color{0.0f, 0.0f, 0.0f, 1.0f};
    src.legend().has_bg_color     = true;
    src.legend().has_border_color = false;
    ASSERT_TRUE(FigureSerializer::save(path.string(), src));

    Figure dst({400, 300});
    ASSERT_TRUE(FigureSerializer::load(path.string(), dst));
    EXPECT_TRUE(dst.legend().has_bg_color);
    EXPECT_FALSE(dst.legend().has_border_color);
    EXPECT_TRUE(dst.legend().uses_bg_color());
    std::filesystem::remove(path);
}

// ─── High-contrast grid ──────────────────────────────────────────────────────

TEST(HighContrastGrid, TranslucentSoFillsAreNotCutByDarkLines)
{
    ui::ThemeManager tm;
    ui::ThemeManager::set_current(&tm);
    tm.ensure_initialized();
    tm.set_theme("high_contrast");
    const auto& c = tm.colors();

    // Over the (black) canvas the grid still reads as #4D4D4D / #1C1C1C ...
    const ui::Color major = c.grid_major.composite_over(c.bg_canvas);
    const ui::Color minor = c.grid_minor.composite_over(c.bg_canvas);
    EXPECT_NEAR(major.r * 255.0f, 0x4D, 1.0f);
    EXPECT_NEAR(minor.r * 255.0f, 0x1C, 1.0f);

    // ... but over a bright fill it only lightens it instead of drawing a dark
    // line through it (the old opaque grey made a checkerboard).
    EXPECT_LT(c.grid_major.a, 0.5f);
    EXPECT_LT(c.grid_minor.a, 0.2f);
    const ui::Color fill(0.10f, 0.70f, 0.80f, 1.0f);
    const ui::Color over = c.grid_minor.composite_over(fill);
    EXPECT_GE(over.g, fill.g);
    EXPECT_GE(over.b, fill.b);
    ui::ThemeManager::set_current(nullptr);
}

// ─── App UI scale ────────────────────────────────────────────────────────────

TEST(UiScale, ClampedAndPersisted)
{
    using ui::ThemeManager;
    EXPECT_FLOAT_EQ(ThemeManager::clamp_ui_scale(0.1f), ThemeManager::UI_SCALE_MIN);
    EXPECT_FLOAT_EQ(ThemeManager::clamp_ui_scale(9.0f), ThemeManager::UI_SCALE_MAX);
    EXPECT_FLOAT_EQ(ThemeManager::clamp_ui_scale(std::nanf("")), 1.0f);
    EXPECT_FLOAT_EQ(ThemeManager::clamp_ui_scale(1.25f), 1.25f);

    const auto path = temp_file("spectra_ui_scale", ".json");
    {
        ui::settings::SettingsStore store;
        store.data_mut().ui_scale = 1.5f;
        ASSERT_TRUE(store.save(path.string()));
    }
    {
        ui::settings::SettingsStore store;
        ASSERT_TRUE(store.load(path.string()));
        EXPECT_FLOAT_EQ(store.data().ui_scale, 1.5f);

        ThemeManager tm;
        store.apply_to(tm);
        EXPECT_FLOAT_EQ(tm.ui_scale(), 1.5f);
    }
    {
        // Out-of-range values in the file are clamped; missing keeps 1.0.
        std::ofstream(path) << R"({"version":1,"ui":{"ui_scale": 7})";
        ui::settings::SettingsStore store;
        ASSERT_TRUE(store.load(path.string()));
        EXPECT_FLOAT_EQ(store.data().ui_scale, ThemeManager::UI_SCALE_MAX);
        std::ofstream(path) << R"({"version":1,"ui":{"default_theme":"light"}})";
        ui::settings::SettingsStore plain;
        ASSERT_TRUE(plain.load(path.string()));
        EXPECT_FLOAT_EQ(plain.data().ui_scale, 1.0f);
    }
    std::filesystem::remove(path);
}

// ─── SDL3 event wait ─────────────────────────────────────────────────────────

TEST(Sdl3EventWait, TimeoutMatchesGlfwSemantics)
{
    EXPECT_EQ(sdl3_wait_timeout_ms(0.0), 0);
    EXPECT_EQ(sdl3_wait_timeout_ms(-1.0), 0);
    EXPECT_EQ(sdl3_wait_timeout_ms(std::nan("")), 0);
    EXPECT_EQ(sdl3_wait_timeout_ms(0.0001), 1);   // never a busy spin
    EXPECT_EQ(sdl3_wait_timeout_ms(0.1), 100);
    EXPECT_EQ(sdl3_wait_timeout_ms(2.5), 2500);   // no 100 ms cap any more
    EXPECT_EQ(sdl3_wait_timeout_ms(1e12), std::numeric_limits<int32_t>::max());
}

// ─── Translucent 3D shape triangle sort ──────────────────────────────────────

namespace
{

// Three parallel triangles at z = 0, 1, 2 (6 floats per vertex: pos, normal).
std::vector<float> stacked_triangles()
{
    std::vector<float> v;
    for (int t = 0; t < 3; ++t)
    {
        const float z = static_cast<float>(t);
        for (const auto& p : {std::array<float, 2>{0, 0}, {1, 0}, {0, 1}})
        {
            v.insert(v.end(), {p[0], p[1], z, 0.0f, 0.0f, 1.0f});
        }
    }
    return v;
}

float tri_z(const std::vector<float>& v, const std::vector<uint32_t>& idx, size_t tri)
{
    return v[idx[tri * 3] * 6 + 2];
}

}   // namespace

TEST(Shape3DTriangleSort, BackToFrontForCamera)
{
    const auto            v   = stacked_triangles();
    std::vector<uint32_t> idx = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    const mat4            id  = mat4_identity();

    // Camera above (+z): farthest is z = 0, nearest z = 2.
    sort_triangles_back_to_front(idx, 0, idx.size(), v, id, {0, 0, 10}, {0, 0, -1}, false);
    EXPECT_FLOAT_EQ(tri_z(v, idx, 0), 0.0f);
    EXPECT_FLOAT_EQ(tri_z(v, idx, 2), 2.0f);

    // Camera below: order reverses.  Orthographic uses depth along forward.
    sort_triangles_back_to_front(idx, 0, idx.size(), v, id, {0, 0, -10}, {0, 0, 1}, true);
    EXPECT_FLOAT_EQ(tri_z(v, idx, 0), 2.0f);
    EXPECT_FLOAT_EQ(tri_z(v, idx, 2), 0.0f);

    // Triangles keep their own vertices (winding intact).
    for (size_t t = 0; t < 3; ++t)
    {
        EXPECT_EQ(idx[t * 3 + 1], idx[t * 3] + 1);
        EXPECT_EQ(idx[t * 3 + 2], idx[t * 3] + 2);
    }
}

TEST(Shape3DTriangleSort, OnlyTouchesTheGivenRange)
{
    const auto            v   = stacked_triangles();
    std::vector<uint32_t> idx = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    // Sort only triangles 1..2 with the camera below: they swap, 0 stays.
    sort_triangles_back_to_front(idx, 3, 6, v, mat4_identity(), {0, 0, -10}, {0, 0, 1}, false);
    EXPECT_EQ(idx[0], 0u);
    EXPECT_FLOAT_EQ(tri_z(v, idx, 1), 2.0f);
    EXPECT_FLOAT_EQ(tri_z(v, idx, 2), 1.0f);

    // Out-of-range vertex references leave the range untouched.
    std::vector<uint32_t> bad    = {0, 1, 2, 99, 4, 5};
    const auto            before = bad;
    sort_triangles_back_to_front(bad,
                                 0,
                                 bad.size(),
                                 v,
                                 mat4_identity(),
                                 {0, 0, -10},
                                 {0, 0, 1},
                                 false);
    EXPECT_EQ(bad, before);
}
