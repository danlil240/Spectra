// test_series_anim_callback.cpp — timeline callbacks bound to a figure's series
// (the inproc App's demo animation channels) must not touch the series once
// the figure is closed or the series removed.  The QA soak (animation_stress
// --repeat) hit a heap-use-after-free here: the callbacks captured raw Series*
// of the start-up figure and kept writing opacity/width after it was closed.

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/series.hpp>

#include "ui/app/window_ui_context_runtime.hpp"

using namespace spectra;

namespace
{

struct Fixture
{
    FigureRegistry     registry;
    FigureId           id    = INVALID_FIGURE_ID;
    LineSeries*        line  = nullptr;
    LineSeries*        other = nullptr;
    int                calls = 0;
    std::vector<float> x{0.0f, 1.0f}, y{0.0f, 1.0f};

    Fixture()
    {
        auto  fig = std::make_unique<Figure>();
        auto& ax  = fig->subplot(1, 1, 1);
        line      = &ax.line(x, y);
        other     = &ax.line(x, y);
        id        = registry.register_figure(std::move(fig));
    }

    std::function<void(float)> opacity_cb(const Series* s)
    {
        return make_series_anim_callback(registry,
                                         id,
                                         s,
                                         [this](Series& live, float v)
                                         {
                                             ++calls;
                                             live.opacity(v);
                                         });
    }
};

}   // namespace

TEST(SeriesAnimCallback, DrivesTheLiveSeries)
{
    Fixture f;
    auto    cb = f.opacity_cb(f.line);
    cb(0.25f);
    EXPECT_EQ(f.calls, 1);
    EXPECT_FLOAT_EQ(f.line->opacity(), 0.25f);
    EXPECT_FLOAT_EQ(f.other->opacity(), 1.0f);
}

TEST(SeriesAnimCallback, NoOpAfterFigureIsClosed)
{
    Fixture f;
    auto    cb = f.opacity_cb(f.line);
    f.registry.unregister_figure(f.id);   // destroys the figure and its series
    cb(0.5f);                             // ASan: must not touch the freed series
    EXPECT_EQ(f.calls, 0);
}

TEST(SeriesAnimCallback, NoOpAfterSeriesIsRemoved)
{
    Fixture f;
    auto    cb_line  = f.opacity_cb(f.line);
    auto    cb_other = f.opacity_cb(f.other);
    Figure* fig      = f.registry.get(f.id);
    ASSERT_NE(fig, nullptr);
    ASSERT_TRUE(fig->axes()[0]->remove_series(0));   // removes `line`
    cb_line(0.5f);
    EXPECT_EQ(f.calls, 0);
    cb_other(0.75f);
    EXPECT_EQ(f.calls, 1);
    EXPECT_FLOAT_EQ(f.other->opacity(), 0.75f);
}

TEST(SeriesAnimCallback, NullSeriesIsIgnored)
{
    Fixture f;
    auto    cb = f.opacity_cb(nullptr);
    cb(0.5f);
    EXPECT_EQ(f.calls, 0);
}
