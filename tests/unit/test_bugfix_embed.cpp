// Regression tests for embed-layer fixes that are testable without a GPU.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <spectra/embed.hpp>
#include <spectra/series3d.hpp>
#include <spectra/spectra_embed_c.h>

#include "embed/embed_detail.hpp"

using namespace spectra;
namespace ed = spectra::embed_detail;

// ─── Bug #35: hover indices for 2D series ────────────────────────────────────

TEST(BugfixEmbed, FindSeriesIndicesFinds2DSeries)
{
    Figure             fig;
    std::vector<float> x = {0.0f, 1.0f, 2.0f};
    std::vector<float> y = {0.0f, 1.0f, 4.0f};

    auto& ax1 = fig.subplot(1, 2, 1);
    auto& ax2 = fig.subplot(1, 2, 2);
    ax1.line(x, y);
    ax2.line(x, y);
    auto& target = ax2.scatter(x, y);

    int ai = -2;
    int si = -2;
    ed::find_series_indices(&fig, &target, ai, si);
    EXPECT_EQ(ai, 1);
    EXPECT_EQ(si, 1);
}

TEST(BugfixEmbed, FindSeriesIndicesFinds3DSeries)
{
    Figure             fig;
    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {0.0f, 1.0f};
    std::vector<float> z = {0.0f, 1.0f};

    auto& ax3    = fig.subplot3d(1, 1, 1);
    auto& target = ax3.line3d(x, y, z);

    int ai = -2;
    int si = -2;
    ed::find_series_indices(&fig, &target, ai, si);
    EXPECT_EQ(ai, 0);
    EXPECT_EQ(si, 0);
}

TEST(BugfixEmbed, FindSeriesIndicesNotFound)
{
    Figure             fig;
    Figure             other;
    std::vector<float> x = {0.0f, 1.0f};
    fig.subplot(1, 1, 1).line(x, x);
    auto& foreign = other.subplot(1, 1, 1).line(x, x);

    int ai = 7;
    int si = 7;
    ed::find_series_indices(&fig, &foreign, ai, si);
    EXPECT_EQ(ai, -1);
    EXPECT_EQ(si, -1);

    ed::find_series_indices(nullptr, &foreign, ai, si);
    EXPECT_EQ(ai, -1);
    EXPECT_EQ(si, -1);
}

// ─── Bug #37: subplot argument validation ────────────────────────────────────

TEST(BugfixEmbed, ValidSubplotArgs)
{
    EXPECT_TRUE(ed::valid_subplot_args(1, 1, 1));
    EXPECT_TRUE(ed::valid_subplot_args(2, 3, 6));
    EXPECT_FALSE(ed::valid_subplot_args(1, 1, 2));
    EXPECT_FALSE(ed::valid_subplot_args(0, 1, 1));
    EXPECT_FALSE(ed::valid_subplot_args(1, 0, 1));
    EXPECT_FALSE(ed::valid_subplot_args(1, 1, 0));
    EXPECT_FALSE(ed::valid_subplot_args(-1, -1, 1));
    // rows * cols would overflow int.
    const int big = std::numeric_limits<int>::max();
    EXPECT_FALSE(ed::valid_subplot_args(big, big, 1));
}

TEST(BugfixEmbed, CApiSubplotNullFigureReturnsNull)
{
    EXPECT_EQ(spectra_figure_subplot(nullptr, 1, 1, 2), nullptr);
    EXPECT_EQ(spectra_figure_subplot3d(nullptr, 0, 0, 0), nullptr);
}

// ─── Bug #38: RGBA buffer size without 32-bit wraparound ────────────────────

TEST(BugfixEmbed, RgbaBufferSizeNoWrap)
{
    std::size_t bytes = 123;
    EXPECT_FALSE(ed::rgba_buffer_size(0, 10, bytes));
    EXPECT_EQ(bytes, 0u);
    EXPECT_FALSE(ed::rgba_buffer_size(10, 0, bytes));

    ASSERT_TRUE(ed::rgba_buffer_size(800, 600, bytes));
    EXPECT_EQ(bytes, 800u * 600u * 4u);

    if constexpr (sizeof(std::size_t) >= 8)
    {
        ASSERT_TRUE(ed::rgba_buffer_size(32768, 32768, bytes));
        EXPECT_EQ(bytes, std::size_t{1} << 32);
    }
}

// ─── Bug #86: animation stepping always terminates ───────────────────────────

TEST(BugfixEmbed, AnimationStepSanitizesFps)
{
    const float def = 1.0f / 60.0f;
    EXPECT_FLOAT_EQ(ed::animation_step(30.0f), 1.0f / 30.0f);
    EXPECT_FLOAT_EQ(ed::animation_step(0.0f), def);
    EXPECT_FLOAT_EQ(ed::animation_step(-5.0f), def);
    EXPECT_FLOAT_EQ(ed::animation_step(std::numeric_limits<float>::infinity()), def);
    EXPECT_FLOAT_EQ(ed::animation_step(std::numeric_limits<float>::quiet_NaN()), def);
    EXPECT_FLOAT_EQ(ed::animation_step(1e9f), def);
    EXPECT_GT(ed::animation_step(1e9f), 0.0f);
}

