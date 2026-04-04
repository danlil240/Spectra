#include <gtest/gtest.h>
#include <spectra/axes.hpp>

#include <limits>

#include "ui/viewmodel/axes_view_model.hpp"

namespace spectra
{

// ─── Basic Construction ──────────────────────────────────────────────────────

TEST(AxesViewModelTest, DefaultConstruction)
{
    AxesViewModel vm;
    EXPECT_EQ(vm.model(), nullptr);
    EXPECT_FALSE(vm.is_hovered());
    EXPECT_FLOAT_EQ(vm.scroll_y(), 0.0f);
}

TEST(AxesViewModelTest, ConstructWithModel)
{
    Axes          ax;
    AxesViewModel vm(&ax);
    EXPECT_EQ(vm.model(), &ax);
}

// ─── Model Binding ───────────────────────────────────────────────────────────

TEST(AxesViewModelTest, SetModel)
{
    AxesViewModel vm;
    EXPECT_EQ(vm.model(), nullptr);

    Axes ax;
    vm.set_model(&ax);
    EXPECT_EQ(vm.model(), &ax);

    vm.set_model(nullptr);
    EXPECT_EQ(vm.model(), nullptr);
}

// ─── Forwarding Accessors (Phase 1) ──────────────────────────────────────────

TEST(AxesViewModelTest, VisualXLimForwardsToModel)
{
    Axes ax;
    ax.xlim(-5.0, 5.0);

    AxesViewModel vm(&ax);
    AxisLimits    lim = vm.visual_xlim();
    EXPECT_DOUBLE_EQ(lim.min, -5.0);
    EXPECT_DOUBLE_EQ(lim.max, 5.0);
}

TEST(AxesViewModelTest, VisualYLimForwardsToModel)
{
    Axes ax;
    ax.ylim(0.0, 100.0);

    AxesViewModel vm(&ax);
    AxisLimits    lim = vm.visual_ylim();
    EXPECT_DOUBLE_EQ(lim.min, 0.0);
    EXPECT_DOUBLE_EQ(lim.max, 100.0);
}

TEST(AxesViewModelTest, SetVisualXLimUpdatesModel)
{
    Axes ax;
    ax.xlim(0.0, 1.0);

    AxesViewModel vm(&ax);
    vm.set_visual_xlim(-10.0, 10.0);

    // Model should be updated
    EXPECT_DOUBLE_EQ(ax.x_limits().min, -10.0);
    EXPECT_DOUBLE_EQ(ax.x_limits().max, 10.0);

    // ViewModel should reflect the change
    AxisLimits lim = vm.visual_xlim();
    EXPECT_DOUBLE_EQ(lim.min, -10.0);
    EXPECT_DOUBLE_EQ(lim.max, 10.0);
}

TEST(AxesViewModelTest, SetVisualYLimUpdatesModel)
{
    Axes ax;
    ax.ylim(0.0, 1.0);

    AxesViewModel vm(&ax);
    vm.set_visual_ylim(-50.0, 50.0);

    EXPECT_DOUBLE_EQ(ax.y_limits().min, -50.0);
    EXPECT_DOUBLE_EQ(ax.y_limits().max, 50.0);
}

TEST(AxesViewModelTest, VisualLimitsReturnDefaultsWithNoModel)
{
    AxesViewModel vm;
    AxisLimits    xlim = vm.visual_xlim();
    AxisLimits    ylim = vm.visual_ylim();

    EXPECT_DOUBLE_EQ(xlim.min, 0.0);
    EXPECT_DOUBLE_EQ(xlim.max, 1.0);
    EXPECT_DOUBLE_EQ(ylim.min, 0.0);
    EXPECT_DOUBLE_EQ(ylim.max, 1.0);
}

TEST(AxesViewModelTest, SetVisualLimitsNoModelIsNoop)
{
    AxesViewModel vm;
    vm.set_visual_xlim(-1.0, 1.0);   // Should not crash
    vm.set_visual_ylim(-1.0, 1.0);   // Should not crash
}

// ─── Per-View State ──────────────────────────────────────────────────────────

TEST(AxesViewModelTest, HoverState)
{
    AxesViewModel vm;
    EXPECT_FALSE(vm.is_hovered());

    vm.set_is_hovered(true);
    EXPECT_TRUE(vm.is_hovered());

    vm.set_is_hovered(false);
    EXPECT_FALSE(vm.is_hovered());
}

TEST(AxesViewModelTest, ScrollY)
{
    AxesViewModel vm;
    EXPECT_FLOAT_EQ(vm.scroll_y(), 0.0f);

    vm.set_scroll_y(42.5f);
    EXPECT_FLOAT_EQ(vm.scroll_y(), 42.5f);
}

// ─── Change Notification ─────────────────────────────────────────────────────

TEST(AxesViewModelTest, ChangeCallbackOnVisualXLim)
{
    Axes ax;
    ax.xlim(0.0, 1.0);

    AxesViewModel              vm(&ax);
    AxesViewModel::ChangeField last_field{};
    int                        change_count = 0;

    vm.set_on_changed(
        [&](AxesViewModel&, AxesViewModel::ChangeField f)
        {
            last_field = f;
            ++change_count;
        });

    vm.set_visual_xlim(-5.0, 5.0);
    EXPECT_EQ(change_count, 1);
    EXPECT_EQ(last_field, AxesViewModel::ChangeField::VisualXLim);
}

TEST(AxesViewModelTest, ChangeCallbackOnVisualYLim)
{
    Axes ax;
    ax.ylim(0.0, 1.0);

    AxesViewModel              vm(&ax);
    AxesViewModel::ChangeField last_field{};
    int                        change_count = 0;

    vm.set_on_changed(
        [&](AxesViewModel&, AxesViewModel::ChangeField f)
        {
            last_field = f;
            ++change_count;
        });

    vm.set_visual_ylim(-5.0, 5.0);
    EXPECT_EQ(change_count, 1);
    EXPECT_EQ(last_field, AxesViewModel::ChangeField::VisualYLim);
}

TEST(AxesViewModelTest, ChangeCallbackOnHover)
{
    AxesViewModel              vm;
    AxesViewModel::ChangeField last_field{};
    int                        change_count = 0;

    vm.set_on_changed(
        [&](AxesViewModel&, AxesViewModel::ChangeField f)
        {
            last_field = f;
            ++change_count;
        });

    vm.set_is_hovered(true);
    EXPECT_EQ(change_count, 1);
    EXPECT_EQ(last_field, AxesViewModel::ChangeField::IsHovered);
}

TEST(AxesViewModelTest, ChangeCallbackOnScrollY)
{
    AxesViewModel              vm;
    AxesViewModel::ChangeField last_field{};
    int                        change_count = 0;

    vm.set_on_changed(
        [&](AxesViewModel&, AxesViewModel::ChangeField f)
        {
            last_field = f;
            ++change_count;
        });

    vm.set_scroll_y(10.0f);
    EXPECT_EQ(change_count, 1);
    EXPECT_EQ(last_field, AxesViewModel::ChangeField::ScrollY);
}

TEST(AxesViewModelTest, NoCallbackOnSameValue)
{
    Axes ax;
    ax.xlim(0.0, 1.0);

    AxesViewModel vm(&ax);
    int           change_count = 0;

    vm.set_on_changed([&](AxesViewModel&, AxesViewModel::ChangeField) { ++change_count; });

    // Setting the same value should not fire
    vm.set_visual_xlim(0.0, 1.0);
    EXPECT_EQ(change_count, 0);

    vm.set_is_hovered(false);   // already false
    EXPECT_EQ(change_count, 0);

    vm.set_scroll_y(0.0f);   // already 0
    EXPECT_EQ(change_count, 0);
}

// ─── Multiple ViewModels for Same Axes ───────────────────────────────────────

TEST(AxesViewModelTest, MultipleViewModelsShareModel)
{
    Axes ax;
    ax.xlim(0.0, 10.0);

    AxesViewModel vm1(&ax);
    AxesViewModel vm2(&ax);

    // Both should reflect the same model state
    EXPECT_DOUBLE_EQ(vm1.visual_xlim().max, 10.0);
    EXPECT_DOUBLE_EQ(vm2.visual_xlim().max, 10.0);

    // Per-view state is independent
    vm1.set_is_hovered(true);
    vm2.set_is_hovered(false);
    EXPECT_TRUE(vm1.is_hovered());
    EXPECT_FALSE(vm2.is_hovered());

    // Changing limits through one ViewModel affects the model
    vm1.set_visual_xlim(0.0, 20.0);
    EXPECT_DOUBLE_EQ(vm2.visual_xlim().max, 20.0);
}

// ─── Input Validation ────────────────────────────────────────────────────────

TEST(AxesViewModelTest, SetVisualXLimRejectsInvertedRange)
{
    Axes ax;
    ax.xlim(0.0, 1.0);

    AxesViewModel vm(&ax);
    int           change_count = 0;
    vm.set_on_changed([&](AxesViewModel&, AxesViewModel::ChangeField) { ++change_count; });

    // min > max — must be rejected
    vm.set_visual_xlim(5.0, 2.0);
    EXPECT_DOUBLE_EQ(ax.x_limits().min, 0.0);
    EXPECT_DOUBLE_EQ(ax.x_limits().max, 1.0);
    EXPECT_EQ(change_count, 0);
}

TEST(AxesViewModelTest, SetVisualXLimRejectsEqualMinMax)
{
    Axes ax;
    ax.xlim(0.0, 1.0);

    AxesViewModel vm(&ax);
    int           change_count = 0;
    vm.set_on_changed([&](AxesViewModel&, AxesViewModel::ChangeField) { ++change_count; });

    // min == max — degenerate range, must be rejected
    vm.set_visual_xlim(3.0, 3.0);
    EXPECT_DOUBLE_EQ(ax.x_limits().min, 0.0);
    EXPECT_DOUBLE_EQ(ax.x_limits().max, 1.0);
    EXPECT_EQ(change_count, 0);
}

TEST(AxesViewModelTest, SetVisualXLimRejectsNaN)
{
    Axes ax;
    ax.xlim(0.0, 1.0);

    AxesViewModel vm(&ax);
    int           change_count = 0;
    vm.set_on_changed([&](AxesViewModel&, AxesViewModel::ChangeField) { ++change_count; });

    const double nan = std::numeric_limits<double>::quiet_NaN();
    vm.set_visual_xlim(nan, 1.0);
    EXPECT_DOUBLE_EQ(ax.x_limits().min, 0.0);
    EXPECT_EQ(change_count, 0);

    vm.set_visual_xlim(0.0, nan);
    EXPECT_DOUBLE_EQ(ax.x_limits().max, 1.0);
    EXPECT_EQ(change_count, 0);
}

TEST(AxesViewModelTest, SetVisualXLimRejectsInfinity)
{
    Axes ax;
    ax.xlim(0.0, 1.0);

    AxesViewModel vm(&ax);
    int           change_count = 0;
    vm.set_on_changed([&](AxesViewModel&, AxesViewModel::ChangeField) { ++change_count; });

    const double inf = std::numeric_limits<double>::infinity();
    vm.set_visual_xlim(-inf, inf);
    EXPECT_DOUBLE_EQ(ax.x_limits().min, 0.0);
    EXPECT_DOUBLE_EQ(ax.x_limits().max, 1.0);
    EXPECT_EQ(change_count, 0);
}

TEST(AxesViewModelTest, SetVisualYLimRejectsInvertedRange)
{
    Axes ax;
    ax.ylim(0.0, 1.0);

    AxesViewModel vm(&ax);
    int           change_count = 0;
    vm.set_on_changed([&](AxesViewModel&, AxesViewModel::ChangeField) { ++change_count; });

    // min > max — must be rejected
    vm.set_visual_ylim(10.0, -10.0);
    EXPECT_DOUBLE_EQ(ax.y_limits().min, 0.0);
    EXPECT_DOUBLE_EQ(ax.y_limits().max, 1.0);
    EXPECT_EQ(change_count, 0);
}

TEST(AxesViewModelTest, SetVisualYLimRejectsNaN)
{
    Axes ax;
    ax.ylim(0.0, 1.0);

    AxesViewModel vm(&ax);
    int           change_count = 0;
    vm.set_on_changed([&](AxesViewModel&, AxesViewModel::ChangeField) { ++change_count; });

    const double nan = std::numeric_limits<double>::quiet_NaN();
    vm.set_visual_ylim(nan, 5.0);
    EXPECT_DOUBLE_EQ(ax.y_limits().min, 0.0);
    EXPECT_DOUBLE_EQ(ax.y_limits().max, 1.0);
    EXPECT_EQ(change_count, 0);
}

TEST(AxesViewModelTest, SetVisualLimValidRangeIsAccepted)
{
    Axes ax;
    ax.xlim(0.0, 1.0);
    ax.ylim(0.0, 1.0);

    AxesViewModel vm(&ax);
    int           change_count = 0;
    vm.set_on_changed([&](AxesViewModel&, AxesViewModel::ChangeField) { ++change_count; });

    vm.set_visual_xlim(-100.0, 100.0);
    EXPECT_DOUBLE_EQ(ax.x_limits().min, -100.0);
    EXPECT_DOUBLE_EQ(ax.x_limits().max, 100.0);
    EXPECT_EQ(change_count, 1);

    vm.set_visual_ylim(-50.0, 50.0);
    EXPECT_DOUBLE_EQ(ax.y_limits().min, -50.0);
    EXPECT_DOUBLE_EQ(ax.y_limits().max, 50.0);
    EXPECT_EQ(change_count, 2);
}

}   // namespace spectra
