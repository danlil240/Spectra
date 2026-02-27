#include <gtest/gtest.h>

#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>

// DataEditor is behind SPECTRA_USE_IMGUI guard, so we test the data model
// aspects that don't require ImGui.  The UI rendering is verified manually.

using namespace spectra;

// ─── Test Fixtures ──────────────────────────────────────────────────────────

class DataEditorDataTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        fig_ = std::make_unique<Figure>();
    }

    std::unique_ptr<Figure> fig_;
};

// ─── 2D Series Data Access ──────────────────────────────────────────────────

TEST_F(DataEditorDataTest, LineSeries2DDataAccess)
{
    auto& ax = fig_->subplot(1, 1, 1);
    std::vector<float> x = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    std::vector<float> y = {10.0f, 20.0f, 30.0f, 40.0f, 50.0f};
    auto& line = ax.line(x, y);

    EXPECT_EQ(line.point_count(), 5u);
    EXPECT_FLOAT_EQ(line.x_data()[0], 1.0f);
    EXPECT_FLOAT_EQ(line.y_data()[4], 50.0f);
}

TEST_F(DataEditorDataTest, ScatterSeries2DDataAccess)
{
    auto& ax = fig_->subplot(1, 1, 1);
    std::vector<float> x = {0.5f, 1.5f, 2.5f};
    std::vector<float> y = {5.0f, 15.0f, 25.0f};
    auto& scatter = ax.scatter(x, y);

    EXPECT_EQ(scatter.point_count(), 3u);
    EXPECT_FLOAT_EQ(scatter.x_data()[1], 1.5f);
    EXPECT_FLOAT_EQ(scatter.y_data()[2], 25.0f);
}

// ─── 2D Series Inline Edit (set_x / set_y) ─────────────────────────────────

TEST_F(DataEditorDataTest, LineSeriesEditX)
{
    auto& ax = fig_->subplot(1, 1, 1);
    std::vector<float> x = {1.0f, 2.0f, 3.0f};
    std::vector<float> y = {10.0f, 20.0f, 30.0f};
    auto& line = ax.line(x, y);

    // Edit X[1] from 2.0 to 99.0
    std::vector<float> new_x(line.x_data().begin(), line.x_data().end());
    new_x[1] = 99.0f;
    line.set_x(new_x);

    EXPECT_FLOAT_EQ(line.x_data()[1], 99.0f);
    EXPECT_EQ(line.point_count(), 3u);
}

TEST_F(DataEditorDataTest, LineSeriesEditY)
{
    auto& ax = fig_->subplot(1, 1, 1);
    std::vector<float> x = {1.0f, 2.0f, 3.0f};
    std::vector<float> y = {10.0f, 20.0f, 30.0f};
    auto& line = ax.line(x, y);

    std::vector<float> new_y(line.y_data().begin(), line.y_data().end());
    new_y[0] = -5.0f;
    line.set_y(new_y);

    EXPECT_FLOAT_EQ(line.y_data()[0], -5.0f);
}

TEST_F(DataEditorDataTest, ScatterSeriesEditX)
{
    auto& ax = fig_->subplot(1, 1, 1);
    std::vector<float> x = {1.0f, 2.0f};
    std::vector<float> y = {10.0f, 20.0f};
    auto& scatter = ax.scatter(x, y);

    std::vector<float> new_x(scatter.x_data().begin(), scatter.x_data().end());
    new_x[0] = 42.0f;
    scatter.set_x(new_x);

    EXPECT_FLOAT_EQ(scatter.x_data()[0], 42.0f);
}

TEST_F(DataEditorDataTest, ScatterSeriesEditY)
{
    auto& ax = fig_->subplot(1, 1, 1);
    std::vector<float> x = {1.0f, 2.0f};
    std::vector<float> y = {10.0f, 20.0f};
    auto& scatter = ax.scatter(x, y);

    std::vector<float> new_y(scatter.y_data().begin(), scatter.y_data().end());
    new_y[1] = -100.0f;
    scatter.set_y(new_y);

    EXPECT_FLOAT_EQ(scatter.y_data()[1], -100.0f);
}

// ─── 3D Series Data Access ──────────────────────────────────────────────────

TEST_F(DataEditorDataTest, LineSeries3DDataAccess)
{
    auto& ax3d = fig_->subplot3d(1, 1, 1);
    std::vector<float> x = {1.0f, 2.0f, 3.0f};
    std::vector<float> y = {4.0f, 5.0f, 6.0f};
    std::vector<float> z = {7.0f, 8.0f, 9.0f};
    auto& line3d = ax3d.line3d(x, y, z);

    EXPECT_EQ(line3d.point_count(), 3u);
    EXPECT_FLOAT_EQ(line3d.x_data()[0], 1.0f);
    EXPECT_FLOAT_EQ(line3d.y_data()[1], 5.0f);
    EXPECT_FLOAT_EQ(line3d.z_data()[2], 9.0f);
}

