// Double-precision data API (audit finding P2-6): data that float cannot hold
// precisely (Unix timestamps, 1e7 + small variations) is stored as float
// offsets from a double origin, so the GPU upload, axis limits, picking and
// readouts keep full precision.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <gtest/gtest.h>
#include <set>
#include <spectra/axes.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <unordered_map>
#include <vector>

#include "core/data_origin.hpp"
#include "ipc/codec.hpp"
#include "render/backend.hpp"
#include "render/renderer.hpp"
#include "ui/app/figure_snapshot.hpp"
#include "ui/theme/theme.hpp"
#include "ui/data/clipboard_export.hpp"
#include "ui/workspace/figure_serializer.hpp"
#include "ui/workspace/workspace.hpp"

#if defined(SPECTRA_USE_IMGUI) && __has_include(<imgui.h>)
    #define SPECTRA_TEST_HOVER 1
    #include <imgui.h>

    #include "ui/input/region_select.hpp"
    #include "ui/overlay/axis_mapping.hpp"
    #include "ui/overlay/data_interaction.hpp"
#endif

using namespace spectra;

namespace
{

constexpr double kEpoch  = 1.7e9;
constexpr double kBigY   = 1.0e7;
constexpr double kSmallA = 0.01;

// t = 1.7e9 + (0..3600) s, y = 1e7 + 0.01 * sin(t)
struct TestSignal
{
    std::vector<double> t;
    std::vector<double> y;
};

TestSignal make_signal(size_t n = 3601)
{
    TestSignal s;
    s.t.resize(n);
    s.y.resize(n);
    for (size_t i = 0; i < n; ++i)
    {
        const double rel = static_cast<double>(i) * 3600.0 / static_cast<double>(n - 1);
        s.t[i]           = kEpoch + rel;
        s.y[i]           = kBigY + kSmallA * std::sin(rel / 300.0);
    }
    return s;
}

// CPU-only backend that keeps a copy of every buffer upload.
class CaptureBackend : public Backend
{
   public:
    bool init(bool) override { return true; }
    void shutdown() override {}
    void wait_idle() override {}
    bool create_surface(void*) override { return true; }
    bool create_swapchain(uint32_t, uint32_t) override { return true; }
    bool recreate_swapchain(uint32_t, uint32_t) override { return true; }
    bool create_offscreen_framebuffer(uint32_t, uint32_t) override { return true; }

    PipelineHandle create_pipeline(PipelineType) override { return PipelineHandle{next_id_++}; }
    BufferHandle   create_buffer(BufferUsage, size_t) override { return BufferHandle{next_id_++}; }
    void           destroy_buffer(BufferHandle) override {}
    void upload_buffer(BufferHandle handle, const void* data, size_t size_bytes, size_t) override
    {
        auto& dst = uploads_[handle.id];
        dst.resize(size_bytes / sizeof(float));
        std::memcpy(dst.data(), data, dst.size() * sizeof(float));
        last_ = handle.id;
    }
    TextureHandle create_texture(uint32_t, uint32_t, const uint8_t*) override
    {
        return TextureHandle{next_id_++};
    }
    void destroy_texture(TextureHandle) override {}

    bool     begin_frame(FrameProfiler*) override { return true; }
    void     end_frame(FrameProfiler*) override {}
    void     begin_render_pass(const Color&) override {}
    void     end_render_pass() override {}
    void     bind_pipeline(PipelineHandle) override {}
    void     bind_buffer(BufferHandle, uint32_t) override {}
    void     bind_index_buffer(BufferHandle) override {}
    void     bind_texture(TextureHandle, uint32_t) override {}
    void     push_constants(const SeriesPushConstants&) override {}
    void     set_viewport(float, float, float, float) override {}
    void     set_scissor(int32_t, int32_t, uint32_t, uint32_t) override {}
    void     set_line_width(float) override {}
    void     draw(uint32_t, uint32_t) override {}
    void     draw_instanced(uint32_t, uint32_t, uint32_t, uint32_t) override {}
    void     draw_indexed(uint32_t, uint32_t, int32_t) override {}
    bool     readback_framebuffer(uint8_t*, uint32_t, uint32_t) override { return false; }
    uint32_t swapchain_width() const override { return 0; }
    uint32_t swapchain_height() const override { return 0; }

    const std::vector<float>& last_upload() const { return uploads_.at(last_); }

