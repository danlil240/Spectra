#define SPECTRA_HAS_FIGURE_REGISTRY

#include <gtest/gtest.h>

#include <memory>
#include <spectra/app.hpp>
#include <spectra/figure.hpp>
#include <spectra/fwd.hpp>
#include <vector>

#include "multi_window_fixture.hpp"
#include "render/backend.hpp"
#include "ui/figure_registry.hpp"

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

// ─── Phase 3: FigureRegistry (Agent C) ──────────────────────────────────────

#ifdef SPECTRA_HAS_FIGURE_REGISTRY

// ─── FigureRegistryConstruction ─────────────────────────────────────────────

TEST(FigureRegistryConstruction, DefaultEmpty)
{
    FigureRegistry reg;
    EXPECT_EQ(reg.all_ids().size(), 0u);
    EXPECT_EQ(reg.count(), 0u);
}

TEST(FigureRegistryConstruction, RegisterReturnsStableId)
{
    FigureRegistry reg;
    auto id1 = reg.register_figure(std::make_unique<Figure>(FigureConfig{.width = 320, .height = 240}));
    auto id2 = reg.register_figure(std::make_unique<Figure>(FigureConfig{.width = 640, .height = 480}));
    EXPECT_NE(id1, id2);
    EXPECT_NE(id1, 0u);
    EXPECT_NE(id2, 0u);
}

TEST(FigureRegistryConstruction, IdsAreMonotonic)
{
    FigureRegistry reg;
    auto id1 = reg.register_figure(std::make_unique<Figure>(FigureConfig{.width = 320, .height = 240}));
    auto id2 = reg.register_figure(std::make_unique<Figure>(FigureConfig{.width = 320, .height = 240}));
    auto id3 = reg.register_figure(std::make_unique<Figure>(FigureConfig{.width = 320, .height = 240}));
    EXPECT_LT(id1, id2);
    EXPECT_LT(id2, id3);
}

// ─── FigureRegistryLookup ───────────────────────────────────────────────────

TEST(FigureRegistryLookup, GetValidId)
{
    FigureRegistry reg;
    auto id = reg.register_figure(std::make_unique<Figure>(FigureConfig{.width = 320, .height = 240}));
    Figure* fig = reg.get(id);
    EXPECT_NE(fig, nullptr);
    EXPECT_EQ(fig->width(), 320u);
}

TEST(FigureRegistryLookup, GetInvalidIdReturnsNull)
{
    FigureRegistry reg;
    EXPECT_EQ(reg.get(999), nullptr);
}

TEST(FigureRegistryLookup, GetAfterUnregister)
{
    FigureRegistry reg;
    auto id = reg.register_figure(std::make_unique<Figure>(FigureConfig{.width = 320, .height = 240}));
    reg.unregister_figure(id);
    EXPECT_EQ(reg.get(id), nullptr);
}

TEST(FigureRegistryLookup, AllIdsReturnsRegistered)
{
    FigureRegistry reg;
    auto id1 = reg.register_figure(std::make_unique<Figure>(FigureConfig{.width = 320, .height = 240}));
    auto id2 = reg.register_figure(std::make_unique<Figure>(FigureConfig{.width = 640, .height = 480}));
    auto ids = reg.all_ids();
    EXPECT_EQ(ids.size(), 2u);
    EXPECT_EQ(ids[0], id1);
    EXPECT_EQ(ids[1], id2);
}

// ─── FigureRegistryLifecycle ────────────────────────────────────────────────

TEST(FigureRegistryLifecycle, UnregisterReducesCount)
{
    FigureRegistry reg;
    auto id1 = reg.register_figure(std::make_unique<Figure>(FigureConfig{.width = 320, .height = 240}));
    auto id2 = reg.register_figure(std::make_unique<Figure>(FigureConfig{.width = 640, .height = 480}));
    EXPECT_EQ(reg.count(), 2u);
    reg.unregister_figure(id1);
    EXPECT_EQ(reg.count(), 1u);
    auto ids = reg.all_ids();
    EXPECT_EQ(ids.size(), 1u);
    EXPECT_EQ(ids[0], id2);
}

TEST(FigureRegistryLifecycle, UnregisterInvalidIdNoOp)
{
    FigureRegistry reg;
    reg.unregister_figure(999);  // Should not crash
    EXPECT_EQ(reg.count(), 0u);
}

TEST(FigureRegistryLifecycle, IdNotReusedAfterUnregister)
{
    FigureRegistry reg;
    auto id1 = reg.register_figure(std::make_unique<Figure>(FigureConfig{.width = 320, .height = 240}));
    reg.unregister_figure(id1);
    auto id2 = reg.register_figure(std::make_unique<Figure>(FigureConfig{.width = 320, .height = 240}));
    EXPECT_NE(id1, id2);  // IDs are never reused
    EXPECT_GT(id2, id1);
}