TEST_F(DataEditorDataTest, ScatterSeries3DDataAccess)
{
    auto& ax3d = fig_->subplot3d(1, 1, 1);
    std::vector<float> x = {0.1f, 0.2f};
    std::vector<float> y = {0.3f, 0.4f};
    std::vector<float> z = {0.5f, 0.6f};
    auto& scatter3d = ax3d.scatter3d(x, y, z);

    EXPECT_EQ(scatter3d.point_count(), 2u);
    EXPECT_FLOAT_EQ(scatter3d.x_data()[0], 0.1f);
    EXPECT_FLOAT_EQ(scatter3d.z_data()[1], 0.6f);
}

// ─── 3D Series Inline Edit ──────────────────────────────────────────────────

TEST_F(DataEditorDataTest, LineSeries3DEditZ)
{
    auto& ax3d = fig_->subplot3d(1, 1, 1);
    std::vector<float> x = {1.0f, 2.0f};
    std::vector<float> y = {3.0f, 4.0f};
    std::vector<float> z = {5.0f, 6.0f};
    auto& line3d = ax3d.line3d(x, y, z);

    std::vector<float> new_z(line3d.z_data().begin(), line3d.z_data().end());
    new_z[0] = 999.0f;
    line3d.set_z(new_z);

    EXPECT_FLOAT_EQ(line3d.z_data()[0], 999.0f);
}

TEST_F(DataEditorDataTest, ScatterSeries3DEditXYZ)
{
    auto& ax3d = fig_->subplot3d(1, 1, 1);
    std::vector<float> x = {1.0f, 2.0f, 3.0f};
    std::vector<float> y = {4.0f, 5.0f, 6.0f};
    std::vector<float> z = {7.0f, 8.0f, 9.0f};
    auto& scatter3d = ax3d.scatter3d(x, y, z);

    std::vector<float> new_x(scatter3d.x_data().begin(), scatter3d.x_data().end());
    new_x[2] = 100.0f;
    scatter3d.set_x(new_x);

    std::vector<float> new_y(scatter3d.y_data().begin(), scatter3d.y_data().end());
    new_y[0] = 200.0f;
    scatter3d.set_y(new_y);

    std::vector<float> new_z(scatter3d.z_data().begin(), scatter3d.z_data().end());
    new_z[1] = 300.0f;
    scatter3d.set_z(new_z);

    EXPECT_FLOAT_EQ(scatter3d.x_data()[2], 100.0f);
    EXPECT_FLOAT_EQ(scatter3d.y_data()[0], 200.0f);
    EXPECT_FLOAT_EQ(scatter3d.z_data()[1], 300.0f);
}

// ─── Multiple Axes ──────────────────────────────────────────────────────────

TEST_F(DataEditorDataTest, MultipleAxesIteration)
{
    auto& ax1 = fig_->subplot(2, 1, 1);
    auto& ax2 = fig_->subplot(2, 1, 2);

    std::vector<float> x1 = {1.0f, 2.0f};
    std::vector<float> y1 = {3.0f, 4.0f};
    ax1.line(x1, y1);

    std::vector<float> x2 = {5.0f, 6.0f, 7.0f};
    std::vector<float> y2 = {8.0f, 9.0f, 10.0f};
    ax2.scatter(x2, y2);

    // 2D axes are stored in axes(), not all_axes()
    EXPECT_EQ(fig_->axes().size(), 2u);
    EXPECT_EQ(fig_->axes()[0]->series().size(), 1u);
    EXPECT_EQ(fig_->axes()[1]->series().size(), 1u);
}

TEST_F(DataEditorDataTest, MixedAxes2D3D)
{
    fig_->subplot(1, 2, 1);
    fig_->subplot3d(1, 2, 2);

    // 2D and 3D axes are stored in separate vectors
    EXPECT_EQ(fig_->axes().size(), 1u);
    EXPECT_EQ(fig_->all_axes().size(), 2u);   // subplot3d uses all_axes_
    EXPECT_NE(dynamic_cast<Axes*>(fig_->axes()[0].get()), nullptr);
    EXPECT_NE(dynamic_cast<Axes3D*>(fig_->all_axes()[1].get()), nullptr);
}

// ─── Empty States ───────────────────────────────────────────────────────────

TEST_F(DataEditorDataTest, EmptyFigure)
{
    EXPECT_TRUE(fig_->axes().empty());
    EXPECT_TRUE(fig_->all_axes().empty());
}

