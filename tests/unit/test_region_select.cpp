#include <cmath>
#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <vector>

// RegionSelect is ImGui-guarded. These tests exercise the pure-logic parts
// (coordinate conversion, point collection, statistics) without requiring
// a running ImGui context, by reimplementing the core algorithms.

using namespace spectra;

// ─── Standalone coordinate conversion (mirrors RegionSelect internals) ──────

namespace
{

void data_to_screen(float data_x,
                    float data_y,
                    const Rect& viewport,
                    float xlim_min,
                    float xlim_max,
                    float ylim_min,
                    float ylim_max,
                    float& screen_x,
                    float& screen_y)
{
    float x_range = xlim_max - xlim_min;
    float y_range = ylim_max - ylim_min;
    if (x_range == 0.0f)
        x_range = 1.0f;
    if (y_range == 0.0f)
        y_range = 1.0f;
    float norm_x = (data_x - xlim_min) / x_range;
    float norm_y = (data_y - ylim_min) / y_range;
    screen_x = viewport.x + norm_x * viewport.w;
    screen_y = viewport.y + (1.0f - norm_y) * viewport.h;
}

void screen_to_data(double screen_x,
                    double screen_y,
                    const Rect& viewport,
                    float xlim_min,
                    float xlim_max,
                    float ylim_min,
                    float ylim_max,
                    float& data_x,
                    float& data_y)
{
    float x_range = xlim_max - xlim_min;
    float y_range = ylim_max - ylim_min;
    float norm_x = (static_cast<float>(screen_x) - viewport.x) / viewport.w;
    float norm_y = 1.0f - (static_cast<float>(screen_y) - viewport.y) / viewport.h;
    data_x = xlim_min + norm_x * x_range;
    data_y = ylim_min + norm_y * y_range;
}

struct SelectedPoint
{
    const Series* series = nullptr;
    size_t index = 0;
    float data_x = 0.0f;
    float data_y = 0.0f;
};

struct RegionStatistics
{
    size_t point_count = 0;
    float x_min = 0.0f, x_max = 0.0f;
    float y_min = 0.0f, y_max = 0.0f;
    float y_mean = 0.0f;
    float y_std = 0.0f;
};

std::vector<SelectedPoint> collect_points(
    const Axes& axes, float sel_xmin, float sel_xmax, float sel_ymin, float sel_ymax)
{
    std::vector<SelectedPoint> result;
    float xmin = std::min(sel_xmin, sel_xmax);
    float xmax = std::max(sel_xmin, sel_xmax);
    float ymin = std::min(sel_ymin, sel_ymax);
    float ymax = std::max(sel_ymin, sel_ymax);

    for (auto& series_ptr : axes.series())
    {
        if (!series_ptr || !series_ptr->visible())
            continue;
        const float* x_data = nullptr;
        const float* y_data = nullptr;
        size_t count = 0;

        if (auto* ls = dynamic_cast<LineSeries*>(series_ptr.get()))
        {
            x_data = ls->x_data().data();
            y_data = ls->y_data().data();
            count = ls->point_count();
        }
        else if (auto* sc = dynamic_cast<ScatterSeries*>(series_ptr.get()))
        {
            x_data = sc->x_data().data();
            y_data = sc->y_data().data();
            count = sc->point_count();
        }
        if (!x_data || !y_data || count == 0)
            continue;

        for (size_t i = 0; i < count; ++i)
        {
            if (x_data[i] >= xmin && x_data[i] <= xmax && y_data[i] >= ymin && y_data[i] <= ymax)
            {
                result.push_back({series_ptr.get(), i, x_data[i], y_data[i]});
            }
        }
    }
    return result;
}

RegionStatistics compute_statistics(const std::vector<SelectedPoint>& points)
{
    RegionStatistics stats;
    if (points.empty())
        return stats;

    stats.point_count = points.size();
    stats.x_min = points[0].data_x;
    stats.x_max = points[0].data_x;
    stats.y_min = points[0].data_y;
    stats.y_max = points[0].data_y;

    double sum_y = 0.0;
    for (const auto& pt : points)
    {
        stats.x_min = std::min(stats.x_min, pt.data_x);
        stats.x_max = std::max(stats.x_max, pt.data_x);
        stats.y_min = std::min(stats.y_min, pt.data_y);
        stats.y_max = std::max(stats.y_max, pt.data_y);
        sum_y += static_cast<double>(pt.data_y);
    }
    stats.y_mean = static_cast<float>(sum_y / static_cast<double>(stats.point_count));

    if (stats.point_count > 1)
    {
        double sum_sq = 0.0;
        for (const auto& pt : points)
        {
            double diff = static_cast<double>(pt.data_y) - static_cast<double>(stats.y_mean);
            sum_sq += diff * diff;
        }
        stats.y_std =
            static_cast<float>(std::sqrt(sum_sq / static_cast<double>(stats.point_count - 1)));
    }
    return stats;
}

}  // anonymous namespace

