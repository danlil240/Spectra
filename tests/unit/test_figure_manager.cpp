#include <gtest/gtest.h>
#include <memory>
#include <spectra/figure.hpp>
#include <string>
#include <vector>

#include "ui/figure_manager.hpp"
#include "ui/figure_registry.hpp"
#include "ui/tab_bar.hpp"

namespace spectra
{

// ─── Test Fixture ─────────────────────────────────────────────────────────────

class FigureManagerTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        registry_.clear();
        first_id_ = registry_.register_figure(std::make_unique<Figure>());
    }

    FigureRegistry registry_;
    FigureId       first_id_ = INVALID_FIGURE_ID;
};

// ─── Construction ─────────────────────────────────────────────────────────────

TEST_F(FigureManagerTest, ConstructWithSingleFigure)
{
    FigureManager mgr(registry_);
    EXPECT_EQ(mgr.count(), 1u);
    EXPECT_EQ(mgr.active_index(), first_id_);
    EXPECT_NE(mgr.active_figure(), nullptr);
    EXPECT_EQ(mgr.active_figure(), registry_.get(first_id_));
}

TEST_F(FigureManagerTest, ConstructWithMultipleFigures)
{
    registry_.register_figure(std::make_unique<Figure>());
    registry_.register_figure(std::make_unique<Figure>());
    FigureManager mgr(registry_);
    EXPECT_EQ(mgr.count(), 3u);
    EXPECT_EQ(mgr.active_index(), first_id_);
}

// ─── Create Figure ────────────────────────────────────────────────────────────

TEST_F(FigureManagerTest, CreateFigure)
{
    FigureManager mgr(registry_);
    FigureId      idx = mgr.create_figure();
    EXPECT_NE(idx, INVALID_FIGURE_ID);
    EXPECT_EQ(mgr.count(), 2u);
    EXPECT_EQ(mgr.active_index(), idx);   // Auto-switches to new figure
    EXPECT_EQ(registry_.count(), 2u);
}

TEST_F(FigureManagerTest, CreateFigureWithConfig)
{
    FigureManager mgr(registry_);
    FigureConfig  cfg;
    cfg.width    = 1920;
    cfg.height   = 1080;
    FigureId idx = mgr.create_figure(cfg);
    EXPECT_NE(idx, INVALID_FIGURE_ID);
    Figure* fig = registry_.get(idx);
    ASSERT_NE(fig, nullptr);
    EXPECT_EQ(fig->width(), 1920u);
    EXPECT_EQ(fig->height(), 1080u);
}

TEST_F(FigureManagerTest, CreateMultipleFigures)
{
    FigureManager mgr(registry_);
    FigureId      id1 = mgr.create_figure();
    FigureId      id2 = mgr.create_figure();
    FigureId      id3 = mgr.create_figure();
    EXPECT_EQ(mgr.count(), 4u);
    EXPECT_EQ(mgr.active_index(), id3);
    (void)id1;
    (void)id2;
}

// ─── Close Figure ─────────────────────────────────────────────────────────────

TEST_F(FigureManagerTest, CloseFigure)
{
    FigureManager mgr(registry_);
    FigureId      id1 = mgr.create_figure();
    mgr.create_figure();
    EXPECT_EQ(mgr.count(), 3u);

    bool closed = mgr.close_figure(id1);
    EXPECT_TRUE(closed);
    EXPECT_EQ(mgr.count(), 2u);
}

TEST_F(FigureManagerTest, CannotCloseLastFigure)
{
    FigureManager mgr(registry_);
    bool          closed = mgr.close_figure(first_id_);
    EXPECT_FALSE(closed);
    EXPECT_EQ(mgr.count(), 1u);
}

TEST_F(FigureManagerTest, CloseOutOfBounds)
{
    FigureManager mgr(registry_);
    bool          closed = mgr.close_figure(99);
    EXPECT_FALSE(closed);
}