TEST_F(DataEditorDataTest, EmptyAxes)
{
    fig_->subplot(1, 1, 1);
    EXPECT_TRUE(fig_->axes()[0]->series().empty());
}

TEST_F(DataEditorDataTest, EmptySeriesData)
{
    auto& ax = fig_->subplot(1, 1, 1);
    auto& line = ax.line();

    EXPECT_EQ(line.point_count(), 0u);
    EXPECT_TRUE(line.x_data().empty());
    EXPECT_TRUE(line.y_data().empty());
}

// ─── Series Labels and Colors ───────────────────────────────────────────────

TEST_F(DataEditorDataTest, SeriesLabelDisplay)
{
    auto& ax = fig_->subplot(1, 1, 1);
    std::vector<float> x = {1.0f};
    std::vector<float> y = {2.0f};
    auto& line = ax.line(x, y).label("Temperature");

    EXPECT_EQ(line.label(), "Temperature");
}

TEST_F(DataEditorDataTest, SeriesColorDisplay)
{
    auto& ax = fig_->subplot(1, 1, 1);
    std::vector<float> x = {1.0f};
    std::vector<float> y = {2.0f};
    auto& line = ax.line(x, y).color(Color{1.0f, 0.0f, 0.0f, 1.0f});

    EXPECT_FLOAT_EQ(line.color().r, 1.0f);
    EXPECT_FLOAT_EQ(line.color().g, 0.0f);
}

// ─── Dirty Flag After Edit ──────────────────────────────────────────────────

TEST_F(DataEditorDataTest, DirtyFlagAfterSetX)
{
    auto& ax = fig_->subplot(1, 1, 1);
    std::vector<float> x = {1.0f, 2.0f};
    std::vector<float> y = {3.0f, 4.0f};
    auto& line = ax.line(x, y);

    line.clear_dirty();
    EXPECT_FALSE(line.is_dirty());

    std::vector<float> new_x = {10.0f, 20.0f};
    line.set_x(new_x);
    EXPECT_TRUE(line.is_dirty());
}

TEST_F(DataEditorDataTest, DirtyFlagAfterSetY)
{
    auto& ax = fig_->subplot(1, 1, 1);
    std::vector<float> x = {1.0f, 2.0f};
    std::vector<float> y = {3.0f, 4.0f};
    auto& line = ax.line(x, y);

    line.clear_dirty();
    std::vector<float> new_y = {30.0f, 40.0f};
    line.set_y(new_y);
    EXPECT_TRUE(line.is_dirty());
}

// ─── Large Dataset ──────────────────────────────────────────────────────────

TEST_F(DataEditorDataTest, LargeDataset)
{
    auto& ax = fig_->subplot(1, 1, 1);
    constexpr size_t N = 10000;
    std::vector<float> x(N), y(N);
    for (size_t i = 0; i < N; ++i)
    {
        x[i] = static_cast<float>(i);
        y[i] = static_cast<float>(i * i);
    }
    auto& line = ax.line(x, y);

    EXPECT_EQ(line.point_count(), N);
    EXPECT_FLOAT_EQ(line.x_data()[9999], 9999.0f);

    // Edit a point in the middle
    std::vector<float> new_x(line.x_data().begin(), line.x_data().end());
    new_x[5000] = -1.0f;
    line.set_x(new_x);
    EXPECT_FLOAT_EQ(line.x_data()[5000], -1.0f);
}

// ─── Multiple Series in Same Axes ───────────────────────────────────────────

TEST_F(DataEditorDataTest, MultipleSeriesPerAxes)
{
    auto& ax = fig_->subplot(1, 1, 1);

    std::vector<float> x1 = {1.0f, 2.0f};
    std::vector<float> y1 = {3.0f, 4.0f};
    ax.line(x1, y1).label("Series A");

    std::vector<float> x2 = {5.0f, 6.0f, 7.0f};
    std::vector<float> y2 = {8.0f, 9.0f, 10.0f};
    ax.scatter(x2, y2).label("Series B");

    EXPECT_EQ(ax.series().size(), 2u);
    EXPECT_EQ(ax.series()[0]->label(), "Series A");
    EXPECT_EQ(ax.series()[1]->label(), "Series B");

    // Verify independent data
    auto* ls = dynamic_cast<LineSeries*>(ax.series()[0].get());
    auto* ss = dynamic_cast<ScatterSeries*>(ax.series()[1].get());
    ASSERT_NE(ls, nullptr);
    ASSERT_NE(ss, nullptr);
    EXPECT_EQ(ls->point_count(), 2u);
    EXPECT_EQ(ss->point_count(), 3u);
}
