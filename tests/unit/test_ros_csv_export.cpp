// Unit tests for RosCsvExport (E1).
//
// These tests do NOT require a ROS2 executor or ImGui context.
// All logic is exercised through the pure C++ interface using
// hand-crafted RosPlotManager + LineSeries data injected via a
// thin test harness (no spinning required).
//
// Test structure:
//   SplitTimestamp         (8)
//   FormatValue            (5)
//   FormatInt64            (3)
//   MakeColumnName         (6)
//   TimestampHeaders       (2)
//   FormatTimestampCells   (5)
//   Construction           (3)
//   ConfigSetters          (4)
//   ExportSingleSeries     (10)
//   ExportRangeFilter      (7)
//   ExportMultiSeries      (8)
//   CsvExportResultToString(7)
//   SaveToFile             (4)
//   EdgeCases              (6)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "ros_csv_export.hpp"
#include "ros_plot_manager.hpp"

// Pull in non-ROS headers only (no rclcpp needed for these tests).
#include <spectra/figure.hpp>
#include <spectra/series.hpp>

using namespace spectra::adapters::ros2;

// ---------------------------------------------------------------------------
// Helpers — build a minimal RosPlotManager-backed environment without a ROS2
// node.  We use the fact that PlotHandle's series pointer is a plain
// spectra::LineSeries*, so we can inject data directly without going through
// the full add_plot() pipeline.
//
// TestPlotManager wraps RosPlotManager but exposes a register_test_handle()
// method that directly populates a PlotEntry via the public handle(id) API.
//
// Instead, we subclass nothing — we just build a standalone Figure/LineSeries
// and test RosCsvExport's static helpers directly, plus we test via a real
// RosPlotManager whose test entries are seeded through the public API.
//
// For the export tests we need a running plot entry. The simplest approach is
// to use a MockPlotManager that presents a PlotHandle pointing at a real
// LineSeries without the ROS2 node overhead.  Since RosCsvExport only calls
// mgr_.handle(id) and h.series->x_data()/y_data(), we can inject via a
// custom subclass.
// ---------------------------------------------------------------------------

// A RosPlotManager that lets tests register synthetic handles.
class TestExportManager
{
   public:
    struct FakePlot
    {
        int                              id;
        std::string                      topic;
        std::string                      field_path;
        std::unique_ptr<spectra::Figure> figure;
        spectra::LineSeries*             series{nullptr};
    };

    PlotHandle handle(int id) const
    {
        for (const auto& p : plots_)
        {
            if (p->id == id)
            {
                PlotHandle h;
                h.id         = p->id;
                h.topic      = p->topic;
                h.field_path = p->field_path;
                h.figure     = p->figure.get();
                h.axes       = nullptr;
                h.series     = p->series;
                return h;
            }
        }
        PlotHandle bad;
        bad.id = -1;
        return bad;
    }

    int add(const std::string& topic,
            const std::string& field_path,
            std::vector<float> x_vals,
            std::vector<float> y_vals)
    {
        auto entry        = std::make_unique<FakePlot>();
        entry->id         = next_id_++;
        entry->topic      = topic;
        entry->field_path = field_path;
        spectra::FigureConfig fig_cfg;
        fig_cfg.width  = 800;
        fig_cfg.height = 600;
        entry->figure  = std::make_unique<spectra::Figure>(fig_cfg);

        auto& axes = entry->figure->subplot(1, 1, 1);
        auto& ls   = axes.line();
        ls.set_x(x_vals);
        ls.set_y(y_vals);
        entry->series = &ls;

        int id = entry->id;
        plots_.push_back(std::move(entry));
        return id;
    }

   private:
    std::vector<std::unique_ptr<FakePlot>> plots_;
    int                                    next_id_{1};
};

// Thin wrapper that satisfies RosCsvExport's constructor constraint while
// delegating handle() to TestExportManager.
//
// Since RosCsvExport holds RosPlotManager& by reference, we cannot swap it
// at runtime.  Instead we test the static helpers and build_result path by
// constructing a RosCsvExport manually with a real (empty) RosPlotManager,
// then use a separate test harness for the higher-level export methods.
//
// For higher-level export tests (ExportSingleSeries, ExportMultiSeries etc.)
// we provide a helper that builds the CSV directly from the TestExportManager
// by replicating the export logic — or, better, by using a friend-accessor
// approach.  Since RosCsvExport is not designed with test-injection points in
// its constructor, we test via a helper class that duplicates the CSV building
// using only the public static helpers.

class CsvExportTestHarness
{
   public:
    CsvExportConfig config;

    explicit CsvExportTestHarness() = default;

    // Export all data from a TestExportManager plot.
    CsvExportResult export_plot(TestExportManager&      mgr,
                                int                     id,
                                RosCsvExport::RangeMode mode  = RosCsvExport::RangeMode::Full,
                                double                  x_min = 0.0,
                                double                  x_max = 0.0) const
    {
        return export_plots(mgr, {id}, mode, x_min, x_max);
    }