TEST_F(FigureManagerTest, CloseActiveFigureAdjustsIndex)
{
    FigureManager mgr(registry_);
    mgr.create_figure();
    FigureId id2 = mgr.create_figure();
    mgr.switch_to(id2);
    EXPECT_EQ(mgr.active_index(), id2);

    mgr.close_figure(id2);   // Close active (last positionally)
    // Should move to nearest remaining figure
    EXPECT_NE(mgr.active_index(), id2);
    EXPECT_EQ(mgr.count(), 2u);
}

TEST_F(FigureManagerTest, CloseBeforeActiveAdjustsIndex)
{
    FigureManager mgr(registry_);
    mgr.create_figure();
    FigureId id2 = mgr.create_figure();
    mgr.switch_to(id2);

    mgr.close_figure(first_id_);          // Close first figure
    EXPECT_EQ(mgr.active_index(), id2);   // Active unchanged (different ID)
    EXPECT_EQ(mgr.count(), 2u);
}

// ─── Close All Except ─────────────────────────────────────────────────────────

TEST_F(FigureManagerTest, CloseAllExcept)
{
    FigureManager mgr(registry_);
    FigureId      id1 = mgr.create_figure();
    mgr.create_figure();
    mgr.create_figure();
    EXPECT_EQ(mgr.count(), 4u);

    bool result = mgr.close_all_except(id1);
    EXPECT_TRUE(result);
    EXPECT_EQ(mgr.count(), 1u);
    EXPECT_EQ(mgr.active_index(), id1);
}

TEST_F(FigureManagerTest, CloseAllExceptOutOfBounds)
{
    FigureManager mgr(registry_);
    bool          result = mgr.close_all_except(99);
    EXPECT_FALSE(result);
}

// ─── Close To Right ───────────────────────────────────────────────────────────

TEST_F(FigureManagerTest, CloseToRight)
{
    FigureManager mgr(registry_);
    FigureId      id1 = mgr.create_figure();
    mgr.create_figure();
    mgr.create_figure();
    EXPECT_EQ(mgr.count(), 4u);

    bool result = mgr.close_to_right(id1);
    EXPECT_TRUE(result);
    EXPECT_EQ(mgr.count(), 2u);   // first_id_ + id1
}

TEST_F(FigureManagerTest, CloseToRightLastTab)
{
    FigureManager mgr(registry_);
    FigureId      id1    = mgr.create_figure();
    bool          result = mgr.close_to_right(id1);
    EXPECT_FALSE(result);   // Nothing to close (id1 is last positionally)
}

TEST_F(FigureManagerTest, CloseToRightAdjustsActiveIndex)
{
    FigureManager mgr(registry_);
    FigureId      id1 = mgr.create_figure();
    mgr.create_figure();
    FigureId id3 = mgr.create_figure();
    mgr.switch_to(id3);   // Active = last

    mgr.close_to_right(id1);   // Close everything after id1
    EXPECT_EQ(mgr.count(), 2u);
    EXPECT_EQ(mgr.active_index(), id1);   // Clamped to id1
}

// ─── Duplicate Figure ─────────────────────────────────────────────────────────

TEST_F(FigureManagerTest, DuplicateFigure)
{
    FigureManager mgr(registry_);
    FigureId      dup_idx = mgr.duplicate_figure(first_id_);
    EXPECT_NE(dup_idx, INVALID_FIGURE_ID);
    EXPECT_EQ(mgr.count(), 2u);
    EXPECT_EQ(mgr.active_index(), dup_idx);   // Switches to duplicate
}

TEST_F(FigureManagerTest, DuplicatePreservesDimensions)
{
    FigureConfig cfg;
    cfg.width  = 1920;
    cfg.height = 1080;
    registry_.clear();
    FigureId src_id = registry_.register_figure(std::make_unique<Figure>(cfg));

    FigureManager mgr(registry_);
    FigureId      dup_id = mgr.duplicate_figure(src_id);
    Figure*       dup    = registry_.get(dup_id);
    ASSERT_NE(dup, nullptr);
    EXPECT_EQ(dup->width(), 1920u);
    EXPECT_EQ(dup->height(), 1080u);
}

TEST_F(FigureManagerTest, DuplicateOutOfBounds)
{
    FigureManager mgr(registry_);
    FigureId      idx = mgr.duplicate_figure(99);
    EXPECT_EQ(idx, INVALID_FIGURE_ID);
    EXPECT_EQ(mgr.count(), 1u);
}

