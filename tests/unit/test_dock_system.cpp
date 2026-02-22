#include <gtest/gtest.h>

#include "ui/dock_system.hpp"

using namespace spectra;

// ─── DockSystem Construction ─────────────────────────────────────────────────

TEST(DockSystemConstruction, DefaultState)
{
    DockSystem ds;
    EXPECT_FALSE(ds.is_split());
    EXPECT_EQ(ds.pane_count(), 1u);
    EXPECT_EQ(ds.active_figure_index(), 0u);
    EXPECT_FALSE(ds.is_dragging());
    EXPECT_FALSE(ds.is_dragging_splitter());
}

// ─── DockSystem Split Operations ─────────────────────────────────────────────

TEST(DockSystemSplit, SplitRight)
{
    DockSystem ds;
    auto*      pane = ds.split_right(1);
    EXPECT_NE(pane, nullptr);
    EXPECT_TRUE(ds.is_split());
    EXPECT_EQ(ds.pane_count(), 2u);
}

TEST(DockSystemSplit, SplitDown)
{
    DockSystem ds;
    auto*      pane = ds.split_down(1);
    EXPECT_NE(pane, nullptr);
    EXPECT_TRUE(ds.is_split());
    EXPECT_EQ(ds.pane_count(), 2u);
}

TEST(DockSystemSplit, SplitFigureRight)
{
    DockSystem ds;
    ds.split_right(1);
    auto* pane = ds.split_figure_right(1, 2, 0.4f);
    EXPECT_NE(pane, nullptr);
    EXPECT_EQ(ds.pane_count(), 3u);
}

TEST(DockSystemSplit, SplitFigureDown)
{
    DockSystem ds;
    ds.split_right(1);
    auto* pane = ds.split_figure_down(1, 2, 0.6f);
    EXPECT_NE(pane, nullptr);
    EXPECT_EQ(ds.pane_count(), 3u);
}

TEST(DockSystemSplit, CloseSplit)
{
    DockSystem ds;
    ds.split_right(1);
    EXPECT_TRUE(ds.close_split(1));
    EXPECT_FALSE(ds.is_split());
    EXPECT_EQ(ds.pane_count(), 1u);
}

TEST(DockSystemSplit, CloseNonExistent)
{
    DockSystem ds;
    ds.split_right(1);
    EXPECT_FALSE(ds.close_split(99));
}

TEST(DockSystemSplit, ResetSplits)
{
    DockSystem ds;
    ds.split_right(1);
    ds.split_figure_down(1, 2);
    EXPECT_EQ(ds.pane_count(), 3u);

    ds.reset_splits();
    EXPECT_FALSE(ds.is_split());
    EXPECT_EQ(ds.pane_count(), 1u);
}

TEST(DockSystemSplit, LayoutChangedCallback)
{
    DockSystem ds;
    int        callback_count = 0;
    ds.set_on_layout_changed([&]() { callback_count++; });

    ds.split_right(1);
    EXPECT_EQ(callback_count, 1);

    ds.split_figure_down(1, 2);
    EXPECT_EQ(callback_count, 2);

    ds.close_split(2);
    EXPECT_EQ(callback_count, 3);

    ds.reset_splits();
    EXPECT_EQ(callback_count, 4);
}

// ─── DockSystem Drag-to-Dock ─────────────────────────────────────────────────

TEST(DockSystemDrag, BeginDrag)
{
    DockSystem ds;
    ds.update_layout(Rect{0, 0, 1000, 600});

    ds.begin_drag(0, 500.0f, 300.0f);
    EXPECT_TRUE(ds.is_dragging());
    EXPECT_EQ(ds.dragging_figure(), 0u);
}

TEST(DockSystemDrag, CancelDrag)
{
    DockSystem ds;
    ds.begin_drag(0, 500.0f, 300.0f);
    ds.cancel_drag();
    EXPECT_FALSE(ds.is_dragging());
}

TEST(DockSystemDrag, UpdateDragComputesDropTarget)
{
    DockSystem ds;
    ds.update_layout(Rect{0, 0, 1000, 600});

    ds.begin_drag(0, 500.0f, 300.0f);
    auto target = ds.update_drag(50.0f, 300.0f);   // Near left edge

    // Should detect a drop zone
    EXPECT_NE(target.zone, DropZone::None);
    EXPECT_NE(target.target_pane, nullptr);
}

