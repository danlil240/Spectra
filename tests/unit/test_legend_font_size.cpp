// Audit S14: a requested LegendConfig::font_size gives the same legend in the
// GPU/PNG (renderer), SVG and window (ImGui LegendInteraction) paths.  The
// renderer used to snap any size to a baked 13/14/18 px preset, so 30 px text
// came out at 18 px in a smaller box than the window and SVG legends.

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <gtest/gtest.h>
#include <limits>
#include <regex>
#include <spectra/axes.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "render/backend.hpp"
#include "render/renderer.hpp"
#include "render/text_renderer.hpp"
#include "ui/overlay/legend_layout.hpp"
#include "ui/theme/theme.hpp"

#if defined(SPECTRA_USE_IMGUI) && __has_include(<imgui.h>)
    #define SPECTRA_TEST_IMGUI_LEGEND 1
    #include <format>
    #include <imgui.h>
    #include <imgui_internal.h>

    #include "../../third_party/inter_font.hpp"
    #include "ui/overlay/legend_interaction.hpp"
#endif

using namespace spectra;

namespace
{

// CPU-only backend that keeps uploaded buffer contents and, for each draw,
// the buffer bound at binding 0 (the vertex / point buffer), so legend box
// geometry and glyph quads can be read back.
class CapturingBackend : public Backend
{
   public:
    struct Draw
    {
        PipelineType        pipeline;
        SeriesPushConstants pc;
        uint32_t            count;
        uint32_t            first;
        uint64_t            buffer;
    };

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
        pipeline_types_[h.id] = type;
        return h;
    }
    BufferHandle create_buffer(BufferUsage, size_t) override { return BufferHandle{next_id_++}; }
    void         destroy_buffer(BufferHandle) override {}
    void upload_buffer(BufferHandle h, const void* data, size_t size, size_t offset) override
    {
        auto& bytes = buffers_[h.id];
        if (bytes.size() < offset + size)
            bytes.resize(offset + size);
        std::memcpy(bytes.data() + offset, data, size);
    }
    TextureHandle create_texture(uint32_t, uint32_t, const uint8_t*) override
    {
        return TextureHandle{next_id_++};
    }
    void destroy_texture(TextureHandle) override {}

    bool begin_frame(FrameProfiler*) override { return true; }
    void end_frame(FrameProfiler*) override {}
    void begin_render_pass(const Color&) override {}
    void end_render_pass() override {}
    void bind_pipeline(PipelineHandle h) override
    {
        auto it = pipeline_types_.find(h.id);
        bound_  = it != pipeline_types_.end() ? it->second : PipelineType::Image3D;
    }
    void bind_buffer(BufferHandle h, uint32_t binding) override
    {
        if (binding == 0)
            bound_buffer_ = h.id;
    }
    void bind_index_buffer(BufferHandle) override {}
    void bind_texture(TextureHandle, uint32_t) override {}
    void push_constants(const SeriesPushConstants& pc) override { pc_ = pc; }
    void set_viewport(float, float, float, float) override {}
    void set_scissor(int32_t, int32_t, uint32_t, uint32_t) override {}
    void set_line_width(float) override {}
    void draw(uint32_t n, uint32_t first) override { record(n, first); }
    void draw_instanced(uint32_t n, uint32_t, uint32_t first, uint32_t) override
    {
        record(n, first);
    }
    void     draw_indexed(uint32_t n, uint32_t first, int32_t) override { record(n, first); }
    bool     readback_framebuffer(uint8_t*, uint32_t, uint32_t) override { return false; }
    uint32_t swapchain_width() const override { return 800; }
    uint32_t swapchain_height() const override { return 600; }

    // Copy of a buffer's contents as T elements, captured at draw time.
    template <typename T>
    std::vector<T> as(const Draw& d) const
    {
        const auto&    bytes = snapshots_[static_cast<size_t>(&d - draws_.data())];
        std::vector<T> out(bytes.size() / sizeof(T));
        if (!out.empty())
            std::memcpy(out.data(), bytes.data(), out.size() * sizeof(T));
        return out;
    }