TEST_F(FigureManagerTest, DuplicateTitleUsesNextAvailableName)
{
    FigureManager mgr(registry_);
    mgr.set_title(first_id_, "My Plot");
    FigureId    dup_id    = mgr.duplicate_figure(first_id_);
    std::string dup_title = mgr.get_title(dup_id);
    // Duplicate gets next available "Figure N" name, not a copy suffix
    EXPECT_EQ(dup_title, "Figure 2");
}

// ─── Switch ───────────────────────────────────────────────────────────────────

TEST_F(FigureManagerTest, SwitchTo)
{
    FigureManager mgr(registry_);
    mgr.create_figure();
    mgr.create_figure();
    mgr.switch_to(first_id_);
    EXPECT_EQ(mgr.active_index(), first_id_);
    EXPECT_EQ(mgr.active_figure(), registry_.get(first_id_));
}

TEST_F(FigureManagerTest, SwitchToSameIsNoop)
{
    FigureManager mgr(registry_);
    FigureId      id1 = mgr.create_figure();
    mgr.switch_to(id1);
    EXPECT_EQ(mgr.active_index(), id1);
    mgr.switch_to(id1);   // Same index
    EXPECT_EQ(mgr.active_index(), id1);
}

TEST_F(FigureManagerTest, SwitchToOutOfBounds)
{
    FigureManager mgr(registry_);
    mgr.switch_to(99);
    EXPECT_EQ(mgr.active_index(), first_id_);   // Unchanged
}

TEST_F(FigureManagerTest, SwitchToNext)
{
    FigureManager mgr(registry_);
    FigureId      id1 = mgr.create_figure();
    FigureId      id2 = mgr.create_figure();
    mgr.switch_to(first_id_);

    mgr.switch_to_next();
    EXPECT_EQ(mgr.active_index(), id1);
    mgr.switch_to_next();
    EXPECT_EQ(mgr.active_index(), id2);
    mgr.switch_to_next();
    EXPECT_EQ(mgr.active_index(), first_id_);   // Wraps around
}

TEST_F(FigureManagerTest, SwitchToPrevious)
{
    FigureManager mgr(registry_);
    mgr.create_figure();
    FigureId id2 = mgr.create_figure();
    mgr.switch_to(first_id_);

    mgr.switch_to_previous();
    EXPECT_EQ(mgr.active_index(), id2);   // Wraps around
}

TEST_F(FigureManagerTest, SwitchNextSingleFigureNoop)
{
    FigureManager mgr(registry_);
    mgr.switch_to_next();
    EXPECT_EQ(mgr.active_index(), first_id_);
}

// ─── Move Tab ─────────────────────────────────────────────────────────────────

TEST_F(FigureManagerTest, MoveTab)
{
    FigureManager mgr(registry_);
    FigureId      id1 = mgr.create_figure();
    FigureId      id2 = mgr.create_figure();
    mgr.set_title(first_id_, "A");
    mgr.set_title(id1, "B");
    mgr.set_title(id2, "C");

    mgr.switch_to(first_id_);
    // Move first_id_ from pos 0 to pos 2 (where id2 is)
    mgr.move_tab(first_id_, id2);
    // After move: ordered should be [id1, id2, first_id_]
    // Titles are keyed by ID, so get_title still works by ID
    EXPECT_EQ(mgr.get_title(first_id_), "A");
    EXPECT_EQ(mgr.get_title(id1), "B");
    EXPECT_EQ(mgr.get_title(id2), "C");
    EXPECT_EQ(mgr.count(), 3u);
}

TEST_F(FigureManagerTest, MoveTabSameIndex)
{
    FigureManager mgr(registry_);
    mgr.create_figure();
    mgr.move_tab(first_id_, first_id_);   // Noop
    EXPECT_EQ(mgr.count(), 2u);
}

// ─── Title Management ─────────────────────────────────────────────────────────

TEST_F(FigureManagerTest, DefaultTitle)
{
    // default_title uses the FigureId directly (1-based IDs from registry)
    EXPECT_EQ(FigureManager::default_title(1), "Figure 1");
    EXPECT_EQ(FigureManager::default_title(5), "Figure 5");
}