   private:
    uint64_t                                         next_id_ = 1;
    uint64_t                                         last_    = 0;
    std::unordered_map<uint64_t, std::vector<float>> uploads_;
};

}   // namespace

// ─── Origin selection ───────────────────────────────────────────────────────

TEST(DataOrigin, OrdinaryDataNeedsNoOrigin)
{
    const std::vector<double> a = {0.0, 1.0, 2.0, 10.0};
    const std::vector<double> b = {-5.0, 5.0};
    const std::vector<double> c = {100.0, 110.0};
    EXPECT_EQ(detail::choose_data_origin(a), 0.0);
    EXPECT_EQ(detail::choose_data_origin(b), 0.0);
    EXPECT_EQ(detail::choose_data_origin(c), 0.0);
}

TEST(DataOrigin, LargeValuesWithSmallSpreadUseMidpoint)
{
    const auto s = make_signal(101);
    EXPECT_NEAR(detail::choose_data_origin(s.t), kEpoch + 1800.0, 1e-6);
    EXPECT_NEAR(detail::choose_data_origin(s.y), kBigY, kSmallA);
}

TEST(DataOrigin, IgnoresNonFiniteAndHandlesEmpty)
{
    const std::vector<double> v = {NAN, kEpoch, INFINITY, kEpoch + 2.0, -INFINITY};
    EXPECT_DOUBLE_EQ(detail::choose_data_origin(v), kEpoch + 1.0);
    EXPECT_EQ(detail::choose_data_origin(std::span<const double>{}), 0.0);
    const std::vector<double> only_nan = {NAN, NAN};
    EXPECT_EQ(detail::choose_data_origin(only_nan), 0.0);
}

TEST(DataOrigin, ConstantData)
{
    const std::vector<double> exact = {5.0, 5.0};
    EXPECT_EQ(detail::choose_data_origin(exact), 0.0);
    const std::vector<double> inexact = {kEpoch + 0.3, kEpoch + 0.3};
    EXPECT_DOUBLE_EQ(detail::choose_data_origin(inexact), kEpoch + 0.3);
}

// ─── Series storage ─────────────────────────────────────────────────────────

TEST(DoubleDataApi, LineSeriesKeepsFullPrecision)
{
    const auto s = make_signal();
    LineSeries line;
    line.set_x(std::span<const double>(s.t));
    line.set_y(std::span<const double>(s.y));

    ASSERT_EQ(line.point_count(), s.t.size());
    EXPECT_NE(line.x_offset(), 0.0);
    EXPECT_NE(line.y_offset(), 0.0);
    EXPECT_TRUE(line.x_is_sorted());
    for (size_t i = 0; i < s.t.size(); ++i)
    {
        const double x = line.x_offset() + static_cast<double>(line.x_data()[i]);
        const double y = line.y_offset() + static_cast<double>(line.y_data()[i]);
        ASSERT_NEAR(x, s.t[i], 1e-3) << i;   // float alone: ~128 s steps
        ASSERT_NEAR(y, s.y[i], 1e-8) << i;   // float alone: 1.0 steps
    }
}

TEST(DoubleDataApi, ScatterSeriesKeepsFullPrecision)
{
    const auto    s = make_signal(50);
    ScatterSeries sc(std::span<const double>(s.t), std::span<const double>(s.y));
    for (size_t i = 0; i < s.t.size(); ++i)
    {
        EXPECT_NEAR(sc.x_offset() + sc.x_data()[i], s.t[i], 1e-3);
        EXPECT_NEAR(sc.y_offset() + sc.y_data()[i], s.y[i], 1e-8);
    }
}

TEST(DoubleDataApi, OrdinaryDataKeepsPlainValues)
{
    const std::vector<double> x = {0.0, 0.5, 1.0};
    const std::vector<double> y = {-1.0, 2.0, 3.5};
    Axes                      ax;
    auto&                     line = ax.plot(x, y, "r--");
    EXPECT_EQ(line.x_offset(), 0.0);
    EXPECT_EQ(line.y_offset(), 0.0);
    EXPECT_FLOAT_EQ(line.x_data()[1], 0.5f);
    EXPECT_FLOAT_EQ(line.y_data()[2], 3.5f);
    EXPECT_EQ(line.line_style(), LineStyle::Dashed);
}

