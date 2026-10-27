// test_custom_series_render.cpp — Custom (plugin) series reach the GPU.
//
// Plugin audit P-B: Renderer::render_series() returned before custom-series
// dispatch whenever the host SSBO was null, but custom uploads keep their
// buffers in plugin-owned state and never allocate it, so plugin series were
// uploaded but never drawn.
//
// Plugin audit P-C: SpectraSeriesTypeDesc::topology is a Spectra ABI value
// (0 = triangle list, as documented), not a raw VkPrimitiveTopology (where 0 is
// a point list).  The backend must map it explicitly.
//
// CPU-only: uses a fake backend, so it runs without a GPU.

#include <gtest/gtest.h>
#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <spectra/axes.hpp>
#include <spectra/custom_series.hpp>
#include <spectra/figure.hpp>
#include <vector>

#include "render/backend.hpp"
#include "render/renderer.hpp"
#include "render/series_type_registry.hpp"
#include "render/vulkan/vk_pipeline.hpp"
#include "ui/theme/theme.hpp"
#include "ui/workspace/plugin_api.hpp"

using namespace spectra;

namespace
{

class FakeBackend : public Backend
{
   public:
    bool init(bool) override { return true; }
    void shutdown() override {}
    void wait_idle() override {}
    bool create_surface(void*) override { return true; }
    bool create_swapchain(uint32_t, uint32_t) override { return true; }
    bool recreate_swapchain(uint32_t, uint32_t) override { return true; }
    bool create_offscreen_framebuffer(uint32_t, uint32_t) override { return true; }

    PipelineHandle create_pipeline(PipelineType type) override
    {
        PipelineHandle h{next_id_++};
        pipeline_types[h.id] = type;
        return h;
    }
    PipelineHandle create_custom_pipeline(const CustomPipelineDesc& desc) override
    {
        ++custom_pipelines;
        last_topology = desc.topology;
        return PipelineHandle{next_id_++};
    }

    BufferHandle  create_buffer(BufferUsage, size_t) override { return BufferHandle{next_id_++}; }
    void          destroy_buffer(BufferHandle) override {}
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
    void     bind_pipeline(PipelineHandle h) override
    {
        auto it = pipeline_types.find(h.id);
        bound = it != pipeline_types.end() ? std::optional<PipelineType>(it->second) : std::nullopt;
    }
    void     bind_buffer(BufferHandle, uint32_t) override {}
    void     bind_index_buffer(BufferHandle) override {}
    void     bind_texture(TextureHandle, uint32_t) override {}
    void     push_constants(const SeriesPushConstants&) override {}
    void     set_viewport(float, float, float, float) override {}
    void     set_scissor(int32_t, int32_t, uint32_t, uint32_t) override {}
    void     set_line_width(float) override {}
    void     draw(uint32_t, uint32_t) override
    {
        if (events)
            events->push_back(bound == PipelineType::Grid ? "grid" : "draw");
    }
    void     draw_instanced(uint32_t, uint32_t, uint32_t, uint32_t) override {}
    void     draw_indexed(uint32_t, uint32_t, int32_t) override {}
    bool     readback_framebuffer(uint8_t*, uint32_t, uint32_t) override { return false; }
    uint32_t swapchain_width() const override { return 0; }
    uint32_t swapchain_height() const override { return 0; }

    int      custom_pipelines = 0;
    uint32_t last_topology    = 0xFFFFFFFFu;

    // Draw order log ("grid" for grid-pipeline draws), shared with plugin callbacks.
    std::vector<std::string>*                  events = nullptr;
    std::unordered_map<uint64_t, PipelineType> pipeline_types;
    std::optional<PipelineType>                bound;

