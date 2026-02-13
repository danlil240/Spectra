#include <gtest/gtest.h>

#include <plotix/series.hpp>

using namespace plotix;

TEST(SeriesVisibility, DefaultVisible) {
    LineSeries ls;
    EXPECT_TRUE(ls.visible());
}

TEST(SeriesVisibility, SetInvisible) {
    LineSeries ls;
    ls.visible(false);
    EXPECT_FALSE(ls.visible());
}

TEST(SeriesVisibility, ToggleBack) {
    LineSeries ls;
    ls.visible(false);
    EXPECT_FALSE(ls.visible());
    ls.visible(true);
    EXPECT_TRUE(ls.visible());
}

TEST(SeriesVisibility, ScatterDefaultVisible) {
    ScatterSeries ss;
    EXPECT_TRUE(ss.visible());
}

TEST(SeriesVisibility, ScatterSetInvisible) {
    ScatterSeries ss;
    ss.visible(false);
    EXPECT_FALSE(ss.visible());
}

TEST(SeriesVisibility, FluentChaining) {
    float x[] = {1.0f, 2.0f};
    float y[] = {3.0f, 4.0f};
    LineSeries ls(std::span<const float>(x, 2), std::span<const float>(y, 2));
    ls.label("test").color(colors::red);
    ls.visible(false);
    const Series& base = ls;
    EXPECT_EQ(base.label(), "test");
    EXPECT_FALSE(ls.visible());
}
