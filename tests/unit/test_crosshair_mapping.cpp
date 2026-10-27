// Linked crosshair across subplots with nonlinear axis scales (audit S01).
//
// The multi-axes crosshair unprojects the cursor on the hovered axes and
// reprojects it onto every other subplot.  Both steps must go through the
// axis scale transform used by the renderer, otherwise on a log x-axis [1,100]
// the viewport midpoint reads 50.5 instead of 10 and a linked linear subplot
// draws its line at 50.5.

#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <string>

#include "ui/overlay/crosshair_mapping.hpp"
#include "ui/overlay/readout_format.hpp"

#if defined(SPECTRA_USE_IMGUI) && __has_include(<imgui.h>)
    #define SPECTRA_TEST_CROSSHAIR_DRAW 1
    #include <imgui.h>

    #include "ui/data/axis_link.hpp"
    #include "ui/input/input.hpp"
    #include "ui/overlay/crosshair.hpp"
    #include "ui/overlay/data_interaction.hpp"
    #include "ui/theme/theme.hpp"
#endif

using namespace spectra;
namespace cm = spectra::crosshair_mapping;

namespace
{

constexpr Rect TOP_VP{100.0f, 50.0f, 800.0f, 300.0f};
constexpr Rect BOTTOM_VP{100.0f, 450.0f, 800.0f, 300.0f};

// Two stacked subplots with explicit viewports (no renderer layout needed).
struct TwoAxes
{
    Figure fig{{.width = 1000, .height = 800}};
    Axes&  top;
    Axes&  bottom;

    TwoAxes() : top(fig.subplot(2, 1, 1)), bottom(fig.subplot(2, 1, 2))
    {
        top.set_viewport(TOP_VP);
        bottom.set_viewport(BOTTOM_VP);
    }
};

double mid_x(const Rect& vp)
{
    return vp.x + 0.5 * vp.w;
}
double mid_y(const Rect& vp)
{
    return vp.y + 0.5 * vp.h;
}

}   // namespace

// ─── Pure mapping ───────────────────────────────────────────────────────────

TEST(CrosshairMapping, LogXMidpointUnprojectsToGeometricMean)
{
    TwoAxes t;
    t.top.xscale(ScaleType::Log10);
    t.top.xlim(1.0, 100.0);
    EXPECT_NEAR(cm::screen_to_data_x(t.top, mid_x(TOP_VP)), 10.0, 1e-9);
    EXPECT_NEAR(cm::screen_to_data_x(t.top, TOP_VP.x), 1.0, 1e-9);
    EXPECT_NEAR(cm::screen_to_data_x(t.top, TOP_VP.x + TOP_VP.w), 100.0, 1e-9);
    // A quarter of the way along a two-decade axis is 10^0.5.
    EXPECT_NEAR(cm::screen_to_data_x(t.top, TOP_VP.x + 0.25 * TOP_VP.w), std::sqrt(10.0), 1e-9);
}

TEST(CrosshairMapping, LogToLinearLinkedXLandsAtTrueValue)
{
    TwoAxes t;
    t.top.xscale(ScaleType::Log10);
    t.top.xlim(1.0, 100.0);
    t.bottom.xlim(1.0, 100.0);   // linear

    const double x  = cm::screen_to_data_x(t.top, mid_x(TOP_VP));
    const double sx = cm::data_to_screen_x(t.bottom, x);
    // x = 10 on a linear [1,100] axis sits 9/99 of the way across.
    EXPECT_NEAR(sx, BOTTOM_VP.x + 9.0 / 99.0 * BOTTOM_VP.w, 1e-6);
    // The old affine mapping put it at the midpoint (≈ 392 px too far right).
    EXPECT_GT(std::abs(sx - mid_x(BOTTOM_VP)), 300.0);
}

TEST(CrosshairMapping, LinearToLogLinkedXLandsAtTrueValue)
{
    TwoAxes t;
    t.top.xlim(1.0, 100.0);   // linear
    t.bottom.xscale(ScaleType::Log10);
    t.bottom.xlim(1.0, 100.0);

    const double x = cm::screen_to_data_x(t.top, mid_x(TOP_VP));
    EXPECT_NEAR(x, 50.5, 1e-9);
    EXPECT_NEAR(cm::data_to_screen_x(t.bottom, x),
                BOTTOM_VP.x + std::log10(50.5) / 2.0 * BOTTOM_VP.w,
                1e-6);
}