TEST_F(FigureManagerTest, GetSetTitle)
{
    FigureManager mgr(registry_);
    mgr.set_title(first_id_, "My Custom Plot");
    EXPECT_EQ(mgr.get_title(first_id_), "My Custom Plot");
}

TEST_F(FigureManagerTest, GetTitleOutOfBounds)
{
    FigureManager mgr(registry_);
    std::string   title = mgr.get_title(99);
    // Should return a default title, not crash
    EXPECT_FALSE(title.empty());
}

// ─── Modified State ───────────────────────────────────────────────────────────

TEST_F(FigureManagerTest, ModifiedState)
{
    FigureManager mgr(registry_);
    EXPECT_FALSE(mgr.is_modified(first_id_));
    mgr.mark_modified(first_id_, true);
    EXPECT_TRUE(mgr.is_modified(first_id_));
    mgr.mark_modified(first_id_, false);
    EXPECT_FALSE(mgr.is_modified(first_id_));
}

TEST_F(FigureManagerTest, ModifiedOutOfBounds)
{
    FigureManager mgr(registry_);
    EXPECT_FALSE(mgr.is_modified(99));
}

// ─── Per-Figure State ─────────────────────────────────────────────────────────

TEST_F(FigureManagerTest, StateAccessor)
{
    FigureManager mgr(registry_);
    auto&         st         = mgr.state(first_id_);
    st.selected_series_index = 3;
    EXPECT_EQ(mgr.state(first_id_).selected_series_index, 3);
}

TEST_F(FigureManagerTest, ActiveState)
{
    FigureManager mgr(registry_);
    mgr.active_state().inspector_scroll_y = 42.0f;
    EXPECT_FLOAT_EQ(mgr.state(first_id_).inspector_scroll_y, 42.0f);
}

TEST_F(FigureManagerTest, SaveRestoreAxisState)
{
    // Create a figure with axes
    registry_.clear();
    auto  fig = std::make_unique<Figure>();
    auto& ax  = fig->subplot(1, 1, 1);
    ax.xlim(10.0f, 20.0f);
    ax.ylim(30.0f, 40.0f);
    FigureId fig_id = registry_.register_figure(std::move(fig));

    FigureManager mgr(registry_);
    mgr.create_figure();   // Creates new fig, switches to it

    // The save should have captured fig_id's axis state
    const auto& st = mgr.state(fig_id);
    ASSERT_EQ(st.axes_snapshots.size(), 1u);
    EXPECT_FLOAT_EQ(st.axes_snapshots[0].x_limits.min, 10.0f);
    EXPECT_FLOAT_EQ(st.axes_snapshots[0].x_limits.max, 20.0f);
    EXPECT_FLOAT_EQ(st.axes_snapshots[0].y_limits.min, 30.0f);
    EXPECT_FLOAT_EQ(st.axes_snapshots[0].y_limits.max, 40.0f);
}

TEST_F(FigureManagerTest, SwitchPreservesAndRestoresState)
{
    // Create a figure with axes
    registry_.clear();
    auto fig0 = std::make_unique<Figure>();
    fig0->subplot(1, 1, 1).xlim(1.0f, 2.0f);
    fig0->subplot(1, 1, 1).ylim(3.0f, 4.0f);
    FigureId id0 = registry_.register_figure(std::move(fig0));

    FigureManager mgr(registry_);
    FigureId      id1 = mgr.create_figure();
    // Now active = id1

    // Switch back to fig 0, modify limits
    mgr.switch_to(id0);
    auto& ax0 = *registry_.get(id0)->axes_mut()[0];
    ax0.xlim(100.0f, 200.0f);

    // Switch to fig 1 (saves fig 0 state)
    mgr.switch_to(id1);

    // Switch back to fig 0 (restores fig 0 state)
    mgr.switch_to(id0);
    auto xlim = registry_.get(id0)->axes()[0]->x_limits();
    EXPECT_FLOAT_EQ(xlim.min, 100.0f);
    EXPECT_FLOAT_EQ(xlim.max, 200.0f);
}

