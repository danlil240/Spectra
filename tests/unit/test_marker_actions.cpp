// Nav rail "Markers" popover actions: list the current figure's data tips,
// remove one / clear all with undo, and pan the view to a tip.

#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <vector>

#include "ui/commands/undo_manager.hpp"
#include "ui/overlay/data_interaction.hpp"
#include "ui/overlay/marker_actions.hpp"

using namespace spectra;

namespace
{

class MarkerActions : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        ax1_ = &fig_.subplot(1, 2, 1);
        ax2_ = &fig_.subplot(1, 2, 2);
        s1_  = &ax1_->line(x_, y_).label("alpha");
        s2_  = &ax2_->line(x_, y_);
        oax_ = &other_.subplot(1, 1, 1);
        os_  = &oax_->line(x_, y_).label("other");

        di_.add_marker(1.0, 10.0, s1_, 1, ax1_);
        di_.add_marker(3.0, 30.0, os_, 3, oax_);   // belongs to another figure
        di_.add_marker(2.0, 20.0, s2_, 2, ax2_);
        di_.add_marker(4.0, 40.0, s1_, 4, ax1_);
    }

    CurrentFigureFn current()
    {
        return [this]() { return current_; };
    }

    std::vector<float> x_ = {0, 1, 2, 3, 4, 5};
    std::vector<float> y_ = {0, 10, 20, 30, 40, 50};
    Figure             fig_;
    Figure             other_;
    Axes*              ax1_ = nullptr;
    Axes*              ax2_ = nullptr;
    Axes*              oax_ = nullptr;
    Series*            s1_  = nullptr;
    Series*            s2_  = nullptr;
    Series*            os_  = nullptr;
    DataInteraction    di_;
    UndoManager        undo_;
    Figure*            current_ = &fig_;
};

}   // namespace

TEST_F(MarkerActions, ListsOnlyTipsOfTheFigure)
{
    EXPECT_EQ(figure_marker_indices(di_.markers(), fig_), (std::vector<size_t>{0, 2, 3}));
    EXPECT_EQ(figure_marker_indices(di_.markers(), other_), (std::vector<size_t>{1}));
    EXPECT_EQ(marker_series_name(di_.markers()[0]), "alpha");
    EXPECT_EQ(marker_series_name(di_.markers()[2]), "Series");   // unnamed
}

TEST_F(MarkerActions, ClearAllIsUndoableAndKeepsOtherFigures)
{
    EXPECT_EQ(clear_figure_markers_undoable(di_, fig_, &undo_, current()), 3u);
    ASSERT_EQ(di_.markers().size(), 1u);
    EXPECT_EQ(di_.markers()[0].series, os_);
    EXPECT_EQ(undo_.undo_description(), "Clear data tips");

    ASSERT_TRUE(undo_.undo());
    ASSERT_EQ(di_.markers().size(), 4u);
    // Original order is restored.
    EXPECT_EQ(di_.markers()[0].point_index, 1u);
    EXPECT_EQ(di_.markers()[1].series, os_);
    EXPECT_EQ(di_.markers()[2].point_index, 2u);
    EXPECT_EQ(di_.markers()[3].point_index, 4u);

    ASSERT_TRUE(undo_.redo());
    EXPECT_EQ(di_.markers().size(), 1u);
}

TEST_F(MarkerActions, ClearAllWithNoTipsPushesNothing)
{
    Figure empty;
    EXPECT_EQ(clear_figure_markers_undoable(di_, empty, &undo_, current()), 0u);
    EXPECT_FALSE(undo_.can_undo());
}

TEST_F(MarkerActions, RemoveOneIsUndoable)
{
    ASSERT_TRUE(remove_marker_undoable(di_, 2, &undo_, current()));
    ASSERT_EQ(di_.markers().size(), 3u);
    EXPECT_EQ(di_.markers()[2].point_index, 4u);
    ASSERT_TRUE(undo_.undo());
    ASSERT_EQ(di_.markers().size(), 4u);
    EXPECT_EQ(di_.markers()[2].series, s2_);
    ASSERT_TRUE(undo_.redo());
    EXPECT_EQ(di_.markers().size(), 3u);
    EXPECT_FALSE(remove_marker_undoable(di_, 99, &undo_, current()));
}

TEST_F(MarkerActions, UndoDoesNotRestoreTipsOfARemovedSeries)
{
    ASSERT_TRUE(remove_marker_undoable(di_, 2, &undo_, current()));
    // The series goes away (as on series deletion) before the undo.
    di_.notify_series_removed(s2_);
    ax2_->clear_series();
    ASSERT_TRUE(undo_.undo());
    EXPECT_EQ(di_.markers().size(), 3u);
}

TEST_F(MarkerActions, UndoAfterSwitchingFigureRestoresNothing)
{
    clear_figure_markers_undoable(di_, fig_, &undo_, current());
    current_ = &other_;
    ASSERT_TRUE(undo_.undo());
    EXPECT_EQ(di_.markers().size(), 1u);
}

TEST_F(MarkerActions, CenterViewKeepsSpanAndIsUndoable)
{
    ax1_->xlim(0.0, 2.0);
    ax1_->ylim(0.0, 100.0);
    ASSERT_TRUE(center_view_on_marker(di_.markers()[3], fig_, &undo_));   // (4, 40)
    EXPECT_DOUBLE_EQ(ax1_->x_limits().min, 3.0);
    EXPECT_DOUBLE_EQ(ax1_->x_limits().max, 5.0);
    EXPECT_DOUBLE_EQ(ax1_->y_limits().min, -10.0);
    EXPECT_DOUBLE_EQ(ax1_->y_limits().max, 90.0);
    ASSERT_TRUE(undo_.undo());
    EXPECT_DOUBLE_EQ(ax1_->x_limits().min, 0.0);
    EXPECT_DOUBLE_EQ(ax1_->x_limits().max, 2.0);
}

TEST_F(MarkerActions, CenterViewOnLogAxisCentresInLogSpace)
{
    ax1_->yscale(ScaleType::Log10);
    ax1_->xlim(0.0, 2.0);
    ax1_->ylim(1.0, 100.0);
    ASSERT_TRUE(center_view_on_marker(di_.markers()[0], fig_, nullptr));   // y = 10
    EXPECT_NEAR(ax1_->y_limits().min, 1.0, 1e-9);
    EXPECT_NEAR(ax1_->y_limits().max, 100.0, 1e-9);
}

TEST_F(MarkerActions, CenterViewRejectsTipOfAnotherFigure)
{
    EXPECT_FALSE(center_view_on_marker(di_.markers()[1], fig_, &undo_));
    EXPECT_FALSE(undo_.can_undo());
}