TEST(BugfixEmbed, AnimationFrameCountIsFinite)
{
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();

    EXPECT_EQ(ed::animation_frame_count(60.0f, 1.0f), 60);
    EXPECT_EQ(ed::animation_frame_count(10.0f, 0.25f), 3);
    EXPECT_EQ(ed::animation_frame_count(60.0f, 0.0f), 1);
    EXPECT_EQ(ed::animation_frame_count(60.0f, -1.0f), 1);
    EXPECT_EQ(ed::animation_frame_count(60.0f, -inf), 1);
    EXPECT_EQ(ed::animation_frame_count(60.0f, inf), 0);
    EXPECT_EQ(ed::animation_frame_count(60.0f, nan), 0);

    // Previously hung forever (step 0 / step below float ULP of t).
    EXPECT_EQ(ed::animation_frame_count(inf, 1.0f), 60);
    EXPECT_EQ(ed::animation_frame_count(1e9f, 1.0f), 60);

    // Huge durations are capped instead of stalling.
    EXPECT_EQ(ed::animation_frame_count(60.0f, 1e30f), ed::kMaxAnimationFrames);
}

TEST(BugfixEmbed, CApiAnimationNullSurface)
{
    EXPECT_EQ(spectra_embed_animation_play(nullptr, std::numeric_limits<float>::infinity(), 1.0f),
              0);
}

// ─── Surface-backed checks (skipped when no Vulkan device is available) ──────

namespace
{
struct EmbedDeleter
{
    void operator()(SpectraEmbed* s) const { spectra_embed_destroy(s); }
};
using EmbedPtr = std::unique_ptr<SpectraEmbed, EmbedDeleter>;

EmbedPtr make_embed()
{
    return EmbedPtr(spectra_embed_create(160, 120));
}
}   // namespace

// Bug #37: invalid subplot arguments on a real figure return NULL (no abort).
TEST(BugfixEmbedSurface, CApiSubplotInvalidArgsReturnNull)
{
    auto s = make_embed();
    if (!s)
        GTEST_SKIP() << "No Vulkan device";
    SpectraFigure* fig = spectra_embed_figure(s.get());
    ASSERT_NE(fig, nullptr);

    EXPECT_EQ(spectra_figure_subplot(fig, 1, 1, 2), nullptr);
    EXPECT_EQ(spectra_figure_subplot(fig, 0, 1, 1), nullptr);
    EXPECT_EQ(spectra_figure_subplot(fig, 1, 1, 0), nullptr);
    EXPECT_EQ(spectra_figure_subplot3d(fig, 2, 2, 5), nullptr);
    const int big = std::numeric_limits<int>::max();
    EXPECT_EQ(spectra_figure_subplot(fig, big, big, 1), nullptr);

    EXPECT_NE(spectra_figure_subplot(fig, 1, 1, 1), nullptr);
}

// Bug #36: handles are owned by the surface, not the creating thread, and
// repeated lookups of the same object return the same handle.
TEST(BugfixEmbedSurface, CApiHandlesSurviveCreatingThread)
{
    auto s = make_embed();
    if (!s)
        GTEST_SKIP() << "No Vulkan device";

    SpectraFigure* fig1 = spectra_embed_figure(s.get());
    ASSERT_NE(fig1, nullptr);
    EXPECT_EQ(spectra_embed_active_figure(s.get()), fig1);
    EXPECT_EQ(spectra_embed_active_figure(s.get()), spectra_embed_active_figure(s.get()));

    SpectraAxes*   ax     = nullptr;
    SpectraSeries* series = nullptr;
    std::thread    worker(
        [&]()
        {
            ax               = spectra_figure_subplot(fig1, 1, 1, 1);
            const float x[2] = {0.0f, 1.0f};
            const float y[2] = {0.0f, 1.0f};
            series           = spectra_axes_line(ax, x, y, 2, "w");
        });
    worker.join();
    ASSERT_NE(ax, nullptr);
    ASSERT_NE(series, nullptr);
    EXPECT_EQ(spectra_figure_subplot(fig1, 1, 1, 1), ax);

    // Worker thread is gone: handles must still be usable.
    spectra_series_append_xy(series, 2.0f, 4.0f);
    spectra_series_set_capacity(series, 2);
    spectra_series_append_xy(series, 3.0f, 9.0f);
    uint8_t buf[160 * 120 * 4];
    EXPECT_EQ(spectra_embed_render(s.get(), buf), 1);
}

// Bug #9: set_theme only affects the surface it is called on.
TEST(BugfixEmbedSurface, ThemeIsPerSurface)
{
    EmbedConfig cfg_a;
    cfg_a.width  = 64;
    cfg_a.height = 48;
    cfg_a.theme  = "dark";
    EmbedSurface a(cfg_a);
    if (!a.is_valid())
        GTEST_SKIP() << "No Vulkan device";
    EmbedConfig  cfg_b = cfg_a;
    EmbedSurface b(cfg_b);
    ASSERT_TRUE(b.is_valid());

    a.set_theme("light");
    EXPECT_EQ(a.theme(), "light");
    EXPECT_EQ(b.theme(), "dark");
}

// Bug #9: a temporary surface (render_easy) must not leave the theme redirect
// dangling for a surface that outlives it.  Under ASan the old code reported a
// heap-use-after-free here.
TEST(BugfixEmbedSurface, ThemeAfterTemporarySurfaceDestroyed)
{
    auto s = make_embed();
    if (!s)
        GTEST_SKIP() << "No Vulkan device";
    SpectraFigure* fig  = spectra_embed_figure(s.get());
    SpectraAxes*   ax   = spectra_figure_subplot(fig, 1, 1, 1);
    const float    x[3] = {0.0f, 1.0f, 2.0f};
    const float    y[3] = {1.0f, 0.0f, 1.0f};
    ASSERT_NE(spectra_axes_line(ax, x, y, 3, nullptr), nullptr);

    uint32_t w   = 0;
    uint32_t h   = 0;
    uint8_t* img = spectra_render_line(x, y, 3, 32, 24, &w, &h);
    if (img)
        spectra_free_pixels(img);

    spectra_embed_set_theme(s.get(), "light");
    uint8_t buf[160 * 120 * 4];
    EXPECT_EQ(spectra_embed_render(s.get(), buf), 1);
}