   private:
    uint64_t next_id_ = 1;
};

// Stub SPIR-V: the fake backend never compiles it, but the registry only
// creates a pipeline when both stages are present.
const uint8_t kStubSpirv[] = {0x03, 0x02, 0x23, 0x07, 0x00, 0x00, 0x01, 0x00};

struct PluginState
{
    int value = 0;
};

struct Counts
{
    int  uploads           = 0;
    int  draws             = 0;
    bool draw_saw_state    = false;
    bool draw_saw_pipeline = false;
};

void register_counting_type(SeriesTypeRegistry& reg, Counts& c)
{
    CustomPipelineDesc desc;
    desc.vert_spirv      = kStubSpirv;
    desc.vert_spirv_size = sizeof(kStubSpirv);
    desc.frag_spirv      = kStubSpirv;
    desc.frag_spirv_size = sizeof(kStubSpirv);
    desc.topology        = static_cast<uint32_t>(CustomTopology::TriangleList);
    reg.register_type(
        "blob",
        desc,
        [&c](Backend&, const void*, void*& state, size_t) -> int
        {
            ++c.uploads;
            if (!state)
                state = new PluginState{42};
            return 0;
        },
        [&c](Backend&,
             PipelineHandle pipeline,
             const void*    state,
             const float*,
             const SeriesPushConstants&) -> int
        {
            ++c.draws;
            // The C++ API passes the per-series state the upload allocated.
            auto* st            = static_cast<const PluginState*>(state);
            c.draw_saw_state    = st && st->value == 42;
            c.draw_saw_pipeline = static_cast<bool>(pipeline);
            return 0;
        },
        [](const void*, size_t, float* b) -> int
        {
            b[0] = 0.0f;
            b[1] = 1.0f;
            b[2] = 0.0f;
            b[3] = 1.0f;
            return 0;
        },
        [](Backend&, void*& state)
        {
            delete static_cast<PluginState*>(state);
            state = nullptr;
        });
}

}   // namespace

// ─── P-B: custom series without a host SSBO are drawn ───────────────────────

TEST(CustomSeriesRender, DrawsCustomSeriesWithPluginOwnedState)
{
    SeriesTypeRegistry reg;   // must outlive the renderer
    Counts             counts;
    register_counting_type(reg, counts);

    FakeBackend      backend;
    ui::ThemeManager theme;
    Renderer         renderer(backend, theme);
    ASSERT_TRUE(renderer.init());
    renderer.set_series_type_registry(&reg);

    Figure fig;
    auto&  ax      = fig.subplot(1, 1, 1);
    auto   series  = std::make_unique<CustomSeries>("blob");
    float  blob[4] = {0.0f, 1.0f, 2.0f, 3.0f};
    series->set_data(blob, sizeof(blob), 4);
    ax.series_mut().push_back(std::move(series));
    ax.xlim(0.0, 1.0);
    ax.ylim(0.0, 1.0);
    fig.compute_layout();

    renderer.render_figure_content(fig);

    EXPECT_EQ(counts.uploads, 1);
    EXPECT_EQ(counts.draws, 1) << "custom series was uploaded but never drawn";
    EXPECT_TRUE(counts.draw_saw_state);
    EXPECT_TRUE(counts.draw_saw_pipeline);
    // The pipeline is created lazily on first draw, with the plugin's topology.
    EXPECT_EQ(backend.custom_pipelines, 1);
    EXPECT_EQ(backend.last_topology, static_cast<uint32_t>(CustomTopology::TriangleList));

    // A second frame draws again (no re-upload: data is clean).
    renderer.render_figure_content(fig);
    EXPECT_EQ(counts.uploads, 1);
    EXPECT_EQ(counts.draws, 2);

    renderer.set_series_type_registry(nullptr);
}

// ─── Grid below series; plugin bounds recorded ──────────────────────────────

