#include <gtest/gtest.h>
#include <spectra/figure.hpp>

#include "ui/commands/undo_manager.hpp"
#include "ui/figures/figure_manager.hpp"
#include "ui/viewmodel/figure_view_model.hpp"

namespace spectra
{

// ─── Basic Construction ──────────────────────────────────────────────────────

TEST(FigureViewModelTest, DefaultConstruction)
{
    FigureViewModel vm;
    EXPECT_EQ(vm.figure_id(), INVALID_FIGURE_ID);
    EXPECT_EQ(vm.model(), nullptr);
    EXPECT_TRUE(vm.axes_snapshots().empty());
    EXPECT_EQ(vm.selected_series_index(), -1);
    EXPECT_EQ(vm.selected_axes_index(), -1);
    EXPECT_FLOAT_EQ(vm.inspector_scroll_y(), 0.0f);
    EXPECT_FALSE(vm.is_modified());
    EXPECT_TRUE(vm.custom_title().empty());
}

TEST(FigureViewModelTest, ConstructWithIdAndModel)
{
    Figure fig;
    FigureViewModel vm(42, &fig);
    EXPECT_EQ(vm.figure_id(), 42u);
    EXPECT_EQ(vm.model(), &fig);
}

// ─── Model Binding ───────────────────────────────────────────────────────────

TEST(FigureViewModelTest, SetModel)
{
    FigureViewModel vm;
    EXPECT_EQ(vm.model(), nullptr);

    Figure fig;
    vm.set_model(&fig);
    EXPECT_EQ(vm.model(), &fig);

    vm.set_model(nullptr);
    EXPECT_EQ(vm.model(), nullptr);
}

// ─── Save / Restore Axes State ───────────────────────────────────────────────

TEST(FigureViewModelTest, SaveAxesStateSnapshotsLimits)
{
    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);
    ax.xlim(-5.0, 5.0);
    ax.ylim(0.0, 100.0);

    FigureViewModel vm(1, &fig);
    vm.save_axes_state();

    ASSERT_EQ(vm.axes_snapshots().size(), 1u);
    EXPECT_DOUBLE_EQ(vm.axes_snapshots()[0].x_limits.min, -5.0);
    EXPECT_DOUBLE_EQ(vm.axes_snapshots()[0].x_limits.max, 5.0);
    EXPECT_DOUBLE_EQ(vm.axes_snapshots()[0].y_limits.min, 0.0);
    EXPECT_DOUBLE_EQ(vm.axes_snapshots()[0].y_limits.max, 100.0);
}

TEST(FigureViewModelTest, RestoreAxesStateAppliesLimits)
{
    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);
    ax.xlim(-5.0, 5.0);
    ax.ylim(0.0, 100.0);

    FigureViewModel vm(1, &fig);
    vm.save_axes_state();

    // Change limits on the model
    ax.xlim(0.0, 1.0);
    ax.ylim(-1.0, 1.0);
    EXPECT_DOUBLE_EQ(ax.x_limits().min, 0.0);

    // Restore should bring back the original limits
    vm.restore_axes_state();
    EXPECT_DOUBLE_EQ(ax.x_limits().min, -5.0);
    EXPECT_DOUBLE_EQ(ax.x_limits().max, 5.0);
    EXPECT_DOUBLE_EQ(ax.y_limits().min, 0.0);
    EXPECT_DOUBLE_EQ(ax.y_limits().max, 100.0);
}

TEST(FigureViewModelTest, SaveWithNoModelIsNoop)
{
    FigureViewModel vm;
    vm.save_axes_state();
    EXPECT_TRUE(vm.axes_snapshots().empty());
}

TEST(FigureViewModelTest, RestoreWithNoModelIsNoop)
{
    FigureViewModel vm;
    FigureViewModel::AxesSnapshot snap;
    snap.x_limits = {-10.0, 10.0};
    snap.y_limits = {-10.0, 10.0};
    vm.axes_snapshots_mut().push_back(snap);

    // Should not crash
    vm.restore_axes_state();
}

// ─── Multiple Axes ───────────────────────────────────────────────────────────