TEST(DoubleDataApi, FloatOverloadsStillChosenForBracedCalls)
{
    // Existing code such as line({}, {}) or set_x({}) must keep compiling and
    // pick the float overloads.
    Axes  ax;
    auto& line = ax.line({}, {});
    line.set_x({});
    line.set_y({});
    auto& sc = ax.scatter({}, {});
    sc.set_x({});
    EXPECT_EQ(line.point_count(), 0u);
    EXPECT_EQ(sc.point_count(), 0u);
}

TEST(DoubleDataApi, AxisLimitsShowTrueValues)
{
    const auto s = make_signal();
    Axes       ax;
    ax.plot(std::span<const double>(s.t), std::span<const double>(s.y));
    ax.autoscale_mode(AutoscaleMode::Tight);
    ax.auto_fit();

    const auto xl = ax.x_limits();
    const auto yl = ax.y_limits();
    EXPECT_NEAR(xl.min, kEpoch, 1e-3);
    EXPECT_NEAR(xl.max, kEpoch + 3600.0, 1e-3);
    EXPECT_NEAR(yl.min, kBigY - kSmallA, 1e-6);
    EXPECT_NEAR(yl.max, kBigY + kSmallA, 1e-6);
}

TEST(DoubleDataApi, ThreadSafeSetterAppliesOffsetWithData)
{
    const auto s = make_signal(10);
    LineSeries line;
    line.set_thread_safe(true);
    line.set_x(std::span<const double>(s.t));
    line.set_y(std::span<const double>(s.y));
    // Nothing applied until the frame-boundary commit.
    EXPECT_EQ(line.x_offset(), 0.0);
    EXPECT_EQ(line.point_count(), 0u);

    EXPECT_TRUE(line.commit_pending());
    ASSERT_EQ(line.point_count(), s.t.size());
    for (size_t i = 0; i < s.t.size(); ++i)
    {
        EXPECT_NEAR(line.x_offset() + line.x_data()[i], s.t[i], 1e-3);
        EXPECT_NEAR(line.y_offset() + line.y_data()[i], s.y[i], 1e-8);
    }
}

TEST(DoubleDataApi, CopyKeepsOffsets)
{
    const auto s = make_signal(10);
    LineSeries a(std::span<const double>(s.t), std::span<const double>(s.y));
    LineSeries b(a);
    EXPECT_EQ(b.x_offset(), a.x_offset());
    EXPECT_EQ(b.y_offset(), a.y_offset());
}

// ─── GPU upload ─────────────────────────────────────────────────────────────

TEST(DoubleDataApi, UploadIsCameraRelativeAndSmooth)
{
    const auto       s = make_signal();
    CaptureBackend   backend;
    ui::ThemeManager theme;
    Renderer         renderer(backend, theme);
    LineSeries       line(std::span<const double>(s.t), std::span<const double>(s.y));

    // The renderer uploads relative to the view centre.
    const double view_cx = kEpoch + 1800.0;
    const double view_cy = kBigY;
    renderer.upload_series_data(line, view_cx, view_cy);

    const auto& up = backend.last_upload();
    ASSERT_GE(up.size(), s.t.size() * 2);
    std::set<float> distinct_y;
    for (size_t i = 0; i < s.t.size(); ++i)
    {
        const float ux = up[i * 2];
        const float uy = up[i * 2 + 1];
        // Values near zero: small floats keep their precision on the GPU.
        EXPECT_LE(std::abs(ux), 1800.0f + 1e-3f);
        EXPECT_LE(std::abs(uy), static_cast<float>(kSmallA) * 1.01f);
        EXPECT_NEAR(view_cx + ux, s.t[i], 1e-3);
        EXPECT_NEAR(view_cy + uy, s.y[i], 1e-8);
        distinct_y.insert(uy);
    }
    // A float-only pipeline collapses 1e7 + 0.01*sin into a few stair levels.
    EXPECT_GT(distinct_y.size(), s.t.size() / 2);
}

// ─── SVG export ─────────────────────────────────────────────────────────────

