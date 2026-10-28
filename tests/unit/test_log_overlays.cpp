// Overlays on log axes (issue #103).
//
// Log axes are drawn linearly in log(v).  Every overlay that maps between
// screen and data coordinates (data markers, region select, box select,
// annotations) and every limit animation (inertial pan, linked-axis zoom/pan)
// must go through the same axis-space transform, or it lands at the wrong
// place: on a log x axis [1, 100] the viewport midpoint is 10, not 50.5.

#include <cmath>
#include <gtest/gtest.h>
#include <memory>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <vector>

#include "ui/animation/animation_controller.hpp"
#include "ui/animation/transition_engine.hpp"
#include "ui/data/axis_link.hpp"
#include "ui/overlay/axis_mapping.hpp"

#if defined(SPECTRA_USE_IMGUI) && __has_include(<imgui.h>)
    #define SPECTRA_TEST_LOG_OVERLAYS 1
    #include <imgui.h>

    #include "ui/input/region_select.hpp"
    #include "ui/overlay/data_interaction.hpp"
    #include "ui/overlay/data_marker.hpp"
    #include "ui/theme/theme.hpp"
#endif

using namespace spectra;

namespace
{

constexpr Rect VP{100.0f, 50.0f, 800.0f, 400.0f};

double mid_x()
{
    return VP.x + 0.5 * VP.w;
}
double mid_y()
{
    return VP.y + 0.5 * VP.h;
}

// Log10 x on [1, 100], Log2 y on [1, 16].
struct LogAxes
{
    Figure fig{{.width = 1000, .height = 500}};
    Axes&  ax;

    LogAxes() : ax(fig.subplot(1, 1, 1))
    {
        ax.xscale(ScaleType::Log10);
        ax.yscale(ScaleType::Log2);
        ax.xlim(1.0, 100.0);
        ax.ylim(1.0, 16.0);
        ax.set_viewport(VP);
    }
};

}   // namespace

// ─── Shared mapping ─────────────────────────────────────────────────────────

TEST(AxisMapping, LogAxesMapThroughAxisSpace)
{
    LogAxes           t;
    const AxisMapping m(t.ax);

    EXPECT_NEAR(m.to_screen_x(10.0), mid_x(), 1e-9);   // geometric mean
    EXPECT_NEAR(m.to_screen_y(4.0), mid_y(), 1e-9);    // 2^2 of 2^0..2^4
    EXPECT_NEAR(m.to_screen_y(2.0), VP.y + 0.75 * VP.h, 1e-9);
    EXPECT_NEAR(m.to_data_x(mid_x()), 10.0, 1e-9);
    EXPECT_NEAR(m.to_data_y(mid_y()), 4.0, 1e-9);

    for (double v : {1.0, 3.0, 42.0, 100.0})
        EXPECT_NEAR(m.to_data_x(m.to_screen_x(v)), v, 1e-9 * v);

    EXPECT_FALSE(m.x_drawable(0.0));
    EXPECT_FALSE(m.y_drawable(-1.0));
    EXPECT_TRUE(m.x_drawable(1e-30));
}

TEST(AxisMapping, LinearStaysLinear)
{
    const ScaleType   s = ScaleType::Linear;
    const AxisMapping m(VP, {0.0, 100.0}, {-1.0, 1.0}, s, s);
    EXPECT_NEAR(m.to_screen_x(50.0), mid_x(), 1e-9);
    EXPECT_NEAR(m.to_screen_y(0.0), mid_y(), 1e-9);
    EXPECT_NEAR(m.to_data_x(VP.x + 0.25 * VP.w), 25.0, 1e-9);
    EXPECT_TRUE(m.x_drawable(-5.0));
}

TEST(AxisMapping, SqrtMapsThroughSignedSquareRoot)
{
    const ScaleType   s = ScaleType::Sqrt;
    const AxisMapping m(VP, {0.0, 100.0}, {-1.0, 1.0}, s, s);
    EXPECT_NEAR(m.to_screen_x(25.0), mid_x(), 1e-9);
    EXPECT_NEAR(m.to_screen_y(0.0), mid_y(), 1e-9);
    EXPECT_NEAR(m.to_data_x(VP.x + 0.25 * VP.w), 6.25, 1e-9);
    EXPECT_NEAR(m.to_data_y(VP.y + 0.75 * VP.h), -0.25, 1e-9);
    // Negative values stay drawable (signed sqrt), left of zero.
    EXPECT_TRUE(m.x_drawable(-5.0));
    EXPECT_LT(m.to_screen_x(-5.0), VP.x);
}