TEST(CustomSeriesRender, GridIsDrawnBeforeSeriesAndPluginBoundsAreRecorded)
{
    std::vector<std::string> events;
    SeriesTypeRegistry       reg;
    CustomPipelineDesc       desc;
    desc.vert_spirv      = kStubSpirv;
    desc.vert_spirv_size = sizeof(kStubSpirv);
    desc.frag_spirv      = kStubSpirv;
    desc.frag_spirv_size = sizeof(kStubSpirv);
    desc.topology        = static_cast<uint32_t>(CustomTopology::TriangleList);
    reg.register_type(
        "opaque",
        desc,
        [](Backend&, const void*, void*&, size_t) -> int { return 0; },
        [&events](Backend&, PipelineHandle, const void*, const float*, const SeriesPushConstants&)
            -> int
        {
            events.push_back("custom");
            return 0;
        },
        [](const void*, size_t, float* b) -> int
        {
            b[0] = -2.0f;
            b[1] = 6.0f;
            b[2] = 10.0f;
            b[3] = 30.0f;
            return 0;
        },
        [](Backend&, void*&) {});

    FakeBackend backend;
    backend.events = &events;
    ui::ThemeManager theme;
    Renderer         renderer(backend, theme);
    ASSERT_TRUE(renderer.init());
    renderer.set_series_type_registry(&reg);

    Figure fig;
    auto&  ax      = fig.subplot(1, 1, 1);
    auto   owned   = std::make_unique<CustomSeries>("opaque");
    auto*  series  = owned.get();
    float  blob[4] = {0.0f, 1.0f, 2.0f, 3.0f};
    series->set_data(blob, sizeof(blob), 4);
    ax.series_mut().push_back(std::move(owned));
    ax.plot(std::vector<float>{0.0f, 1.0f}, std::vector<float>{0.0f, 1.0f});
    ax.xlim(0.0, 1.0);
    ax.ylim(0.0, 1.0);
    ax.grid(true);
    fig.compute_layout();

    renderer.render_figure_content(fig);

    const auto grid   = std::find(events.begin(), events.end(), "grid");
    const auto custom = std::find(events.begin(), events.end(), "custom");
    ASSERT_NE(grid, events.end()) << "grid was not drawn";
    ASSERT_NE(custom, events.end()) << "custom series was not drawn";
    EXPECT_LT(grid - events.begin(), custom - events.begin())
        << "the grid must be drawn below (before) opaque plugin series";
    // The host asked the plugin for its data bounds after the upload.
    ASSERT_TRUE(series->has_bounds());
    EXPECT_FLOAT_EQ(series->bounds_x_min(), -2.0f);
    EXPECT_FLOAT_EQ(series->bounds_x_max(), 6.0f);
    EXPECT_FLOAT_EQ(series->bounds_y_min(), 10.0f);
    EXPECT_FLOAT_EQ(series->bounds_y_max(), 30.0f);

    renderer.set_series_type_registry(nullptr);
}

// ─── P-C: ABI topology → Vulkan topology ────────────────────────────────────

TEST(CustomSeriesTopology, AbiEnumMatchesBackendEnum)
{
    EXPECT_EQ(static_cast<uint32_t>(SPECTRA_TOPOLOGY_TRIANGLE_LIST),
              static_cast<uint32_t>(CustomTopology::TriangleList));
    EXPECT_EQ(static_cast<uint32_t>(SPECTRA_TOPOLOGY_POINT_LIST),
              static_cast<uint32_t>(CustomTopology::PointList));
    // The zero-initialised descriptor default is the documented triangle list.
    SpectraSeriesTypeDesc desc{};
    EXPECT_EQ(desc.topology, static_cast<uint32_t>(SPECTRA_TOPOLOGY_TRIANGLE_LIST));
}

TEST(CustomSeriesTopology, MapsAbiValuesToVulkan)
{
    struct Case
    {
        uint32_t            abi;
        VkPrimitiveTopology vk;
    };
    const Case cases[] = {
        {SPECTRA_TOPOLOGY_TRIANGLE_LIST, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST},
        {SPECTRA_TOPOLOGY_LINE_LIST, VK_PRIMITIVE_TOPOLOGY_LINE_LIST},
        {SPECTRA_TOPOLOGY_LINE_STRIP, VK_PRIMITIVE_TOPOLOGY_LINE_STRIP},
        {SPECTRA_TOPOLOGY_TRIANGLE_LIST_VULKAN, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST},
        {SPECTRA_TOPOLOGY_TRIANGLE_STRIP, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP},
        {SPECTRA_TOPOLOGY_TRIANGLE_FAN, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN},
        {SPECTRA_TOPOLOGY_POINT_LIST, VK_PRIMITIVE_TOPOLOGY_POINT_LIST},
    };
    for (const auto& c : cases)
    {
        VkPrimitiveTopology out = VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
        EXPECT_TRUE(vk::custom_topology_to_vk(c.abi, out)) << "abi=" << c.abi;
        EXPECT_EQ(out, c.vk) << "abi=" << c.abi;
    }
}

TEST(CustomSeriesTopology, ZeroIsTriangleListNotPointList)
{
    VkPrimitiveTopology out = VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
    ASSERT_TRUE(vk::custom_topology_to_vk(0, out));
    EXPECT_EQ(out, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    EXPECT_NE(out, VK_PRIMITIVE_TOPOLOGY_POINT_LIST);
}

TEST(CustomSeriesTopology, RejectsUnknownValues)
{
    for (uint32_t bad : {7u, 10u, 0xFFFFFFFFu})
    {
        VkPrimitiveTopology out = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        EXPECT_FALSE(vk::custom_topology_to_vk(bad, out)) << "abi=" << bad;
        EXPECT_EQ(out, VK_PRIMITIVE_TOPOLOGY_LINE_LIST) << "out must be untouched";
    }
}