    void clear()
    {
        draws_.clear();
        snapshots_.clear();
    }

    std::vector<Draw> draws_;

   private:
    void record(uint32_t n, uint32_t first)
    {
        draws_.push_back({bound_, pc_, n, first, bound_buffer_});
        auto it = buffers_.find(bound_buffer_);
        snapshots_.push_back(it != buffers_.end() ? it->second : std::vector<uint8_t>{});
    }

    uint64_t                                           next_id_ = 1;
    std::unordered_map<uint64_t, PipelineType>         pipeline_types_;
    std::unordered_map<uint64_t, std::vector<uint8_t>> buffers_;
    std::vector<std::vector<uint8_t>>                  snapshots_;
    PipelineType                                       bound_        = PipelineType::Image3D;
    uint64_t                                           bound_buffer_ = 0;
    SeriesPushConstants                                pc_{};
};

struct Box
{
    float x0 = std::numeric_limits<float>::max();
    float y0 = std::numeric_limits<float>::max();
    float x1 = std::numeric_limits<float>::lowest();
    float y1 = std::numeric_limits<float>::lowest();
    float w() const { return x1 - x0; }
    float h() const { return y1 - y0; }
    bool  valid() const { return x1 >= x0; }
    void  add(float x, float y)
    {
        x0 = std::min(x0, x);
        y0 = std::min(y0, y);
        x1 = std::max(x1, x);
        y1 = std::max(y1, y);
    }
};

// Labels without descenders so glyph heights compare cleanly.
constexpr const char* LABEL_A = "sine wave";
constexpr const char* LABEL_B = "cosine";

void build_figure(Figure& fig, float font_size)
{
    auto&              ax = fig.subplot(1, 1, 1);
    std::vector<float> x  = {0.0f, 1.0f};
    std::vector<float> y  = {0.0f, 1.0f};
    ax.plot(x, y, "r-").label(LABEL_A);
    ax.plot(x, y, "b-").label(LABEL_B);
    fig.legend().position  = LegendPosition::TopLeft;
    fig.legend().font_size = font_size;
    fig.legend().bg_color  = Color{0.2f, 0.4f, 0.6f, 1.0f};   // easy to find
}

struct RasterLegend
{
    Box   box;           // legend background
    float glyph_h = 0;   // tallest glyph quad inside the box
};

struct LegendFontFixture : ::testing::Test
{
    CapturingBackend backend;
    ui::ThemeManager theme;
    Renderer         renderer{backend, theme};

    void SetUp() override { ASSERT_TRUE(renderer.init()); }

    RasterLegend raster(float font_size)
    {
        Figure fig({.width = 800, .height = 600});
        build_figure(fig, font_size);
        fig.compute_layout();
        for (auto& ax : fig.axes_mut())
            for (auto& s : ax->series_mut())
                renderer.upload_series_data(*s);
        backend.clear();
        renderer.render_figure(fig);

        RasterLegend out;
        const Color& bg = fig.legend().bg_color;
        for (const auto& d : backend.draws_)
        {
            if (d.pipeline != PipelineType::Overlay || std::abs(d.pc.color[0] - bg.r) > 1e-4f
                || std::abs(d.pc.color[1] - bg.g) > 1e-4f || std::abs(d.pc.color[2] - bg.b) > 1e-4f)
                continue;
            const auto v = backend.as<float>(d);
            for (uint32_t i = d.first; i < d.first + d.count && 2 * i + 1 < v.size(); ++i)
                out.box.add(v[2 * i], v[2 * i + 1]);
            break;
        }
        if (!out.box.valid())
            return out;
        for (const auto& d : backend.draws_)
        {
            if (d.pipeline != PipelineType::Text)
                continue;
            const auto v = backend.as<TextVertex>(d);
            for (size_t q = 0; q + 6 <= std::min<size_t>(v.size(), d.count); q += 6)
            {
                Box g;
                for (size_t k = 0; k < 6; ++k)
                    g.add(v[q + k].x, v[q + k].y);
                if (g.x0 >= out.box.x0 && g.x1 <= out.box.x1 && g.y0 >= out.box.y0
                    && g.y1 <= out.box.y1)
                    out.glyph_h = std::max(out.glyph_h, g.h());
            }
        }
        return out;
    }
};

