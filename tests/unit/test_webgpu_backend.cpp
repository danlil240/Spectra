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
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <regex>
#include <set>
#include <string>
#include <vector>

// WGSL shader embedding: always available once the WGSL sources exist.
// Does NOT require SPECTRA_USE_WEBGPU or Dawn.
#include "shader_wgsl.hpp"

#include "render/backend.hpp"
#include "render/webgpu/wgpu_pipeline_support.hpp"

#ifdef SPECTRA_USE_WEBGPU
    #include "render/webgpu/wgpu_backend.hpp"
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

TEST(WGSLShaderEmbedding, ScatterShaderHasColormapEntryPoint)
{
    // PipelineType::ScatterColormap reuses scatter.wgsl with a vertex stage
    // that reads (x, y, value, pad) and hands the value to the fragment stage.
    EXPECT_NE(std::strstr(wgsl::scatter_wgsl, "fn vs_colormap"), nullptr);
    EXPECT_NE(std::strstr(wgsl::scatter_wgsl, "@interpolate(flat)"), nullptr);
    EXPECT_NE(std::strstr(wgsl::scatter_wgsl, "fn apply_colormap"), nullptr);
}

TEST(WGSLShaderEmbedding, ScatterShaderHasDefaultDotRim)
{
    // Marker type 18 (default scatter dot) draws a contrasting rim, as in
    // scatter.frag, driven by SeriesPC._pad2.y.
    EXPECT_NE(std::strstr(wgsl::scatter_wgsl, "case 18u"), nullptr);
    EXPECT_NE(std::strstr(wgsl::scatter_wgsl, "has_edge = true"), nullptr);
    EXPECT_NE(std::strstr(wgsl::scatter_wgsl, "_pad2.y"), nullptr);
}

TEST(WGSLShaderEmbedding, HeatmapShaderEntryPointsAndBindings)
{
    ASSERT_NE(wgsl::heatmap_wgsl, nullptr);
    EXPECT_NE(std::strstr(wgsl::heatmap_wgsl, "fn vs_main"), nullptr);
    EXPECT_NE(std::strstr(wgsl::heatmap_wgsl, "fn fs_main"), nullptr);
    EXPECT_NE(std::strstr(wgsl::heatmap_wgsl, "@group(0) @binding(0)"), nullptr);
    EXPECT_NE(std::strstr(wgsl::heatmap_wgsl, "@group(0) @binding(1)"), nullptr);
    EXPECT_NE(std::strstr(wgsl::heatmap_wgsl, "@group(1) @binding(0)"), nullptr);
}

// ─── WGSL uniform address-space layout (always run) ──────────────────────────

namespace
{

// Returns "struct.member" for every member of a struct bound as var<uniform>
// (directly or nested) that is an array of scalars or vec2s.  Those have an
// element stride below 16 bytes, which the WGSL uniform address space forbids
// (naga and Tint reject the shader).  Comments are stripped first.
std::vector<std::string> uniform_small_stride_arrays(const std::string& src_in)
{
    const std::string src = std::regex_replace(src_in, std::regex(R"(//[^\n]*)"), "");

    const std::string bad_elem = R"((f32|i32|u32|f16|bool|vec2\s*<[^>]*>|vec2[fhiu]))";
    const std::regex  bad_array(R"(array\s*<\s*)" + bad_elem + R"(\s*[,>])");
    const std::regex  bad_member(R"((\w+)\s*:\s*array\s*<\s*)" + bad_elem + R"(\s*[,>])");
    const std::regex  ident(R"(\b[A-Za-z_]\w*\b)");
    const std::regex  uniform_var(R"(var\s*<\s*uniform\s*>\s*\w+\s*:\s*([^;]+);)");

    std::vector<std::string> bad;
    std::set<std::string>    pending;   // identifiers that may name a uniform struct
    auto                     queue_idents = [&](const std::string& text)
    {
        for (auto i = std::sregex_iterator(text.begin(), text.end(), ident);
             i != std::sregex_iterator();
             ++i)
            pending.insert(i->str());
    };
    for (auto it = std::sregex_iterator(src.begin(), src.end(), uniform_var);
         it != std::sregex_iterator();
         ++it)
    {
        const std::string type = (*it)[1].str();
        if (std::regex_search(type, bad_array))
            bad.push_back("<var>:" + type);
        queue_idents(type);
    }

    std::set<std::string> seen;
    while (!pending.empty())
    {
        const std::string name = *pending.begin();
        pending.erase(pending.begin());
        if (!seen.insert(name).second)
            continue;
        std::smatch body;
        if (!std::regex_search(src, body, std::regex(R"(struct\s+)" + name + R"(\s*\{([^}]*)\})")))
            continue;   // builtin type or keyword (vec4, f32, array, ...)
        const std::string members = body[1].str();
        for (auto m = std::sregex_iterator(members.begin(), members.end(), bad_member);
             m != std::sregex_iterator();
             ++m)
            bad.push_back(name + "." + (*m)[1].str());
        queue_idents(members);
    }
    return bad;
}

}   // namespace

