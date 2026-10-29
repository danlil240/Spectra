// test_plugin_lifecycle.cpp — One plug-in through its whole lifetime.
//
// mock_lifecycle_plugin is built as a real shared library (.so here, .dll on
// the Windows CI job, which runs this test: it needs no GPU).  The test drives
//
//   load -> use (custom series rendered, transform applied) -> disable ->
//   enable -> unload -> reload -> host shutdown with the plug-in still loaded
//
// through the real PluginManager, SeriesTypeRegistry, TransformRegistry and
// Renderer (on a fake backend).  The plug-in counts every callback in
// host-owned counters, so the test can assert that nothing calls into the
// library while it is disabled or unloaded, that per-series state and buffers
// are released before every unload, and that shutdown runs exactly once.

#include <gtest/gtest.h>

#include <spectra/axes.hpp>
#include <spectra/custom_series.hpp>
#include <spectra/figure.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "math/data_transform.hpp"
#include "mock_lifecycle_counters.hpp"
#include "render/backend.hpp"
#include "render/renderer.hpp"
#include "render/series_type_registry.hpp"
#include "ui/theme/theme.hpp"
#include "ui/workspace/plugin_api.hpp"

#ifdef _WIN32
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

#ifndef SPECTRA_MOCK_LIFECYCLE_PLUGIN_PATH
    #define SPECTRA_MOCK_LIFECYCLE_PLUGIN_PATH ""
#endif

using namespace spectra;

namespace
{

constexpr const char* kPluginName = "MockLifecyclePlugin";
constexpr const char* kSeriesType = "lifecycle_series";
constexpr const char* kTransform  = "LifecycleDouble";

// Minimal backend: hands out handles and counts live buffers.
class CountingBackend : public Backend
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
    PipelineHandle create_custom_pipeline(const CustomPipelineDesc&) override
    {
        ++custom_pipelines;
        return PipelineHandle{next_id_++};
    }

    BufferHandle create_buffer(BufferUsage, size_t) override
    {
        ++live_buffers;
        return BufferHandle{next_id_++};
    }
    void destroy_buffer(BufferHandle h) override
    {
        if (h)
            --live_buffers;
    }
    void          upload_buffer(BufferHandle, const void*, size_t, size_t) override {}
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

    int     custom_pipelines = 0;
    int64_t live_buffers     = 0;

   private:
    uint64_t next_id_ = 1;
};

bool counters_equal(const MockLifecycleCounters& a, const MockLifecycleCounters& b)
{
    return a.shutdown_calls == b.shutdown_calls && a.upload_calls == b.upload_calls
           && a.draw_calls == b.draw_calls && a.bounds_calls == b.bounds_calls
           && a.cleanup_calls == b.cleanup_calls && a.transform_calls == b.transform_calls
           && a.live_states == b.live_states && a.live_buffers == b.live_buffers;
}

std::string describe(const MockLifecycleCounters& c)
{
    return "shutdown=" + std::to_string(c.shutdown_calls) + " upload="
           + std::to_string(c.upload_calls) + " draw=" + std::to_string(c.draw_calls) + " bounds="
           + std::to_string(c.bounds_calls) + " cleanup=" + std::to_string(c.cleanup_calls)
           + " transform=" + std::to_string(c.transform_calls) + " live_states="
           + std::to_string(c.live_states) + " live_buffers=" + std::to_string(c.live_buffers);
}

#define LIFECYCLE_STEP(name) SCOPED_TRACE("step: " name)

}   // namespace

