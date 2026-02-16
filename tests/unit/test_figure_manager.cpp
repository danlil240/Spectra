#include <gtest/gtest.h>
#include <memory>
#include <spectra/figure.hpp>
#include <string>
#include <vector>

#include "ui/figure_manager.hpp"
#include "ui/tab_bar.hpp"

namespace spectra
{

// ─── Test Fixture ─────────────────────────────────────────────────────────────

class FigureManagerTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        figures_.clear();
        figures_.push_back(std::make_unique<Figure>());
    }

    std::vector<std::unique_ptr<Figure>> figures_;
};

// ─── Construction ─────────────────────────────────────────────────────────────

TEST_F(FigureManagerTest, ConstructWithSingleFigure)
{
    FigureManager mgr(figures_);
    EXPECT_EQ(mgr.count(), 1u);
    EXPECT_EQ(mgr.active_index(), 0u);
    EXPECT_NE(mgr.active_figure(), nullptr);
    EXPECT_EQ(mgr.active_figure(), figures_[0].get());
}

TEST_F(FigureManagerTest, ConstructWithMultipleFigures)
{
    figures_.push_back(std::make_unique<Figure>());
    figures_.push_back(std::make_unique<Figure>());
    FigureManager mgr(figures_);
    EXPECT_EQ(mgr.count(), 3u);
    EXPECT_EQ(mgr.active_index(), 0u);
}

// ─── Create Figure ────────────────────────────────────────────────────────────

TEST_F(FigureManagerTest, CreateFigure)
{
    FigureManager mgr(figures_);
    size_t idx = mgr.create_figure();
    EXPECT_EQ(idx, 1u);
    EXPECT_EQ(mgr.count(), 2u);
    EXPECT_EQ(mgr.active_index(), 1u);  // Auto-switches to new figure
    EXPECT_EQ(figures_.size(), 2u);
}

TEST_F(FigureManagerTest, CreateFigureWithConfig)
{
    FigureManager mgr(figures_);
    FigureConfig cfg;
    cfg.width = 1920;
    cfg.height = 1080;
    size_t idx = mgr.create_figure(cfg);
    EXPECT_EQ(idx, 1u);
    EXPECT_EQ(figures_[1]->width(), 1920u);
    EXPECT_EQ(figures_[1]->height(), 1080u);
}

TEST_F(FigureManagerTest, CreateMultipleFigures)
{
    FigureManager mgr(figures_);
    mgr.create_figure();
    mgr.create_figure();
    mgr.create_figure();
    EXPECT_EQ(mgr.count(), 4u);
    EXPECT_EQ(mgr.active_index(), 3u);
}

// ─── Close Figure ─────────────────────────────────────────────────────────────

TEST_F(FigureManagerTest, CloseFigure)
{
    FigureManager mgr(figures_);
    mgr.create_figure();
    mgr.create_figure();
    EXPECT_EQ(mgr.count(), 3u);

    bool closed = mgr.close_figure(1);
    EXPECT_TRUE(closed);
    EXPECT_EQ(mgr.count(), 2u);
}

TEST_F(FigureManagerTest, CannotCloseLastFigure)
{
    FigureManager mgr(figures_);
    bool closed = mgr.close_figure(0);
    EXPECT_FALSE(closed);
    EXPECT_EQ(mgr.count(), 1u);
}

TEST_F(FigureManagerTest, CloseOutOfBounds)
{
    FigureManager mgr(figures_);
    bool closed = mgr.close_figure(99);
    EXPECT_FALSE(closed);
}

TEST_F(FigureManagerTest, CloseActiveFigureAdjustsIndex)
{
    FigureManager mgr(figures_);
    mgr.create_figure();
    mgr.create_figure();
    mgr.switch_to(2);  // Active = 2
    EXPECT_EQ(mgr.active_index(), 2u);

    mgr.close_figure(2);                // Close active (last)
    EXPECT_EQ(mgr.active_index(), 1u);  // Should move to previous
}

