#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <spectra/series.hpp>

#include "math/expression_eval.hpp"
#include "ui/plot/plot_annotations.hpp"

TEST(PlotAnnotations, HorizontalReferenceLine)
{
    spectra::Axes ax;
    auto&         line = spectra::ui::add_horizontal_reference_line(ax, 0.0f, "y = 0");
    ASSERT_EQ(line.point_count(), 2u);
    EXPECT_FLOAT_EQ(line.y_data()[0], 0.0f);
    EXPECT_FLOAT_EQ(line.y_data()[1], 0.0f);
}

TEST(PlotAnnotations, VerticalReferenceLine)
{
    spectra::Axes ax;
    auto&         line = spectra::ui::add_vertical_reference_line(ax, 1.5f);
    ASSERT_EQ(line.point_count(), 2u);
    EXPECT_FLOAT_EQ(line.x_data()[0], 1.5f);
    EXPECT_FLOAT_EQ(line.x_data()[1], 1.5f);
}

TEST(PlotAnnotations, FunctionPlotParabola)
{
    spectra::Axes ax;
    auto          info = spectra::parse_expression("x^2");
    ASSERT_TRUE(info.ast != nullptr);
    auto& line = spectra::ui::add_function_plot(ax, *info.ast, -2.0f, 2.0f, 5, "x^2");
    ASSERT_EQ(line.point_count(), 5u);
    EXPECT_FLOAT_EQ(line.x_data()[0], -2.0f);
    EXPECT_FLOAT_EQ(line.y_data()[0], 4.0f);
    EXPECT_FLOAT_EQ(line.y_data()[2], 0.0f);
}