// ─── Queued Operations ────────────────────────────────────────────────────────

TEST_F(FigureManagerTest, QueueCreate)
{
    FigureManager mgr(registry_);
    mgr.queue_create();
    EXPECT_EQ(mgr.count(), 1u);   // Not yet processed

    bool changed = mgr.process_pending();
    EXPECT_TRUE(changed);
    EXPECT_EQ(mgr.count(), 2u);
}

TEST_F(FigureManagerTest, QueueClose)
{
    FigureManager mgr(registry_);
    mgr.create_figure();
    EXPECT_EQ(mgr.count(), 2u);

    mgr.queue_close(first_id_);
    EXPECT_EQ(mgr.count(), 2u);   // Not yet processed

    bool changed = mgr.process_pending();
    EXPECT_TRUE(changed);
    EXPECT_EQ(mgr.count(), 1u);
}

TEST_F(FigureManagerTest, QueueSwitch)
{
    FigureManager mgr(registry_);
    FigureId      id1 = mgr.create_figure();
    mgr.switch_to(first_id_);

    mgr.queue_switch(id1);
    EXPECT_EQ(mgr.active_index(), first_id_);   // Not yet

    bool changed = mgr.process_pending();
    EXPECT_TRUE(changed);
    EXPECT_EQ(mgr.active_index(), id1);
}

TEST_F(FigureManagerTest, ProcessPendingNoOps)
{
    FigureManager mgr(registry_);
    bool          changed = mgr.process_pending();
    EXPECT_FALSE(changed);
}

// ─── Can Close ────────────────────────────────────────────────────────────────

TEST_F(FigureManagerTest, CanClose)
{
    FigureManager mgr(registry_);
    EXPECT_FALSE(mgr.can_close(first_id_));   // Only one figure

    FigureId id1 = mgr.create_figure();
    EXPECT_TRUE(mgr.can_close(first_id_));
    EXPECT_TRUE(mgr.can_close(id1));
}

// ─── Callbacks ────────────────────────────────────────────────────────────────

TEST_F(FigureManagerTest, FigureChangedCallback)
{
    FigureManager mgr(registry_);
    FigureId      id1 = mgr.create_figure();
    mgr.switch_to(first_id_);

    FigureId callback_index = INVALID_FIGURE_ID;
    Figure*  callback_fig   = nullptr;
    mgr.set_on_figure_changed(
        [&](FigureId idx, Figure* fig)
        {
            callback_index = idx;
            callback_fig   = fig;
        });

    mgr.switch_to(id1);
    EXPECT_EQ(callback_index, id1);
    EXPECT_EQ(callback_fig, registry_.get(id1));
}

TEST_F(FigureManagerTest, FigureClosedCallback)
{
    FigureManager mgr(registry_);
    FigureId      id1 = mgr.create_figure();

    FigureId closed_index = INVALID_FIGURE_ID;
    mgr.set_on_figure_closed([&](FigureId idx) { closed_index = idx; });

    mgr.close_figure(id1);
    EXPECT_EQ(closed_index, id1);
}

// ─── TabBar Integration ───────────────────────────────────────────────────────

// Note: TabBar requires ImGui context for draw(), but we can test the
// data management methods without drawing.

TEST_F(FigureManagerTest, TabBarWiring)
{
    FigureManager mgr(registry_);
    EXPECT_EQ(mgr.tab_bar(), nullptr);

    // We can't fully test TabBar without ImGui, but we can verify
    // the pointer is stored correctly
    TabBar tabs;
    mgr.set_tab_bar(&tabs);
    EXPECT_EQ(mgr.tab_bar(), &tabs);
}

// ─── Edge Cases ───────────────────────────────────────────────────────────────

TEST_F(FigureManagerTest, RapidCreateClose)
{
    FigureManager mgr(registry_);
    for (int i = 0; i < 10; ++i)
    {
        mgr.create_figure();
    }
    EXPECT_EQ(mgr.count(), 11u);

    // Close all except first
    mgr.close_all_except(first_id_);
    EXPECT_EQ(mgr.count(), 1u);
    EXPECT_EQ(mgr.active_index(), first_id_);
}

