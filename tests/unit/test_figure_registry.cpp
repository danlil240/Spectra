#include <gtest/gtest.h>

#include <memory>
#include <spectra/app.hpp>
#include <spectra/figure.hpp>
#include <spectra/fwd.hpp>
#include <vector>

#include "multi_window_fixture.hpp"
#include "render/backend.hpp"

using namespace spectra;
using namespace spectra::test;

// ═══════════════════════════════════════════════════════════════════════════════
// FigureRegistry Test Scaffolding
//
// Agent C will create FigureRegistry (src/ui/figure_registry.hpp/.cpp).
// These tests are structured and ready to be filled in once the type exists.
// Until then, the Phase 3+ tests are behind SPECTRA_HAS_FIGURE_REGISTRY guard
// and the Phase 0 tests validate current figure management behavior.
// ═══════════════════════════════════════════════════════════════════════════════

// ─── Phase 0: Current Figure Behavior (always runs) ─────────────────────────
// Validates the current vector<unique_ptr<Figure>> ownership model so we have
// a regression baseline before Agent C refactors it.

TEST(FigureBaseline, CreateSingleFigure)
{
    App app({.headless = true});
    auto& fig = app.figure({.width = 640, .height = 480});
    EXPECT_EQ(fig.width(), 640u);
    EXPECT_EQ(fig.height(), 480u);
}

TEST(FigureBaseline, CreateMultipleFigures)
{
    App app({.headless = true});
    auto& fig1 = app.figure({.width = 320, .height = 240});
    auto& fig2 = app.figure({.width = 640, .height = 480});
    auto& fig3 = app.figure({.width = 800, .height = 600});

    EXPECT_EQ(fig1.width(), 320u);
    EXPECT_EQ(fig2.width(), 640u);
    EXPECT_EQ(fig3.width(), 800u);
}

TEST(FigureBaseline, FigureOwnsAxes)
{
    App app({.headless = true});
    auto& fig = app.figure({.width = 640, .height = 480});
    auto& ax = fig.subplot(1, 1, 1);

    std::vector<float> x = {0.0f, 1.0f, 2.0f};
    std::vector<float> y = {0.0f, 1.0f, 0.5f};
    ax.line(x, y).label("test");

    EXPECT_EQ(fig.axes().size(), 1u);
}

TEST(FigureBaseline, FigureSubplotGrid)
{
    App app({.headless = true});
    auto& fig = app.figure({.width = 800, .height = 600});
    fig.subplot(2, 2, 1);
    fig.subplot(2, 2, 2);
    fig.subplot(2, 2, 3);
    fig.subplot(2, 2, 4);

    EXPECT_EQ(fig.grid_rows(), 2);
    EXPECT_EQ(fig.grid_cols(), 2);
}

TEST(FigureBaseline, FigureRenderAndReadback)
{
    App app({.headless = true});
    auto& fig = app.figure({.width = 320, .height = 240});
    auto& ax = fig.subplot(1, 1, 1);
    std::vector<float> x = {0.0f, 1.0f, 2.0f, 3.0f};
    std::vector<float> y = {0.0f, 1.0f, 0.5f, 1.5f};
    ax.line(x, y);
    ax.xlim(0.0f, 3.0f);
    ax.ylim(-0.5f, 2.0f);

    app.run();

    auto* backend = app.backend();
    ASSERT_NE(backend, nullptr);

    std::vector<uint8_t> pixels(320 * 240 * 4);
    EXPECT_TRUE(backend->readback_framebuffer(pixels.data(), 320, 240));

    // Verify something was rendered
    bool has_content = false;
    for (auto p : pixels)
    {
        if (p != 0)
        {
            has_content = true;
            break;
        }
    }
    EXPECT_TRUE(has_content);
}