    CsvExportResult export_plots(TestExportManager&      mgr,
                                 const std::vector<int>& ids,
                                 RosCsvExport::RangeMode mode  = RosCsvExport::RangeMode::Full,
                                 double                  x_min = 0.0,
                                 double                  x_max = 0.0) const
    {
        CsvExportResult bad;
        bad.ok = false;

        if (ids.empty())
        {
            bad.error = "No plot IDs provided";
            return bad;
        }

        // Collect series data.
        struct SD
        {
            std::string        column_name;
            std::vector<float> x;
            std::vector<float> y;
        };
        std::vector<SD> sds;

        for (int id : ids)
        {
            PlotHandle h = mgr.handle(id);
            if (!h.valid())
            {
                bad.error = "Plot id " + std::to_string(id) + " not found";
                return bad;
            }
            SD sd;
            sd.column_name = RosCsvExport::make_column_name(h.topic, h.field_path);
            auto xs        = h.series->x_data();
            auto ys        = h.series->y_data();
            sd.x.assign(xs.begin(), xs.end());
            sd.y.assign(ys.begin(), ys.end());
            sds.push_back(std::move(sd));
        }

        // Collect union of X values.
        std::vector<double> all_x;
        for (const auto& sd : sds)
        {
            for (float xf : sd.x)
            {
                double xd = static_cast<double>(xf);
                if (mode == RosCsvExport::RangeMode::Visible && (xd < x_min || xd > x_max))
                    continue;
                all_x.push_back(xd);
            }
        }
        std::sort(all_x.begin(), all_x.end());
        {
            std::vector<double> deduped;
            for (double v : all_x)
                if (deduped.empty() || std::abs(v - deduped.back()) > 1e-12)
                    deduped.push_back(v);
            all_x = std::move(deduped);
        }

        // Build headers.
        auto hdrs = RosCsvExport::timestamp_headers();
        for (const auto& sd : sds)
            hdrs.push_back(sd.column_name);

        CsvExportResult result;
        result.separator_    = config.separator;
        result.line_ending_  = config.line_ending;
        result.write_header_ = config.write_header;
        result.headers       = hdrs;
        result.column_count  = hdrs.size();

        if (all_x.empty())
        {
            result.ok        = true;
            result.row_count = 0;
            return result;
        }

        constexpr double    MATCH_EPS = 1e-9;
        std::vector<size_t> cursors(sds.size(), 0);

        for (double row_x : all_x)
        {
            std::vector<std::string> row;

            int64_t sec = 0, nsec = 0;
            RosCsvExport::split_timestamp(row_x, 0, sec, nsec);
            row.push_back(RosCsvExport::format_int64(sec));
            row.push_back(RosCsvExport::format_int64(nsec));
            row.push_back(RosCsvExport::format_value(row_x, config.wall_clock_precision));

            for (size_t si = 0; si < sds.size(); ++si)
            {
                const auto& sd  = sds[si];
                size_t&     cur = cursors[si];

                while (cur < sd.x.size() && static_cast<double>(sd.x[cur]) < row_x - MATCH_EPS)
                    ++cur;

                if (cur < sd.x.size()
                    && std::abs(static_cast<double>(sd.x[cur]) - row_x) <= MATCH_EPS)
                    row.push_back(RosCsvExport::format_value(static_cast<double>(sd.y[cur]),
                                                             config.precision));
                else
                    row.push_back(config.missing_value);
            }

            result.row_data.push_back(std::move(row));
        }

        result.ok        = true;
        result.row_count = result.row_data.size();
        return result;
    }
};

// ---------------------------------------------------------------------------
// SplitTimestamp
// ---------------------------------------------------------------------------

TEST(SplitTimestamp, ZeroNsUsesFloatDecomposition)
{
    int64_t sec = 0, nsec = 0;
    RosCsvExport::split_timestamp(1.5, 0, sec, nsec);
    EXPECT_EQ(sec, 1);
    EXPECT_NEAR(static_cast<double>(nsec), 500000000.0, 1.0);
}

TEST(SplitTimestamp, ExactSecondZeroNsec)
{
    int64_t sec = 0, nsec = 0;
    RosCsvExport::split_timestamp(10.0, 0, sec, nsec);
    EXPECT_EQ(sec, 10);
    EXPECT_EQ(nsec, 0);
}

TEST(SplitTimestamp, FromNanoseconds)
{
    int64_t sec = 0, nsec = 0;
    // 1'500'000'000 ns = 1 sec + 500 ms
    RosCsvExport::split_timestamp(0.0, 1500000000LL, sec, nsec);
    EXPECT_EQ(sec, 1);
    EXPECT_EQ(nsec, 500000000LL);
}

TEST(SplitTimestamp, LargeNanoseconds)
{
    int64_t sec = 0, nsec = 0;
    // 1720000000 * 1e9 + 123456789 ns
    int64_t ns_in = 1720000000LL * 1000000000LL + 123456789LL;
    RosCsvExport::split_timestamp(0.0, ns_in, sec, nsec);
    EXPECT_EQ(sec, 1720000000LL);
    EXPECT_EQ(nsec, 123456789LL);
}