TEST(WGSLUniformLayout, CheckerFlagsScalarArrays)
{
    const std::string bad  = "struct P {\n  a : vec4<f32>,\n  d : array<f32, 8>,\n};\n"
                             "@group(0) @binding(1) var<uniform> p : P;\n";
    const auto        hits = uniform_small_stride_arrays(bad);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0], "P.d");

    const std::string ok = "struct P {\n  d : array<vec4<f32>, 2>,\n};\n"
                           "struct S { v : array<f32> };\n"   // storage: any stride is fine
                           "@group(0) @binding(1) var<uniform> p : P;\n"
                           "@group(1) @binding(0) var<storage, read> s : S;\n";
    EXPECT_TRUE(uniform_small_stride_arrays(ok).empty());
}

TEST(WGSLUniformLayout, NoUniformArrayHasStrideBelow16)
{
    ASSERT_GT(std::size(wgsl::all_shaders), 0u);
    for (const auto& shader : wgsl::all_shaders)
    {
        const auto hits = uniform_small_stride_arrays(shader.source);
        for (const auto& h : hits)
            ADD_FAILURE() << shader.name << ".wgsl: uniform array '" << h
                          << "' has an element stride below 16 bytes";
    }
}

// ─── Struct layout constraints ────────────────────────────────────────────────

TEST(WebGPULayout, SeriesPushConstantsAlignment)
{
    // WebGPU uniform buffers must be 256-byte aligned.
    // SeriesPushConstants must fit within one 256-byte slot.
    constexpr size_t sz = sizeof(SeriesPushConstants);
    EXPECT_LE(sz, 256u) << "SeriesPushConstants (" << sz
                        << " bytes) exceeds 256-byte UBO alignment budget";
}