// ─── Tests ──────────────────────────────────────────────────────────────────

class RegionSelectTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        axes_ = std::make_unique<Axes>();
        axes_->xlim(0.0f, 10.0f);
        axes_->ylim(0.0f, 10.0f);
        axes_->set_viewport(Rect{100.0f, 100.0f, 800.0f, 600.0f});

        // 11 evenly spaced points: (0,0), (1,1), ..., (10,10)
        std::vector<float> x, y;
        for (int i = 0; i <= 10; ++i)
        {
            x.push_back(static_cast<float>(i));
            y.push_back(static_cast<float>(i));
        }
        axes_->line(x, y).label("diagonal");
    }

    std::unique_ptr<Axes> axes_;
};

TEST_F(RegionSelectTest, ScreenToDataRoundTrip)
{
    Rect vp{100.0f, 100.0f, 800.0f, 600.0f};
    float dx, dy, sx, sy;

    // Data (5, 5) -> screen -> data should round-trip
    data_to_screen(5.0f, 5.0f, vp, 0.0f, 10.0f, 0.0f, 10.0f, sx, sy);
    screen_to_data(sx, sy, vp, 0.0f, 10.0f, 0.0f, 10.0f, dx, dy);
    EXPECT_NEAR(dx, 5.0f, 0.01f);
    EXPECT_NEAR(dy, 5.0f, 0.01f);
}

TEST_F(RegionSelectTest, ScreenToDataCorners)
{
    Rect vp{0.0f, 0.0f, 1000.0f, 1000.0f};
    float dx, dy;

    // Top-left of viewport = (xlim_min, ylim_max)
    screen_to_data(0.0, 0.0, vp, 0.0f, 10.0f, 0.0f, 10.0f, dx, dy);
    EXPECT_FLOAT_EQ(dx, 0.0f);
    EXPECT_FLOAT_EQ(dy, 10.0f);

    // Bottom-right of viewport = (xlim_max, ylim_min)
    screen_to_data(1000.0, 1000.0, vp, 0.0f, 10.0f, 0.0f, 10.0f, dx, dy);
    EXPECT_FLOAT_EQ(dx, 10.0f);
    EXPECT_FLOAT_EQ(dy, 0.0f);
}

TEST_F(RegionSelectTest, CollectPointsFullRange)
{
    auto pts = collect_points(*axes_, 0.0f, 10.0f, 0.0f, 10.0f);
    EXPECT_EQ(pts.size(), 11u);
}

TEST_F(RegionSelectTest, CollectPointsSubRange)
{
    // Select region [2, 5] x [2, 5] — should get points (2,2), (3,3), (4,4), (5,5)
    auto pts = collect_points(*axes_, 2.0f, 5.0f, 2.0f, 5.0f);
    EXPECT_EQ(pts.size(), 4u);
    for (const auto& pt : pts)
    {
        EXPECT_GE(pt.data_x, 2.0f);
        EXPECT_LE(pt.data_x, 5.0f);
    }
}

TEST_F(RegionSelectTest, CollectPointsEmptyRegion)
{
    // Select region with no data points
    auto pts = collect_points(*axes_, 3.5f, 3.9f, 3.5f, 3.9f);
    EXPECT_EQ(pts.size(), 0u);
}

TEST_F(RegionSelectTest, CollectPointsHiddenSeriesSkipped)
{
    for (auto& s : axes_->series_mut())
    {
        if (s)
            s->visible(false);
    }
    auto pts = collect_points(*axes_, 0.0f, 10.0f, 0.0f, 10.0f);
    EXPECT_EQ(pts.size(), 0u);
}