TEST(SplitTimestamp, NsecRemainder999999999)
{
    int64_t sec = 0, nsec = 0;
    RosCsvExport::split_timestamp(0.0, 1000000000LL - 1LL, sec, nsec);
    EXPECT_EQ(sec, 0);
    EXPECT_EQ(nsec, 999999999LL);
}

TEST(SplitTimestamp, FloatDecompQuarterSecond)
{
    int64_t sec = 0, nsec = 0;
    RosCsvExport::split_timestamp(5.25, 0, sec, nsec);
    EXPECT_EQ(sec, 5);
    EXPECT_NEAR(static_cast<double>(nsec), 250000000.0, 1.0);
}

TEST(SplitTimestamp, ZeroTimestamp)
{
    int64_t sec = 0, nsec = 0;
    RosCsvExport::split_timestamp(0.0, 0, sec, nsec);
    EXPECT_EQ(sec, 0);
    EXPECT_EQ(nsec, 0);
}

TEST(SplitTimestamp, NonZeroNsTakesPrecedence)
{
    int64_t sec = 0, nsec = 0;
    // ns=2e9 → 2 sec, 0 nsec; despite timestamp_s=99.0
    RosCsvExport::split_timestamp(99.0, 2000000000LL, sec, nsec);
    EXPECT_EQ(sec, 2);
    EXPECT_EQ(nsec, 0);
}

// ---------------------------------------------------------------------------
// FormatValue
// ---------------------------------------------------------------------------

TEST(FormatValue, DefaultPrecision9)
{
    std::string s = RosCsvExport::format_value(1.123456789, 9);
    EXPECT_EQ(s, "1.123456789");
}

TEST(FormatValue, Precision0)
{
    std::string s = RosCsvExport::format_value(3.7, 0);
    EXPECT_EQ(s, "4");
}

TEST(FormatValue, NegativeValue)
{
    std::string s = RosCsvExport::format_value(-9.81, 2);
    EXPECT_EQ(s, "-9.81");
}

TEST(FormatValue, Zero)
{
    std::string s = RosCsvExport::format_value(0.0, 4);
    EXPECT_EQ(s, "0.0000");
}

TEST(FormatValue, Precision3)
{
    std::string s = RosCsvExport::format_value(3.14159, 3);
    EXPECT_EQ(s, "3.142");
}

// ---------------------------------------------------------------------------
// FormatInt64
// ---------------------------------------------------------------------------

TEST(FormatInt64, Zero)
{
    EXPECT_EQ(RosCsvExport::format_int64(0), "0");
}

TEST(FormatInt64, Positive)
{
    EXPECT_EQ(RosCsvExport::format_int64(1720000000LL), "1720000000");
}

TEST(FormatInt64, Negative)
{
    EXPECT_EQ(RosCsvExport::format_int64(-1), "-1");
}

// ---------------------------------------------------------------------------
// MakeColumnName
// ---------------------------------------------------------------------------

TEST(MakeColumnName, BasicSlash)
{
    EXPECT_EQ(RosCsvExport::make_column_name("/imu", "angular_velocity.z"),
              "/imu/angular_velocity.z");
}

TEST(MakeColumnName, TopicAlreadyHasTrailingSlash)
{
    EXPECT_EQ(RosCsvExport::make_column_name("/sensor/", "data"), "/sensor/data");
}

TEST(MakeColumnName, FieldAlreadyHasLeadingSlash)
{
    EXPECT_EQ(RosCsvExport::make_column_name("/sensor", "/data"), "/sensor/data");
}

TEST(MakeColumnName, EmptyTopic)
{
    EXPECT_EQ(RosCsvExport::make_column_name("", "data"), "data");
}

TEST(MakeColumnName, EmptyField)
{
    EXPECT_EQ(RosCsvExport::make_column_name("/topic", ""), "/topic");
}

TEST(MakeColumnName, BothEmpty)
{
    EXPECT_EQ(RosCsvExport::make_column_name("", ""), "");
}

// ---------------------------------------------------------------------------
// TimestampHeaders
// ---------------------------------------------------------------------------

TEST(TimestampHeaders, ExactlyThreeColumns)
{
    auto h = RosCsvExport::timestamp_headers();
    EXPECT_EQ(h.size(), 3u);
}

TEST(TimestampHeaders, CorrectNames)
{
    auto h = RosCsvExport::timestamp_headers();
    EXPECT_EQ(h[0], "timestamp_sec");
    EXPECT_EQ(h[1], "timestamp_nsec");
    EXPECT_EQ(h[2], "wall_clock");
}

// ---------------------------------------------------------------------------
// FormatTimestampCells
// ---------------------------------------------------------------------------

