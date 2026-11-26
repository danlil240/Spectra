// Layout hold (Axes::LayoutHold, layout_figure): while a view scrolls or
// zooms, the room reserved for tick labels must not shrink and grow with the
// label positions, or the plot areas resize by a pixel or two every frame.
// At rest, and for a single change, the layout is exactly the plain one.

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <memory>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <vector>

#include "core/figure_layout.hpp"
#include "core/layout.hpp"

using namespace spectra;
using Clock = std::chrono::steady_clock;

namespace
{

constexpr uint32_t W = 900;
constexpr uint32_t H = 600;

const std::vector<float> X{0.0f, 5.0f, 10.0f, 15.0f, 20.0f};
const std::vector<float> Y{0.0f, 1.0f, 4.0f, 9.0f, 16.0f};

std::chrono::milliseconds ms(int v)
{
    return std::chrono::milliseconds(v);
}

void layout_at(Figure& fig, Clock::time_point t)
{
    Margins m;
    m.left   = fig.style().margin_left;
    m.right  = fig.style().margin_right;
    m.top    = fig.style().margin_top;
    m.bottom = fig.style().margin_bottom;
    layout_figure(fig, Rect{0.0f, 0.0f, float(W), float(H)}, m, 0.0f, 1.0f, t);
}

std::vector<Rect> viewports(const Figure& fig)
{
    std::vector<Rect> out;
    for (Axes* a : axes_with_twins(fig))
        out.push_back(a->viewport());
    return out;
}

bool same(const std::vector<Rect>& a, const std::vector<Rect>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].w != b[i].w || a[i].h != b[i].h)
            return false;
    return true;
}

// 1x2 grid with no gap of its own: the column gap is exactly what the x tick
// label overhang of the left plot and the y labels of the right plot need.
std::unique_ptr<Figure> scrolling_pair(double d)
{
    auto fig                  = std::make_unique<Figure>(FigureConfig{.width = W, .height = H});
    fig->style().subplot_hgap = 0.0f;
    auto& a                   = fig->subplot(1, 2, 1);
    a.plot(X, Y);
    a.xlabel("time (s)");
    a.xlim(0.0 + d, 10.0 + d);   // ticks every 2: "10" slides off the right edge
    a.ylim(0.0, 16.0);
    auto& b = fig->subplot(1, 2, 2);
    b.plot(X, Y);
    b.ylabel("value");
    return fig;
}

// Fresh figure laid out once at scroll offset d.
std::vector<Rect> fresh_scrolling_pair(double d)
{
    auto fig = scrolling_pair(d);
    layout_at(*fig, Clock::now());
    return viewports(*fig);
}

}   // namespace

// Precondition: without a hold the plain layout really does change with the
// scroll offset (the right overhang of "10" shrinks as it slides left).
TEST(LayoutHold, PlainLayoutDependsOnTheScrollOffset)
{
    EXPECT_FALSE(same(fresh_scrolling_pair(0.0), fresh_scrolling_pair(0.6)));
}

TEST(LayoutHold, ScrollingKeepsViewportsConstantAfterTwoFrames)
{
    auto              fig = scrolling_pair(0.0);
    Clock::time_point t   = Clock::now();
    layout_at(*fig, t);
    std::vector<std::vector<Rect>> frames;
    for (int k = 1; k <= 95; ++k)
    {
        t += ms(16);
        fig->axes()[0]->xlim(0.02 * k, 10.0 + 0.02 * k);
        layout_at(*fig, t);
        frames.push_back(viewports(*fig));
    }
    for (size_t k = 2; k < frames.size(); ++k)
        EXPECT_TRUE(same(frames[k], frames[1])) << "frame " << k;
}

TEST(LayoutHold, AtRestTheLayoutEqualsAFreshFigure)
{
    auto              fig = scrolling_pair(0.0);
    Clock::time_point t   = Clock::now();
    layout_at(*fig, t);
    for (int k = 1; k <= 40; ++k)
    {
        t += ms(16);
        fig->axes()[0]->xlim(0.02 * k, 10.0 + 0.02 * k);
        layout_at(*fig, t);
    }
    // Still within the settle time: the hold is on.
    t += ms(200);
    layout_at(*fig, t);
    EXPECT_FALSE(same(viewports(*fig), fresh_scrolling_pair(0.8)));
    // 0.4 s after the last change: exactly the plain layout again.
    t += ms(250);
    layout_at(*fig, t);
    EXPECT_TRUE(same(viewports(*fig), fresh_scrolling_pair(0.8)));
}

TEST(LayoutHold, SingleLimitsChangeGivesTheFreshLayout)
{
    auto              fig = scrolling_pair(0.0);
    Clock::time_point t   = Clock::now();
    layout_at(*fig, t);
    t += ms(16);
    fig->axes()[0]->xlim(0.6, 10.6);
    layout_at(*fig, t);
    EXPECT_TRUE(same(viewports(*fig), fresh_scrolling_pair(0.6)));
    // Repeated layouts without a change stay the same.
    t += ms(16);
    layout_at(*fig, t);
    EXPECT_TRUE(same(viewports(*fig), fresh_scrolling_pair(0.6)));
}

TEST(LayoutHold, FirstLayoutIsUnaffectedAndLayoutsAreIdempotent)
{
    auto              fig = scrolling_pair(0.3);
    Clock::time_point t   = Clock::now();
    layout_at(*fig, t);
    const auto first = viewports(*fig);
    EXPECT_TRUE(same(first, fresh_scrolling_pair(0.3)));
    layout_at(*fig, t);
    EXPECT_TRUE(same(viewports(*fig), first));
}

