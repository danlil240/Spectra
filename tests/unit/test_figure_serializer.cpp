#include <cmath>
#include <cstdio>
#include <filesystem>
#include <gtest/gtest.h>
#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>
#include <spectra/series_stats.hpp>
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

TEST_F(FigureSerializerTest, RoundTripsScatterColormapAndUncertaintyBand)
{
    Figure             src({800, 600});
    auto&              axes   = src.subplot(1, 1, 1);
    std::vector<float> x      = {0.0f, 1.0f, 2.0f};
    std::vector<float> y      = {2.0f, 3.0f, 4.0f};
    std::vector<float> values = {10.0f, 20.0f, 30.0f};
    axes.scatter(x, y)
        .label("temperature")
        .color_values(values)
        .colormap(ColormapType::Plasma)
        .colormap_range(0.0f, 40.0f);

    std::vector<float> lower = {1.0f, 1.5f, 2.0f};
    std::vector<float> upper = {3.0f, 4.5f, 6.0f};
    axes.band(x, lower, upper)
        .label("95% CI")
        .fill_opacity(0.35f)
        .edge_width(2.0f)
        .show_edges(false);

    ASSERT_TRUE(FigureSerializer::save(tmp_path_, src));
    Figure dst({640, 480});
    ASSERT_TRUE(FigureSerializer::load(tmp_path_, dst));
    ASSERT_EQ(dst.axes().size(), 1u);
    ASSERT_EQ(dst.axes()[0]->series().size(), 2u);

    auto* scatter = dynamic_cast<ScatterSeries*>(dst.axes()[0]->series()[0].get());
    ASSERT_NE(scatter, nullptr);
    EXPECT_TRUE(scatter->has_colormap());
    EXPECT_EQ(scatter->colormap(), ColormapType::Plasma);
    EXPECT_FLOAT_EQ(scatter->colormap_min(), 0.0f);
    EXPECT_FLOAT_EQ(scatter->colormap_max(), 40.0f);
    EXPECT_EQ(scatter->color_values_data().size(), 3u);

    auto* band = dynamic_cast<BandSeries*>(dst.axes()[0]->series()[1].get());
    ASSERT_NE(band, nullptr);
    EXPECT_EQ(band->sample_count(), 3u);
    EXPECT_FLOAT_EQ(band->fill_opacity(), 0.35f);
    EXPECT_FLOAT_EQ(band->edge_width(), 2.0f);
    EXPECT_FALSE(band->show_edges());
}

TEST_F(FigureSerializerTest, RoundTripsFigureBackgroundOverride)
{
    Figure src({800, 600});
    src.subplot(1, 1, 1);
    src.style().background          = Color(0.2f, 0.3f, 0.4f, 1.0f);
    src.style().background_override = true;
    ASSERT_TRUE(FigureSerializer::save(tmp_path_, src));

    Figure dst({640, 480});
    ASSERT_TRUE(FigureSerializer::load(tmp_path_, dst));
    EXPECT_TRUE(dst.style().background_override);
    EXPECT_FLOAT_EQ(dst.style().background.g, 0.3f);

    // A figure that follows the theme stays on "auto" after a round trip.
    Figure auto_src({800, 600});
    auto_src.subplot(1, 1, 1);
    ASSERT_TRUE(FigureSerializer::save(tmp_path_, auto_src));
    Figure auto_dst({640, 480});
    auto_dst.style().background_override = true;
    ASSERT_TRUE(FigureSerializer::load(tmp_path_, auto_dst));
    EXPECT_FALSE(auto_dst.style().background_override);
}