TEST(PluginLifecycle, LoadUseDisableUnloadReloadShutdown)
{
    const std::string path = SPECTRA_MOCK_LIFECYCLE_PLUGIN_PATH;
    if (path.empty() || !std::filesystem::exists(path))
        GTEST_SKIP() << "mock_lifecycle_plugin not found at: " << path;

    // Host-owned: outlives every module instance of the plug-in.
    MockLifecycleCounters counters;

    // Host services, in the order an App owns them: registries outlive the
    // renderer, the renderer outlives the plug-in manager.
    SeriesTypeRegistry series_types;
    TransformRegistry  transforms;
    CountingBackend    backend;
    ui::ThemeManager   theme;
    auto               renderer = std::make_unique<Renderer>(backend, theme);
    ASSERT_TRUE(renderer->init());
    renderer->set_series_type_registry(&series_types);

    Figure             fig;
    auto&              ax     = fig.subplot(1, 1, 1);
    auto               owned  = std::make_unique<CustomSeries>(kSeriesType);
    auto*              series = owned.get();
    std::vector<float> blob{1.0f, 2.0f, 3.0f, 4.0f};
    series->set_data(blob.data(), blob.size() * sizeof(float), blob.size());
    ax.series_mut().push_back(std::move(owned));
    ax.xlim(0.0, 4.0);
    ax.ylim(0.0, 5.0);
    fig.compute_layout();
    auto frame = [&](int n = 1)
    {
        for (int i = 0; i < n; ++i)
            renderer->render_figure_content(fig);
    };
    // The figure is drawn before the plug-in is loaded (a workspace restored
    // ahead of its plug-ins): its type is unknown, so nothing is drawn yet,
    // but the upload must stay pending until the type is registered.
    frame(2);
    const int64_t host_buffers = backend.live_buffers;

    auto mgr = std::make_unique<PluginManager>();
    mgr->set_series_type_registry(&series_types);
    mgr->set_transform_registry(&transforms);

    auto load_and_bind = [&]() -> bool
    {
        if (!mgr->load_plugin(path))
            return false;
        const PluginEntry* p = mgr->find_plugin(kPluginName);
        if (!p || !p->handle)
            return false;
#ifdef _WIN32
        auto bind = reinterpret_cast<MockLifecycleBindFn>(
            GetProcAddress(static_cast<HMODULE>(p->handle), "mock_lifecycle_bind"));
#else
        auto bind = reinterpret_cast<MockLifecycleBindFn>(dlsym(p->handle, "mock_lifecycle_bind"));
#endif
        if (!bind)
            return false;
        bind(&counters);
        return true;
    };

    // ── load + use ──────────────────────────────────────────────────────────
    DataTransform retained;
    {
        LIFECYCLE_STEP("load and use");
        ASSERT_TRUE(load_and_bind());
        EXPECT_NE(series_types.find_shared(kSeriesType), nullptr);

        frame(2);
        EXPECT_EQ(counters.upload_calls, 1) << "series drawn before the load was never uploaded";
        EXPECT_EQ(counters.bounds_calls, 1);
        EXPECT_EQ(counters.draw_calls, 2);
        EXPECT_EQ(counters.live_states, 1);
        EXPECT_EQ(counters.live_buffers, 1);
        EXPECT_EQ(backend.live_buffers, host_buffers + 1);
        EXPECT_EQ(backend.custom_pipelines, 1);
        // The plug-in's bounds reached the series (autoscale uses them).
        ASSERT_TRUE(series->has_bounds());
        EXPECT_FLOAT_EQ(series->bounds_x_max(), 4.0f);
        EXPECT_FLOAT_EQ(series->bounds_y_max(), 4.0f);

        ASSERT_TRUE(transforms.get_transform(kTransform, retained));
        EXPECT_FLOAT_EQ(retained.apply_scalar(3.0f), 6.0f);
        TransformPipeline pipe;
        pipe.push_back(retained);
        std::vector<float> xo, yo;
        pipe.apply(std::vector<float>{0.0f, 1.0f}, std::vector<float>{2.5f, -1.0f}, xo, yo);
        EXPECT_EQ(yo, (std::vector<float>{5.0f, -2.0f}));
        EXPECT_EQ(counters.transform_calls, 3);
    }

    // ── disable: nothing of the plug-in runs ─────────────────────────────────
    {
        LIFECYCLE_STEP("disable");
        mgr->set_plugin_enabled(kPluginName, false);
        const MockLifecycleCounters before = counters;
        std::vector<float>          blob2{4.0f, 3.0f, 2.0f, 1.0f, 0.0f};
        series->set_data(blob2.data(), blob2.size() * sizeof(float), blob2.size());
        frame(3);
        EXPECT_FLOAT_EQ(retained.apply_scalar(3.0f), 3.0f) << "disabled transform is identity";
        DataTransform t;
        EXPECT_FALSE(transforms.get_transform(kTransform, t));
        EXPECT_TRUE(counters_equal(counters, before))
            << "plug-in code ran while disabled: " << describe(before) << " -> "
            << describe(counters);
    }

    // ── enable: the pending upload happens, drawing resumes ─────────────────
    {
        LIFECYCLE_STEP("enable");
        mgr->set_plugin_enabled(kPluginName, true);
        const MockLifecycleCounters before = counters;
        frame();
        EXPECT_EQ(counters.upload_calls, before.upload_calls + 1) << "data set while disabled";
        EXPECT_EQ(counters.draw_calls, before.draw_calls + 1);
        EXPECT_EQ(counters.live_states, 1) << "state reused, not reallocated";
        EXPECT_FLOAT_EQ(retained.apply_scalar(3.0f), 6.0f);
    }

    // ── unload: state released while the code is mapped, then silence ────────
    {
        LIFECYCLE_STEP("unload");
        const MockLifecycleCounters before = counters;
        ASSERT_TRUE(mgr->unload_plugin(kPluginName));
        EXPECT_EQ(mgr->plugin_count(), 0u);
        EXPECT_EQ(counters.cleanup_calls, before.cleanup_calls + 1);
        EXPECT_EQ(counters.shutdown_calls, before.shutdown_calls + 1);
        EXPECT_EQ(counters.live_states, 0);
        EXPECT_EQ(counters.live_buffers, 0);
        EXPECT_EQ(backend.live_buffers, host_buffers) << "plug-in buffer leaked";
        EXPECT_EQ(series_types.find_shared(kSeriesType), nullptr);

        // The series stays in the figure; rendering it, changing its data and
        // applying retained transform copies must not enter the unmapped code.
        const MockLifecycleCounters after_unload = counters;
        frame(2);
        std::vector<float> blob3{7.0f, 8.0f};
        series->set_data(blob3.data(), blob3.size() * sizeof(float), blob3.size());
        frame(2);
        EXPECT_FALSE(retained.is_available());
        EXPECT_FLOAT_EQ(retained.apply_scalar(3.0f), 3.0f);
        DataTransform t;
        EXPECT_FALSE(transforms.get_transform(kTransform, t));
        EXPECT_TRUE(counters_equal(counters, after_unload))
            << "use after unload: " << describe(after_unload) << " -> " << describe(counters);
    }

    // ── reload: a fresh module instance serves the existing series ───────────
    {
        LIFECYCLE_STEP("reload");
        const MockLifecycleCounters before = counters;
        ASSERT_TRUE(load_and_bind());
        frame();
        EXPECT_EQ(counters.upload_calls, before.upload_calls + 1) << "state rebuilt after reload";
        EXPECT_EQ(counters.draw_calls, before.draw_calls + 1);
        EXPECT_EQ(counters.live_states, 1);
        EXPECT_EQ(counters.live_buffers, 1);
        EXPECT_FLOAT_EQ(series->bounds_x_max(), 2.0f) << "bounds of the data set while unloaded";
        EXPECT_FLOAT_EQ(retained.apply_scalar(3.0f), 3.0f) << "old copies stay revoked";
        DataTransform fresh;
        ASSERT_TRUE(transforms.get_transform(kTransform, fresh));
        EXPECT_FLOAT_EQ(fresh.apply_scalar(3.0f), 6.0f);
    }

    // ── host shutdown with the plug-in loaded ────────────────────────────────
    {
        LIFECYCLE_STEP("shutdown with plug-in loaded");
        const MockLifecycleCounters before = counters;
        mgr.reset();   // ~PluginManager unloads everything still loaded
        EXPECT_EQ(counters.shutdown_calls, before.shutdown_calls + 1);
        EXPECT_EQ(counters.cleanup_calls, before.cleanup_calls + 1);
        EXPECT_EQ(counters.live_states, 0);
        EXPECT_EQ(counters.live_buffers, 0);

        // Tearing down the rest of the host after the library is gone must
        // not call into it either.
        const MockLifecycleCounters after = counters;
        frame();
        renderer->set_series_type_registry(nullptr);
        renderer.reset();
        EXPECT_TRUE(counters_equal(counters, after))
            << "host teardown called into the unloaded plug-in: " << describe(after) << " -> "
            << describe(counters);
        EXPECT_EQ(counters.shutdown_calls, 2) << "one shutdown per load";
    }
}