TEST_F(FigureManagerTest, CloseBeforeActiveAdjustsIndex)
{
    FigureManager mgr(figures_);
    mgr.create_figure();
    mgr.create_figure();
    mgr.switch_to(2);  // Active = 2

    mgr.close_figure(0);                // Close before active
    EXPECT_EQ(mgr.active_index(), 1u);  // Should decrement
    EXPECT_EQ(mgr.count(), 2u);
}

// ─── Close All Except ─────────────────────────────────────────────────────────

TEST_F(FigureManagerTest, CloseAllExcept)
{
    FigureManager mgr(figures_);
    mgr.create_figure();
    mgr.create_figure();
    mgr.create_figure();
    EXPECT_EQ(mgr.count(), 4u);

    bool result = mgr.close_all_except(1);
    EXPECT_TRUE(result);
    EXPECT_EQ(mgr.count(), 1u);
    EXPECT_EQ(mgr.active_index(), 0u);
}

TEST_F(FigureManagerTest, CloseAllExceptOutOfBounds)
{
    FigureManager mgr(figures_);
    bool result = mgr.close_all_except(99);
    EXPECT_FALSE(result);
}

// ─── Close To Right ───────────────────────────────────────────────────────────

TEST_F(FigureManagerTest, CloseToRight)
{
    FigureManager mgr(figures_);
    mgr.create_figure();
    mgr.create_figure();
    mgr.create_figure();
    EXPECT_EQ(mgr.count(), 4u);

    bool result = mgr.close_to_right(1);
    EXPECT_TRUE(result);
    EXPECT_EQ(mgr.count(), 2u);
}

TEST_F(FigureManagerTest, CloseToRightLastTab)
{
    FigureManager mgr(figures_);
    mgr.create_figure();
    bool result = mgr.close_to_right(1);
    EXPECT_FALSE(result);  // Nothing to close
}

TEST_F(FigureManagerTest, CloseToRightAdjustsActiveIndex)
{
    FigureManager mgr(figures_);
    mgr.create_figure();
    mgr.create_figure();
    mgr.create_figure();
    mgr.switch_to(3);  // Active = last

    mgr.close_to_right(1);  // Close indices 2, 3
    EXPECT_EQ(mgr.count(), 2u);
    EXPECT_EQ(mgr.active_index(), 1u);  // Clamped to index 1
}

// ─── Duplicate Figure ─────────────────────────────────────────────────────────

TEST_F(FigureManagerTest, DuplicateFigure)
{
    FigureManager mgr(figures_);
    size_t dup_idx = mgr.duplicate_figure(0);
    EXPECT_EQ(dup_idx, 1u);
    EXPECT_EQ(mgr.count(), 2u);
    EXPECT_EQ(mgr.active_index(), 1u);  // Switches to duplicate
}

TEST_F(FigureManagerTest, DuplicatePreservesDimensions)
{
    FigureConfig cfg;
    cfg.width = 1920;
    cfg.height = 1080;
    figures_.clear();
    figures_.push_back(std::make_unique<Figure>(cfg));

    FigureManager mgr(figures_);
    mgr.duplicate_figure(0);
    EXPECT_EQ(figures_[1]->width(), 1920u);
    EXPECT_EQ(figures_[1]->height(), 1080u);
}

TEST_F(FigureManagerTest, DuplicateOutOfBounds)
{
    FigureManager mgr(figures_);
    size_t idx = mgr.duplicate_figure(99);
    EXPECT_EQ(idx, SIZE_MAX);
    EXPECT_EQ(mgr.count(), 1u);
}

TEST_F(FigureManagerTest, DuplicateTitleHasCopySuffix)
{
    FigureManager mgr(figures_);
    mgr.set_title(0, "My Plot");
    mgr.duplicate_figure(0);
    std::string dup_title = mgr.get_title(1);
    EXPECT_EQ(dup_title, "My Plot (Copy)");
}