TEST(CrosshairMapping, LogYUnprojectsAndReprojectsAcrossScales)
{
    TwoAxes t;
    t.top.yscale(ScaleType::Log10);
    t.top.ylim(0.01, 100.0);   // four decades
    t.bottom.yscale(ScaleType::Log2);
    t.bottom.ylim(1.0, 16.0);   // four octaves

    // Screen y grows downward: the vertical midpoint is 10^0 = 1.
    EXPECT_NEAR(cm::screen_to_data_y(t.top, mid_y(TOP_VP)), 1.0, 1e-9);
    // Three quarters up the top axes is 10^1.
    const double y = cm::screen_to_data_y(t.top, TOP_VP.y + 0.25 * TOP_VP.h);
    EXPECT_NEAR(y, 10.0, 1e-9);
    // 10 on a log2 [1,16] axis: log2(10)/4 of the way up.
    EXPECT_NEAR(cm::data_to_screen_y(t.bottom, y),
                BOTTOM_VP.y + (1.0 - std::log2(10.0) / 4.0) * BOTTOM_VP.h,
                1e-6);
}

TEST(CrosshairMapping, Log2ToLinearLinkedX)
{
    TwoAxes t;
    t.top.xscale(ScaleType::Log2);
    t.top.xlim(1.0, 256.0);
    t.bottom.xlim(0.0, 256.0);

    const double x = cm::screen_to_data_x(t.top, mid_x(TOP_VP));
    EXPECT_NEAR(x, 16.0, 1e-9);
    EXPECT_NEAR(cm::data_to_screen_x(t.bottom, x), BOTTOM_VP.x + 16.0 / 256.0 * BOTTOM_VP.w, 1e-6);
}

TEST(CrosshairMapping, RoundTripsOnEveryScale)
{
    for (ScaleType s : {ScaleType::Linear, ScaleType::Log10, ScaleType::Log2, ScaleType::Sqrt})
    {
        TwoAxes t;
        t.top.xscale(s);
        t.top.yscale(s);
        t.top.xlim(0.5, 2000.0);
        t.top.ylim(3.0, 7.0e5);
        for (double f : {0.0, 0.1, 0.37, 0.5, 0.9, 1.0})
        {
            const double sx = TOP_VP.x + f * TOP_VP.w;
            const double sy = TOP_VP.y + f * TOP_VP.h;
            EXPECT_NEAR(cm::data_to_screen_x(t.top, cm::screen_to_data_x(t.top, sx)), sx, 1e-6)
                << static_cast<int>(s) << " f=" << f;
            EXPECT_NEAR(cm::data_to_screen_y(t.top, cm::screen_to_data_y(t.top, sy)), sy, 1e-6)
                << static_cast<int>(s) << " f=" << f;
        }
    }
}

TEST(CrosshairMapping, SqrtMatchesRendererIdentityMapping)
{
    // core/axis_scale.hpp draws Sqrt axes untransformed; the crosshair must
    // agree with the renderer, not with an idealized sqrt mapping.
    TwoAxes t;
    t.top.xscale(ScaleType::Sqrt);
    t.top.xlim(0.0, 100.0);
    EXPECT_NEAR(cm::screen_to_data_x(t.top, mid_x(TOP_VP)), 50.0, 1e-9);
}

TEST(CrosshairMapping, NonPositiveValueOnLogTargetFallsOutsideViewport)
{
    TwoAxes t;
    t.bottom.xscale(ScaleType::Log10);
    t.bottom.xlim(1.0, 100.0);
    const double sx = cm::data_to_screen_x(t.bottom, -5.0);
    EXPECT_TRUE(std::isfinite(sx));
    EXPECT_LT(sx, BOTTOM_VP.x);
}

TEST(CrosshairMapping, DegenerateRangeStaysFinite)
{
    TwoAxes t;
    t.top.xlim(5.0, 5.0);
    t.top.ylim(5.0, 5.0);
    EXPECT_TRUE(std::isfinite(cm::data_to_screen_x(t.top, 5.0)));
    EXPECT_TRUE(std::isfinite(cm::data_to_screen_y(t.top, 5.0)));
    EXPECT_TRUE(std::isfinite(cm::screen_to_data_x(t.top, mid_x(TOP_VP))));
}

