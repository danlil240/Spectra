#include <cmath>
#include <cstdio>
#include <filesystem>
#include <gtest/gtest.h>
#include <spectra/figure.hpp>
#include <vector>

#include "ui/workspace/figure_serializer.hpp"

using namespace spectra;

class FigureSerializerTest : public ::testing::Test
{
   protected:
    std::string tmp_path_;

    void SetUp() override
    {
        tmp_path_ =
            (std::filesystem::temp_directory_path() / "spectra_test_figure_serializer.spectra")
                .string();
    }

    void TearDown() override { std::remove(tmp_path_.c_str()); }
};

TEST_F(FigureSerializerTest, SaveLoadRestores2DAxesAndSeries)
{
    Figure src({800, 600});
    auto&  ax = src.subplot(1, 1, 1);

    std::vector<float> x(80), y1(80), y2(80);
    for (int i = 0; i < 80; ++i)
    {
        x[i]  = static_cast<float>(i) * 0.1f;
        y1[i] = std::sin(x[i]);
        y2[i] = std::cos(x[i]);
    }

    ax.line(x, y1).label("sin");
    ax.scatter(x, y2).label("cos");
    ax.title("Serialization Test");
    ax.xlabel("X");
    ax.ylabel("Y");

    ASSERT_TRUE(FigureSerializer::save(tmp_path_, src));

    Figure dst({640, 480});
    dst.subplot(1, 1, 1);   // Ensure load clears existing content before restoring.

    ASSERT_TRUE(FigureSerializer::load(tmp_path_, dst));
    ASSERT_FALSE(dst.axes().empty());
    ASSERT_NE(dst.axes()[0], nullptr);
    EXPECT_EQ(dst.axes().size(), 1u);

    const auto& loaded_ax = *dst.axes()[0];
    EXPECT_EQ(loaded_ax.title(), "Serialization Test");
    EXPECT_EQ(loaded_ax.xlabel(), "X");
    EXPECT_EQ(loaded_ax.ylabel(), "Y");
    EXPECT_EQ(loaded_ax.series().size(), 2u);
    EXPECT_EQ(loaded_ax.series()[0]->label(), "sin");
    EXPECT_EQ(loaded_ax.series()[1]->label(), "cos");
}