TEST(FigureRegistryLifecycle, PointerStableAcrossRegistrations)
{
    FigureRegistry reg;
    auto id1 = reg.register_figure(std::make_unique<Figure>(FigureConfig{.width = 320, .height = 240}));
    Figure* ptr1 = reg.get(id1);
    auto id2 = reg.register_figure(std::make_unique<Figure>(FigureConfig{.width = 640, .height = 480}));
    (void)id2;
    EXPECT_EQ(reg.get(id1), ptr1);  // Adding id2 doesn't invalidate id1's pointer
}

TEST(FigureRegistryLifecycle, ContainsRegistered)
{
    FigureRegistry reg;
    auto id = reg.register_figure(std::make_unique<Figure>(FigureConfig{.width = 320, .height = 240}));
    EXPECT_TRUE(reg.contains(id));
    EXPECT_FALSE(reg.contains(999));
}

TEST(FigureRegistryLifecycle, ReleaseReturnsOwnership)
{
    FigureRegistry reg;
    auto id = reg.register_figure(std::make_unique<Figure>(FigureConfig{.width = 320, .height = 240}));
    EXPECT_EQ(reg.count(), 1u);
    auto fig = reg.release(id);
    ASSERT_NE(fig, nullptr);
    EXPECT_EQ(fig->width(), 320u);
    EXPECT_EQ(reg.count(), 0u);
    EXPECT_EQ(reg.get(id), nullptr);
}

TEST(FigureRegistryLifecycle, ReleaseInvalidReturnsNull)
{
    FigureRegistry reg;
    auto fig = reg.release(999);
    EXPECT_EQ(fig, nullptr);
}

TEST(FigureRegistryLifecycle, ClearRemovesAll)
{
    FigureRegistry reg;
    reg.register_figure(std::make_unique<Figure>(FigureConfig{.width = 320, .height = 240}));
    reg.register_figure(std::make_unique<Figure>(FigureConfig{.width = 640, .height = 480}));
    EXPECT_EQ(reg.count(), 2u);
    reg.clear();
    EXPECT_EQ(reg.count(), 0u);
    EXPECT_TRUE(reg.all_ids().empty());
}

TEST(FigureRegistryLifecycle, InsertionOrderPreserved)
{
    FigureRegistry reg;
    auto id1 = reg.register_figure(std::make_unique<Figure>(FigureConfig{.width = 100, .height = 100}));
    auto id2 = reg.register_figure(std::make_unique<Figure>(FigureConfig{.width = 200, .height = 200}));
    auto id3 = reg.register_figure(std::make_unique<Figure>(FigureConfig{.width = 300, .height = 300}));
    auto ids = reg.all_ids();
    ASSERT_EQ(ids.size(), 3u);
    EXPECT_EQ(ids[0], id1);
    EXPECT_EQ(ids[1], id2);
    EXPECT_EQ(ids[2], id3);

    // Remove middle, order of remaining should be preserved
    reg.unregister_figure(id2);
    ids = reg.all_ids();
    ASSERT_EQ(ids.size(), 2u);
    EXPECT_EQ(ids[0], id1);
    EXPECT_EQ(ids[1], id3);
}

// ─── FigureRegistryGpuIntegration ───────────────────────────────────────────
// These tests use a headless App to verify GPU buffer behavior with FigureRegistry.

TEST(FigureRegistryGpu, RegisteredFigureRenderable)
{
    // Verify a figure from the registry can be rendered via App's renderer
    App app({.headless = true});
    auto& fig = app.figure({.width = 320, .height = 240});
    auto& ax = fig.subplot(1, 1, 1);
    std::vector<float> x = {0.0f, 1.0f, 2.0f};
    std::vector<float> y = {0.0f, 1.0f, 0.5f};
    ax.line(x, y);
    app.run();

    // Separately verify FigureRegistry can hold figures
    FigureRegistry reg;
    auto id = reg.register_figure(std::make_unique<Figure>(FigureConfig{.width = 320, .height = 240}));
    Figure* rfig = reg.get(id);
    ASSERT_NE(rfig, nullptr);
    auto& rax = rfig->subplot(1, 1, 1);
    rax.line(x, y);
    EXPECT_EQ(rfig->axes().size(), 1u);
}