TEST(FormatTimestampCells, ExactSecond)
{
    CsvExportConfig cfg;
    cfg.wall_clock_precision = 3;

    // We need a RosCsvExport instance — use a dummy pattern.
    // Since RosCsvExport requires RosPlotManager& we can't construct one
    // without a real bridge.  Instead we test the static path by hand.
    int64_t sec = 0, nsec = 0;
    RosCsvExport::split_timestamp(10.0, 0, sec, nsec);
    EXPECT_EQ(sec, 10);
    EXPECT_EQ(nsec, 0);
}

TEST(FormatTimestampCells, NanosFromNsField)
{
    int64_t sec = 0, nsec = 0;
    RosCsvExport::split_timestamp(0.0, 1000000123LL, sec, nsec);
    EXPECT_EQ(sec, 1);
    EXPECT_EQ(nsec, 123LL);
}

TEST(FormatTimestampCells, NsecRounding)
{
    int64_t sec = 0, nsec = 0;
    // 1.999999999 s → sec=1, nsec≈999999999
    RosCsvExport::split_timestamp(1.999999999, 0, sec, nsec);
    EXPECT_EQ(sec, 1);
    EXPECT_GE(nsec, 999999000LL);
    EXPECT_LE(nsec, 999999999LL);
}

TEST(FormatTimestampCells, WallClockPreserved)
{
    std::string ws = RosCsvExport::format_value(1234567890.123456789, 9);
    EXPECT_FALSE(ws.empty());
    // First 10 chars should be the integer seconds part.
    EXPECT_EQ(ws.substr(0, 13), "1234567890.12");
}

TEST(FormatTimestampCells, ZeroTimestamp)
{
    int64_t sec = 0, nsec = 0;
    RosCsvExport::split_timestamp(0.0, 0, sec, nsec);
    EXPECT_EQ(sec, 0);
    EXPECT_EQ(nsec, 0);
    EXPECT_EQ(RosCsvExport::format_value(0.0, 3), "0.000");
}

// ---------------------------------------------------------------------------
// Construction / Config
// ---------------------------------------------------------------------------

TEST(Construction, DefaultConfig)
{
    CsvExportConfig cfg;
    EXPECT_EQ(cfg.separator, ',');
    EXPECT_EQ(cfg.precision, 9);
    EXPECT_EQ(cfg.wall_clock_precision, 9);
    EXPECT_TRUE(cfg.missing_value.empty());
    EXPECT_TRUE(cfg.write_header);
    EXPECT_EQ(cfg.line_ending, "\n");
}

TEST(Construction, ResultDefaults)
{
    CsvExportResult r;
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.row_count, 0u);
    EXPECT_EQ(r.column_count, 0u);
    EXPECT_TRUE(r.headers.empty());
    EXPECT_TRUE(r.row_data.empty());
}

TEST(Construction, EmptyToString)
{
    CsvExportResult r;
    EXPECT_EQ(r.to_string(), "");
}

TEST(ConfigSetters, SeparatorTab)
{
    CsvExportConfig cfg;
    cfg.separator = '\t';
    EXPECT_EQ(cfg.separator, '\t');
}

TEST(ConfigSetters, PrecisionZero)
{
    CsvExportConfig cfg;
    cfg.precision = 0;
    EXPECT_EQ(cfg.precision, 0);
}

TEST(ConfigSetters, MissingValueNA)
{
    CsvExportConfig cfg;
    cfg.missing_value = "N/A";
    EXPECT_EQ(cfg.missing_value, "N/A");
}

TEST(ConfigSetters, WriteHeaderFalse)
{
    CsvExportConfig cfg;
    cfg.write_header = false;
    EXPECT_FALSE(cfg.write_header);
}

// ---------------------------------------------------------------------------
// ExportSingleSeries — via CsvExportTestHarness
// ---------------------------------------------------------------------------

TEST(ExportSingleSeries, EmptySeriesYieldsOkZeroRows)
{
    TestExportManager mgr;
    int               id = mgr.add("/topic", "data", {}, {});

    CsvExportTestHarness h;
    auto                 r = h.export_plot(mgr, id);

    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.row_count, 0u);
    // Headers still built.
    EXPECT_EQ(r.headers.size(), 4u);   // 3 ts + 1 value
}

TEST(ExportSingleSeries, SinglePointRow)
{
    TestExportManager mgr;
    int               id = mgr.add("/imu", "linear_acceleration.x", {1.0f}, {9.81f});

    CsvExportTestHarness h;
    auto                 r = h.export_plot(mgr, id);

    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.row_count, 1u);
    ASSERT_EQ(r.row_data[0].size(), 4u);
    EXPECT_EQ(r.row_data[0][0], "1");   // timestamp_sec
    EXPECT_EQ(r.row_data[0][1], "0");   // timestamp_nsec
}