// SVG legend <rect> and label font-size for the same figure.
struct SvgLegend
{
    float w = 0, h = 0, font_px = 0;
};

SvgLegend svg(float font_size)
{
    Figure fig({.width = 800, .height = 600});
    build_figure(fig, font_size);
    fig.compute_layout();
    const std::string s     = SvgExporter::to_string(fig);
    const size_t      start = s.find("<g class=\"legend\">");
    SvgLegend         out;
    if (start == std::string::npos)
        return out;
    const std::string legend = s.substr(start, s.find("</g>", start) - start);
    std::smatch       m;
    if (std::regex_search(legend,
                          m,
                          std::regex("<rect [^>]*width=\"([0-9.]+)\" height=\"([0-9.]+)\"")))
    {
        out.w = std::stof(m[1]);
        out.h = std::stof(m[2]);
    }
    if (std::regex_search(legend, m, std::regex("font-size=\"([0-9.]+)")))
        out.font_px = std::stof(m[1]);
    return out;
}

// Box height every path uses for an explicit size: rows of font_size + 6.
float expected_height(float font_size, int rows, float padding = 8.0f)
{
    return 2.0f * padding + static_cast<float>(rows) * (font_size + 6.0f);
}

}   // namespace

TEST(TextRendererScaledFont, PicksPresetAtLeastAsLargeAndScales)
{
    auto sf = TextRenderer::scaled_font(30.0f);
    EXPECT_EQ(sf.preset, FontSize::Title);
    EXPECT_FLOAT_EQ(sf.scale * TextRenderer::preset_pixel_size(sf.preset), 30.0f);

    sf = TextRenderer::scaled_font(14.0f);
    EXPECT_EQ(sf.preset, FontSize::Label);
    EXPECT_FLOAT_EQ(sf.scale, 1.0f);

    sf = TextRenderer::scaled_font(9.0f);
    EXPECT_EQ(sf.preset, FontSize::Tick);
    EXPECT_FLOAT_EQ(sf.scale * TextRenderer::preset_pixel_size(sf.preset), 9.0f);

    sf = TextRenderer::scaled_font(0.0f);
    EXPECT_EQ(sf.preset, FontSize::Label);
    EXPECT_FLOAT_EQ(sf.scale, 1.0f);
}

TEST_F(LegendFontFixture, RequestedSizeMatchesSvgBoxAndText)
{
    const RasterLegend r30 = raster(30.0f);
    const SvgLegend    s30 = svg(30.0f);
    ASSERT_TRUE(r30.box.valid());
    ASSERT_GT(s30.h, 0.0f);

    // Box height: both paths lay out two rows of 30 + 6 px.
    EXPECT_NEAR(r30.box.h(), expected_height(30.0f, 2), 1.0f);
    EXPECT_NEAR(r30.box.h(), s30.h, 1.0f);
    EXPECT_FLOAT_EQ(s30.font_px, 30.0f);

    // Box width: the SVG exporter sizes the box from a per-character width
    // estimate (estimate_text_width), which runs ~15-20% wider than Inter's
    // real advances, so only a loose bound is possible here; snapping to
    // 18 px used to make the raster box ~37% narrower than the SVG one.  The
    // raster label width is checked against real Inter metrics (ImGui) below.
    EXPECT_NEAR(r30.box.w(), s30.w, 0.2f * s30.w);

    // Text size: glyphs are drawn at 30/18 of the 18 px preset, i.e. a 30 px
    // font (cap height ~0.73 em for Inter), not clamped to 18 px.
    const RasterLegend r18 = raster(18.0f);
    ASSERT_GT(r18.glyph_h, 0.0f);
    EXPECT_NEAR(r30.glyph_h / r18.glyph_h, 30.0f / 18.0f, 0.03f);
    EXPECT_NEAR(r18.box.h(), expected_height(18.0f, 2), 1.0f);
}