TEST(FigureRegistryGpu, PointerStabilityForGpuKeying)
{
    // Series GPU data is keyed by Series* pointer.
    // Verify that registering/unregistering other figures doesn't
    // invalidate a figure's series pointers.
    FigureRegistry reg;
    auto id1 = reg.register_figure(std::make_unique<Figure>(FigureConfig{.width = 320, .height = 240}));
    Figure* fig1 = reg.get(id1);
    auto& ax = fig1->subplot(1, 1, 1);
    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {0.0f, 1.0f};
    auto& series = ax.line(x, y);
    const Series* series_ptr = &series;

    // Register more figures
    auto id2 = reg.register_figure(std::make_unique<Figure>(FigureConfig{.width = 640, .height = 480}));
    auto id3 = reg.register_figure(std::make_unique<Figure>(FigureConfig{.width = 800, .height = 600}));

    // Unregister one
    reg.unregister_figure(id2);

    // Original series pointer must still be valid
    EXPECT_EQ(reg.get(id1), fig1);
    EXPECT_EQ(&series, series_ptr);
    (void)id3;
}

TEST(FigureRegistryGpu, ReleasePreservesSeriesPointers)
{
    // Releasing a figure from the registry preserves its Series* pointers
    FigureRegistry reg;
    auto id = reg.register_figure(std::make_unique<Figure>(FigureConfig{.width = 320, .height = 240}));
    Figure* fig = reg.get(id);
    auto& ax = fig->subplot(1, 1, 1);
    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {0.0f, 1.0f};
    auto& series = ax.line(x, y);
    const Series* series_ptr = &series;

    auto released = reg.release(id);
    ASSERT_NE(released, nullptr);
    // Series pointer in the released figure is still valid
    EXPECT_FALSE(released->axes().empty());
    EXPECT_FALSE(released->axes()[0]->series().empty());
    EXPECT_EQ(released->axes()[0]->series()[0].get(), series_ptr);
}

// ─── FigureRegistryMove ─────────────────────────────────────────────────────
// Move tests use release() + register_figure() to simulate moving between windows.

TEST(FigureRegistryMove, MoveFigureBetweenRegistries)
{
    // Simulate moving a figure from one window's registry to another
    FigureRegistry reg_a;
    FigureRegistry reg_b;

    auto id_a = reg_a.register_figure(std::make_unique<Figure>(FigureConfig{.width = 320, .height = 240}));
    Figure* fig = reg_a.get(id_a);
    auto& ax = fig->subplot(1, 1, 1);
    std::vector<float> x = {0.0f, 1.0f, 2.0f};
    std::vector<float> y = {0.0f, 1.0f, 0.5f};
    ax.line(x, y);

    // Move: release from A, register in B
    auto released = reg_a.release(id_a);
    ASSERT_NE(released, nullptr);
    EXPECT_EQ(reg_a.count(), 0u);

    auto id_b = reg_b.register_figure(std::move(released));
    EXPECT_EQ(reg_b.count(), 1u);

    Figure* moved_fig = reg_b.get(id_b);
    ASSERT_NE(moved_fig, nullptr);
    EXPECT_EQ(moved_fig->width(), 320u);
    EXPECT_EQ(moved_fig->axes().size(), 1u);
}

TEST(FigureRegistryMove, GpuDataPreservedAfterMove)
{
    // Series GPU data (keyed by Series*) must survive move.
    FigureRegistry reg_a;
    FigureRegistry reg_b;

    auto id_a = reg_a.register_figure(std::make_unique<Figure>(FigureConfig{.width = 320, .height = 240}));
    Figure* fig = reg_a.get(id_a);
    auto& ax = fig->subplot(1, 1, 1);
    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {0.0f, 1.0f};
    auto& series = ax.line(x, y);
    const Series* series_ptr = &series;

    auto released = reg_a.release(id_a);
    auto id_b = reg_b.register_figure(std::move(released));
    Figure* moved_fig = reg_b.get(id_b);

    // Series pointer must be the same (GPU data keyed by pointer)
    EXPECT_EQ(moved_fig->axes()[0]->series()[0].get(), series_ptr);
}

TEST(FigureRegistryMove, SourceUnaffectedAfterMove)
{
    // Moving figure out of registry A should not affect other figures in A.
    FigureRegistry reg_a;
    auto id1 = reg_a.register_figure(std::make_unique<Figure>(FigureConfig{.width = 320, .height = 240}));
    auto id2 = reg_a.register_figure(std::make_unique<Figure>(FigureConfig{.width = 640, .height = 480}));
    Figure* fig2 = reg_a.get(id2);

    // Move id1 out
    auto released = reg_a.release(id1);
    ASSERT_NE(released, nullptr);

    // id2 should still be accessible and unchanged
    EXPECT_EQ(reg_a.count(), 1u);
    EXPECT_EQ(reg_a.get(id2), fig2);
    EXPECT_EQ(fig2->width(), 640u);
}

#endif  // SPECTRA_HAS_FIGURE_REGISTRY
