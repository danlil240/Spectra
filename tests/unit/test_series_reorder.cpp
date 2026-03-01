#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <spectra/series.hpp>

using namespace spectra;

TEST(SeriesReorder, MoveForward)
{
    Axes ax;
    auto& s0 = ax.line();
    s0.label("A");
    auto& s1 = ax.line();
    s1.label("B");
    auto& s2 = ax.line();
    s2.label("C");

    // Move A (index 0) to index 2
    EXPECT_TRUE(ax.move_series(0, 2));
    EXPECT_EQ(ax.series()[0]->label(), "B");
    EXPECT_EQ(ax.series()[1]->label(), "C");
    EXPECT_EQ(ax.series()[2]->label(), "A");
}

TEST(SeriesReorder, MoveBackward)
{
    Axes ax;
    auto& s0 = ax.line();
    s0.label("A");
    auto& s1 = ax.line();
    s1.label("B");
    auto& s2 = ax.line();
    s2.label("C");

    // Move C (index 2) to index 0
    EXPECT_TRUE(ax.move_series(2, 0));
    EXPECT_EQ(ax.series()[0]->label(), "C");
    EXPECT_EQ(ax.series()[1]->label(), "A");
    EXPECT_EQ(ax.series()[2]->label(), "B");
}

TEST(SeriesReorder, MoveToSameIndex)
{
    Axes ax;
    ax.line().label("A");
    ax.line().label("B");

    // No-op: move to same index returns false
    EXPECT_FALSE(ax.move_series(0, 0));
    EXPECT_EQ(ax.series()[0]->label(), "A");
    EXPECT_EQ(ax.series()[1]->label(), "B");
}

TEST(SeriesReorder, OutOfRange)
{
    Axes ax;
    ax.line().label("A");

    EXPECT_FALSE(ax.move_series(0, 5));
    EXPECT_FALSE(ax.move_series(5, 0));
    EXPECT_EQ(ax.series().size(), 1u);
    EXPECT_EQ(ax.series()[0]->label(), "A");
}

TEST(SeriesReorder, AdjacentSwap)
{
    Axes ax;
    ax.line().label("X");
    ax.line().label("Y");

    EXPECT_TRUE(ax.move_series(0, 1));
    EXPECT_EQ(ax.series()[0]->label(), "Y");
    EXPECT_EQ(ax.series()[1]->label(), "X");
}

TEST(SeriesReorder, EmptyAxes)
{
    Axes ax;
    EXPECT_FALSE(ax.move_series(0, 1));
}
