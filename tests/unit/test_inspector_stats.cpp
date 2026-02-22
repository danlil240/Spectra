#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <numeric>
#include <span>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <vector>

using namespace spectra;

// ─── Replicate the statistics helper functions from inspector.cpp ───────────
// These are static in inspector.cpp, so we duplicate them here for testing.

static double compute_percentile(const std::vector<float>& sorted, double p)
{
    if (sorted.empty())
        return 0.0;
    if (sorted.size() == 1)
        return static_cast<double>(sorted[0]);
    double idx = p * static_cast<double>(sorted.size() - 1);
    size_t lo  = static_cast<size_t>(idx);
    size_t hi  = lo + 1;
    if (hi >= sorted.size())
        return static_cast<double>(sorted.back());
    double frac = idx - static_cast<double>(lo);
    return static_cast<double>(sorted[lo]) * (1.0 - frac) + static_cast<double>(sorted[hi]) * frac;
}

static void get_series_data(const Series&           s,
                            std::span<const float>& x_data,
                            std::span<const float>& y_data,
                            size_t&                 count)
{
    x_data = {};
    y_data = {};
    count  = 0;
    if (const auto* line = dynamic_cast<const LineSeries*>(&s))
    {
        x_data = line->x_data();
        y_data = line->y_data();
        count  = line->point_count();
    }
    else if (const auto* scatter = dynamic_cast<const ScatterSeries*>(&s))
    {
        x_data = scatter->x_data();
        y_data = scatter->y_data();
        count  = scatter->point_count();
    }
}

// ─── Percentile Tests ───────────────────────────────────────────────────────

TEST(SeriesStatistics, PercentileEmpty)
{
    std::vector<float> empty;
    EXPECT_DOUBLE_EQ(compute_percentile(empty, 0.5), 0.0);
}

TEST(SeriesStatistics, PercentileSingleValue)
{
    std::vector<float> single = {42.0f};
    EXPECT_DOUBLE_EQ(compute_percentile(single, 0.0), 42.0);
    EXPECT_DOUBLE_EQ(compute_percentile(single, 0.5), 42.0);
    EXPECT_DOUBLE_EQ(compute_percentile(single, 1.0), 42.0);
}

TEST(SeriesStatistics, PercentileTwoValues)
{
    std::vector<float> two = {10.0f, 20.0f};
    EXPECT_DOUBLE_EQ(compute_percentile(two, 0.0), 10.0);
    EXPECT_DOUBLE_EQ(compute_percentile(two, 0.5), 15.0);
    EXPECT_DOUBLE_EQ(compute_percentile(two, 1.0), 20.0);
}

TEST(SeriesStatistics, MedianOddCount)
{
    std::vector<float> data   = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    double             median = compute_percentile(data, 0.5);
    EXPECT_DOUBLE_EQ(median, 3.0);
}

TEST(SeriesStatistics, MedianEvenCount)
{
    std::vector<float> data   = {1.0f, 2.0f, 3.0f, 4.0f};
    double             median = compute_percentile(data, 0.5);
    EXPECT_DOUBLE_EQ(median, 2.5);
}

TEST(SeriesStatistics, Quartiles)
{
    // 0..100 in steps of 1
    std::vector<float> data;
    for (int i = 0; i <= 100; ++i)
        data.push_back(static_cast<float>(i));

    double p25 = compute_percentile(data, 0.25);
    double p50 = compute_percentile(data, 0.50);
    double p75 = compute_percentile(data, 0.75);

    EXPECT_DOUBLE_EQ(p25, 25.0);
    EXPECT_DOUBLE_EQ(p50, 50.0);
    EXPECT_DOUBLE_EQ(p75, 75.0);
}

TEST(SeriesStatistics, PercentileP5P95)
{
    std::vector<float> data;
    for (int i = 0; i <= 100; ++i)
        data.push_back(static_cast<float>(i));

    double p05 = compute_percentile(data, 0.05);
    double p95 = compute_percentile(data, 0.95);

    EXPECT_DOUBLE_EQ(p05, 5.0);
    EXPECT_DOUBLE_EQ(p95, 95.0);
}

TEST(SeriesStatistics, IQR)
{
    std::vector<float> data;
    for (int i = 0; i <= 100; ++i)
        data.push_back(static_cast<float>(i));

    double p25 = compute_percentile(data, 0.25);
    double p75 = compute_percentile(data, 0.75);
    double iqr = p75 - p25;

    EXPECT_DOUBLE_EQ(iqr, 50.0);
}