TEST(DockSystemDrag, EndDragOnSelfDoesNothing)
{
    DockSystem ds;
    ds.update_layout(Rect{0, 0, 1000, 600});

    ds.begin_drag(0, 500.0f, 300.0f);
    bool result = ds.end_drag(500.0f, 300.0f);
    // Dragging onto self should not create a split
    EXPECT_FALSE(result);
    EXPECT_FALSE(ds.is_dragging());
}

TEST(DockSystemDrag, EndDragOnDifferentPane)
{
    DockSystem ds;
    ds.split_right(1);
    ds.update_layout(Rect{0, 0, 1000, 600});

    // Drag figure 0 onto the right pane (figure 1)
    ds.begin_drag(0, 100.0f, 300.0f);
    bool result = ds.end_drag(800.0f, 300.0f);
    // This should attempt a dock operation
    // Whether it succeeds depends on drop zone detection
    EXPECT_FALSE(ds.is_dragging());
    (void)result;   // Result depends on exact drop zone hit
}

TEST(DockSystemDrag, EndDragOutsideBounds)
{
    DockSystem ds;
    ds.update_layout(Rect{0, 0, 1000, 600});

    ds.begin_drag(0, 500.0f, 300.0f);
    bool result = ds.end_drag(-100.0f, -100.0f);
    EXPECT_FALSE(result);
}

TEST(DockSystemDrag, DragWithoutBeginReturnsEmpty)
{
    DockSystem ds;
    auto       target = ds.update_drag(500.0f, 300.0f);
    EXPECT_EQ(target.zone, DropZone::None);
}

// ─── DockSystem Layout ───────────────────────────────────────────────────────

TEST(DockSystemLayout, UpdateLayout)
{
    DockSystem ds;
    ds.split_right(1, 0.5f);
    ds.update_layout(Rect{100, 50, 800, 600});

    auto infos = ds.get_pane_infos();
    EXPECT_EQ(infos.size(), 2u);

    for (const auto& info : infos)
    {
        EXPECT_GT(info.bounds.w, 0.0f);
        EXPECT_GT(info.bounds.h, 0.0f);
    }
}

TEST(DockSystemLayout, PaneInfosContainActiveFlag)
{
    DockSystem ds;
    ds.split_right(1);
    ds.set_active_figure_index(1);
    ds.update_layout(Rect{0, 0, 1000, 600});

    auto infos = ds.get_pane_infos();
    EXPECT_EQ(infos.size(), 2u);

    bool found_active = false;
    for (const auto& info : infos)
    {
        if (info.figure_index == 1)
        {
            EXPECT_TRUE(info.is_active);
            found_active = true;
        }
        else
        {
            EXPECT_FALSE(info.is_active);
        }
    }
    EXPECT_TRUE(found_active);
}

TEST(DockSystemLayout, SinglePaneInfo)
{
    DockSystem ds;
    ds.update_layout(Rect{0, 0, 1000, 600});

    auto infos = ds.get_pane_infos();
    EXPECT_EQ(infos.size(), 1u);
    EXPECT_EQ(infos[0].figure_index, 0u);
    EXPECT_TRUE(infos[0].is_active);
    EXPECT_FLOAT_EQ(infos[0].bounds.w, 1000.0f);
    // content_bounds() subtracts PANE_TAB_HEIGHT (26px) for the unified tab header
    EXPECT_FLOAT_EQ(infos[0].bounds.h, 600.0f - SplitPane::PANE_TAB_HEIGHT);
}

// ─── DockSystem Splitter Interaction ─────────────────────────────────────────

TEST(DockSystemSplitter, IsOverSplitter)
{
    DockSystem ds;
    ds.split_right(1, 0.5f);
    ds.update_layout(Rect{0, 0, 1000, 600});

    EXPECT_TRUE(ds.is_over_splitter(500.0f, 300.0f));
    EXPECT_FALSE(ds.is_over_splitter(100.0f, 300.0f));
}