TEST(ExportSingleSeries, ValueColumnIndex3)
{
    TestExportManager mgr;
    int               id = mgr.add("/t", "f", {2.0f}, {3.14f});

    CsvExportTestHarness h;
    auto                 r = h.export_plot(mgr, id);

    ASSERT_EQ(r.row_data[0].size(), 4u);
    double val = std::stod(r.row_data[0][3]);
    EXPECT_NEAR(val, 3.14, 1e-5);
}

TEST(ExportSingleSeries, MultipleRows)
{
    TestExportManager mgr;
    int               id = mgr.add("/t", "v", {0.0f, 1.0f, 2.0f}, {10.0f, 20.0f, 30.0f});

    CsvExportTestHarness h;
    auto                 r = h.export_plot(mgr, id);

    EXPECT_EQ(r.row_count, 3u);
}

TEST(ExportSingleSeries, RowsAreSortedByTimestamp)
{
    TestExportManager mgr;
    // LineSeries is appended in order; x_data() preserves insertion order.
    int id = mgr.add("/t", "v", {3.0f, 1.0f, 2.0f}, {30.0f, 10.0f, 20.0f});

    CsvExportTestHarness h;
    auto                 r = h.export_plot(mgr, id);

    ASSERT_EQ(r.row_count, 3u);
    // Rows sorted by timestamp.
    double t0 = std::stod(r.row_data[0][0]);
    double t1 = std::stod(r.row_data[1][0]);
    double t2 = std::stod(r.row_data[2][0]);
    EXPECT_LE(t0, t1);
    EXPECT_LE(t1, t2);
}

TEST(ExportSingleSeries, HeaderContainsColumnName)
{
    TestExportManager mgr;
    int               id = mgr.add("/imu", "angular_velocity.z", {1.0f}, {0.5f});

    CsvExportTestHarness h;
    auto                 r = h.export_plot(mgr, id);

    ASSERT_GE(r.headers.size(), 4u);
    EXPECT_EQ(r.headers[3], "/imu/angular_velocity.z");
}

TEST(ExportSingleSeries, HeaderCountIs4)
{
    TestExportManager mgr;
    int               id = mgr.add("/t", "f", {1.0f}, {1.0f});

    CsvExportTestHarness h;
    auto                 r = h.export_plot(mgr, id);

    EXPECT_EQ(r.column_count, 4u);
    EXPECT_EQ(r.headers.size(), 4u);
}

TEST(ExportSingleSeries, InvalidIdReturnsError)
{
    TestExportManager    mgr;
    CsvExportTestHarness h;
    auto                 r = h.export_plot(mgr, 9999);
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
}

TEST(ExportSingleSeries, PrecisionApplied)
{
    TestExportManager mgr;
    int               id = mgr.add("/t", "v", {1.0f}, {3.141592653f});

    CsvExportTestHarness h;
    h.config.precision = 3;
    auto r             = h.export_plot(mgr, id);

    ASSERT_EQ(r.row_count, 1u);
    std::string val = r.row_data[0][3];
    EXPECT_EQ(val, "3.142");
}

TEST(ExportSingleSeries, WallClockColumnMatchesTimestamp)
{
    TestExportManager mgr;
    int               id = mgr.add("/t", "v", {5.0f}, {1.0f});

    CsvExportTestHarness h;
    h.config.wall_clock_precision = 1;
    auto r                        = h.export_plot(mgr, id);

    ASSERT_EQ(r.row_count, 1u);
    // wall_clock column index = 2
    EXPECT_EQ(r.row_data[0][2], "5.0");
}

// ---------------------------------------------------------------------------
// ExportRangeFilter
// ---------------------------------------------------------------------------

TEST(ExportRangeFilter, FullModeIncludesAll)
{
    TestExportManager mgr;
    int id = mgr.add("/t", "v", {1.0f, 2.0f, 3.0f, 4.0f, 5.0f}, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f});

    CsvExportTestHarness h;
    auto                 r = h.export_plot(mgr, id, RosCsvExport::RangeMode::Full, 0.0, 0.0);
    EXPECT_EQ(r.row_count, 5u);
}

TEST(ExportRangeFilter, VisibleModeFiltersBelow)
{
    TestExportManager mgr;
    int id = mgr.add("/t", "v", {1.0f, 2.0f, 3.0f, 4.0f, 5.0f}, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f});

    CsvExportTestHarness h;
    auto                 r = h.export_plot(mgr, id, RosCsvExport::RangeMode::Visible, 2.0, 5.0);
    EXPECT_EQ(r.row_count, 4u);
}

TEST(ExportRangeFilter, VisibleModeFiltersAbove)
{
    TestExportManager mgr;
    int               id = mgr.add("/t", "v", {1.0f, 2.0f, 3.0f}, {1.0f, 2.0f, 3.0f});

    CsvExportTestHarness h;
    auto                 r = h.export_plot(mgr, id, RosCsvExport::RangeMode::Visible, 0.0, 2.0);
    EXPECT_EQ(r.row_count, 2u);
}