TEST_F(RegionSelectTest, CollectPointsReversedBounds)
{
    // Reversed selection bounds should still work (min/max normalization)
    auto pts = collect_points(*axes_, 5.0f, 2.0f, 5.0f, 2.0f);
    EXPECT_EQ(pts.size(), 4u);
}

TEST_F(RegionSelectTest, StatisticsPointCount)
{
    auto pts = collect_points(*axes_, 0.0f, 10.0f, 0.0f, 10.0f);
    auto stats = compute_statistics(pts);
    EXPECT_EQ(stats.point_count, 11u);
}

TEST_F(RegionSelectTest, StatisticsMean)
{
    auto pts = collect_points(*axes_, 0.0f, 10.0f, 0.0f, 10.0f);
    auto stats = compute_statistics(pts);
    // Mean of 0,1,2,...,10 = 5.0
    EXPECT_NEAR(stats.y_mean, 5.0f, 0.01f);
}

TEST_F(RegionSelectTest, StatisticsRange)
{
    auto pts = collect_points(*axes_, 2.0f, 8.0f, 2.0f, 8.0f);
    auto stats = compute_statistics(pts);
    EXPECT_FLOAT_EQ(stats.x_min, 2.0f);
    EXPECT_FLOAT_EQ(stats.x_max, 8.0f);
    EXPECT_FLOAT_EQ(stats.y_min, 2.0f);
    EXPECT_FLOAT_EQ(stats.y_max, 8.0f);
}

TEST_F(RegionSelectTest, StatisticsStdDev)
{
    auto pts = collect_points(*axes_, 0.0f, 10.0f, 0.0f, 10.0f);
    auto stats = compute_statistics(pts);
    // Std dev of 0,1,...,10 with sample variance = sqrt(11.0) ≈ 3.317
    EXPECT_NEAR(stats.y_std, std::sqrt(11.0f), 0.01f);
}

TEST_F(RegionSelectTest, StatisticsEmpty)
{
    std::vector<SelectedPoint> empty;
    auto stats = compute_statistics(empty);
    EXPECT_EQ(stats.point_count, 0u);
    EXPECT_FLOAT_EQ(stats.y_mean, 0.0f);
    EXPECT_FLOAT_EQ(stats.y_std, 0.0f);
}

TEST_F(RegionSelectTest, StatisticsSinglePoint)
{
    auto pts = collect_points(*axes_, 4.9f, 5.1f, 4.9f, 5.1f);
    ASSERT_EQ(pts.size(), 1u);
    auto stats = compute_statistics(pts);
    EXPECT_EQ(stats.point_count, 1u);
    EXPECT_FLOAT_EQ(stats.y_mean, 5.0f);
    EXPECT_FLOAT_EQ(stats.y_std, 0.0f);  // single point has no std dev
}

TEST_F(RegionSelectTest, CollectPointsScatterSeries)
{
    Axes scatter_axes;
    scatter_axes.xlim(0.0f, 100.0f);
    scatter_axes.ylim(0.0f, 100.0f);
    scatter_axes.set_viewport(Rect{0.0f, 0.0f, 1000.0f, 1000.0f});

    std::vector<float> x = {10.0f, 50.0f, 90.0f};
    std::vector<float> y = {10.0f, 50.0f, 90.0f};
    scatter_axes.scatter(x, y).label("scatter");

    auto pts = collect_points(scatter_axes, 40.0f, 60.0f, 40.0f, 60.0f);
    ASSERT_EQ(pts.size(), 1u);
    EXPECT_FLOAT_EQ(pts[0].data_x, 50.0f);
}

TEST_F(RegionSelectTest, SelectionPersistsThroughZoom)
{
    // Verify that data-coordinate selection bounds are independent of viewport limits
    // Select [3, 7] x [3, 7]
    auto pts_before = collect_points(*axes_, 3.0f, 7.0f, 3.0f, 7.0f);

    // "Zoom in" by changing limits — same data selection should yield same points
    axes_->xlim(2.0f, 8.0f);
    axes_->ylim(2.0f, 8.0f);
    auto pts_after = collect_points(*axes_, 3.0f, 7.0f, 3.0f, 7.0f);

    EXPECT_EQ(pts_before.size(), pts_after.size());
}