TEST(DockSystemSplitter, SplitterDirection)
{
    DockSystem ds;
    ds.split_right(1, 0.5f);
    ds.update_layout(Rect{0, 0, 1000, 600});

    auto dir = ds.splitter_direction_at(500.0f, 300.0f);
    EXPECT_EQ(dir, SplitDirection::Horizontal);
}

TEST(DockSystemSplitter, SplitterDirectionVertical)
{
    DockSystem ds;
    ds.split_down(1, 0.5f);
    ds.update_layout(Rect{0, 0, 1000, 600});

    auto dir = ds.splitter_direction_at(500.0f, 300.0f);
    EXPECT_EQ(dir, SplitDirection::Vertical);
}

TEST(DockSystemSplitter, BeginAndEndSplitterDrag)
{
    DockSystem ds;
    ds.split_right(1, 0.5f);
    ds.update_layout(Rect{0, 0, 1000, 600});

    ds.begin_splitter_drag(500.0f, 300.0f);
    EXPECT_TRUE(ds.is_dragging_splitter());

    ds.update_splitter_drag(600.0f);
    ds.end_splitter_drag();
    EXPECT_FALSE(ds.is_dragging_splitter());
}

TEST(DockSystemSplitter, BeginSplitterDragMiss)
{
    DockSystem ds;
    ds.split_right(1, 0.5f);
    ds.update_layout(Rect{0, 0, 1000, 600});

    ds.begin_splitter_drag(100.0f, 300.0f);   // Not on a splitter
    EXPECT_FALSE(ds.is_dragging_splitter());
}

// ─── DockSystem Active Pane ──────────────────────────────────────────────────

TEST(DockSystemActive, ActivatePaneAtPoint)
{
    DockSystem ds;
    ds.split_right(1, 0.5f);
    ds.update_layout(Rect{0, 0, 1000, 600});

    ds.activate_pane_at(800.0f, 300.0f);
    EXPECT_EQ(ds.active_figure_index(), 1u);

    ds.activate_pane_at(100.0f, 300.0f);
    EXPECT_EQ(ds.active_figure_index(), 0u);
}

TEST(DockSystemActive, SetActiveFigure)
{
    DockSystem ds;
    ds.split_right(1);

    ds.set_active_figure_index(1);
    EXPECT_EQ(ds.active_figure_index(), 1u);

    ds.set_active_figure_index(0);
    EXPECT_EQ(ds.active_figure_index(), 0u);
}

// ─── DockSystem Serialization ────────────────────────────────────────────────

TEST(DockSystemSerialization, RoundTrip)
{
    DockSystem ds;
    ds.split_right(1, 0.6f);
    ds.set_active_figure_index(1);
    ds.update_layout(Rect{0, 0, 1000, 600});

    std::string data = ds.serialize();
    EXPECT_FALSE(data.empty());

    DockSystem ds2;
    ds2.update_layout(Rect{0, 0, 1000, 600});
    EXPECT_TRUE(ds2.deserialize(data));

    EXPECT_TRUE(ds2.is_split());
    EXPECT_EQ(ds2.pane_count(), 2u);
    EXPECT_EQ(ds2.active_figure_index(), 1u);
}

TEST(DockSystemSerialization, DeserializeEmpty)
{
    DockSystem ds;
    EXPECT_FALSE(ds.deserialize(""));
}

TEST(DockSystemSerialization, DeserializeCallsLayoutChanged)
{
    DockSystem ds;
    ds.split_right(1);
    std::string data = ds.serialize();

    DockSystem ds2;
    int        callback_count = 0;
    ds2.set_on_layout_changed([&]() { callback_count++; });
    ds2.update_layout(Rect{0, 0, 1000, 600});
    ds2.deserialize(data);
    EXPECT_EQ(callback_count, 1);
}

// ─── DockSystem Drop Zones ───────────────────────────────────────────────────

TEST(DockSystemDropZones, LeftEdge)
{
    DockSystem ds;
    ds.update_layout(Rect{0, 0, 1000, 600});

    ds.begin_drag(0, 500.0f, 300.0f);
    auto target = ds.update_drag(10.0f, 300.0f);
    EXPECT_EQ(target.zone, DropZone::Left);
    ds.cancel_drag();
}