TEST(FigureBaseline, SeriesGpuDataKeyedByPointer)
{
    // Verify that series GPU data is keyed by Series* pointer.
    // This is important because Agent C must preserve this invariant
    // when moving figures between windows.
    App app({.headless = true});
    auto& fig = app.figure({.width = 320, .height = 240});
    auto& ax = fig.subplot(1, 1, 1);
    std::vector<float> x = {0.0f, 1.0f, 2.0f};
    std::vector<float> y = {0.0f, 1.0f, 0.5f};
    auto& series = ax.line(x, y);

    // Get the series pointer address
    const Series* series_ptr = &series;
    EXPECT_NE(series_ptr, nullptr);

    // Render to upload GPU data
    app.run();

    // The series pointer should still be valid after render
    EXPECT_EQ(&series, series_ptr);
}

TEST(FigureBaseline, MultipleAppsSequential)
{
    // Creating and destroying multiple Apps in sequence should not crash.
    // This validates that resource cleanup is correct — important baseline
    // for multi-window where windows are created/destroyed at runtime.
    for (int i = 0; i < 5; ++i)
    {
        App app({.headless = true});
        auto& fig = app.figure({.width = 320, .height = 240});
        auto& ax = fig.subplot(1, 1, 1);
        std::vector<float> x = {0.0f, 1.0f};
        std::vector<float> y = {0.0f, static_cast<float>(i)};
        ax.line(x, y);
        app.run();
    }
}

// ─── Phase 3: FigureRegistry (after Agent C merge) ──────────────────────────

#ifdef SPECTRA_HAS_FIGURE_REGISTRY

// ─── FigureRegistryConstruction ─────────────────────────────────────────────

TEST(FigureRegistryConstruction, DefaultEmpty)
{
    // FigureRegistry reg;
    // EXPECT_EQ(reg.all_ids().size(), 0u);
    GTEST_SKIP() << "FigureRegistry not yet implemented (Agent C)";
}

TEST(FigureRegistryConstruction, RegisterReturnsStableId)
{
    // FigureRegistry reg;
    // auto id1 = reg.register_figure(std::make_unique<Figure>());
    // auto id2 = reg.register_figure(std::make_unique<Figure>());
    // EXPECT_NE(id1, id2);
    // EXPECT_NE(id1, 0u);
    // EXPECT_NE(id2, 0u);
    GTEST_SKIP() << "FigureRegistry not yet implemented (Agent C)";
}

TEST(FigureRegistryConstruction, IdsAreMonotonic)
{
    // FigureRegistry reg;
    // auto id1 = reg.register_figure(std::make_unique<Figure>());
    // auto id2 = reg.register_figure(std::make_unique<Figure>());
    // auto id3 = reg.register_figure(std::make_unique<Figure>());
    // EXPECT_LT(id1, id2);
    // EXPECT_LT(id2, id3);
    GTEST_SKIP() << "FigureRegistry not yet implemented (Agent C)";
}

// ─── FigureRegistryLookup ───────────────────────────────────────────────────

TEST(FigureRegistryLookup, GetValidId)
{
    // FigureRegistry reg;
    // auto id = reg.register_figure(std::make_unique<Figure>());
    // Figure* fig = reg.get(id);
    // EXPECT_NE(fig, nullptr);
    GTEST_SKIP() << "FigureRegistry not yet implemented (Agent C)";
}

TEST(FigureRegistryLookup, GetInvalidIdReturnsNull)
{
    // FigureRegistry reg;
    // EXPECT_EQ(reg.get(999), nullptr);
    GTEST_SKIP() << "FigureRegistry not yet implemented (Agent C)";
}

TEST(FigureRegistryLookup, GetAfterUnregister)
{
    // FigureRegistry reg;
    // auto id = reg.register_figure(std::make_unique<Figure>());
    // reg.unregister_figure(id);
    // EXPECT_EQ(reg.get(id), nullptr);
    GTEST_SKIP() << "FigureRegistry not yet implemented (Agent C)";
}