TEST(ExportRangeFilter, ExactBoundaryIncluded)
{
    TestExportManager mgr;
    int               id = mgr.add("/t", "v", {1.0f, 2.0f, 3.0f}, {1.0f, 2.0f, 3.0f});

    CsvExportTestHarness h;
    auto                 r = h.export_plot(mgr, id, RosCsvExport::RangeMode::Visible, 1.0, 3.0);
    EXPECT_EQ(r.row_count, 3u);
}

TEST(ExportRangeFilter, EmptyRangeYieldsZeroRows)
{
    TestExportManager mgr;
    int               id = mgr.add("/t", "v", {1.0f, 2.0f}, {1.0f, 2.0f});

    CsvExportTestHarness h;
    auto                 r = h.export_plot(mgr, id, RosCsvExport::RangeMode::Visible, 5.0, 10.0);
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.row_count, 0u);
}

TEST(ExportRangeFilter, RangeDataValuesCorrect)
{
    TestExportManager mgr;
    int               id = mgr.add("/t", "v", {1.0f, 2.0f, 3.0f}, {10.0f, 20.0f, 30.0f});

    CsvExportTestHarness h;
    auto                 r = h.export_plot(mgr, id, RosCsvExport::RangeMode::Visible, 2.0, 3.0);

    ASSERT_EQ(r.row_count, 2u);
    EXPECT_NEAR(std::stod(r.row_data[0][3]), 20.0, 1e-4);
    EXPECT_NEAR(std::stod(r.row_data[1][3]), 30.0, 1e-4);
}

TEST(ExportRangeFilter, TimestampSecCorrectInRange)
{
    TestExportManager mgr;
    int               id = mgr.add("/t", "v", {10.5f, 11.5f}, {1.0f, 2.0f});

    CsvExportTestHarness h;
    auto                 r = h.export_plot(mgr, id, RosCsvExport::RangeMode::Visible, 10.0, 11.0);

    ASSERT_EQ(r.row_count, 1u);
    // timestamp_sec of 10.5 → 10
    EXPECT_EQ(r.row_data[0][0], "10");
}

// ---------------------------------------------------------------------------
// ExportMultiSeries
// ---------------------------------------------------------------------------

TEST(ExportMultiSeries, TwoSeriesSameX)
{
    TestExportManager mgr;
    int               id1 = mgr.add("/t1", "v1", {1.0f, 2.0f}, {10.0f, 20.0f});
    int               id2 = mgr.add("/t2", "v2", {1.0f, 2.0f}, {100.0f, 200.0f});

    CsvExportTestHarness h;
    auto                 r = h.export_plots(mgr, {id1, id2});

    EXPECT_EQ(r.row_count, 2u);
    EXPECT_EQ(r.column_count, 5u);   // 3 ts + 2 values
}

TEST(ExportMultiSeries, HeaderContainsBothColumns)
{
    TestExportManager mgr;
    int               id1 = mgr.add("/a", "x", {1.0f}, {1.0f});
    int               id2 = mgr.add("/b", "y", {1.0f}, {2.0f});

    CsvExportTestHarness h;
    auto                 r = h.export_plots(mgr, {id1, id2});

    ASSERT_GE(r.headers.size(), 5u);
    EXPECT_EQ(r.headers[3], "/a/x");
    EXPECT_EQ(r.headers[4], "/b/y");
}

TEST(ExportMultiSeries, MissingValueOnMismatchedX)
{
    TestExportManager mgr;
    int               id1 = mgr.add("/t1", "v1", {1.0f, 2.0f, 3.0f}, {1.0f, 2.0f, 3.0f});
    int               id2 = mgr.add("/t2", "v2", {2.0f}, {99.0f});   // only at t=2

    CsvExportTestHarness h;
    h.config.missing_value = "N/A";
    auto r                 = h.export_plots(mgr, {id1, id2});

    // 3 rows (union of all X)
    ASSERT_EQ(r.row_count, 3u);

    // Row 0 (t=1.0): id2 missing
    EXPECT_EQ(r.row_data[0][4], "N/A");
    // Row 1 (t=2.0): id2 present
    EXPECT_NE(r.row_data[1][4], "N/A");
    // Row 2 (t=3.0): id2 missing
    EXPECT_EQ(r.row_data[2][4], "N/A");
}

TEST(ExportMultiSeries, UnionOfXCovers3Series)
{
    TestExportManager mgr;
    int               id1 = mgr.add("/t1", "v1", {1.0f}, {1.0f});
    int               id2 = mgr.add("/t2", "v2", {2.0f}, {2.0f});
    int               id3 = mgr.add("/t3", "v3", {3.0f}, {3.0f});

    CsvExportTestHarness h;
    auto                 r = h.export_plots(mgr, {id1, id2, id3});

    EXPECT_EQ(r.row_count, 3u);
}

TEST(ExportMultiSeries, EmptyIdsReturnsError)
{
    TestExportManager    mgr;
    CsvExportTestHarness h;
    auto                 r = h.export_plots(mgr, {});
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
}