TEST(FigureViewModelTest, SaveRestoreMultipleAxes)
{
    Figure fig;
    auto&  ax1 = fig.subplot(2, 1, 1);
    auto&  ax2 = fig.subplot(2, 1, 2);
    ax1.xlim(0.0, 10.0);
    ax1.ylim(0.0, 20.0);
    ax2.xlim(-1.0, 1.0);
    ax2.ylim(-2.0, 2.0);

    FigureViewModel vm(1, &fig);
    vm.save_axes_state();

    ASSERT_EQ(vm.axes_snapshots().size(), 2u);

    // Mutate
    ax1.xlim(100.0, 200.0);
    ax2.xlim(100.0, 200.0);

    // Restore
    vm.restore_axes_state();
    EXPECT_DOUBLE_EQ(ax1.x_limits().min, 0.0);
    EXPECT_DOUBLE_EQ(ax2.x_limits().min, -1.0);
}

// ─── Backward Compatibility (FigureState alias) ─────────────────────────────

TEST(FigureViewModelTest, FigureStateAliasWorks)
{
    // FigureState is a type alias for FigureViewModel
    FigureState st;
    st.set_custom_title("Test Figure");
    st.set_is_modified(true);
    st.set_selected_series_index(3);

    // Should compile and work as FigureViewModel
    FigureViewModel& vm_ref = st;
    EXPECT_EQ(vm_ref.custom_title(), "Test Figure");
    EXPECT_TRUE(vm_ref.is_modified());
    EXPECT_EQ(vm_ref.selected_series_index(), 3);
}

// ─── Change Notification (Phase 3) ──────────────────────────────────────────

TEST(FigureViewModelTest, ChangeCallbackFiresOnSetters)
{
    FigureViewModel vm;
    int call_count = 0;
    FigureViewModel::ChangeField last_field{};
    vm.set_on_changed(
        [&](FigureViewModel&, FigureViewModel::ChangeField f)
        {
            ++call_count;
            last_field = f;
        });

    vm.set_selected_series_index(5);
    EXPECT_EQ(call_count, 1);
    EXPECT_EQ(last_field, FigureViewModel::ChangeField::SelectedSeriesIndex);

    vm.set_selected_axes_index(2);
    EXPECT_EQ(call_count, 2);
    EXPECT_EQ(last_field, FigureViewModel::ChangeField::SelectedAxesIndex);

    vm.set_inspector_scroll_y(42.0f);
    EXPECT_EQ(call_count, 3);
    EXPECT_EQ(last_field, FigureViewModel::ChangeField::InspectorScrollY);

    vm.set_is_modified(true);
    EXPECT_EQ(call_count, 4);
    EXPECT_EQ(last_field, FigureViewModel::ChangeField::IsModified);

    vm.set_custom_title("Hello");
    EXPECT_EQ(call_count, 5);
    EXPECT_EQ(last_field, FigureViewModel::ChangeField::CustomTitle);

    vm.set_is_in_3d_mode(true);
    EXPECT_EQ(call_count, 6);
    EXPECT_EQ(last_field, FigureViewModel::ChangeField::IsIn3DMode);
}

TEST(FigureViewModelTest, NoCallbackOnSameValue)
{
    FigureViewModel vm;
    int call_count = 0;
    vm.set_on_changed([&](FigureViewModel&, FigureViewModel::ChangeField) { ++call_count; });

    vm.set_selected_series_index(-1);   // default is -1 — no change
    EXPECT_EQ(call_count, 0);

    vm.set_is_modified(false);   // default is false
    EXPECT_EQ(call_count, 0);

    vm.set_custom_title("");   // default is empty
    EXPECT_EQ(call_count, 0);
}

TEST(FigureViewModelTest, ZoomCacheAccessors)
{
    FigureViewModel vm;
    EXPECT_FALSE(vm.zoom_cache_valid());

    vm.set_zoom_cache(-100.0f, 100.0f, 5);
    EXPECT_TRUE(vm.zoom_cache_valid());
    EXPECT_FLOAT_EQ(vm.cached_data_min(), -100.0f);
    EXPECT_FLOAT_EQ(vm.cached_data_max(), 100.0f);
    EXPECT_EQ(vm.cached_zoom_series_count(), 5u);

    vm.invalidate_zoom_cache();
    EXPECT_FALSE(vm.zoom_cache_valid());
}

TEST(FigureViewModelTest, HomeLimitAccessor)
{
    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);
    ax.xlim(0.0, 10.0);
    ax.ylim(-5.0, 5.0);

    FigureViewModel vm(1, &fig);
    Axes*           ax_ptr = fig.axes()[0].get();
    vm.set_home_limit(ax_ptr, {ax_ptr->x_limits(), ax_ptr->y_limits()});

    ASSERT_EQ(vm.home_limits().size(), 1u);
    auto it = vm.home_limits().find(ax_ptr);
    ASSERT_NE(it, vm.home_limits().end());
    EXPECT_FLOAT_EQ(it->second.x.min, 0.0f);
    EXPECT_FLOAT_EQ(it->second.x.max, 10.0f);
    EXPECT_FLOAT_EQ(it->second.y.min, -5.0f);
    EXPECT_FLOAT_EQ(it->second.y.max, 5.0f);
}