TEST_F(FigureManagerTest, CreateCloseCreateSequence)
{
    FigureManager mgr(registry_);
    mgr.create_figure();                     // Now 2
    mgr.close_figure(first_id_);             // Now 1
    FigureId id_new = mgr.create_figure();   // Now 2 again
    EXPECT_EQ(mgr.count(), 2u);
    EXPECT_EQ(mgr.active_index(), id_new);
}

TEST_F(FigureManagerTest, QueueMultipleOperations)
{
    FigureManager mgr(registry_);
    mgr.queue_create();
    mgr.process_pending();
    EXPECT_EQ(mgr.count(), 2u);

    mgr.queue_close(first_id_);
    mgr.process_pending();
    EXPECT_EQ(mgr.count(), 1u);
}

TEST_F(FigureManagerTest, EmptyRegistry)
{
    FigureRegistry empty_reg;
    FigureManager  mgr(empty_reg);
    EXPECT_EQ(mgr.count(), 0u);
    EXPECT_EQ(mgr.active_figure(), nullptr);
}

// ─── Cross-window figure transfer ─────────────────────────────────────────────

TEST_F(FigureManagerTest, RemoveFigureReturnsState)
{
    FigureManager mgr(registry_);
    mgr.set_title(first_id_, "Custom Title");
    mgr.mark_modified(first_id_, true);

    // Need at least 2 figures (remove_figure doesn't enforce min, close does)
    FigureId second = mgr.create_figure();

    FigureState state = mgr.remove_figure(first_id_);
    EXPECT_EQ(state.custom_title, "Custom Title");
    EXPECT_TRUE(state.is_modified);
    EXPECT_EQ(mgr.count(), 1u);
    EXPECT_EQ(mgr.active_index(), second);

    // Figure still exists in registry (not unregistered)
    EXPECT_NE(registry_.get(first_id_), nullptr);
}

TEST_F(FigureManagerTest, RemoveFigureInvalidId)
{
    FigureManager mgr(registry_);
    FigureState   state = mgr.remove_figure(999);
    // Should return default state, no crash
    EXPECT_TRUE(state.custom_title.empty());
    EXPECT_EQ(mgr.count(), 1u);
}

TEST_F(FigureManagerTest, AddFigureFromAnotherManager)
{
    // Simulate two per-window FigureManagers sharing the same registry
    FigureId second = registry_.register_figure(std::make_unique<Figure>());

    FigureManager src(registry_);
    EXPECT_EQ(src.count(), 2u);

    // Remove figure from source (preserves in registry)
    FigureState transferred = src.remove_figure(second);
    EXPECT_EQ(src.count(), 1u);

    // Create a target manager with only first_id_ initially
    // (FigureManager imports all registry figures on construction, so we
    //  simulate the target by removing second, then re-adding)
    FigureManager dst(registry_);
    // dst imported both figures from registry; remove second to simulate
    // it not being in this window yet
    dst.remove_figure(second);
    EXPECT_EQ(dst.count(), 1u);

    // Transfer
    transferred.custom_title = "Transferred";
    dst.add_figure(second, std::move(transferred));
    EXPECT_EQ(dst.count(), 2u);
    EXPECT_EQ(dst.active_index(), second);
    EXPECT_EQ(dst.get_title(second), "Transferred");
}

TEST_F(FigureManagerTest, AddFigureDuplicateIsNoop)
{
    FigureManager mgr(registry_);
    FigureState   state;
    state.custom_title = "Duplicate";
    mgr.add_figure(first_id_, std::move(state));
    // Should be no-op — first_id_ already in manager
    EXPECT_EQ(mgr.count(), 1u);
}

TEST_F(FigureManagerTest, RemoveLastFigureSetsInvalidActive)
{
    FigureManager mgr(registry_);
    EXPECT_EQ(mgr.count(), 1u);

    FigureState state = mgr.remove_figure(first_id_);
    EXPECT_EQ(mgr.count(), 0u);
    EXPECT_EQ(mgr.active_index(), INVALID_FIGURE_ID);
}

}   // namespace spectra