TEST(DockSystemDropZones, RightEdge)
{
    DockSystem ds;
    ds.update_layout(Rect{0, 0, 1000, 600});

    ds.begin_drag(0, 500.0f, 300.0f);
    auto target = ds.update_drag(990.0f, 300.0f);
    EXPECT_EQ(target.zone, DropZone::Right);
    ds.cancel_drag();
}

TEST(DockSystemDropZones, TopEdge)
{
    DockSystem ds;
    ds.update_layout(Rect{0, 0, 1000, 600});

    ds.begin_drag(0, 500.0f, 300.0f);
    auto target = ds.update_drag(500.0f, 10.0f);
    EXPECT_EQ(target.zone, DropZone::Top);
    ds.cancel_drag();
}

TEST(DockSystemDropZones, BottomEdge)
{
    DockSystem ds;
    ds.update_layout(Rect{0, 0, 1000, 600});

    ds.begin_drag(0, 500.0f, 300.0f);
    auto target = ds.update_drag(500.0f, 590.0f);
    EXPECT_EQ(target.zone, DropZone::Bottom);
    ds.cancel_drag();
}

TEST(DockSystemDropZones, Center)
{
    DockSystem ds;
    ds.update_layout(Rect{0, 0, 1000, 600});

    ds.begin_drag(0, 500.0f, 300.0f);
    auto target = ds.update_drag(500.0f, 300.0f);
    EXPECT_EQ(target.zone, DropZone::Center);
    ds.cancel_drag();
}

TEST(DockSystemDropZones, HighlightRectNonZero)
{
    DockSystem ds;
    ds.update_layout(Rect{0, 0, 1000, 600});

    ds.begin_drag(0, 500.0f, 300.0f);
    auto target = ds.update_drag(10.0f, 300.0f);
    EXPECT_GT(target.highlight_rect.w, 0.0f);
    EXPECT_GT(target.highlight_rect.h, 0.0f);
    ds.cancel_drag();
}

// ─── Edge Cases ──────────────────────────────────────────────────────────────

TEST(DockSystemEdgeCases, SplitAndCloseRepeatedly)
{
    DockSystem ds;
    for (int i = 0; i < 5; ++i)
    {
        ds.split_right(static_cast<size_t>(i + 1));
        EXPECT_TRUE(ds.is_split());
        ds.close_split(static_cast<size_t>(i + 1));
        EXPECT_FALSE(ds.is_split());
    }
}

TEST(DockSystemEdgeCases, ComplexSplitTree)
{
    DockSystem ds;
    ds.split_right(1);
    ds.split_figure_down(0, 2);
    ds.split_figure_right(2, 3);

    EXPECT_EQ(ds.pane_count(), 4u);

    auto infos = ds.get_pane_infos();
    EXPECT_EQ(infos.size(), 4u);

    // All figures should be present
    std::set<size_t> figures;
    for (const auto& info : infos)
    {
        figures.insert(info.figure_index);
    }
    EXPECT_EQ(figures.count(0), 1u);
    EXPECT_EQ(figures.count(1), 1u);
    EXPECT_EQ(figures.count(2), 1u);
    EXPECT_EQ(figures.count(3), 1u);
}

TEST(DockSystemEdgeCases, SplitWithCustomRatios)
{
    DockSystem ds;
    ds.split_right(1, 0.3f);
    ds.update_layout(Rect{0, 0, 1000, 600});

    auto infos = ds.get_pane_infos();
    EXPECT_EQ(infos.size(), 2u);

    // First pane should be narrower (30%)
    // Find the pane with figure 0
    for (const auto& info : infos)
    {
        if (info.figure_index == 0)
        {
            EXPECT_LT(info.bounds.w, 500.0f);
        }
    }
}

TEST(DockSystemEdgeCases, ActivateAtPointOutsideBounds)
{
    DockSystem ds;
    ds.update_layout(Rect{0, 0, 1000, 600});

    ds.activate_pane_at(-100.0f, -100.0f);
    // Should not crash, active should remain 0
    EXPECT_EQ(ds.active_figure_index(), 0u);
}