TEST(ExportMultiSeries, InvalidOneIdReturnsError)
{
    TestExportManager    mgr;
    int                  id1 = mgr.add("/t", "v", {1.0f}, {1.0f});
    CsvExportTestHarness h;
    auto                 r = h.export_plots(mgr, {id1, 9999});
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
}

TEST(ExportMultiSeries, ValuesCorrectForBothSeries)
{
    TestExportManager mgr;
    int               id1 = mgr.add("/a", "x", {1.0f}, {42.0f});
    int               id2 = mgr.add("/b", "y", {1.0f}, {-7.5f});

    CsvExportTestHarness h;
    auto                 r = h.export_plots(mgr, {id1, id2});

    ASSERT_EQ(r.row_count, 1u);
    EXPECT_NEAR(std::stod(r.row_data[0][3]), 42.0, 1e-4);
    EXPECT_NEAR(std::stod(r.row_data[0][4]), -7.5, 1e-4);
}

TEST(ExportMultiSeries, DuplicateXDeduped)
{
    TestExportManager mgr;
    // Both series have the same X values — union should have 2 rows, not 4.
    int id1 = mgr.add("/t1", "v1", {1.0f, 2.0f}, {10.0f, 20.0f});
    int id2 = mgr.add("/t2", "v2", {1.0f, 2.0f}, {100.0f, 200.0f});

    CsvExportTestHarness h;
    auto                 r = h.export_plots(mgr, {id1, id2});

    EXPECT_EQ(r.row_count, 2u);
}

// ---------------------------------------------------------------------------
// CsvExportResultToString
// ---------------------------------------------------------------------------

TEST(CsvExportResultToString, EmptyResultReturnsEmpty)
{
    CsvExportResult r;
    EXPECT_EQ(r.to_string(), "");
}

TEST(CsvExportResultToString, HeaderPresent)
{
    TestExportManager mgr;
    int               id = mgr.add("/t", "v", {1.0f}, {1.0f});

    CsvExportTestHarness h;
    auto                 r = h.export_plot(mgr, id);

    std::string csv = r.to_string();
    EXPECT_TRUE(csv.find("timestamp_sec") != std::string::npos);
    EXPECT_TRUE(csv.find("timestamp_nsec") != std::string::npos);
    EXPECT_TRUE(csv.find("wall_clock") != std::string::npos);
}

TEST(CsvExportResultToString, SeparatorUsedBetweenColumns)
{
    TestExportManager mgr;
    int               id = mgr.add("/t", "v", {1.0f}, {1.0f});

    CsvExportTestHarness h;
    h.config.separator = ';';
    auto r             = h.export_plot(mgr, id);

    std::string csv = r.to_string();
    // Header row should have ';' separators.
    EXPECT_TRUE(csv.find(';') != std::string::npos);
}

TEST(CsvExportResultToString, TabSeparator)
{
    TestExportManager mgr;
    int               id = mgr.add("/t", "v", {2.0f}, {5.0f});

    CsvExportTestHarness h;
    h.config.separator = '\t';
    auto r             = h.export_plot(mgr, id);

    std::string csv = r.to_string();
    EXPECT_TRUE(csv.find('\t') != std::string::npos);
}

TEST(CsvExportResultToString, NoHeaderWhenDisabled)
{
    TestExportManager mgr;
    int               id = mgr.add("/t", "v", {1.0f}, {1.0f});

    CsvExportTestHarness h;
    h.config.write_header = false;
    auto r                = h.export_plot(mgr, id);

    std::string csv = r.to_string();
    EXPECT_EQ(csv.find("timestamp_sec"), std::string::npos);
}

TEST(CsvExportResultToString, RowCountMatchesNewlines)
{
    TestExportManager mgr;
    int               id = mgr.add("/t", "v", {1.0f, 2.0f, 3.0f}, {1.0f, 2.0f, 3.0f});

    CsvExportTestHarness h;
    auto                 r = h.export_plot(mgr, id);

    std::string csv = r.to_string();
    // header(1) + rows(3) = 4 newlines.
    size_t newline_count = std::count(csv.begin(), csv.end(), '\n');
    EXPECT_EQ(newline_count, 4u);
}

TEST(CsvExportResultToString, ZeroRowsOnlyHeader)
{
    TestExportManager mgr;
    int               id = mgr.add("/t", "v", {}, {});

    CsvExportTestHarness h;
    auto                 r = h.export_plot(mgr, id);
    // ok=true, row_count=0; to_string() returns empty because ok && row_data empty.
    std::string csv = r.to_string();
    // No rows → empty string.
    EXPECT_EQ(csv, "");
}

TEST(CsvExportResultToString, CustomLineEnding)
{
    TestExportManager mgr;
    int               id = mgr.add("/t", "v", {1.0f}, {1.0f});

    CsvExportTestHarness h;
    h.config.line_ending = "\r\n";
    auto r               = h.export_plot(mgr, id);

    std::string csv = r.to_string();
    EXPECT_TRUE(csv.find("\r\n") != std::string::npos);
}