// ─── Per-Figure View State ───────────────────────────────────────────────────

TEST(FigureViewModelTest, DefaultPerFigureViewState)
{
    FigureViewModel vm;
    EXPECT_FALSE(vm.is_in_3d_mode());
    EXPECT_TRUE(vm.home_limits().empty());
    EXPECT_FLOAT_EQ(vm.cached_data_min(), 0.0f);
    EXPECT_FLOAT_EQ(vm.cached_data_max(), 0.0f);
    EXPECT_EQ(vm.cached_zoom_series_count(), 0u);
    EXPECT_FALSE(vm.zoom_cache_valid());
}

TEST(FigureViewModelTest, ThreeDModePerFigure)
{
    FigureViewModel vm1(1, nullptr);
    FigureViewModel vm2(2, nullptr);

    vm1.set_is_in_3d_mode(true);
    EXPECT_TRUE(vm1.is_in_3d_mode());
    EXPECT_FALSE(vm2.is_in_3d_mode());   // Independent

    vm2.set_is_in_3d_mode(true);
    vm1.set_is_in_3d_mode(false);
    EXPECT_FALSE(vm1.is_in_3d_mode());
    EXPECT_TRUE(vm2.is_in_3d_mode());    // Still independent
}

TEST(FigureViewModelTest, ModelPointerWiredOnConstruction)
{
    Figure fig;
    FigureViewModel vm(42, &fig);

    EXPECT_EQ(vm.figure_id(), 42u);
    EXPECT_EQ(vm.model(), &fig);

    // set_figure_id / set_model update independently
    vm.set_figure_id(99);
    EXPECT_EQ(vm.figure_id(), 99u);
    EXPECT_EQ(vm.model(), &fig);

    Figure fig2;
    vm.set_model(&fig2);
    EXPECT_EQ(vm.model(), &fig2);
    EXPECT_EQ(vm.figure_id(), 99u);
}

// ─── Undo Integration (Phase 3) ─────────────────────────────────────────────

TEST(FigureViewModelTest, UndoCustomTitle)
{
    UndoManager undo;
    FigureViewModel vm(1, nullptr);
    vm.set_undo_manager(&undo);

    vm.set_custom_title("Original");
    EXPECT_EQ(vm.custom_title(), "Original");
    EXPECT_TRUE(undo.can_undo());

    vm.set_custom_title("Changed");
    EXPECT_EQ(vm.custom_title(), "Changed");

    undo.undo();
    EXPECT_EQ(vm.custom_title(), "Original");

    undo.redo();
    EXPECT_EQ(vm.custom_title(), "Changed");
}

TEST(FigureViewModelTest, Undo3DMode)
{
    UndoManager undo;
    FigureViewModel vm(1, nullptr);
    vm.set_undo_manager(&undo);

    EXPECT_FALSE(vm.is_in_3d_mode());

    vm.set_is_in_3d_mode(true);
    EXPECT_TRUE(vm.is_in_3d_mode());
    EXPECT_TRUE(undo.can_undo());

    undo.undo();
    EXPECT_FALSE(vm.is_in_3d_mode());

    undo.redo();
    EXPECT_TRUE(vm.is_in_3d_mode());
}

TEST(FigureViewModelTest, UndoSeriesSelection)
{
    UndoManager undo;
    FigureViewModel vm(1, nullptr);
    vm.set_undo_manager(&undo);

    vm.set_selected_series_index(3);
    EXPECT_EQ(vm.selected_series_index(), 3);

    vm.set_selected_series_index(7);
    EXPECT_EQ(vm.selected_series_index(), 7);

    undo.undo();
    EXPECT_EQ(vm.selected_series_index(), 3);

    undo.redo();
    EXPECT_EQ(vm.selected_series_index(), 7);
}

TEST(FigureViewModelTest, NoUndoWithoutManager)
{
    FigureViewModel vm(1, nullptr);
    EXPECT_EQ(vm.undo_manager(), nullptr);

    // Setters should still work without undo manager
    vm.set_custom_title("Test");
    EXPECT_EQ(vm.custom_title(), "Test");

    vm.set_is_in_3d_mode(true);
    EXPECT_TRUE(vm.is_in_3d_mode());
}

}   // namespace spectra