// ─── Switch ───────────────────────────────────────────────────────────────────

TEST_F(FigureManagerTest, SwitchTo)
{
    FigureManager mgr(figures_);
    mgr.create_figure();
    mgr.create_figure();
    mgr.switch_to(0);
    EXPECT_EQ(mgr.active_index(), 0u);
    EXPECT_EQ(mgr.active_figure(), figures_[0].get());
}

TEST_F(FigureManagerTest, SwitchToSameIsNoop)
{
    FigureManager mgr(figures_);
    mgr.create_figure();
    mgr.switch_to(1);
    EXPECT_EQ(mgr.active_index(), 1u);
    mgr.switch_to(1);  // Same index
    EXPECT_EQ(mgr.active_index(), 1u);
}

TEST_F(FigureManagerTest, SwitchToOutOfBounds)
{
    FigureManager mgr(figures_);
    mgr.switch_to(99);
    EXPECT_EQ(mgr.active_index(), 0u);  // Unchanged
}

TEST_F(FigureManagerTest, SwitchToNext)
{
    FigureManager mgr(figures_);
    mgr.create_figure();
    mgr.create_figure();
    mgr.switch_to(0);

    mgr.switch_to_next();
    EXPECT_EQ(mgr.active_index(), 1u);
    mgr.switch_to_next();
    EXPECT_EQ(mgr.active_index(), 2u);
    mgr.switch_to_next();
    EXPECT_EQ(mgr.active_index(), 0u);  // Wraps around
}

TEST_F(FigureManagerTest, SwitchToPrevious)
{
    FigureManager mgr(figures_);
    mgr.create_figure();
    mgr.create_figure();
    mgr.switch_to(0);

    mgr.switch_to_previous();
    EXPECT_EQ(mgr.active_index(), 2u);  // Wraps around
    mgr.switch_to_previous();
    EXPECT_EQ(mgr.active_index(), 1u);
}

TEST_F(FigureManagerTest, SwitchNextSingleFigureNoop)
{
    FigureManager mgr(figures_);
    mgr.switch_to_next();
    EXPECT_EQ(mgr.active_index(), 0u);
}

// ─── Move Tab ─────────────────────────────────────────────────────────────────

TEST_F(FigureManagerTest, MoveTab)
{
    FigureManager mgr(figures_);
    mgr.create_figure();
    mgr.create_figure();
    mgr.set_title(0, "A");
    mgr.set_title(1, "B");
    mgr.set_title(2, "C");

    mgr.switch_to(0);
    mgr.move_tab(0, 2);
    EXPECT_EQ(mgr.get_title(0), "B");
    EXPECT_EQ(mgr.get_title(1), "C");
    EXPECT_EQ(mgr.get_title(2), "A");
    EXPECT_EQ(mgr.active_index(), 2u);  // Followed the moved figure
}

TEST_F(FigureManagerTest, MoveTabSameIndex)
{
    FigureManager mgr(figures_);
    mgr.create_figure();
    mgr.move_tab(0, 0);  // Noop
    EXPECT_EQ(mgr.count(), 2u);
}

// ─── Title Management ─────────────────────────────────────────────────────────

TEST_F(FigureManagerTest, DefaultTitle)
{
    EXPECT_EQ(FigureManager::default_title(0), "Figure 1");
    EXPECT_EQ(FigureManager::default_title(4), "Figure 5");
}

TEST_F(FigureManagerTest, GetSetTitle)
{
    FigureManager mgr(figures_);
    mgr.set_title(0, "My Custom Plot");
    EXPECT_EQ(mgr.get_title(0), "My Custom Plot");
}

TEST_F(FigureManagerTest, GetTitleOutOfBounds)
{
    FigureManager mgr(figures_);
    std::string title = mgr.get_title(99);
    // Should return a default title, not crash
    EXPECT_FALSE(title.empty());
}

// ─── Modified State ───────────────────────────────────────────────────────────

