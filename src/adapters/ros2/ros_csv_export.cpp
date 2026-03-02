// RosCsvExport — implementation.
//
// See ros_csv_export.hpp for design notes.

#include "ros_csv_export.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "ros_plot_manager.hpp"

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// CsvExportResult helpers
// ---------------------------------------------------------------------------

std::string CsvExportResult::to_string() const
{
    if (!ok || row_data.empty())
        return {};

    std::string out;
    out.reserve(row_count * column_count * 12);  // rough estimate

    if (write_header_)
    {
        for (size_t c = 0; c < headers.size(); ++c)
        {
            if (c > 0)
                out += separator_;
            out += headers[c];
        }
        out += line_ending_;
    }

    for (const auto& row : row_data)
    {
        for (size_t c = 0; c < row.size(); ++c)
        {
            if (c > 0)
                out += separator_;
            out += row[c];
        }
        out += line_ending_;
    }

    return out;
}

bool CsvExportResult::save_to_file(const std::string& path) const
{
    if (path.empty() || !ok)
        return false;

    std::ofstream f(path);
    if (!f.is_open())
        return false;

    f << to_string();
    return f.good();
}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

void RosCsvExport::split_timestamp(double    timestamp_s,
                                   int64_t   timestamp_ns,
                                   int64_t&  out_sec,
                                   int64_t&  out_nsec)
{
    if (timestamp_ns != 0)
    {
        // Derive from the nanosecond value (exact).
        out_sec  = timestamp_ns / 1000000000LL;
        out_nsec = timestamp_ns % 1000000000LL;
        if (out_nsec < 0)
        {
            out_sec  -= 1;
            out_nsec += 1000000000LL;
        }
    }
    else
    {
        // Fall back: derive from float seconds (loses sub-ns precision).
        double sec_floor  = std::floor(timestamp_s);
        out_sec           = static_cast<int64_t>(sec_floor);
        double nsec_d     = (timestamp_s - sec_floor) * 1e9;
        out_nsec          = static_cast<int64_t>(std::round(nsec_d));
        // Clamp nsec to valid range.
        if (out_nsec >= 1000000000LL)
        {
            out_sec  += 1;
            out_nsec -= 1000000000LL;
        }
        if (out_nsec < 0)
            out_nsec = 0;
    }
}

std::string RosCsvExport::format_value(double v, int precision)
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(precision) << v;
    return ss.str();
}

std::string RosCsvExport::format_int64(int64_t v)
{
    return std::to_string(v);
}

std::string RosCsvExport::make_column_name(const std::string& topic,
                                           const std::string& field_path)
{
    if (topic.empty())
        return field_path;
    if (field_path.empty())
        return topic;

    // Ensure exactly one '/' separator between topic and field_path.
    bool topic_ends_slash = topic.back() == '/';
    bool field_starts_slash = !field_path.empty() && field_path.front() == '/';

    if (topic_ends_slash || field_starts_slash)
        return topic + field_path;

    return topic + "/" + field_path;
}

std::vector<std::string> RosCsvExport::timestamp_headers()
{
    return {"timestamp_sec", "timestamp_nsec", "wall_clock"};
}