TEST_F(FigureSerializerTest, RoundTrips3DBoxStyleAndSurfaceColormapRange)
{
    Figure             src({800, 600});
    auto&              ax = src.subplot3d(1, 2, 1);
    std::vector<float> xg = {0.0f, 1.0f};
    std::vector<float> yg = {0.0f, 1.0f};
    std::vector<float> z  = {2.0f, 3.0f, 4.0f, 5.0f};
    ax.box_style(BoxStyle::Full);
    ax.surface(xg, yg, z).colormap(ColormapType::Viridis).colormap_range(0.0f, 10.0f);
    ax.surface(xg, yg, z).colormap(ColormapType::Plasma);   // automatic range
    auto& ax2 = src.subplot3d(1, 2, 2);
    ax2.box_style(BoxStyle::None);

    ASSERT_TRUE(FigureSerializer::save(tmp_path_, src));
    Figure dst({640, 480});
    ASSERT_TRUE(FigureSerializer::load(tmp_path_, dst));
    ASSERT_EQ(dst.all_axes().size(), 2u);

    auto* a1 = dynamic_cast<Axes3D*>(dst.all_axes()[0].get());
    auto* a2 = dynamic_cast<Axes3D*>(dst.all_axes()[1].get());
    ASSERT_NE(a1, nullptr);
    ASSERT_NE(a2, nullptr);
    EXPECT_EQ(a1->box_style(), BoxStyle::Full);
    EXPECT_EQ(a2->box_style(), BoxStyle::None);

    ASSERT_EQ(a1->series().size(), 2u);
    auto* explicit_range = dynamic_cast<SurfaceSeries*>(a1->series()[0].get());
    auto* auto_range     = dynamic_cast<SurfaceSeries*>(a1->series()[1].get());
    ASSERT_NE(explicit_range, nullptr);
    ASSERT_NE(auto_range, nullptr);
    EXPECT_TRUE(explicit_range->colormap_range_set());
    EXPECT_FLOAT_EQ(explicit_range->colormap_min(), 0.0f);
    EXPECT_FLOAT_EQ(explicit_range->colormap_max(), 10.0f);
    EXPECT_FALSE(auto_range->colormap_range_set());
    EXPECT_FLOAT_EQ(auto_range->colormap_min(), 2.0f);
    EXPECT_FLOAT_EQ(auto_range->colormap_max(), 5.0f);
}

TEST_F(FigureSerializerTest, RoundTrips3DDepthCueLightAndSurfaceStyle)
{
    Figure             src({800, 600});
    auto&              ax = src.subplot3d(1, 1, 1);
    std::vector<float> g  = {0.0f, 1.0f, 2.0f};
    std::vector<float> z  = {0.0f, 1.0f, 2.0f, 1.0f, 2.0f, 3.0f, 2.0f, 3.0f, 4.0f};
    ax.light_mode(LightMode::Headlight);
    ax.fog(0.3f);
    ax.scatter3d(g, g, g).depth_shade(false);
    ax.line3d(g, g, g).depth_shade(true);
    ax.surface(g, g, z)
        .show_edges(true)
        .edge_color(Color(1.0f, 0.0f, 0.0f, 0.5f))
        .contour_projection(true)
        .contour_levels(7);
    ax.surface(g, g, z);   // defaults

    ASSERT_TRUE(FigureSerializer::save(tmp_path_, src));
    Figure dst({640, 480});
    ASSERT_TRUE(FigureSerializer::load(tmp_path_, dst));
    auto* a = dynamic_cast<Axes3D*>(dst.all_axes()[0].get());
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->light_mode(), LightMode::Headlight);
    EXPECT_FLOAT_EQ(a->fog(), 0.3f);
    ASSERT_EQ(a->series().size(), 4u);
    auto* sc = dynamic_cast<ScatterSeries3D*>(a->series()[0].get());
    auto* ln = dynamic_cast<LineSeries3D*>(a->series()[1].get());
    auto* sf = dynamic_cast<SurfaceSeries*>(a->series()[2].get());
    auto* sd = dynamic_cast<SurfaceSeries*>(a->series()[3].get());
    ASSERT_TRUE(sc && ln && sf && sd);
    EXPECT_FALSE(sc->depth_shade());
    EXPECT_TRUE(ln->depth_shade());
    EXPECT_TRUE(sf->show_edges());
    EXPECT_FLOAT_EQ(sf->edge_color().r, 1.0f);
    EXPECT_FLOAT_EQ(sf->edge_color().a, 0.5f);
    EXPECT_TRUE(sf->contour_projection());
    EXPECT_EQ(sf->contour_levels(), 7);
    EXPECT_FALSE(sd->show_edges());
    EXPECT_FALSE(sd->contour_projection());
}