TEST(AxisMapping, EpochScaleLinearKeepsPrecision)
{
    const double      t0 = 1.7e9;
    const AxisMapping m(VP, {t0, t0 + 1.0}, {0.0, 1.0});
    EXPECT_NEAR(m.to_screen_x(t0 + 0.5), mid_x(), 1e-4);
    EXPECT_NEAR(m.to_data_x(mid_x()), t0 + 0.5, 1e-6);
}

// ─── Inertial pan ───────────────────────────────────────────────────────────

TEST(LogOverlays, InertialPanGlidesInDecades)
{
    LogAxes             t;
    AnimationController ctrl;
    // +1 decade/s on x (log10), -1 octave/s on y (log2), for 0.5 s.
    ctrl.animate_inertial_pan(t.ax, 1.0f, -1.0f, 0.5f);
    for (int i = 0; i < 60; ++i)
        ctrl.update(1.0f / 60.0f);

    const auto xl = t.ax.x_limits();
    const auto yl = t.ax.y_limits();
    // The axis keeps its two-decade / four-octave span; nothing crosses zero.
    EXPECT_GT(xl.min, 1.0);
    EXPECT_NEAR(xl.max / xl.min, 100.0, 1e-6);
    EXPECT_GT(yl.min, 0.0);
    EXPECT_LT(yl.min, 1.0);
    EXPECT_NEAR(yl.max / yl.min, 16.0, 1e-6);
}

TEST(LogOverlays, TransitionEngineInertialPanKeepsLogRatios)
{
    LogAxes          t;
    TransitionEngine te;
    // A linear displacement of -20 data units/s would push [1, 100] below 0.
    te.animate_inertial_pan(t.ax, -2.0f, 0.0f, 0.5f);
    for (int i = 0; i < 60; ++i)
        te.update(1.0f / 60.0f);

    const auto xl = t.ax.x_limits();
    EXPECT_GT(xl.min, 0.0);
    EXPECT_LT(xl.min, 1.0);
    EXPECT_NEAR(xl.max / xl.min, 100.0, 1e-6);

    // Linear axes still move by data units.
    Axes lin;
    lin.xlim(0.0, 10.0);
    lin.ylim(0.0, 10.0);
    te.animate_inertial_pan(lin, 3.0f, 0.0f, 1.0f);
    for (int i = 0; i < 120; ++i)
        te.update(1.0f / 60.0f);
    EXPECT_NEAR(lin.x_limits().max - lin.x_limits().min, 10.0, 1e-4);
    EXPECT_GT(lin.x_limits().min, 0.5);
}

// ─── Linked axes ────────────────────────────────────────────────────────────

TEST(LogOverlays, LinkedZoomOnLogPeerKeepsRatiosAboutAnchor)
{
    Figure fig({.width = 800, .height = 600});
    Axes&  a = fig.subplot(2, 1, 1);
    Axes&  b = fig.subplot(2, 1, 2);
    for (Axes* ax : {&a, &b})
    {
        ax->xscale(ScaleType::Log10);
        ax->xlim(1.0, 100.0);
        ax->ylim(0.0, 1.0);
    }
    AxisLinkManager mgr;
    mgr.link(&a, &b, LinkAxis::X);

    // Zoom 2x about x = 10: [1, 100] → [10^0.5, 10^1.5] (the source does the
    // same through zoom_limits_about).  A linear zoom gives [5.5, 55].
    mgr.propagate_zoom(&a, 10.0, 0.5, 0.5);
    EXPECT_NEAR(b.x_limits().min, std::sqrt(10.0), 1e-9);
    EXPECT_NEAR(b.x_limits().max, 10.0 * std::sqrt(10.0), 1e-9);

    // Pan by one decade.
    mgr.propagate_pan(&a, -1.0, 0.0);
    EXPECT_NEAR(b.x_limits().min, std::sqrt(10.0) / 10.0, 1e-9);
    EXPECT_NEAR(b.x_limits().max, std::sqrt(10.0), 1e-9);
}

#ifdef SPECTRA_TEST_LOG_OVERLAYS