TEST(DoubleDataApi, SvgExportUsesTrueValues)
{
    const std::vector<double> t = {kEpoch, kEpoch + 1.0, kEpoch + 2.0};
    const std::vector<double> y = {kBigY - kSmallA, kBigY, kBigY + kSmallA};
    Figure                    fig({.width = 400, .height = 300});
    auto&                     ax = fig.subplot(1, 1, 1);
    ax.plot(std::span<const double>(t), std::span<const double>(y));
    ax.xlim(kEpoch, kEpoch + 2.0);
    ax.ylim(kBigY - kSmallA, kBigY + kSmallA);

    fig.compute_layout();
    const std::string svg = SvgExporter::to_string(fig);
    const std::string key = "<path class=\"line\" d=\"M";
    const auto        pos = svg.find(key);
    ASSERT_NE(pos, std::string::npos);
    // Three points spread over the plot area: distinct x and y coordinates.
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    ASSERT_EQ(std::sscanf(svg.c_str() + pos + key.size(),
                          "%f %f %f %f %f %f",
                          &x0,
                          &y0,
                          &x1,
                          &y1,
                          &x2,
                          &y2),
              6);
    EXPECT_LT(x0, x1);
    EXPECT_LT(x1, x2);
    EXPECT_GT(y0, y1);   // SVG y grows downwards
    EXPECT_GT(y1, y2);
    EXPECT_NEAR(x1 - x0, x2 - x1, 0.5f);
    EXPECT_NEAR(y0 - y1, y1 - y2, 0.5f);
}

// ─── Picking / readout round trip ───────────────────────────────────────────

#ifdef SPECTRA_TEST_HOVER
TEST(DoubleDataApi, PickedValueRoundTrip)
{
    const auto s = make_signal(37);
    Figure     fig({.width = 800, .height = 600});
    auto&      ax   = fig.subplot(1, 1, 1);
    auto&      line = ax.plot(std::span<const double>(s.t), std::span<const double>(s.y));
    ax.autoscale_mode(AutoscaleMode::Tight);
    ax.auto_fit();
    ax.set_viewport(Rect{0.0f, 0.0f, 800.0f, 600.0f});

    DataInteraction di;

    // Programmatic selection reports the absolute values.
    ASSERT_TRUE(di.select_point(&line, 7));
    EXPECT_NEAR(di.nearest_point().data_x, s.t[7], 1e-3);
    EXPECT_NEAR(di.nearest_point().data_y, s.y[7], 1e-8);
    ASSERT_EQ(di.markers().size(), 1u);
    EXPECT_NEAR(di.markers()[0].data_y, s.y[7], 1e-8);

    // Hovering a point's screen position finds it and reports true values.
    ImGui::CreateContext();
    ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
    const auto    xl         = ax.x_limits();
    const auto    yl         = ax.y_limits();
    const size_t  k          = 20;
    CursorReadout cur;
    cur.valid    = true;
    cur.screen_x = (s.t[k] - xl.min) / (xl.max - xl.min) * 800.0;
    cur.screen_y = (1.0 - (s.y[k] - yl.min) / (yl.max - yl.min)) * 600.0;
    cur.data_x   = s.t[k];
    cur.data_y   = s.y[k];
    di.update(cur, fig);
    ImGui::DestroyContext();

    const auto& np = di.nearest_point();
    ASSERT_TRUE(np.found);
    EXPECT_EQ(np.point_index, k);
    EXPECT_NEAR(np.data_x, s.t[k], 1e-3);
    EXPECT_NEAR(np.data_y, s.y[k], 1e-8);
    EXPECT_LT(np.distance_px, 1.0f);
}
#endif

// ─── Persistence: IPC snapshot and .spectra files keep the origins ──────────

namespace
{

// Absolute value of point i of a series (origin + stored float offset).
double abs_x(const Series& s, std::span<const float> xs, size_t i)
{
    return s.x_offset() + static_cast<double>(xs[i]);
}
double abs_y(const Series& s, std::span<const float> ys, size_t i)
{
    return s.y_offset() + static_cast<double>(ys[i]);
}

}   // namespace