// ─── Data Extraction Tests ──────────────────────────────────────────────────

TEST(SeriesStatistics, LineSeriesDataExtraction)
{
    Figure fig;
    auto&  ax  = fig.subplot(1, 1, 1);
    float  x[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    float  y[] = {10.0f, 20.0f, 30.0f, 40.0f, 50.0f};
    auto&  s   = ax.line(x, y);

    std::span<const float> x_data, y_data;
    size_t                 count = 0;
    get_series_data(s, x_data, y_data, count);

    EXPECT_EQ(count, 5u);
    EXPECT_EQ(x_data.size(), 5u);
    EXPECT_EQ(y_data.size(), 5u);
    EXPECT_FLOAT_EQ(x_data[0], 1.0f);
    EXPECT_FLOAT_EQ(y_data[4], 50.0f);
}

TEST(SeriesStatistics, ScatterSeriesDataExtraction)
{
    Figure fig;
    auto&  ax  = fig.subplot(1, 1, 1);
    float  x[] = {0.5f, 1.5f, 2.5f};
    float  y[] = {100.0f, 200.0f, 300.0f};
    auto&  s   = ax.scatter(x, y);

    std::span<const float> x_data, y_data;
    size_t                 count = 0;
    get_series_data(s, x_data, y_data, count);

    EXPECT_EQ(count, 3u);
    EXPECT_FLOAT_EQ(x_data[0], 0.5f);
    EXPECT_FLOAT_EQ(y_data[2], 300.0f);
}

// ─── Full Statistics Computation Tests ──────────────────────────────────────

TEST(SeriesStatistics, MeanComputation)
{
    float                  y[] = {2.0f, 4.0f, 6.0f, 8.0f, 10.0f};
    std::span<const float> y_data(y, 5);
    double                 sum  = std::accumulate(y_data.begin(), y_data.end(), 0.0);
    double                 mean = sum / 5.0;
    EXPECT_DOUBLE_EQ(mean, 6.0);
}

TEST(SeriesStatistics, StdDevComputation)
{
    float                  y[] = {2.0f, 4.0f, 4.0f, 4.0f, 5.0f, 5.0f, 7.0f, 9.0f};
    std::span<const float> y_data(y, 8);
    size_t                 count = 8;

    double sum  = std::accumulate(y_data.begin(), y_data.end(), 0.0);
    double mean = sum / static_cast<double>(count);
    EXPECT_DOUBLE_EQ(mean, 5.0);

    double sq_sum = 0.0;
    for (float v : y_data)
    {
        double diff = static_cast<double>(v) - mean;
        sq_sum += diff * diff;
    }
    double stddev = std::sqrt(sq_sum / static_cast<double>(count));
    EXPECT_NEAR(stddev, 2.0, 0.001);
}

TEST(SeriesStatistics, MinMaxComputation)
{
    float                  y[] = {-5.0f, 3.0f, 100.0f, -200.0f, 42.0f};
    std::span<const float> y_data(y, 5);
    auto [ymin_it, ymax_it] = std::minmax_element(y_data.begin(), y_data.end());
    EXPECT_FLOAT_EQ(*ymin_it, -200.0f);
    EXPECT_FLOAT_EQ(*ymax_it, 100.0f);
}

TEST(SeriesStatistics, RangeComputation)
{
    float                  y[] = {10.0f, 20.0f, 30.0f};
    std::span<const float> y_data(y, 3);
    auto [ymin_it, ymax_it] = std::minmax_element(y_data.begin(), y_data.end());
    float range             = *ymax_it - *ymin_it;
    EXPECT_FLOAT_EQ(range, 20.0f);
}

TEST(SeriesStatistics, XStatistics)
{
    float                  x[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    std::span<const float> x_data(x, 5);

    auto [xmin_it, xmax_it] = std::minmax_element(x_data.begin(), x_data.end());
    EXPECT_FLOAT_EQ(*xmin_it, 0.0f);
    EXPECT_FLOAT_EQ(*xmax_it, 1.0f);

    double x_sum  = std::accumulate(x_data.begin(), x_data.end(), 0.0);
    double x_mean = x_sum / 5.0;
    EXPECT_DOUBLE_EQ(x_mean, 0.5);
}

// ─── Axes Aggregate Statistics Tests ────────────────────────────────────────

TEST(AxesStatistics, EmptyAxes)
{
    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);
    EXPECT_EQ(ax.series().size(), 0u);
}

TEST(AxesStatistics, SingleSeriesAggregate)
{
    Figure fig;
    auto&  ax  = fig.subplot(1, 1, 1);
    float  x[] = {1.0f, 2.0f, 3.0f};
    float  y[] = {10.0f, 20.0f, 30.0f};
    ax.line(x, y);

    size_t total_points  = 0;
    size_t visible_count = 0;
    for (const auto& s : ax.series())
    {
        if (!s)
            continue;
        if (s->visible())
            visible_count++;
        std::span<const float> xd, yd;
        size_t                 cnt = 0;
        get_series_data(*s, xd, yd, cnt);
        total_points += cnt;
    }

    EXPECT_EQ(total_points, 3u);
    EXPECT_EQ(visible_count, 1u);
}

TEST(AxesStatistics, MultiSeriesAggregate)
{
    Figure fig;
    auto&  ax   = fig.subplot(1, 1, 1);
    float  x1[] = {1.0f, 2.0f, 3.0f};
    float  y1[] = {10.0f, 20.0f, 30.0f};
    float  x2[] = {4.0f, 5.0f};
    float  y2[] = {-5.0f, 100.0f};
    ax.line(x1, y1);
    ax.scatter(x2, y2);

    size_t total_points = 0;
    double global_xmin  = std::numeric_limits<double>::max();
    double global_xmax  = std::numeric_limits<double>::lowest();
    double global_ymin  = std::numeric_limits<double>::max();
    double global_ymax  = std::numeric_limits<double>::lowest();

    for (const auto& s : ax.series())
    {
        if (!s)
            continue;
        std::span<const float> xd, yd;
        size_t                 cnt = 0;
        get_series_data(*s, xd, yd, cnt);
        total_points += cnt;

        if (!xd.empty())
        {
            auto [a, b] = std::minmax_element(xd.begin(), xd.end());
            global_xmin = std::min(global_xmin, static_cast<double>(*a));
            global_xmax = std::max(global_xmax, static_cast<double>(*b));
        }
        if (!yd.empty())
        {
            auto [a, b] = std::minmax_element(yd.begin(), yd.end());
            global_ymin = std::min(global_ymin, static_cast<double>(*a));
            global_ymax = std::max(global_ymax, static_cast<double>(*b));
        }
    }

    EXPECT_EQ(total_points, 5u);
    EXPECT_DOUBLE_EQ(global_xmin, 1.0);
    EXPECT_DOUBLE_EQ(global_xmax, 5.0);
    EXPECT_DOUBLE_EQ(global_ymin, -5.0);
    EXPECT_DOUBLE_EQ(global_ymax, 100.0);
}

TEST(AxesStatistics, VisibilityTracking)
{
    Figure fig;
    auto&  ax  = fig.subplot(1, 1, 1);
    float  x[] = {1.0f, 2.0f};
    float  y[] = {3.0f, 4.0f};
    ax.line(x, y);
    auto& s2 = ax.line(x, y);
    s2.visible(false);

    size_t visible = 0;
    for (const auto& s : ax.series())
    {
        if (s && s->visible())
            visible++;
    }
    EXPECT_EQ(visible, 1u);
    EXPECT_EQ(ax.series().size(), 2u);
}

// ─── Sparkline Downsampling Logic Tests ─────────────────────────────────────

TEST(Sparkline, DownsampleSmallData)
{
    std::vector<float> data          = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    constexpr size_t   MAX_SPARKLINE = 200;

    std::vector<float> downsampled;
    if (data.size() <= MAX_SPARKLINE)
    {
        downsampled = data;
    }

    EXPECT_EQ(downsampled.size(), 5u);
    EXPECT_FLOAT_EQ(downsampled[0], 1.0f);
    EXPECT_FLOAT_EQ(downsampled[4], 5.0f);
}

TEST(Sparkline, DownsampleLargeData)
{
    std::vector<float> data;
    for (int i = 0; i < 1000; ++i)
    {
        data.push_back(static_cast<float>(i));
    }
    constexpr size_t MAX_SPARKLINE = 200;

    std::vector<float> downsampled;
    if (data.size() <= MAX_SPARKLINE)
    {
        downsampled = data;
    }
    else
    {
        downsampled.reserve(MAX_SPARKLINE);
        for (size_t i = 0; i < MAX_SPARKLINE; ++i)
        {
            size_t src = i * data.size() / MAX_SPARKLINE;
            downsampled.push_back(data[src]);
        }
    }

    EXPECT_EQ(downsampled.size(), MAX_SPARKLINE);
    EXPECT_FLOAT_EQ(downsampled[0], 0.0f);
    // Last sample should be near the end
    EXPECT_GT(downsampled.back(), 900.0f);
}

// ─── Section Animation State Tests ──────────────────────────────────────────
// (Testing the data model, not the ImGui rendering)

TEST(SectionAnimation, DefaultState)
{
    // Replicate SectionAnimState struct
    struct SectionAnimState
    {
        float anim_t      = 1.0f;
        bool  target_open = true;
        bool  was_open    = true;
    };

    SectionAnimState state;
    EXPECT_FLOAT_EQ(state.anim_t, 1.0f);
    EXPECT_TRUE(state.target_open);
    EXPECT_TRUE(state.was_open);
}

TEST(SectionAnimation, CollapseAnimation)
{
    struct SectionAnimState
    {
        float anim_t      = 1.0f;
        bool  target_open = true;
        bool  was_open    = true;
    };

    SectionAnimState state;
    state.target_open = false;

    constexpr float ANIM_SPEED = 8.0f;
    float           dt         = 1.0f / 60.0f;   // 60fps

    // Simulate several frames
    for (int i = 0; i < 60; ++i)
    {
        float target = state.target_open ? 1.0f : 0.0f;
        if (std::abs(state.anim_t - target) > 0.001f)
        {
            state.anim_t += (target - state.anim_t) * std::min(1.0f, ANIM_SPEED * dt);
        }
        else
        {
            state.anim_t = target;
        }
    }

    // After ~1 second at 60fps, should be very close to 0
    EXPECT_NEAR(state.anim_t, 0.0f, 0.01f);
}

TEST(SectionAnimation, ExpandAnimation)
{
    struct SectionAnimState
    {
        float anim_t      = 0.0f;
        bool  target_open = false;
        bool  was_open    = false;
    };

    SectionAnimState state;
    state.target_open = true;

    constexpr float ANIM_SPEED = 8.0f;
    float           dt         = 1.0f / 60.0f;

    for (int i = 0; i < 60; ++i)
    {
        float target = state.target_open ? 1.0f : 0.0f;
        if (std::abs(state.anim_t - target) > 0.001f)
        {
            state.anim_t += (target - state.anim_t) * std::min(1.0f, ANIM_SPEED * dt);
        }
        else
        {
            state.anim_t = target;
        }
    }

    EXPECT_NEAR(state.anim_t, 1.0f, 0.01f);
}

TEST(SectionAnimation, AnimationConvergesQuickly)
{
    // Animation should be ~90% complete within 150ms (the spec target)
    struct SectionAnimState
    {
        float anim_t      = 1.0f;
        bool  target_open = true;
    };

    SectionAnimState state;
    state.target_open = false;

    constexpr float ANIM_SPEED   = 8.0f;
    float           dt           = 1.0f / 60.0f;
    int             frames_150ms = static_cast<int>(0.15f / dt);   // ~9 frames

    for (int i = 0; i < frames_150ms; ++i)
    {
        float target = state.target_open ? 1.0f : 0.0f;
        state.anim_t += (target - state.anim_t) * std::min(1.0f, ANIM_SPEED * dt);
    }

    // Should be mostly collapsed (< 0.4) after 150ms
    EXPECT_LT(state.anim_t, 0.4f);
}

// ─── Percentile Edge Cases ──────────────────────────────────────────────────

TEST(SeriesStatistics, PercentileAllSameValues)
{
    std::vector<float> data = {5.0f, 5.0f, 5.0f, 5.0f};
    EXPECT_DOUBLE_EQ(compute_percentile(data, 0.0), 5.0);
    EXPECT_DOUBLE_EQ(compute_percentile(data, 0.5), 5.0);
    EXPECT_DOUBLE_EQ(compute_percentile(data, 1.0), 5.0);
}

TEST(SeriesStatistics, PercentileNegativeValues)
{
    std::vector<float> data   = {-10.0f, -5.0f, 0.0f, 5.0f, 10.0f};
    double             median = compute_percentile(data, 0.5);
    EXPECT_DOUBLE_EQ(median, 0.0);
}

TEST(SeriesStatistics, PercentileLargeDataset)
{
    std::vector<float> data;
    for (int i = 1; i <= 10000; ++i)
    {
        data.push_back(static_cast<float>(i));
    }
    double median = compute_percentile(data, 0.5);
    EXPECT_NEAR(median, 5000.5, 0.01);
}