namespace
{

class LogOverlayImGuiTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        ImGui::CreateContext();
        ImGuiIO& io    = ImGui::GetIO();
        io.DisplaySize = ImVec2(1000.0f, 500.0f);
        io.DeltaTime   = 1.0f / 60.0f;
        io.Fonts->AddFontDefault();
        unsigned char* px = nullptr;
        int            w = 0, h = 0;
        io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
        ImGui::NewFrame();
        ui::ThemeManager::set_current(&theme_);   // AnnotationManager::add reads the accent
    }
    void TearDown() override
    {
        ui::ThemeManager::set_current(nullptr);
        ImGui::EndFrame();
        ImGui::DestroyContext();
    }

    ui::ThemeManager theme_;

    static void hover(DataInteraction& di, Figure& fig, double sx, double sy)
    {
        CursorReadout cur;
        cur.valid    = true;
        cur.screen_x = sx;
        cur.screen_y = sy;
        di.update(cur, fig);
    }
};

}   // namespace

TEST_F(LogOverlayImGuiTest, DataMarkerHitTestUsesLogScale)
{
    LogAxes           t;
    DataMarkerManager markers;
    // (10, 4) is drawn at the centre of the viewport.
    markers.toggle_or_add(10.0, 4.0, nullptr, 0, &t.ax);

    const AxisMapping m(t.ax);
    EXPECT_EQ(markers.hit_test(static_cast<float>(mid_x()), static_cast<float>(mid_y()), m, 6.0f),
              0);
    // A linear mapping would put it at x ≈ 100 + 800 * 9/99 = 172.7: no hit there.
    EXPECT_EQ(markers.hit_test(172.7f, static_cast<float>(mid_y()), m, 6.0f), -1);
}

TEST_F(LogOverlayImGuiTest, RegionSelectBoundsFollowLogScale)
{
    LogAxes           t;
    const AxisMapping m(t.ax);
    RegionSelect      rs;
    // Drag from the left quarter to the right quarter, bottom quarter to top quarter.
    rs.begin(VP.x + 0.25 * VP.w, VP.y + 0.75 * VP.h, m);
    rs.update_drag(VP.x + 0.75 * VP.w, VP.y + 0.25 * VP.h, m);

    EXPECT_NEAR(rs.data_x_min(), std::pow(10.0, 0.5), 1e-4);
    EXPECT_NEAR(rs.data_x_max(), std::pow(10.0, 1.5), 1e-3);
    EXPECT_NEAR(rs.data_y_min(), 2.0, 1e-5);
    EXPECT_NEAR(rs.data_y_max(), 8.0, 1e-5);
}

TEST_F(LogOverlayImGuiTest, AnnotationPlacedUnderCursorOnLogAxes)
{
    LogAxes         t;
    DataInteraction di;
    hover(di, t.fig, mid_x(), mid_y());
    ASSERT_TRUE(di.on_mouse_click_annotate(0, mid_x(), mid_y()));
    ASSERT_EQ(di.annotations().count(), 1u);
    const Annotation& a = di.annotations().annotations()[0];
    EXPECT_NEAR(a.data_x, 10.0, 1e-4);
    EXPECT_NEAR(a.data_y, 4.0, 1e-4);
}

TEST_F(LogOverlayImGuiTest, BoxSelectHitsPointsWhereTheyAreDrawn)
{
    LogAxes t;
    // One point at (10, 4): drawn at the viewport centre on these log axes,
    // but at x ≈ 172 (far left) with a linear mapping.
    std::vector<float> x = {10.0f};
    std::vector<float> y = {4.0f};
    t.ax.scatter(x, y);

    DataInteraction di;
    size_t          hits = 0;
    di.set_on_rect_series_selected([&](const auto& v) { hits = v.size(); });

    BoxZoomRect centre;
    centre.x0 = mid_x() - 10.0;
    centre.x1 = mid_x() + 10.0;
    centre.y0 = mid_y() - 10.0;
    centre.y1 = mid_y() + 10.0;
    di.select_series_in_rect(centre, t.fig);
    EXPECT_EQ(hits, 1u);

    BoxZoomRect linear_spot = centre;
    linear_spot.x0          = 160.0;
    linear_spot.x1          = 185.0;
    di.select_series_in_rect(linear_spot, t.fig);
    EXPECT_EQ(hits, 0u);
}

#endif   // SPECTRA_TEST_LOG_OVERLAYS