TEST_F(LegendFontFixture, SmallRequestedSizeIsNotRoundedUp)
{
    const RasterLegend r10 = raster(10.0f);
    const SvgLegend    s10 = svg(10.0f);
    ASSERT_TRUE(r10.box.valid());
    EXPECT_NEAR(r10.box.h(), expected_height(10.0f, 2), 1.0f);
    EXPECT_NEAR(r10.box.h(), s10.h, 1.0f);
}

// The default (unset) size keeps its historical raster layout: Label preset
// rows (line height + 6), so default-size golden images are unchanged.
TEST_F(LegendFontFixture, DefaultSizeLayoutUnchanged)
{
    const RasterLegend r0 = raster(0.0f);
    ASSERT_TRUE(r0.box.valid());
    const float line_h = renderer.text_renderer().measure_text(LABEL_A, FontSize::Label).height;
    EXPECT_NEAR(r0.box.h(), 2.0f * 8.0f + 2.0f * (line_h + 6.0f), 1.0f);
    const RasterLegend r14 = raster(14.0f);
    // Explicit 14 px draws the same Label glyphs unscaled.
    EXPECT_NEAR(r0.glyph_h, r14.glyph_h, 0.01f);
}

#ifdef SPECTRA_TEST_IMGUI_LEGEND

// Window path: the ImGui legend window for the same request has the same
// height as the raster box, and Inter label widths agree within a pixel or two.
TEST_F(LegendFontFixture, WindowLegendMatchesRasterHeight)
{
    ImGui::CreateContext();
    ImGuiIO& io    = ImGui::GetIO();
    io.DisplaySize = ImVec2(800.0f, 600.0f);
    io.DeltaTime   = 1.0f / 60.0f;
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    ImFont* inter            = io.Fonts->AddFontFromMemoryCompressedTTF(InterFont_compressed_data,
                                                             InterFont_compressed_size,
                                                             14.0f,
                                                             &cfg);
    ASSERT_NE(inter, nullptr);
    unsigned char* px = nullptr;
    int            tw = 0, th = 0;
    io.Fonts->GetTexDataAsRGBA32(&px, &tw, &th);

    ui::ThemeManager  theme_mgr;
    LegendInteraction legend;
    legend.set_theme_manager(&theme_mgr);

    Figure fig({.width = 800, .height = 600});
    build_figure(fig, 30.0f);
    fig.compute_layout();
    auto&      ax = *fig.axes_mut()[0];
    const Rect vp = ax.viewport();

    ImGui::NewFrame();
    ImGui::PushFont(inter);
    legend.draw(ax, vp, 0, fig.legend(), 1);
    ImGui::PopFont();
    ImGui::Render();

    ImGuiWindow* win = ImGui::FindWindowByName(std::format("##legend_{:x}_{}", 1ul, 0).c_str());
    ASSERT_NE(win, nullptr);
    const RasterLegend r30 = raster(30.0f);
    EXPECT_NEAR(win->Size.y, expected_height(30.0f, 2), 1.0f);
    EXPECT_NEAR(win->Size.y, r30.box.h(), 1.0f);

    // Same Inter face at 30 px: raster label width ~= ImGui label width.
    const auto  sf = TextRenderer::scaled_font(30.0f);
    const float raster_w =
        renderer.text_renderer().measure_text(LABEL_A, sf.preset, sf.scale).width;
    const float imgui_w = inter->CalcTextSizeA(30.0f, FLT_MAX, 0.0f, LABEL_A).x;
    EXPECT_NEAR(raster_w, imgui_w, 0.03f * imgui_w);
    ImGui::DestroyContext();
}

#endif