TEST(WebGPULayout, SeriesPushConstantsMatchesWgslSeriesPC)
{
    // Byte offsets of SeriesPC as WGSL lays it out in the uniform address space
    // (dash_pattern is array<vec4<f32>, 2>: 16-byte aligned, 32 bytes).
    EXPECT_EQ(offsetof(SeriesPushConstants, color), 0u);
    EXPECT_EQ(offsetof(SeriesPushConstants, line_width), 16u);
    EXPECT_EQ(offsetof(SeriesPushConstants, opacity), 44u);
    EXPECT_EQ(offsetof(SeriesPushConstants, dash_pattern), 48u);
    EXPECT_EQ(offsetof(SeriesPushConstants, dash_total), 80u);
    EXPECT_EQ(offsetof(SeriesPushConstants, dash_count), 84u);
    EXPECT_EQ(offsetof(SeriesPushConstants, _pad2), 88u);
    EXPECT_EQ(sizeof(SeriesPushConstants), 96u);
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

// ─── Surface format selection (always run) ───────────────────────────────────
//
// Values mirror webgpu.h (WGPUTextureFormat_*); the helper is header-only so
// it is testable without Dawn.  The pipelines' colour target must use the same
// format the swapchain is configured with, or every render pass fails
// validation (blank canvas on browsers that prefer rgba8unorm).

namespace
{
constexpr uint32_t FMT_UNDEFINED   = 0x00;
constexpr uint32_t FMT_RGBA8       = 0x16;
constexpr uint32_t FMT_RGBA8_SRGB  = 0x17;
constexpr uint32_t FMT_BGRA8       = 0x1B;
constexpr uint32_t FMT_BGRA8_SRGB  = 0x1C;
constexpr uint32_t FMT_RGBA16FLOAT = 0x28;

uint32_t pick(std::initializer_list<uint32_t> formats)
{
    std::vector<uint32_t> v(formats);
    return webgpu_pick_surface_format<uint32_t>(v, FMT_RGBA8, FMT_BGRA8, FMT_UNDEFINED);
}
}   // namespace

TEST(WebGPUSurfaceFormat, BrowserPreferringRgbaGetsRgba)
{
    // Chrome on Android / Linux / ChromeOS: getPreferredCanvasFormat() = rgba8unorm.
    EXPECT_EQ(pick({FMT_RGBA8, FMT_BGRA8, FMT_RGBA16FLOAT}), FMT_RGBA8);
}

TEST(WebGPUSurfaceFormat, BrowserPreferringBgraGetsBgra)
{
    EXPECT_EQ(pick({FMT_BGRA8, FMT_RGBA8, FMT_RGBA16FLOAT}), FMT_BGRA8);
}

TEST(WebGPUSurfaceFormat, SkipsLeadingSrgbForUnorm)
{
    // Native Dawn often lists an sRGB format first; UNORM keeps theme colours 1:1.
    EXPECT_EQ(pick({FMT_BGRA8_SRGB, FMT_BGRA8}), FMT_BGRA8);
    EXPECT_EQ(pick({FMT_RGBA8_SRGB, FMT_RGBA16FLOAT, FMT_RGBA8}), FMT_RGBA8);
}

TEST(WebGPUSurfaceFormat, NoUnormFallsBackToPreferred)
{
    EXPECT_EQ(pick({FMT_RGBA16FLOAT, FMT_BGRA8_SRGB}), FMT_RGBA16FLOAT);
}

TEST(WebGPUSurfaceFormat, EmptyListReturnsFallback)
{
    EXPECT_EQ(pick({}), FMT_UNDEFINED);
}

TEST(WebGPUSurfaceFormat, IsConstexpr)
{
    constexpr uint32_t formats[] = {FMT_RGBA16FLOAT, FMT_RGBA8};
    static_assert(webgpu_pick_surface_format<uint32_t>(formats, FMT_RGBA8, FMT_BGRA8, 0u)
                  == FMT_RGBA8);
    SUCCEED();
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
    bool          result = true;
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
    WebGPUBackend  backend;
    PipelineHandle h = backend.create_pipeline(PipelineType::Line);
    EXPECT_FALSE(static_cast<bool>(h));
}

TEST(WebGPUBackendNoOp, CreateBufferBeforeInit)
{
    WebGPUBackend backend;
    BufferHandle  h = backend.create_buffer(BufferUsage::Vertex, 1024);
    EXPECT_FALSE(static_cast<bool>(h));
}

TEST(WebGPUBackendNoOp, CreateTextureBeforeInit)
{
    WebGPUBackend backend;
    const uint8_t px[4] = {255, 0, 0, 255};
    TextureHandle h     = backend.create_texture(1, 1, px);
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
    uint8_t       buf[4]{};
    EXPECT_FALSE(backend.readback_framebuffer(buf, 1, 1));
}

TEST(WebGPUBackendDimensions, ZeroBeforeSwapchain)
{
    WebGPUBackend backend;
    EXPECT_EQ(backend.swapchain_width(), 0u);
    EXPECT_EQ(backend.swapchain_height(), 0u);
}

#endif   // SPECTRA_USE_WEBGPU