// ─── Compact crosshair label format ─────────────────────────────────────────

TEST(CrosshairLabelFormat, EpochValuesNeverOverflowTheLabel)
{
    using ui::format_compact_value;
    // Unix timestamps at second / centisecond / no resolution: <= 12 chars.
    for (double res : {0.0, 86.4, 1.0, 0.01, 1e-6})
    {
        for (double v : {1.7e9 + 43210.123, -1.7e9 - 0.5, 1.0e12 + 7.0, 3.0e20})
        {
            const std::string s = format_compact_value(v, res);
            EXPECT_LE(s.size(), 12u) << v << " res " << res << " -> " << s;
            EXPECT_NEAR(std::stod(s), v, std::max(std::abs(v) * 1e-5, res)) << s;
        }
    }
    // The previous fixed "%.3f" readout was 14 characters for an epoch.
    EXPECT_EQ(format_compact_value(1700043210.123, 86.4), "1700043210");
    EXPECT_EQ(format_compact_value(1700043210.123, 0.0), "1.70004e+09");
}

TEST(CrosshairLabelFormat, DigitsFollowPixelResolution)
{
    using ui::format_compact_value;
    EXPECT_EQ(format_compact_value(0.5, 0.001), "0.500");
    EXPECT_EQ(format_compact_value(12345.678, 0.1), "12345.7");
    EXPECT_EQ(format_compact_value(42.0, 0.0), "42");
    EXPECT_EQ(format_compact_value(2.5e-6, 1e-9), "2.5e-06");
    // Closer to zero than one pixel: reads as zero, not 1e-17.
    EXPECT_EQ(format_compact_value(1e-17, 0.002), "0.000");
    EXPECT_EQ(format_compact_value(0.0, 0.0), "0");
    EXPECT_EQ(format_compact_value(std::numeric_limits<double>::infinity()), "inf");
    EXPECT_EQ(format_compact_value(std::nan("")), "nan");
}

TEST(CrosshairLabelFormat, PixelResolutionFollowsScale)
{
    TwoAxes t;
    t.top.xlim(0.0, 800.0);   // 800 px wide viewport: 1 data unit per pixel
    EXPECT_NEAR(cm::pixel_resolution_x(t.top, mid_x(TOP_VP)), 1.0, 1e-9);
    t.top.xscale(ScaleType::Log10);
    t.top.xlim(1.0, 1e8);   // 8 decades over 800 px: finer near the left edge
    const double left  = cm::pixel_resolution_x(t.top, TOP_VP.x + 10.0);
    const double right = cm::pixel_resolution_x(t.top, TOP_VP.x + TOP_VP.w - 10.0);
    EXPECT_GT(right, left * 1e6);
    t.top.ylim(-1.0, 2.0);   // 300 px tall: 0.01 per pixel
    EXPECT_NEAR(cm::pixel_resolution_y(t.top, mid_y(TOP_VP)), 0.01, 1e-12);
}

// ─── Real Crosshair::draw_all_axes output ───────────────────────────────────

#ifdef SPECTRA_TEST_CROSSHAIR_DRAW
namespace
{

class CrosshairDrawTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        ImGui::CreateContext();
        ImGuiIO& io    = ImGui::GetIO();
        io.DisplaySize = ImVec2(1000.0f, 800.0f);
        io.DeltaTime   = 1.0f;   // fade straight to full opacity
        io.Fonts->AddFontDefault();
        unsigned char* px = nullptr;
        int            w = 0, h = 0;
        io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
        ImGui::NewFrame();
        theme_.ensure_initialized();
    }
    void TearDown() override
    {
        ImGui::EndFrame();
        ImGui::DestroyContext();
    }

    static std::unique_ptr<ImDrawList> new_list()
    {
        auto dl = std::make_unique<ImDrawList>(ImGui::GetDrawListSharedData());
        dl->_ResetForNewFrame();
        dl->PushClipRectFullScreen();
        dl->PushTexture(ImGui::GetIO().Fonts->TexRef);
        return dl;
    }

    // Mean x of the vertices strictly inside vp's vertical extent: with the
    // linked horizontal line pushed out of vp, only the vertical line is there.
    static double vertical_line_x(const ImDrawList& dl, const Rect& vp)
    {
        double sum = 0.0;
        int    n   = 0;
        for (const ImDrawVert& v : dl.VtxBuffer)
        {
            if (v.pos.y > vp.y + 2.0f && v.pos.y < vp.y + vp.h - 2.0f && v.pos.x >= vp.x
                && v.pos.x <= vp.x + vp.w)
            {
                sum += v.pos.x;
                ++n;
            }
        }
        return n > 0 ? sum / n : std::numeric_limits<double>::quiet_NaN();
    }

    ui::ThemeManager theme_;
};

}   // namespace