TEST(DoubleDataApi, SnapshotRoundTripKeepsOrigins)
{
    const auto s = make_signal(101);
    Figure     fig({.width = 800, .height = 600});
    auto&      ax   = fig.subplot(1, 1, 1);
    auto&      line = ax.line(std::span<const double>(s.t), std::span<const double>(s.y));
    auto&      sc   = ax.scatter(std::span<const double>(s.t), std::span<const double>(s.y));
    ASSERT_NE(line.x_offset(), 0.0);
    ASSERT_NE(line.y_offset(), 0.0);

    ipc::StateSnapshotPayload payload;
    payload.figures.push_back(figure_to_snapshot(fig, 1));
    const auto bytes   = ipc::encode_state_snapshot(payload);
    const auto decoded = ipc::decode_state_snapshot(bytes);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->figures.size(), 1u);
    const auto& series = decoded->figures[0].series;
    ASSERT_EQ(series.size(), 2u);

    const Series* originals[] = {&line, &sc};
    for (size_t k = 0; k < series.size(); ++k)
    {
        const auto& ss = series[k];
        EXPECT_DOUBLE_EQ(ss.x_origin, originals[k]->x_offset());
        EXPECT_DOUBLE_EQ(ss.y_origin, originals[k]->y_offset());

        // Rebuild the series the way the window agent does and compare the
        // absolute positions with the source data.
        std::vector<float> xs, ys;
        for (size_t j = 0; j + 1 < ss.data.size(); j += 2)
        {
            xs.push_back(ss.data[j]);
            ys.push_back(ss.data[j + 1]);
        }
        ASSERT_EQ(xs.size(), s.t.size());
        Axes  rebuilt;
        auto& r = rebuilt.line(xs, ys);
        r.x_offset(ss.x_origin).y_offset(ss.y_origin);
        for (size_t i = 0; i < xs.size(); ++i)
        {
            EXPECT_NEAR(abs_x(r, r.x_data(), i), s.t[i], 1e-3);
            EXPECT_NEAR(abs_y(r, r.y_data(), i), s.y[i], 1e-8);
        }
    }

    // Incremental data updates carry the origins in f1/f2.
    ipc::StateDiffPayload diff;
    ipc::DiffOp           op;
    op.type = ipc::DiffOp::Type::SET_SERIES_DATA;
    op.f1   = line.x_offset();
    op.f2   = line.y_offset();
    op.data = {0.5f, 0.25f};
    diff.ops.push_back(op);
    const auto dec_diff = ipc::decode_state_diff(ipc::encode_state_diff(diff));
    ASSERT_TRUE(dec_diff.has_value());
    ASSERT_EQ(dec_diff->ops.size(), 1u);
    EXPECT_DOUBLE_EQ(dec_diff->ops[0].f1, line.x_offset());
    EXPECT_DOUBLE_EQ(dec_diff->ops[0].f2, line.y_offset());
}

TEST(DoubleDataApi, SnapshotDefaultsOriginsToZero)
{
    Figure             fig({.width = 400, .height = 300});
    auto&              ax = fig.subplot(1, 1, 1);
    std::vector<float> x  = {1.0f, 2.0f};
    ax.line(x, x);
    ipc::StateSnapshotPayload payload;
    payload.figures.push_back(figure_to_snapshot(fig, 1));
    const auto decoded = ipc::decode_state_snapshot(ipc::encode_state_snapshot(payload));
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->figures[0].series.size(), 1u);
    EXPECT_EQ(decoded->figures[0].series[0].x_origin, 0.0);
    EXPECT_EQ(decoded->figures[0].series[0].y_origin, 0.0);
}

TEST(DoubleDataApi, SerializerRoundTripKeepsOriginsAndLimits)
{
    const auto        s = make_signal(101);
    const std::string path =
        (std::filesystem::temp_directory_path() / "spectra_test_double_origin.spectra").string();

    Figure src({.width = 800, .height = 600});
    auto&  ax = src.subplot(1, 1, 1);
    ax.line(std::span<const double>(s.t), std::span<const double>(s.y));
    ax.scatter(std::span<const double>(s.t), std::span<const double>(s.y));
    ax.xlim(kEpoch + 100.0, kEpoch + 200.0);   // a zoom finer than float limits can hold
    ax.ylim(kBigY - kSmallA, kBigY + kSmallA);
    ASSERT_TRUE(FigureSerializer::save(path, src));

    Figure dst({.width = 800, .height = 600});
    ASSERT_TRUE(FigureSerializer::load(path, dst));
    std::remove(path.c_str());

    ASSERT_EQ(dst.axes().size(), 1u);
    const auto& dax = *dst.axes()[0];
    ASSERT_EQ(dax.series().size(), 2u);
    for (const auto& sp : dax.series())
    {
        std::span<const float> xs, ys;
        if (auto* l = dynamic_cast<const LineSeries*>(sp.get()))
        {
            xs = l->x_data();
            ys = l->y_data();
        }
        else if (auto* c = dynamic_cast<const ScatterSeries*>(sp.get()))
        {
            xs = c->x_data();
            ys = c->y_data();
        }
        ASSERT_EQ(xs.size(), s.t.size());
        EXPECT_NE(sp->x_offset(), 0.0);
        for (size_t i = 0; i < xs.size(); ++i)
        {
            EXPECT_NEAR(abs_x(*sp, xs, i), s.t[i], 1e-3);
            EXPECT_NEAR(abs_y(*sp, ys, i), s.y[i], 1e-8);
        }
    }
    EXPECT_NEAR(dax.x_limits().min, kEpoch + 100.0, 1e-6);
    EXPECT_NEAR(dax.x_limits().max, kEpoch + 200.0, 1e-6);
    EXPECT_NEAR(dax.y_limits().min, kBigY - kSmallA, 1e-9);
    EXPECT_NEAR(dax.y_limits().max, kBigY + kSmallA, 1e-9);
}