std::vector<std::string> RosCsvExport::format_timestamp_cells(double  timestamp_s,
                                                               int64_t timestamp_ns) const
{
    int64_t sec  = 0;
    int64_t nsec = 0;
    split_timestamp(timestamp_s, timestamp_ns, sec, nsec);

    std::string sec_str      = format_int64(sec);
    std::string nsec_str     = format_int64(nsec);
    std::string wall_str     = format_value(timestamp_s, config_.wall_clock_precision);

    return {sec_str, nsec_str, wall_str};
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

RosCsvExport::RosCsvExport(RosPlotManager& mgr)
    : mgr_(mgr)
{
}

// ---------------------------------------------------------------------------
// Export — single plot
// ---------------------------------------------------------------------------

CsvExportResult RosCsvExport::export_plot(int plot_id) const
{
    return export_plots({plot_id}, RangeMode::Full, 0.0, 0.0);
}

CsvExportResult RosCsvExport::export_plot(int    plot_id,
                                          double x_min,
                                          double x_max) const
{
    return export_plots({plot_id}, RangeMode::Visible, x_min, x_max);
}

// ---------------------------------------------------------------------------
// Export — multiple plots
// ---------------------------------------------------------------------------

CsvExportResult RosCsvExport::export_plots(const std::vector<int>& plot_ids) const
{
    return export_plots(plot_ids, RangeMode::Full, 0.0, 0.0);
}

CsvExportResult RosCsvExport::export_plots(const std::vector<int>& plot_ids,
                                           RangeMode               mode,
                                           double                  x_min,
                                           double                  x_max) const
{
    CsvExportResult bad;
    bad.ok    = false;

    if (plot_ids.empty())
    {
        bad.error = "No plot IDs provided";
        return bad;
    }

    // Collect data for each requested plot.
    std::vector<SeriesData> series_vec;
    series_vec.reserve(plot_ids.size());

    for (int id : plot_ids)
    {
        PlotHandle h = mgr_.handle(id);
        if (!h.valid())
        {
            bad.error = "Plot id " + std::to_string(id) + " not found";
            return bad;
        }

        SeriesData sd;
        sd.column_name = make_column_name(h.topic, h.field_path);

        // Copy x/y data from the LineSeries (render-thread safe when called
        // between poll() calls).
        auto xs = h.series->x_data();
        auto ys = h.series->y_data();
        sd.x.assign(xs.begin(), xs.end());
        sd.y.assign(ys.begin(), ys.end());

        // ns data is not stored in LineSeries (it's only in the ring buffer
        // before appending).  We leave ns empty; split_timestamp falls back
        // to float decomposition automatically.
        // (A future enhancement could cache ns alongside the series.)

        series_vec.push_back(std::move(sd));
    }

    return build_result(series_vec, mode, x_min, x_max);
}

// ---------------------------------------------------------------------------
// build_result — merge N series into aligned CSV rows.
// ---------------------------------------------------------------------------

CsvExportResult RosCsvExport::build_result(const std::vector<SeriesData>& series,
                                           RangeMode                      mode,
                                           double                         x_min,
                                           double                         x_max) const
{
    CsvExportResult result;
    result.ok              = false;
    result.separator_      = config_.separator;
    result.line_ending_    = config_.line_ending;
    result.write_header_   = config_.write_header;

    if (series.empty())
    {
        result.error = "No series data";
        return result;
    }

    // ---------------------------------------------------------------------------
    // Step 1: Collect the union of all X (timestamp_s) values.
    //         For single-series exports this is just the series' own X vector.
    // ---------------------------------------------------------------------------

    std::vector<double> all_x;

    for (const auto& sd : series)
    {
        for (float xf : sd.x)
        {
            double xd = static_cast<double>(xf);

            // Range filter.
            if (mode == RangeMode::Visible)
            {
                if (xd < x_min || xd > x_max)
                    continue;
            }

            all_x.push_back(xd);
        }
    }

    if (all_x.empty())
    {
        result.ok        = true;  // valid export, zero rows
        result.row_count = 0;
        // Still build headers.
        auto hdrs = timestamp_headers();
        for (const auto& sd : series)
            hdrs.push_back(sd.column_name);
        result.headers       = std::move(hdrs);
        result.column_count  = result.headers.size();
        return result;
    }

    // Sort + deduplicate X values (within float epsilon).
    std::sort(all_x.begin(), all_x.end());
    {
        std::vector<double> deduped;
        deduped.reserve(all_x.size());
        for (double v : all_x)
        {
            if (deduped.empty() || std::abs(v - deduped.back()) > 1e-12)
                deduped.push_back(v);
        }
        all_x = std::move(deduped);
    }

    // ---------------------------------------------------------------------------
    // Step 2: Build headers.
    // ---------------------------------------------------------------------------

    auto hdrs = timestamp_headers();
    for (const auto& sd : series)
        hdrs.push_back(sd.column_name);

    result.headers      = hdrs;
    result.column_count = hdrs.size();

    // ---------------------------------------------------------------------------
    // Step 3: For each unique X row, find the nearest Y value in each series
    //         (exact match within epsilon).  Write missing_value if no match.
    //
    //         We use a per-series cursor since all series' X is sorted.
    // ---------------------------------------------------------------------------

    constexpr double MATCH_EPS = 1e-9;  // ~1 nanosecond in seconds

    // Per-series lookup index (sorted input assumed — see SeriesData invariant).
    std::vector<size_t> cursors(series.size(), 0);

    result.row_data.reserve(all_x.size());

    for (double row_x : all_x)
    {
        std::vector<std::string> row;
        row.reserve(result.column_count);

        // Timestamp columns (ns=0 → fallback decomposition).
        auto ts_cells = format_timestamp_cells(row_x, 0);
        for (auto& c : ts_cells)
            row.push_back(std::move(c));

        // Value columns.
        for (size_t si = 0; si < series.size(); ++si)
        {
            const SeriesData& sd = series[si];
            size_t&           cur = cursors[si];

            // Advance cursor to first X >= row_x.
            while (cur < sd.x.size() &&
                   static_cast<double>(sd.x[cur]) < row_x - MATCH_EPS)
            {
                ++cur;
            }

            // Check exact match.
            if (cur < sd.x.size() &&
                std::abs(static_cast<double>(sd.x[cur]) - row_x) <= MATCH_EPS)
            {
                // Retrieve ns if available.
                int64_t ns = (cur < sd.ns.size()) ? sd.ns[cur] : 0;
                (void)ns;  // Used in future enhancement; fall through to float decomp.

                row.push_back(format_value(static_cast<double>(sd.y[cur]),
                                           config_.precision));
            }
            else
            {
                row.push_back(config_.missing_value);
            }
        }

        result.row_data.push_back(std::move(row));
    }

    result.ok        = true;
    result.row_count = result.row_data.size();

    return result;
}

}   // namespace spectra::adapters::ros2