// A zoom in y keeps changing the widest y tick label ("-1000", "-600",
// "-0.25" ...): while zooming the left margin may grow for a wider label but
// never shrinks back, so it cannot flicker.
TEST(LayoutHold, YZoomDoesNotShrinkTheLeftMarginWhileMoving)
{
    Figure fig({.width = W, .height = H});
    auto&  a = fig.subplot(1, 1, 1);
    a.plot(X, Y);
    a.ylabel("value");
    a.xlim(0.0, 20.0);
    Clock::time_point t = Clock::now();
    a.ylim(-1000.0, 1000.0);
    layout_at(fig, t);
    std::vector<float> xs;
    for (int k = 1; k <= 60; ++k)
    {
        t += ms(16);
        const double s = 1000.0 * std::pow(0.9, k);   // zoom in towards 0
        a.ylim(-s, s);
        layout_at(fig, t);
        xs.push_back(a.viewport().x);
    }
    for (size_t k = 2; k < xs.size(); ++k)
        EXPECT_GE(xs[k], xs[k - 1]) << "frame " << k;
    // ...and the plain layout at the final zoom has a narrower margin.
    t += ms(500);
    layout_at(fig, t);
    EXPECT_LT(a.viewport().x, xs.back());
}

TEST(LayoutHold, PlainLayoutMarginFlickersDuringYZoom)
{
    // Without the hold (a fresh figure per frame) the left margin goes up and
    // down during the same sweep.
    int   ups = 0, downs = 0;
    float prev = -1.0f;
    for (int k = 1; k <= 60; ++k)
    {
        Figure fig({.width = W, .height = H});
        auto&  a = fig.subplot(1, 1, 1);
        a.plot(X, Y);
        a.ylabel("value");
        a.xlim(0.0, 20.0);
        const double s = 1000.0 * std::pow(0.9, k);
        a.ylim(-s, s);
        layout_at(fig, Clock::now());
        if (prev >= 0.0f && a.viewport().x > prev)
            ++ups;
        if (prev >= 0.0f && a.viewport().x < prev)
            ++downs;
        prev = a.viewport().x;
    }
    EXPECT_GT(ups, 0);
    EXPECT_GT(downs, 0);
}

// The right y axis of a twinx() twin: its tick label room is held too.
TEST(LayoutHold, TwinxRightLabelsAreHeld)
{
    Figure fig({.width = W, .height = H});
    auto&  a = fig.subplot(1, 1, 1);
    a.plot(X, Y);
    a.xlim(0.0, 20.0);
    a.ylim(0.0, 16.0);
    auto& tw = a.twinx();
    tw.plot(X, Y);
    tw.ylabel("voltage");
    Clock::time_point t = Clock::now();
    tw.ylim(-1000.0, 1000.0);
    layout_at(fig, t);
    std::vector<float> ws;
    for (int k = 1; k <= 60; ++k)
    {
        t += ms(16);
        const double s = 1000.0 * std::pow(0.9, k);
        tw.ylim(-s, s);
        layout_at(fig, t);
        ws.push_back(a.viewport().w);
    }
    for (size_t k = 2; k < ws.size(); ++k)
        EXPECT_LE(ws[k], ws[k - 1]) << "frame " << k;
    t += ms(500);
    layout_at(fig, t);
    EXPECT_GT(a.viewport().w, ws.back());
}

// A change of text scale or figure size resets the hold: the next layout is
// the plain one for the new size.
TEST(LayoutHold, SizeChangeResetsTheHold)
{
    auto              fig = scrolling_pair(0.0);
    Clock::time_point t   = Clock::now();
    layout_at(*fig, t);
    for (int k = 1; k <= 30; ++k)
    {
        t += ms(16);
        fig->axes()[0]->xlim(0.02 * k, 10.0 + 0.02 * k);
        layout_at(*fig, t);
    }
    // Lay out at text scale 2 (a UI scale change) without changing limits.
    Margins m;
    m.left   = fig->style().margin_left;
    m.right  = fig->style().margin_right;
    m.top    = fig->style().margin_top;
    m.bottom = fig->style().margin_bottom;
    t += ms(16);
    layout_figure(*fig, Rect{0.0f, 0.0f, float(W), float(H)}, m, 0.0f, 2.0f, t);
    auto fresh = scrolling_pair(0.6);
    layout_figure(*fresh, Rect{0.0f, 0.0f, float(W), float(H)}, m, 0.0f, 2.0f, t);
    EXPECT_TRUE(same(viewports(*fig), viewports(*fresh)));
}

// An app that renders on demand may run no layout while the view rests, so
// the hold of an earlier motion must not survive into the next one.
TEST(LayoutHold, ChangeAfterAPauseStartsFromTheFreshLayout)
{
    auto              fig = scrolling_pair(0.0);
    Clock::time_point t   = Clock::now();
    layout_at(*fig, t);
    for (int k = 1; k <= 30; ++k)
    {
        t += ms(16);
        fig->axes()[0]->xlim(0.02 * k, 10.0 + 0.02 * k);
        layout_at(*fig, t);
    }
    // No layout while at rest; the next one comes with new limits.
    t += ms(5000);
    fig->axes()[0]->xlim(1.2, 11.2);
    layout_at(*fig, t);
    EXPECT_TRUE(same(viewports(*fig), fresh_scrolling_pair(1.2)));
    EXPECT_FALSE(fig->axes()[0]->layout_hold().moving);
}