TEST(FigureRegistryLookup, AllIdsReturnsRegistered)
{
    // FigureRegistry reg;
    // auto id1 = reg.register_figure(std::make_unique<Figure>());
    // auto id2 = reg.register_figure(std::make_unique<Figure>());
    // auto ids = reg.all_ids();
    // EXPECT_EQ(ids.size(), 2u);
    GTEST_SKIP() << "FigureRegistry not yet implemented (Agent C)";
}

// ─── FigureRegistryLifecycle ────────────────────────────────────────────────

TEST(FigureRegistryLifecycle, UnregisterReducesCount)
{
    // FigureRegistry reg;
    // auto id1 = reg.register_figure(std::make_unique<Figure>());
    // auto id2 = reg.register_figure(std::make_unique<Figure>());
    // EXPECT_EQ(reg.all_ids().size(), 2u);
    // reg.unregister_figure(id1);
    // EXPECT_EQ(reg.all_ids().size(), 1u);
    GTEST_SKIP() << "FigureRegistry not yet implemented (Agent C)";
}

TEST(FigureRegistryLifecycle, UnregisterInvalidIdNoOp)
{
    // FigureRegistry reg;
    // reg.unregister_figure(999);  // Should not crash
    GTEST_SKIP() << "FigureRegistry not yet implemented (Agent C)";
}

TEST(FigureRegistryLifecycle, IdNotReusedAfterUnregister)
{
    // FigureRegistry reg;
    // auto id1 = reg.register_figure(std::make_unique<Figure>());
    // reg.unregister_figure(id1);
    // auto id2 = reg.register_figure(std::make_unique<Figure>());
    // EXPECT_NE(id1, id2);  // IDs are never reused
    GTEST_SKIP() << "FigureRegistry not yet implemented (Agent C)";
}

TEST(FigureRegistryLifecycle, PointerStableAcrossRegistrations)
{
    // FigureRegistry reg;
    // auto id1 = reg.register_figure(std::make_unique<Figure>());
    // Figure* ptr1 = reg.get(id1);
    // auto id2 = reg.register_figure(std::make_unique<Figure>());
    // EXPECT_EQ(reg.get(id1), ptr1);  // Adding id2 doesn't invalidate id1's pointer
    GTEST_SKIP() << "FigureRegistry not yet implemented (Agent C)";
}

// ─── FigureRegistryGpuIntegration ───────────────────────────────────────────

TEST(FigureRegistryGpu, GpuBuffersSurviveRegistration)
{
    // Register a figure, render it, verify GPU data exists,
    // then verify the GPU data is still valid after more registrations.
    GTEST_SKIP() << "FigureRegistry not yet implemented (Agent C)";
}

TEST(FigureRegistryGpu, GpuBuffersSurviveUnregister)
{
    // Register two figures, render both, unregister one,
    // verify the other's GPU data is still valid.
    GTEST_SKIP() << "FigureRegistry not yet implemented (Agent C)";
}

TEST(FigureRegistryGpu, GpuBuffersCleanedOnUnregister)
{
    // Register a figure, render it, unregister it,
    // verify GPU resources are properly cleaned up (no leaks).
    GTEST_SKIP() << "FigureRegistry not yet implemented (Agent C)";
}

// ─── FigureRegistryMove ─────────────────────────────────────────────────────

TEST(FigureRegistryMove, MoveFigureBetweenWindows)
{
    // Create figure in window A, move to window B, verify renders in B.
    GTEST_SKIP() << "FigureRegistry + WindowManager not yet implemented (Agent C)";
}

TEST(FigureRegistryMove, GpuDataPreservedAfterMove)
{
    // Series GPU data (keyed by Series*) must survive move.
    GTEST_SKIP() << "FigureRegistry + WindowManager not yet implemented (Agent C)";
}

TEST(FigureRegistryMove, SourceWindowUnaffectedAfterMove)
{
    // Moving figure out of window A should not affect other figures in A.
    GTEST_SKIP() << "FigureRegistry + WindowManager not yet implemented (Agent C)";
}

#endif  // SPECTRA_HAS_FIGURE_REGISTRY