// ─── Streaming: append(double, double) / erase_*(double) (issue #112) ───────

// The double overloads are chosen only when an argument is a double, so
// existing float/integer calls keep their exact meaning and never become
// ambiguous.
static_assert(detail::wants_f64_overload_v<double, double>);
static_assert(detail::wants_f64_overload_v<double, float>);
static_assert(detail::wants_f64_overload_v<int, double>);
static_assert(!detail::wants_f64_overload_v<float, float>);
static_assert(!detail::wants_f64_overload_v<int, float>);
static_assert(!detail::wants_f64_overload_v<int, int>);
static_assert(!detail::wants_f64_overload_v<size_t, float>);

TEST(DoubleStreaming, AppendDoubleKeepsFullPrecision)
{
    const auto s = make_signal(500);
    LineSeries line;
    for (size_t i = 0; i < s.t.size(); ++i)
        line.append(s.t[i], s.y[i]);

    ASSERT_EQ(line.point_count(), s.t.size());
    EXPECT_NE(line.x_offset(), 0.0);
    EXPECT_NE(line.y_offset(), 0.0);
    for (size_t i = 0; i < s.t.size(); ++i)
    {
        EXPECT_NEAR(line.x_offset() + line.x_data()[i], s.t[i], 1e-3);
        EXPECT_NEAR(line.y_offset() + line.y_data()[i], s.y[i], 1e-8);
    }
    EXPECT_TRUE(line.x_is_sorted());
}

TEST(DoubleStreaming, ScatterAppendDoubleKeepsFullPrecision)
{
    const auto    s = make_signal(50);
    ScatterSeries sc;
    for (size_t i = 0; i < s.t.size(); ++i)
        sc.append(s.t[i], s.y[i]);
    ASSERT_EQ(sc.point_count(), s.t.size());
    for (size_t i = 0; i < s.t.size(); ++i)
    {
        EXPECT_NEAR(sc.x_offset() + sc.x_data()[i], s.t[i], 1e-3);
        EXPECT_NEAR(sc.y_offset() + sc.y_data()[i], s.y[i], 1e-8);
    }
}

TEST(DoubleStreaming, AppendThroughBasePointerUsesDoublePath)
{
    std::unique_ptr<Series> base = std::make_unique<LineSeries>();
    base->append(kEpoch + 0.25, kBigY + 0.003);
    base->append(kEpoch + 0.5, kBigY + 0.004);
    auto& line = static_cast<LineSeries&>(*base);
    ASSERT_EQ(line.point_count(), 2u);
    EXPECT_NEAR(line.x_offset() + line.x_data()[1], kEpoch + 0.5, 1e-6);
    EXPECT_NEAR(line.y_offset() + line.y_data()[1], kBigY + 0.004, 1e-9);
}

TEST(DoubleStreaming, FloatAndIntegerCallsKeepFloatPath)
{
    LineSeries line;
    line.append(5000, 7000);      // int, int   -> float overload
    line.append(5001, 7001.0f);   // int, float -> float overload
    line.append(5002.0f, 7002.0f);
    EXPECT_EQ(line.x_offset(), 0.0);
    EXPECT_EQ(line.y_offset(), 0.0);
    ASSERT_EQ(line.point_count(), 3u);
    EXPECT_EQ(line.x_data()[0], 5000.0f);
    EXPECT_EQ(line.y_data()[2], 7002.0f);
}

