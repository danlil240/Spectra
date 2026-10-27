// Double-precision data API (audit finding P2-6): data that float cannot hold
// precisely (Unix timestamps, 1e7 + small variations) is stored as float
// offsets from a double origin, so the GPU upload, axis limits, picking and
// readouts keep full precision.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <gtest/gtest.h>
#include <set>
#include <spectra/axes.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <unordered_map>
#include <vector>

#include "core/data_origin.hpp"
#include "render/backend.hpp"
#include "render/renderer.hpp"
#include "ui/theme/theme.hpp"

#if defined(SPECTRA_USE_IMGUI) && __has_include(<imgui.h>)
    #define SPECTRA_TEST_HOVER 1
    #include <imgui.h>

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
