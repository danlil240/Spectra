#include <spectra/series_stats.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace spectra
{

// ─── Helper: percentile via linear interpolation ────────────────────────────

static float percentile(std::vector<float>& sorted, float p)
{
    if (sorted.empty())
        return 0.0f;
    if (sorted.size() == 1)
        return sorted[0];

    float idx = p * static_cast<float>(sorted.size() - 1);
    auto  lo  = static_cast<size_t>(std::floor(idx));
    auto  hi  = static_cast<size_t>(std::ceil(idx));
    if (lo == hi)
        return sorted[lo];
    float frac = idx - static_cast<float>(lo);
    return sorted[lo] * (1.0f - frac) + sorted[hi] * frac;
}

// ─── Helper: Gaussian KDE ───────────────────────────────────────────────────

static float gaussian_kde(float x, const std::vector<float>& data, float bandwidth)
{
    float sum = 0.0f;
    float n   = static_cast<float>(data.size());
    for (float d : data)
    {
        float z = (x - d) / bandwidth;
        sum += std::exp(-0.5f * z * z);
    }
    constexpr float INV_SQRT_2PI = 0.3989422804014327f;
    return sum * INV_SQRT_2PI / (n * bandwidth);
}

// ─── NaN constant for line breaks ───────────────────────────────────────────

static constexpr float NAN_BREAK = std::numeric_limits<float>::quiet_NaN();

// ═══════════════════════════════════════════════════════════════════════════
// BoxPlotSeries
// ═══════════════════════════════════════════════════════════════════════════

BoxPlotStats BoxPlotSeries::compute_stats(std::span<const float> values)
{
    BoxPlotStats result;
    if (values.empty())
        return result;

    std::vector<float> sorted(values.begin(), values.end());
    // Remove NaN
    sorted.erase(
        std::remove_if(sorted.begin(), sorted.end(), [](float v) { return std::isnan(v); }),
        sorted.end());
    if (sorted.empty())
        return result;

    std::sort(sorted.begin(), sorted.end());

    result.median = percentile(sorted, 0.5f);
    result.q1     = percentile(sorted, 0.25f);
    result.q3     = percentile(sorted, 0.75f);

    float iqr        = result.q3 - result.q1;
    float low_fence  = result.q1 - 1.5f * iqr;
    float high_fence = result.q3 + 1.5f * iqr;

    // Whiskers extend to the most extreme data point within the fences
    result.whisker_low  = sorted.front();
    result.whisker_high = sorted.back();
    for (float v : sorted)
    {
        if (v >= low_fence)
        {
            result.whisker_low = v;
            break;
        }
    }
    for (auto it = sorted.rbegin(); it != sorted.rend(); ++it)
    {
        if (*it <= high_fence)
        {
            result.whisker_high = *it;
            break;
        }
    }

    // Outliers
    for (float v : sorted)
    {
        if (v < low_fence || v > high_fence)
        {
            result.outliers.push_back(v);
        }
    }

    return result;
}

BoxPlotSeries& BoxPlotSeries::add_box(float x_position, std::span<const float> values)
{
    auto s = compute_stats(values);
    positions_.push_back(x_position);
    stats_.push_back(std::move(s));
    dirty_ = true;
    rebuild_geometry();
    return *this;
}

BoxPlotSeries& BoxPlotSeries::add_box(float                  x_position,
                                      float                  median,
                                      float                  q1,
                                      float                  q3,
                                      float                  whisker_low,
                                      float                  whisker_high,
                                      std::span<const float> outliers)
{
    BoxPlotStats s;
    s.median       = median;
    s.q1           = q1;
    s.q3           = q3;
    s.whisker_low  = whisker_low;
    s.whisker_high = whisker_high;
    s.outliers.assign(outliers.begin(), outliers.end());

    positions_.push_back(x_position);
    stats_.push_back(std::move(s));
    dirty_ = true;
    rebuild_geometry();
    return *this;
}

void BoxPlotSeries::rebuild_geometry()
{
    line_x_.clear();
    line_y_.clear();
    outlier_x_.clear();
    outlier_y_.clear();

    float hw = box_width_ * 0.5f;

    for (size_t i = 0; i < positions_.size(); ++i)
    {
        float               x = positions_[i];
        const BoxPlotStats& s = stats_[i];

        // Box rectangle: left, bottom -> left, top -> right, top -> right, bottom -> close
        line_x_.push_back(x - hw);
        line_y_.push_back(s.q1);
        line_x_.push_back(x - hw);
        line_y_.push_back(s.q3);
        line_x_.push_back(x + hw);
        line_y_.push_back(s.q3);
        line_x_.push_back(x + hw);
        line_y_.push_back(s.q1);
        line_x_.push_back(x - hw);
        line_y_.push_back(s.q1);
        // NaN break
        line_x_.push_back(NAN_BREAK);
        line_y_.push_back(NAN_BREAK);

        // Median line (horizontal across box)
        line_x_.push_back(x - hw);
        line_y_.push_back(s.median);
        line_x_.push_back(x + hw);
        line_y_.push_back(s.median);
        line_x_.push_back(NAN_BREAK);
        line_y_.push_back(NAN_BREAK);

        // Lower whisker (vertical line from q1 down to whisker_low)
        line_x_.push_back(x);
        line_y_.push_back(s.q1);
        line_x_.push_back(x);
        line_y_.push_back(s.whisker_low);
        line_x_.push_back(NAN_BREAK);
        line_y_.push_back(NAN_BREAK);

        // Lower whisker cap (horizontal)
        float cap_hw = hw * 0.5f;
        line_x_.push_back(x - cap_hw);
        line_y_.push_back(s.whisker_low);
        line_x_.push_back(x + cap_hw);
        line_y_.push_back(s.whisker_low);
        line_x_.push_back(NAN_BREAK);
        line_y_.push_back(NAN_BREAK);

        // Upper whisker (vertical line from q3 up to whisker_high)
        line_x_.push_back(x);
        line_y_.push_back(s.q3);
        line_x_.push_back(x);
        line_y_.push_back(s.whisker_high);
        line_x_.push_back(NAN_BREAK);
        line_y_.push_back(NAN_BREAK);

        // Upper whisker cap (horizontal)
        line_x_.push_back(x - cap_hw);
        line_y_.push_back(s.whisker_high);
        line_x_.push_back(x + cap_hw);
        line_y_.push_back(s.whisker_high);
        line_x_.push_back(NAN_BREAK);
        line_y_.push_back(NAN_BREAK);

        // Outliers
        if (show_outliers_)
        {
            for (float o : s.outliers)
            {
                outlier_x_.push_back(x);
                outlier_y_.push_back(o);
            }
        }
    }
}

void BoxPlotSeries::record_commands(Renderer& /*renderer*/) {}

// ═══════════════════════════════════════════════════════════════════════════
// ViolinSeries
// ═══════════════════════════════════════════════════════════════════════════

ViolinSeries& ViolinSeries::add_violin(float x_position, std::span<const float> values)
{
    ViolinData vd;
    vd.x_position = x_position;
    vd.values.assign(values.begin(), values.end());
    // Remove NaN
    vd.values.erase(
        std::remove_if(vd.values.begin(), vd.values.end(), [](float v) { return std::isnan(v); }),
        vd.values.end());
    violins_.push_back(std::move(vd));
    dirty_ = true;
    rebuild_geometry();
    return *this;
}

void ViolinSeries::rebuild_geometry()
{
    line_x_.clear();
    line_y_.clear();

    float hw = violin_width_ * 0.5f;

    for (const auto& vd : violins_)
    {
        if (vd.values.empty())
            continue;

        std::vector<float> sorted = vd.values;
        std::sort(sorted.begin(), sorted.end());

        float data_min = sorted.front();
        float data_max = sorted.back();
        float range    = data_max - data_min;
        if (range == 0.0f)
            range = 1.0f;

        // Silverman's rule of thumb for bandwidth
        float n       = static_cast<float>(sorted.size());
        float std_dev = 0.0f;
        float mean    = 0.0f;
        for (float v : sorted)
            mean += v;
        mean /= n;
        for (float v : sorted)
            std_dev += (v - mean) * (v - mean);
        std_dev = std::sqrt(std_dev / n);
        if (std_dev == 0.0f)
            std_dev = 1.0f;
        float bandwidth = 1.06f * std_dev * std::pow(n, -0.2f);

        // Evaluate KDE at resolution_ points
        std::vector<float> y_vals(resolution_);
        std::vector<float> kde_vals(resolution_);
        float              max_kde = 0.0f;

        for (int i = 0; i < resolution_; ++i)
        {
            float t     = static_cast<float>(i) / static_cast<float>(resolution_ - 1);
            float y     = data_min + t * range;
            y_vals[i]   = y;
            kde_vals[i] = gaussian_kde(y, sorted, bandwidth);
            max_kde     = std::max(max_kde, kde_vals[i]);
        }

        // Normalize KDE to fit within violin_width
        if (max_kde > 0.0f)
        {
            for (int i = 0; i < resolution_; ++i)
                kde_vals[i] /= max_kde;
        }

        // Draw right half of violin (going up)
        for (int i = 0; i < resolution_; ++i)
        {
            line_x_.push_back(vd.x_position + kde_vals[i] * hw);
            line_y_.push_back(y_vals[i]);
        }
        // Draw left half of violin (going down)
        for (int i = resolution_ - 1; i >= 0; --i)
        {
            line_x_.push_back(vd.x_position - kde_vals[i] * hw);
            line_y_.push_back(y_vals[i]);
        }
        // Close the shape
        line_x_.push_back(vd.x_position + kde_vals[0] * hw);
        line_y_.push_back(y_vals[0]);
        // NaN break
        line_x_.push_back(NAN_BREAK);
        line_y_.push_back(NAN_BREAK);

        // Inner box plot (thin)
        if (show_box_)
        {
            float q1     = percentile(sorted, 0.25f);
            float median = percentile(sorted, 0.5f);
            float q3     = percentile(sorted, 0.75f);
            float bw     = hw * 0.15f;   // thin inner box

            // Box
            line_x_.push_back(vd.x_position - bw);
            line_y_.push_back(q1);
            line_x_.push_back(vd.x_position - bw);
            line_y_.push_back(q3);
            line_x_.push_back(vd.x_position + bw);
            line_y_.push_back(q3);
            line_x_.push_back(vd.x_position + bw);
            line_y_.push_back(q1);
            line_x_.push_back(vd.x_position - bw);
            line_y_.push_back(q1);
            line_x_.push_back(NAN_BREAK);
            line_y_.push_back(NAN_BREAK);

            // Median line
            line_x_.push_back(vd.x_position - bw);
            line_y_.push_back(median);
            line_x_.push_back(vd.x_position + bw);
            line_y_.push_back(median);
            line_x_.push_back(NAN_BREAK);
            line_y_.push_back(NAN_BREAK);
        }
    }
}

void ViolinSeries::record_commands(Renderer& /*renderer*/) {}

// ═══════════════════════════════════════════════════════════════════════════
// HistogramSeries
// ═══════════════════════════════════════════════════════════════════════════

HistogramSeries::HistogramSeries(std::span<const float> values, int bins)
{
    set_data(values, bins);
}

HistogramSeries& HistogramSeries::set_data(std::span<const float> values, int bins)
{
    raw_values_.assign(values.begin(), values.end());
    // Remove NaN
    raw_values_.erase(std::remove_if(raw_values_.begin(),
                                     raw_values_.end(),
                                     [](float v) { return std::isnan(v); }),
                      raw_values_.end());
    bins_  = bins;
    dirty_ = true;
    rebuild_geometry();
    return *this;
}

void HistogramSeries::rebuild_geometry()
{
    line_x_.clear();
    line_y_.clear();
    bin_edges_.clear();
    bin_counts_.clear();

    if (raw_values_.empty() || bins_ <= 0)
        return;

    float lo = *std::min_element(raw_values_.begin(), raw_values_.end());
    float hi = *std::max_element(raw_values_.begin(), raw_values_.end());
    if (lo == hi)
        hi = lo + 1.0f;

    float bin_width = (hi - lo) / static_cast<float>(bins_);

    // Compute bin edges
    bin_edges_.resize(bins_ + 1);
    for (int i = 0; i <= bins_; ++i)
        bin_edges_[i] = lo + static_cast<float>(i) * bin_width;

    // Count values in each bin
    bin_counts_.resize(bins_, 0.0f);
    for (float v : raw_values_)
    {
        int idx = static_cast<int>((v - lo) / bin_width);
        if (idx >= bins_)
            idx = bins_ - 1;
        if (idx < 0)
            idx = 0;
        bin_counts_[idx] += 1.0f;
    }

    // Cumulative
    if (cumulative_)
    {
        for (int i = 1; i < bins_; ++i)
            bin_counts_[i] += bin_counts_[i - 1];
    }

    // Density normalization
    if (density_)
    {
        float total = static_cast<float>(raw_values_.size());
        for (int i = 0; i < bins_; ++i)
            bin_counts_[i] /= (total * bin_width);
    }

    // Build step-function geometry
    // Start from baseline
    line_x_.push_back(bin_edges_[0]);
    line_y_.push_back(0.0f);

    for (int i = 0; i < bins_; ++i)
    {
        float edge_l = bin_edges_[i];
        float edge_r = bin_edges_[i + 1];
        line_x_.push_back(edge_l);
        line_y_.push_back(bin_counts_[i]);
        line_x_.push_back(edge_r);
        line_y_.push_back(bin_counts_[i]);
    }

    // Close to baseline
    line_x_.push_back(bin_edges_[bins_]);
    line_y_.push_back(0.0f);
}

void HistogramSeries::record_commands(Renderer& /*renderer*/) {}

// ═══════════════════════════════════════════════════════════════════════════
// BarSeries
// ═══════════════════════════════════════════════════════════════════════════

BarSeries::BarSeries(std::span<const float> positions, std::span<const float> heights)
{
    set_data(positions, heights);
}

BarSeries& BarSeries::set_data(std::span<const float> positions, std::span<const float> heights)
{
    positions_.assign(positions.begin(), positions.end());
    heights_.assign(heights.begin(), heights.end());
    dirty_ = true;
    rebuild_geometry();
    return *this;
}

void BarSeries::rebuild_geometry()
{
    line_x_.clear();
    line_y_.clear();

    float  hw = bar_width_ * 0.5f;
    size_t n  = std::min(positions_.size(), heights_.size());

    for (size_t i = 0; i < n; ++i)
    {
        float pos = positions_[i];
        float h   = heights_[i];

        if (orientation_ == BarOrientation::Vertical)
        {
            // Rectangle: 5 points + NaN break
            line_x_.push_back(pos - hw);
            line_y_.push_back(baseline_);
            line_x_.push_back(pos - hw);
            line_y_.push_back(h);
            line_x_.push_back(pos + hw);
            line_y_.push_back(h);
            line_x_.push_back(pos + hw);
            line_y_.push_back(baseline_);
            line_x_.push_back(pos - hw);
            line_y_.push_back(baseline_);
        }
        else
        {
            // Horizontal bars: swap x/y roles
            line_x_.push_back(baseline_);
            line_y_.push_back(pos - hw);
            line_x_.push_back(h);
            line_y_.push_back(pos - hw);
            line_x_.push_back(h);
            line_y_.push_back(pos + hw);
            line_x_.push_back(baseline_);
            line_y_.push_back(pos + hw);
            line_x_.push_back(baseline_);
            line_y_.push_back(pos - hw);
        }

        // NaN break between bars
        line_x_.push_back(NAN_BREAK);
        line_y_.push_back(NAN_BREAK);
    }
}

void BarSeries::record_commands(Renderer& /*renderer*/) {}

}   // namespace spectra
