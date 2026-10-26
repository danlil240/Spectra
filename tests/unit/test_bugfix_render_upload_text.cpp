// Regression tests for render_upload.cpp / text_renderer.cpp fixes:
//   #12  ChunkedLineSeries same-count data change must re-upload
//   #54  Mesh vertex buffer must be reallocated when the float count grows
//   #99  Failed TextRenderer::init must not leak the atlas texture / UBO

#include <fstream>
#include <gtest/gtest.h>
#include <spectra/chunked_series.hpp>
#include <spectra/series3d.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "render/backend.hpp"
#include "render/renderer.hpp"
#include "render/text_renderer.hpp"
#include "ui/theme/theme.hpp"

using namespace spectra;

namespace
{

// Minimal CPU-only backend: tracks live buffers/textures, their sizes, and
// every upload so tests can check for leaks and out-of-bounds writes.
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

    PipelineHandle create_pipeline(PipelineType) override
    {
        if (fail_pipelines_)
            return PipelineHandle{};
        return PipelineHandle{next_id_++};
    }

    BufferHandle create_buffer(BufferUsage, size_t size_bytes) override
    {
        BufferHandle h{next_id_++};
        buffers_[h.id] = size_bytes;
        return h;
    }
    void destroy_buffer(BufferHandle handle) override { buffers_.erase(handle.id); }
    void upload_buffer(BufferHandle handle, const void*, size_t size_bytes, size_t offset) override
    {
        ++upload_count_;
        auto it = buffers_.find(handle.id);
        if (it == buffers_.end() || offset + size_bytes > it->second)
            ++overflow_count_;
    }

    TextureHandle create_texture(uint32_t, uint32_t, const uint8_t*) override
    {
        ++texture_creates_;
        TextureHandle h{next_id_++};
        textures_.push_back(h.id);
        return h;
    }
    void destroy_texture(TextureHandle handle) override { std::erase(textures_, handle.id); }

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

    uint64_t                             next_id_        = 1;
    bool                                 fail_pipelines_ = false;
    std::unordered_map<uint64_t, size_t> buffers_;
    std::vector<uint64_t>                textures_;
    int                                  upload_count_    = 0;
    int                                  texture_creates_ = 0;
    int                                  overflow_count_  = 0;
};

#ifndef SPECTRA_SOURCE_DIR
    #define SPECTRA_SOURCE_DIR "."
#endif

std::vector<uint8_t> load_font()
{
    const std::string paths[] = {
        std::string(SPECTRA_SOURCE_DIR) + "/third_party/Inter-Regular.ttf",
        "third_party/Inter-Regular.ttf",
        "../third_party/Inter-Regular.ttf",
        "../../third_party/Inter-Regular.ttf",
    };
    for (const auto& p : paths)
    {
        std::ifstream f(p, std::ios::binary | std::ios::ate);
        if (!f.is_open())
            continue;
        auto sz = f.tellg();
        if (sz <= 0)
            continue;
        std::vector<uint8_t> data(static_cast<size_t>(sz));
        f.seekg(0);
        f.read(reinterpret_cast<char*>(data.data()), sz);
        if (f)
            return data;
    }
    return {};
}

}   // namespace

// #12: set_data with arrays of the same length must still reach the GPU.
TEST(BugfixRenderUpload, ChunkedLineSameCountDataChangeReuploads)
{
    FakeBackend       backend;
    ui::ThemeManager  theme;
    Renderer          renderer(backend, theme);
    ChunkedLineSeries series;

    std::vector<float> x{0.0f, 1.0f, 2.0f, 3.0f};
    std::vector<float> y1{0.0f, 1.0f, 0.0f, 1.0f};
    std::vector<float> y2{5.0f, 6.0f, 7.0f, 8.0f};

    series.set_data(x, y1);
    renderer.upload_series_data(series);
    const int uploads_after_first = backend.upload_count_;
    EXPECT_GT(uploads_after_first, 0);

    series.set_data(x, y2);   // same point count, different data
    ASSERT_TRUE(series.is_dirty());
    renderer.upload_series_data(series);
    EXPECT_GT(backend.upload_count_, uploads_after_first);
    EXPECT_FALSE(series.is_dirty());
    EXPECT_EQ(backend.overflow_count_, 0);
}

// #54: growing a mesh's float count without changing floor(size/6) must not
// upload past the end of the previously allocated vertex buffer.
TEST(BugfixRenderUpload, MeshVertexBufferGrowsWithFloatCount)
{
    FakeBackend      backend;
    ui::ThemeManager theme;
    Renderer         renderer(backend, theme);

    std::vector<float>    v12(12, 1.0f);
    std::vector<float>    v17(17, 1.0f);
    std::vector<uint32_t> idx{0, 1, 0};

    MeshSeries mesh(v12, idx);
    renderer.upload_series_data(mesh);
    EXPECT_EQ(backend.overflow_count_, 0);

    mesh.set_vertices(v17);   // vertex_count() stays 2, float count grows
    renderer.upload_series_data(mesh);
    EXPECT_EQ(backend.overflow_count_, 0);
}

// #99: an init that fails after the atlas texture is created must release it,
// and repeated failed attempts (as Renderer::init's fallbacks do) must not
// accumulate resources.
TEST(BugfixTextRenderer, FailedInitReleasesResources)
{
    std::vector<uint8_t> font = load_font();
    if (font.empty())
        GTEST_SKIP() << "Inter-Regular.ttf not found";

    FakeBackend backend;
    backend.fail_pipelines_ = true;

    TextRenderer tr;
    for (int i = 0; i < 3; ++i)
    {
        EXPECT_FALSE(tr.init(backend, font.data(), font.size()));
        EXPECT_TRUE(backend.textures_.empty()) << "attempt " << i;
        EXPECT_TRUE(backend.buffers_.empty()) << "attempt " << i;
    }
    // The failure must have happened after the atlas was created, otherwise
    // this test would not exercise the leak path.
    EXPECT_EQ(backend.texture_creates_, 3);
}
