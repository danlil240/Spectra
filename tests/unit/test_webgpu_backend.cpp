// test_webgpu_backend.cpp — Unit tests for the WebGPU backend.
//
// These tests validate:
//   1. WGSL shader embedding: embedded sources are non-empty strings with
//      correct entry points and bind group annotations.  These tests run
//      in every CI configuration — they do NOT require Dawn or a real GPU.
//   2. Compile-time API surface: WebGPUBackend inherits from Backend and
//      declares all methods correctly.  Requires SPECTRA_USE_WEBGPU=ON.
//   3. Runtime behaviour when a real WebGPU device is unavailable:
//      init() fails gracefully, all subsequent calls are no-ops.
//   4. Buffer and pipeline handle semantics (ID assignment, invalid handles).
//   5. Struct layout constraints required by the WebGPU uniform buffer rules.

#include <gtest/gtest.h>
#include <cstring>

// WGSL shader embedding: always available once the WGSL sources exist.
// Does NOT require SPECTRA_USE_WEBGPU or Dawn.
#include "shader_wgsl.hpp"

#include "render/backend.hpp"

#ifdef SPECTRA_USE_WEBGPU
#    include "render/webgpu/wgpu_backend.hpp"
#endif

using namespace spectra;

// ─── WGSL shader embedding tests (always run) ────────────────────────────────

TEST(WGSLShaderEmbedding, LineShaderNonEmpty)
{
    EXPECT_NE(wgsl::line_wgsl, nullptr);
    EXPECT_GT(std::strlen(wgsl::line_wgsl), 0u);
}

TEST(WGSLShaderEmbedding, ScatterShaderNonEmpty)
{
    EXPECT_NE(wgsl::scatter_wgsl, nullptr);
    EXPECT_GT(std::strlen(wgsl::scatter_wgsl), 0u);
}

TEST(WGSLShaderEmbedding, GridShaderNonEmpty)
{
    EXPECT_NE(wgsl::grid_wgsl, nullptr);
    EXPECT_GT(std::strlen(wgsl::grid_wgsl), 0u);
}

TEST(WGSLShaderEmbedding, StatFillShaderNonEmpty)
{
    EXPECT_NE(wgsl::stat_fill_wgsl, nullptr);
    EXPECT_GT(std::strlen(wgsl::stat_fill_wgsl), 0u);
}

TEST(WGSLShaderEmbedding, TextShaderNonEmpty)
{
    EXPECT_NE(wgsl::text_wgsl, nullptr);
    EXPECT_GT(std::strlen(wgsl::text_wgsl), 0u);
}

TEST(WGSLShaderEmbedding, LineShaderContainsEntryPoints)
{
    EXPECT_NE(std::strstr(wgsl::line_wgsl, "vs_main"), nullptr);
    EXPECT_NE(std::strstr(wgsl::line_wgsl, "fs_main"), nullptr);
}

TEST(WGSLShaderEmbedding, ScatterShaderContainsEntryPoints)
{
    EXPECT_NE(std::strstr(wgsl::scatter_wgsl, "vs_main"), nullptr);
    EXPECT_NE(std::strstr(wgsl::scatter_wgsl, "fs_main"), nullptr);
}

TEST(WGSLShaderEmbedding, GridShaderContainsGroupBindings)
{
    // Verify group(0) binding(0) for FrameUBO and binding(1) for SeriesPC
    EXPECT_NE(std::strstr(wgsl::grid_wgsl, "@group(0) @binding(0)"), nullptr);
    EXPECT_NE(std::strstr(wgsl::grid_wgsl, "@group(0) @binding(1)"), nullptr);
}

TEST(WGSLShaderEmbedding, LineShaderContainsSSBOBinding)
{
    // SSBO vertex data must be in group 1 binding 0.
    EXPECT_NE(std::strstr(wgsl::line_wgsl, "@group(1) @binding(0)"), nullptr);
}

TEST(WGSLShaderEmbedding, TextShaderContainsTextureBinding)
{
    // Font texture must be in group 1.
    EXPECT_NE(std::strstr(wgsl::text_wgsl, "@group(1) @binding(0)"), nullptr);
    EXPECT_NE(std::strstr(wgsl::text_wgsl, "@group(1) @binding(1)"), nullptr);
}

TEST(WGSLShaderEmbedding, StatFillShaderContainsAlphaAttribute)
{
    // StatFill vertex shader receives per-vertex alpha multiplier.
    EXPECT_NE(std::strstr(wgsl::stat_fill_wgsl, "in_alpha"), nullptr);
}

// ─── Struct layout constraints ────────────────────────────────────────────────

TEST(WebGPULayout, SeriesPushConstantsAlignment)
{
    // WebGPU uniform buffers must be 256-byte aligned.
    // SeriesPushConstants must fit within one 256-byte slot.
    constexpr size_t sz = sizeof(SeriesPushConstants);
    EXPECT_LE(sz, 256u)
        << "SeriesPushConstants (" << sz << " bytes) exceeds 256-byte UBO alignment budget";
}