TEST_F(FigureManagerTest, ModifiedState)
{
    FigureManager mgr(figures_);
    EXPECT_FALSE(mgr.is_modified(0));
    mgr.mark_modified(0, true);
    EXPECT_TRUE(mgr.is_modified(0));
    mgr.mark_modified(0, false);
    EXPECT_FALSE(mgr.is_modified(0));
}

TEST_F(FigureManagerTest, ModifiedOutOfBounds)
{
    FigureManager mgr(figures_);
    EXPECT_FALSE(mgr.is_modified(99));
}

// ─── Per-Figure State ─────────────────────────────────────────────────────────

TEST_F(FigureManagerTest, StateAccessor)
{
    FigureManager mgr(figures_);
    auto& st = mgr.state(0);
    st.selected_series_index = 3;
    EXPECT_EQ(mgr.state(0).selected_series_index, 3);
}

TEST_F(FigureManagerTest, ActiveState)
{
    FigureManager mgr(figures_);
    mgr.active_state().inspector_scroll_y = 42.0f;
    EXPECT_FLOAT_EQ(mgr.state(0).inspector_scroll_y, 42.0f);
}

TEST_F(FigureManagerTest, SaveRestoreAxisState)
{
    // Create a figure with axes
    figures_.clear();
    figures_.push_back(std::make_unique<Figure>());
    auto& ax = figures_[0]->subplot(1, 1, 1);
    ax.xlim(10.0f, 20.0f);
    ax.ylim(30.0f, 40.0f);

    FigureManager mgr(figures_);
    mgr.create_figure();  // Creates fig 1, switches to it

    // The save should have captured fig 0's axis state
    const auto& st = mgr.state(0);
    ASSERT_EQ(st.axes_snapshots.size(), 1u);
    EXPECT_FLOAT_EQ(st.axes_snapshots[0].x_limits.min, 10.0f);
    EXPECT_FLOAT_EQ(st.axes_snapshots[0].x_limits.max, 20.0f);
    EXPECT_FLOAT_EQ(st.axes_snapshots[0].y_limits.min, 30.0f);
    EXPECT_FLOAT_EQ(st.axes_snapshots[0].y_limits.max, 40.0f);
}

TEST_F(FigureManagerTest, SwitchPreservesAndRestoresState)
{
    // Create two figures with axes
    figures_.clear();
    figures_.push_back(std::make_unique<Figure>());
    figures_[0]->subplot(1, 1, 1).xlim(1.0f, 2.0f);
    figures_[0]->subplot(1, 1, 1).ylim(3.0f, 4.0f);

    FigureManager mgr(figures_);
    mgr.create_figure();
    // Now active = 1

    // Modify fig 0's limits externally (simulating user zoom)
    // First switch back to fig 0
    mgr.switch_to(0);
    auto& ax0 = *figures_[0]->axes_mut()[0];
    ax0.xlim(100.0f, 200.0f);

    // Switch to fig 1 (saves fig 0 state)
    mgr.switch_to(1);

    // Switch back to fig 0 (restores fig 0 state)
    mgr.switch_to(0);
    auto xlim = figures_[0]->axes()[0]->x_limits();
    EXPECT_FLOAT_EQ(xlim.min, 100.0f);
    EXPECT_FLOAT_EQ(xlim.max, 200.0f);
}

// ─── Queued Operations ────────────────────────────────────────────────────────

TEST_F(FigureManagerTest, QueueCreate)
{
    FigureManager mgr(figures_);
    mgr.queue_create();
    EXPECT_EQ(mgr.count(), 1u);  // Not yet processed

    bool changed = mgr.process_pending();
    EXPECT_TRUE(changed);
    EXPECT_EQ(mgr.count(), 2u);
}