TEST_F(CrosshairDrawTest, LinkedLinearSubplotLineFollowsLogSourceValue)
{
    TwoAxes t;
    t.top.xscale(ScaleType::Log10);
    t.top.xlim(1.0, 100.0);
    t.top.ylim(0.0, 1.0);
    t.bottom.xlim(1.0, 100.0);
    t.bottom.ylim(100.0, 200.0);   // keeps the linked horizontal line out of view

    Crosshair ch;
    ch.set_theme_manager(&theme_);
    ch.set_enabled(true);

    CursorReadout cur;
    cur.valid    = true;
    cur.screen_x = mid_x(TOP_VP);
    cur.screen_y = mid_y(TOP_VP);
    cur.data_x   = 10.0;
    cur.data_y   = 0.5;

    auto dl = new_list();
    ch.draw_all_axes(cur, t.fig, nullptr, dl.get());
    ASSERT_GT(dl->VtxBuffer.Size, 0);

    const double expected = BOTTOM_VP.x + 9.0 / 99.0 * BOTTOM_VP.w;   // x = 10
    EXPECT_NEAR(vertical_line_x(*dl, BOTTOM_VP), expected, 1.0);
}

TEST_F(CrosshairDrawTest, LinkedLogSubplotLineFollowsLinearSourceValue)
{
    TwoAxes t;
    t.top.xlim(1.0, 100.0);
    t.top.ylim(0.0, 1.0);
    t.bottom.xscale(ScaleType::Log10);
    t.bottom.xlim(1.0, 100.0);
    t.bottom.ylim(100.0, 200.0);

    Crosshair ch;
    ch.set_theme_manager(&theme_);
    ch.set_enabled(true);

    CursorReadout cur;
    cur.valid    = true;
    cur.screen_x = TOP_VP.x + 9.0 / 99.0 * TOP_VP.w;   // linear x = 10
    cur.screen_y = mid_y(TOP_VP);
    cur.data_x   = 10.0;
    cur.data_y   = 0.5;

    auto dl = new_list();
    ch.draw_all_axes(cur, t.fig, nullptr, dl.get());
    ASSERT_GT(dl->VtxBuffer.Size, 0);

    EXPECT_NEAR(vertical_line_x(*dl, BOTTOM_VP), mid_x(BOTTOM_VP), 1.0);   // log midpoint = 10
}

TEST_F(CrosshairDrawTest, SharedCursorBroadcastUsesLogScale)
{
    TwoAxes t;
    t.top.xscale(ScaleType::Log10);
    t.top.yscale(ScaleType::Log10);
    t.top.xlim(1.0, 100.0);
    t.top.ylim(1.0, 10000.0);

    AxisLinkManager link;
    link.link(&t.top, &t.bottom, LinkAxis::X);
    DataInteraction di;
    di.set_theme_manager(&theme_);
    di.set_axis_link_manager(&link);

    CursorReadout cur;
    cur.valid    = true;
    cur.screen_x = mid_x(TOP_VP);
    cur.screen_y = mid_y(TOP_VP);
    cur.data_x   = 10.0;
    cur.data_y   = 100.0;
    di.update(cur, t.fig);

    const SharedCursor sc = link.shared_cursor_for(&t.bottom);
    ASSERT_TRUE(sc.valid);
    EXPECT_NEAR(sc.data_x, 10.0, 1e-4);
    EXPECT_NEAR(sc.data_y, 100.0, 1e-3);
}
#endif