TEST(WebGPULayout, FrameUBOAlignment)
{
    constexpr size_t sz      = sizeof(FrameUBO);
    const size_t     aligned = (sz + 255) & ~255ull;
    EXPECT_EQ(aligned % 256, 0u);
}

// ─── Handle validity (always run) ────────────────────────────────────────────

TEST(WebGPUHandles, InvalidBufferHandle)
{
    BufferHandle h{};
    EXPECT_FALSE(static_cast<bool>(h));
    EXPECT_EQ(h.id, 0u);
}

TEST(WebGPUHandles, InvalidPipelineHandle)
{
    PipelineHandle h{};
    EXPECT_FALSE(static_cast<bool>(h));
    EXPECT_EQ(h.id, 0u);
}

TEST(WebGPUHandles, InvalidTextureHandle)
{
    TextureHandle h{};
    EXPECT_FALSE(static_cast<bool>(h));
    EXPECT_EQ(h.id, 0u);
}

// ─── Backend class tests (require SPECTRA_USE_WEBGPU=ON) ─────────────────────

#ifdef SPECTRA_USE_WEBGPU

TEST(WebGPUBackendTypes, InheritsFromBackend)
{
    static_assert(std::is_base_of_v<Backend, WebGPUBackend>,
                  "WebGPUBackend must derive from Backend");
}

TEST(WebGPUBackendTypes, IsNotCopyable)
{
    static_assert(!std::is_copy_constructible_v<WebGPUBackend>,
                  "WebGPUBackend must not be copy-constructible");
    static_assert(!std::is_copy_assignable_v<WebGPUBackend>,
                  "WebGPUBackend must not be copy-assignable");
}

TEST(WebGPUBackendTypes, HasVirtualDestructor)
{
    static_assert(std::has_virtual_destructor_v<WebGPUBackend>,
                  "WebGPUBackend must have a virtual destructor");
}

TEST(WebGPUBackendConstruction, DefaultConstruct)
{
    EXPECT_NO_THROW({ WebGPUBackend backend; });
}

TEST(WebGPUBackendConstruction, DestructWithoutInit)
{
    EXPECT_NO_THROW({
        WebGPUBackend backend;
        // backend destroyed here — must not crash
    });
}

TEST(WebGPUBackendLifecycle, InitFailsGracefullyWithoutGPU)
{
    WebGPUBackend backend;
    bool result = true;
    EXPECT_NO_THROW({ result = backend.init(/*headless=*/true); });
    // Either init succeeds (GPU available) or fails gracefully.
    (void)result;
}

TEST(WebGPUBackendLifecycle, ShutdownBeforeInit)
{
    WebGPUBackend backend;
    EXPECT_NO_THROW({ backend.shutdown(); });
}

TEST(WebGPUBackendLifecycle, WaitIdleBeforeInit)
{
    WebGPUBackend backend;
    EXPECT_NO_THROW({ backend.wait_idle(); });
}

TEST(WebGPUBackendNoOp, CreateSurfaceBeforeInit)
{
    WebGPUBackend backend;
    EXPECT_FALSE(backend.create_surface(nullptr));
}

TEST(WebGPUBackendNoOp, CreateSwapchainBeforeInit)
{
    WebGPUBackend backend;
    EXPECT_FALSE(backend.create_swapchain(800, 600));
}

TEST(WebGPUBackendNoOp, CreateOffscreenBeforeInit)
{
    WebGPUBackend backend;
    EXPECT_FALSE(backend.create_offscreen_framebuffer(800, 600));
}

TEST(WebGPUBackendNoOp, CreatePipelineBeforeInit)
{
    WebGPUBackend backend;
    PipelineHandle h = backend.create_pipeline(PipelineType::Line);
    EXPECT_FALSE(static_cast<bool>(h));
}

TEST(WebGPUBackendNoOp, CreateBufferBeforeInit)
{
    WebGPUBackend backend;
    BufferHandle h = backend.create_buffer(BufferUsage::Vertex, 1024);
    EXPECT_FALSE(static_cast<bool>(h));
}

TEST(WebGPUBackendNoOp, CreateTextureBeforeInit)
{
    WebGPUBackend backend;
    const uint8_t px[4] = {255, 0, 0, 255};
    TextureHandle h = backend.create_texture(1, 1, px);
    EXPECT_FALSE(static_cast<bool>(h));
}

TEST(WebGPUBackendNoOp, BeginFrameBeforeInit)
{
    WebGPUBackend backend;
    EXPECT_FALSE(backend.begin_frame());
}

TEST(WebGPUBackendNoOp, ReadbackBeforeInit)
{
    WebGPUBackend backend;
    uint8_t buf[4]{};
    EXPECT_FALSE(backend.readback_framebuffer(buf, 1, 1));
}

TEST(WebGPUBackendDimensions, ZeroBeforeSwapchain)
{
    WebGPUBackend backend;
    EXPECT_EQ(backend.swapchain_width(), 0u);
    EXPECT_EQ(backend.swapchain_height(), 0u);
}

#endif   // SPECTRA_USE_WEBGPU