TEST_F(FigureManagerTest, QueueClose)
{
    FigureManager mgr(figures_);
    mgr.create_figure();
    EXPECT_EQ(mgr.count(), 2u);

    mgr.queue_close(0);
    EXPECT_EQ(mgr.count(), 2u);  // Not yet processed

    bool changed = mgr.process_pending();
    EXPECT_TRUE(changed);
    EXPECT_EQ(mgr.count(), 1u);
}

TEST_F(FigureManagerTest, QueueSwitch)
{
    FigureManager mgr(figures_);
    mgr.create_figure();
    mgr.switch_to(0);

    mgr.queue_switch(1);
    EXPECT_EQ(mgr.active_index(), 0u);  // Not yet

    bool changed = mgr.process_pending();
    EXPECT_TRUE(changed);
    EXPECT_EQ(mgr.active_index(), 1u);
}

TEST_F(FigureManagerTest, ProcessPendingNoOps)
{
    FigureManager mgr(figures_);
    bool changed = mgr.process_pending();
    EXPECT_FALSE(changed);
}

// ─── Can Close ────────────────────────────────────────────────────────────────

TEST_F(FigureManagerTest, CanClose)
{
    FigureManager mgr(figures_);
    EXPECT_FALSE(mgr.can_close(0));  // Only one figure

    mgr.create_figure();
    EXPECT_TRUE(mgr.can_close(0));
    EXPECT_TRUE(mgr.can_close(1));
}

// ─── Callbacks ────────────────────────────────────────────────────────────────

TEST_F(FigureManagerTest, FigureChangedCallback)
{
    FigureManager mgr(figures_);
    mgr.create_figure();
    mgr.switch_to(0);

    size_t callback_index = SIZE_MAX;
    Figure* callback_fig = nullptr;
    mgr.set_on_figure_changed(
        [&](size_t idx, Figure* fig)
        {
            callback_index = idx;
            callback_fig = fig;
        });

    mgr.switch_to(1);
    EXPECT_EQ(callback_index, 1u);
    EXPECT_EQ(callback_fig, figures_[1].get());
}

TEST_F(FigureManagerTest, FigureClosedCallback)
{
    FigureManager mgr(figures_);
    mgr.create_figure();

    size_t closed_index = SIZE_MAX;
    mgr.set_on_figure_closed([&](size_t idx) { closed_index = idx; });

    mgr.close_figure(1);
    EXPECT_EQ(closed_index, 1u);
}

// ─── TabBar Integration ───────────────────────────────────────────────────────

// Note: TabBar requires ImGui context for draw(), but we can test the
// data management methods without drawing.

TEST_F(FigureManagerTest, TabBarWiring)
{
    FigureManager mgr(figures_);
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
    FigureManager mgr(figures_);
    for (int i = 0; i < 10; ++i)
    {
        mgr.create_figure();
    }
    EXPECT_EQ(mgr.count(), 11u);

    // Close all except first
    mgr.close_all_except(0);
    EXPECT_EQ(mgr.count(), 1u);
    EXPECT_EQ(mgr.active_index(), 0u);
}

TEST_F(FigureManagerTest, CreateCloseCreateSequence)
{
    FigureManager mgr(figures_);
    mgr.create_figure();  // Now 2
    mgr.close_figure(0);  // Now 1
    mgr.create_figure();  // Now 2 again
    EXPECT_EQ(mgr.count(), 2u);
    EXPECT_EQ(mgr.active_index(), 1u);
}

TEST_F(FigureManagerTest, QueueMultipleOperations)
{
    FigureManager mgr(figures_);
    mgr.queue_create();
    mgr.process_pending();
    EXPECT_EQ(mgr.count(), 2u);

    mgr.queue_close(0);
    mgr.process_pending();
    EXPECT_EQ(mgr.count(), 1u);
}

TEST_F(FigureManagerTest, EmptyFiguresVector)
{
    std::vector<std::unique_ptr<Figure>> empty;
    FigureManager mgr(empty);
    EXPECT_EQ(mgr.count(), 0u);
    EXPECT_EQ(mgr.active_figure(), nullptr);
}

}  // namespace spectra