TEST(DoubleStreaming, SmallValuesKeepPlainStorage)
{
    LineSeries line;
    line.append(0.5, 2.0);
    line.append(1.5, -3.0);
    EXPECT_EQ(line.x_offset(), 0.0);
    EXPECT_EQ(line.y_offset(), 0.0);
    EXPECT_EQ(line.x_data()[1], 1.5f);
    EXPECT_EQ(line.y_data()[1], -3.0f);
}

TEST(DoubleStreaming, AppendUsesExistingOrigin)
{
    const auto   s = make_signal(11);
    LineSeries   line(std::span<const double>(s.t), std::span<const double>(s.y));
    const double xoff = line.x_offset();
    const double yoff = line.y_offset();
    ASSERT_NE(xoff, 0.0);
    line.append(kEpoch + 3600.5, kBigY + 0.0042);
    EXPECT_EQ(line.x_offset(), xoff);   // origin is not moved by appends
    EXPECT_EQ(line.y_offset(), yoff);
    ASSERT_EQ(line.point_count(), 12u);
    EXPECT_NEAR(line.x_offset() + line.x_data()[11], kEpoch + 3600.5, 1e-3);
    EXPECT_NEAR(line.y_offset() + line.y_data()[11], kBigY + 0.0042, 1e-8);
}

TEST(DoubleStreaming, EraseBeforeAndAfterTakeAbsoluteDoubleX)
{
    LineSeries line;
    for (int i = 0; i < 100; ++i)
        line.append(kEpoch + i, static_cast<double>(i));
    // A double threshold is an absolute x; a float one would be compared
    // against the stored offsets and remove everything.
    EXPECT_EQ(line.erase_before(kEpoch + 10.0), 10u);
    EXPECT_EQ(line.erase_after(kEpoch + 89.0), 10u);
    ASSERT_EQ(line.point_count(), 80u);
    EXPECT_NEAR(line.x_offset() + line.x_data().front(), kEpoch + 10.0, 1e-6);
    EXPECT_NEAR(line.x_offset() + line.x_data().back(), kEpoch + 89.0, 1e-6);
}

TEST(DoubleStreaming, ThreadSafeAppendAndEraseResolveAtCommit)
{
    LineSeries line;
    line.set_thread_safe(true);
    for (int i = 0; i < 20; ++i)
        line.append(kEpoch + 0.5 * i, kBigY + 0.001 * i);
    line.append(1.0f, 2.0f);   // float append: stored as given
    EXPECT_EQ(line.point_count(), 0u);
    EXPECT_TRUE(line.commit_pending());
    ASSERT_EQ(line.point_count(), 21u);
    EXPECT_NE(line.x_offset(), 0.0);
    for (int i = 0; i < 20; ++i)
    {
        EXPECT_NEAR(line.x_offset() + line.x_data()[i], kEpoch + 0.5 * i, 1e-6);
        EXPECT_NEAR(line.y_offset() + line.y_data()[i], kBigY + 0.001 * i, 1e-8);
    }
    EXPECT_EQ(line.x_data()[20], 1.0f);
    EXPECT_FALSE(line.x_is_sorted());

    LineSeries ts;
    ts.set_thread_safe(true);
    for (int i = 0; i < 10; ++i)
        ts.append(kEpoch + i, 0.0);
    ASSERT_TRUE(ts.commit_pending());
    ts.erase_before(kEpoch + 4.0);
    ts.erase_after(kEpoch + 7.0);
    ASSERT_TRUE(ts.commit_pending());
    ASSERT_EQ(ts.point_count(), 4u);
    EXPECT_NEAR(ts.x_offset() + ts.x_data().front(), kEpoch + 4.0, 1e-6);
}

TEST(DoubleStreaming, ClipboardExportUsesAbsoluteValues)
{
    LineSeries line;
    line.label("big");
    const std::vector<double> x{kEpoch, kEpoch + 1.5};
    const std::vector<double> y{kBigY + 0.123, kBigY + 0.5};
    line.set_x(std::span<const double>(x));
    line.set_y(std::span<const double>(y));
    ASSERT_NE(line.y_offset(), 0.0);

    const std::string tsv = series_to_tsv({&line});
    EXPECT_NE(tsv.find("1700000000\t10000000.123\n"), std::string::npos) << tsv;
    EXPECT_NE(tsv.find("1700000001.5\t10000000.5\n"), std::string::npos) << tsv;
}