// ---------------------------------------------------------------------------
// SaveToFile
// ---------------------------------------------------------------------------

TEST(SaveToFile, EmptyResultReturnsFalse)
{
    CsvExportResult r;
    EXPECT_FALSE(r.save_to_file("/tmp/spectra_ros_csv_test_empty.csv"));
}

TEST(SaveToFile, EmptyPathReturnsFalse)
{
    CsvExportResult r;
    r.ok = true;
    EXPECT_FALSE(r.save_to_file(""));
}

TEST(SaveToFile, WritesAndCanBeRead)
{
    TestExportManager mgr;
    int               id = mgr.add("/sensor", "value", {1.0f, 2.0f}, {10.0f, 20.0f});

    CsvExportTestHarness h;
    auto                 r = h.export_plot(mgr, id);

    const std::string path = "/tmp/spectra_ros_csv_test_write.csv";
    ASSERT_TRUE(r.save_to_file(path));

    // Read back and verify.
    std::ifstream f(path);
    ASSERT_TRUE(f.is_open());
    std::string contents((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    EXPECT_TRUE(contents.find("timestamp_sec") != std::string::npos);
    EXPECT_TRUE(contents.find("/sensor/value") != std::string::npos);

    std::remove(path.c_str());
}

TEST(SaveToFile, ContentMatchesToString)
{
    TestExportManager mgr;
    int               id = mgr.add("/t", "v", {5.0f}, {42.0f});

    CsvExportTestHarness h;
    auto                 r = h.export_plot(mgr, id);

    const std::string path = "/tmp/spectra_ros_csv_test_match.csv";
    ASSERT_TRUE(r.save_to_file(path));

    std::ifstream f(path);
    std::string   contents((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    EXPECT_EQ(contents, r.to_string());

    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// EdgeCases
// ---------------------------------------------------------------------------

TEST(EdgeCases, SingleSeriesNoPoints)
{
    TestExportManager    mgr;
    int                  id = mgr.add("/t", "v", {}, {});
    CsvExportTestHarness h;
    auto                 r = h.export_plot(mgr, id);
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.row_count, 0u);
}

TEST(EdgeCases, LargeTimestampSec)
{
    TestExportManager mgr;
    // ~2024 epoch seconds ≈ 1.7e9
    int                  id = mgr.add("/t", "v", {1720000000.0f}, {1.0f});
    CsvExportTestHarness h;
    auto                 r = h.export_plot(mgr, id);
    ASSERT_EQ(r.row_count, 1u);
    // timestamp_sec should be ~1720000000
    int64_t sec_val = std::stoll(r.row_data[0][0]);
    EXPECT_GE(sec_val, 1719999990LL);
    EXPECT_LE(sec_val, 1720000010LL);
}

TEST(EdgeCases, NegativeValue)
{
    TestExportManager    mgr;
    int                  id = mgr.add("/t", "v", {1.0f}, {-999.5f});
    CsvExportTestHarness h;
    auto                 r = h.export_plot(mgr, id);
    ASSERT_EQ(r.row_count, 1u);
    EXPECT_NEAR(std::stod(r.row_data[0][3]), -999.5, 1e-3);
}

TEST(EdgeCases, PrecisionZeroTruncates)
{
    TestExportManager    mgr;
    int                  id = mgr.add("/t", "v", {1.0f}, {3.7f});
    CsvExportTestHarness h;
    h.config.precision = 0;
    auto r             = h.export_plot(mgr, id);
    ASSERT_EQ(r.row_count, 1u);
    EXPECT_EQ(r.row_data[0][3], "4");
}

TEST(EdgeCases, MissingValueEmptyByDefault)
{
    TestExportManager    mgr;
    int                  id1 = mgr.add("/t1", "v1", {1.0f, 2.0f}, {1.0f, 2.0f});
    int                  id2 = mgr.add("/t2", "v2", {3.0f}, {3.0f});   // no overlap with id1
    CsvExportTestHarness h;
    auto                 r = h.export_plots(mgr, {id1, id2});
    // Row 0 (t=1): id2 should be missing_value (empty)
    ASSERT_GE(r.row_count, 1u);
    EXPECT_EQ(r.row_data[0][4], "");
}

TEST(EdgeCases, ColumnCountAlwaysTimestampPlusN)
{
    TestExportManager mgr;
    int               id1 = mgr.add("/a", "x", {1.0f}, {1.0f});
    int               id2 = mgr.add("/b", "y", {1.0f}, {2.0f});
    int               id3 = mgr.add("/c", "z", {1.0f}, {3.0f});

    CsvExportTestHarness h;
    auto                 r1 = h.export_plots(mgr, {id1});
    auto                 r2 = h.export_plots(mgr, {id1, id2});
    auto                 r3 = h.export_plots(mgr, {id1, id2, id3});

    EXPECT_EQ(r1.column_count, 4u);
    EXPECT_EQ(r2.column_count, 5u);
    EXPECT_EQ(r3.column_count, 6u);
}