TEST(DoubleStreaming, BinaryFileKeepsMarkerPrecision)
{
    const std::string path =
        (std::filesystem::temp_directory_path() / "spectra_test_marker_f64.spectra").string();
    Figure src({.width = 400, .height = 300});
    src.subplot(1, 1, 1).line(std::span<const double>(make_signal(5).t),
                              std::span<const double>(make_signal(5).y));
    OverlaySnapshot snap;
    snap.markers.push_back({kEpoch + 0.25, kBigY + 0.0031, "s", 3, 0});
    ASSERT_TRUE(FigureSerializer::save(path, src, &snap));

    Figure          dst({.width = 400, .height = 300});
    OverlaySnapshot loaded;
    ASSERT_TRUE(FigureSerializer::load(path, dst, &loaded));
    std::remove(path.c_str());
    ASSERT_EQ(loaded.markers.size(), 1u);
    EXPECT_EQ(loaded.markers[0].data_x, kEpoch + 0.25);
    EXPECT_EQ(loaded.markers[0].data_y, kBigY + 0.0031);
    EXPECT_EQ(loaded.markers[0].point_index, 3u);
}

TEST(DoubleStreaming, WorkspaceJsonKeepsMarkerPrecision)
{
    WorkspaceData data;
    data.interaction.markers.push_back({kEpoch + 0.25, kBigY + 0.0031, "s", 3, 1});
    const std::string path =
        (std::filesystem::temp_directory_path() / "spectra_test_marker_f64_ws.json").string();
    ASSERT_TRUE(Workspace::save(path, data));

    WorkspaceData back;
    ASSERT_TRUE(Workspace::load(path, back));
    std::remove(path.c_str());
    ASSERT_EQ(back.interaction.markers.size(), 1u);
    EXPECT_EQ(back.interaction.markers[0].data_x, kEpoch + 0.25);
    EXPECT_EQ(back.interaction.markers[0].data_y, kBigY + 0.0031);
    EXPECT_EQ(back.interaction.markers[0].axes_index, 1u);
}

#ifdef SPECTRA_TEST_HOVER
TEST(DoubleStreaming, RegionSelectUsesAbsoluteValues)
{
    Figure fig({.width = 800, .height = 600});
    auto&  ax = fig.subplot(1, 1, 1);
    // y = 1e7 + 0.001 * i, x = epoch + i: float storage alone cannot tell
    // these points apart.
    std::vector<double> x(10), y(10);
    for (int i = 0; i < 10; ++i)
    {
        x[i] = kEpoch + i;
        y[i] = kBigY + 0.001 * i;
    }
    ax.line(std::span<const double>(x), std::span<const double>(y));
    ax.xlim(kEpoch - 1.0, kEpoch + 10.0);
    ax.ylim(kBigY - 0.001, kBigY + 0.010);
    ax.set_viewport({0.0f, 0.0f, 1100.0f, 1100.0f});

    const AxisMapping m(ax);
    RegionSelect      rs;
    // Select x in [epoch + 2.5, epoch + 6.5] and y in [1e7 + 0.0025, 1e7 + 0.0055]:
    // points 3, 4 and 5.
    rs.begin(m.to_screen_x(kEpoch + 2.5), m.to_screen_y(kBigY + 0.0055), m);
    rs.update_drag(m.to_screen_x(kEpoch + 6.5), m.to_screen_y(kBigY + 0.0025), m);
    EXPECT_NEAR(rs.data_x_min(), kEpoch + 2.5, 1e-6);
    rs.finish(&ax);

    ASSERT_TRUE(rs.has_selection());
    const auto& st = rs.statistics();
    EXPECT_EQ(st.point_count, 3u);
    EXPECT_NEAR(st.x_min, kEpoch + 3.0, 1e-6);
    EXPECT_NEAR(st.x_max, kEpoch + 5.0, 1e-6);
    EXPECT_NEAR(st.y_mean, kBigY + 0.004, 1e-8);
    EXPECT_NEAR(st.y_std, 0.001, 1e-8);
    EXPECT_EQ(format_region_value(kBigY + 0.004, 0.002), "10000000.004");
}
#endif
