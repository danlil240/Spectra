#include <gtest/gtest.h>
#include <spectra/series.hpp>

#include "ui/viewmodel/series_view_model.hpp"

namespace spectra
{

// ─── Basic Construction ──────────────────────────────────────────────────────

TEST(SeriesViewModelTest, DefaultConstruction)
{
    SeriesViewModel vm;
    EXPECT_EQ(vm.model(), nullptr);
    EXPECT_FALSE(vm.is_selected());
    EXPECT_FALSE(vm.is_highlighted());
    EXPECT_FALSE(vm.has_visible_override());
    EXPECT_FALSE(vm.has_color_override());
    EXPECT_FALSE(vm.has_label_override());
    EXPECT_FALSE(vm.has_opacity_override());
}

TEST(SeriesViewModelTest, ConstructWithModel)
{
    LineSeries      s;
    SeriesViewModel vm(&s);
    EXPECT_EQ(vm.model(), &s);
}

// ─── Model Binding ───────────────────────────────────────────────────────────

TEST(SeriesViewModelTest, SetModel)
{
    SeriesViewModel vm;
    EXPECT_EQ(vm.model(), nullptr);

    LineSeries s;
    vm.set_model(&s);
    EXPECT_EQ(vm.model(), &s);

    vm.set_model(nullptr);
    EXPECT_EQ(vm.model(), nullptr);
}

// ─── Effective Accessors (forwarding to model) ──────────────────────────────

TEST(SeriesViewModelTest, EffectiveVisibleForwardsToModel)
{
    LineSeries s;
    s.visible(true);

    SeriesViewModel vm(&s);
    EXPECT_TRUE(vm.effective_visible());

    s.visible(false);
    EXPECT_FALSE(vm.effective_visible());
}

TEST(SeriesViewModelTest, EffectiveColorForwardsToModel)
{
    LineSeries s;
    s.color(colors::red);

    SeriesViewModel vm(&s);
    Color           c = vm.effective_color();
    EXPECT_FLOAT_EQ(c.r, colors::red.r);
    EXPECT_FLOAT_EQ(c.g, colors::red.g);
    EXPECT_FLOAT_EQ(c.b, colors::red.b);
}

TEST(SeriesViewModelTest, EffectiveLabelForwardsToModel)
{
    LineSeries s;
    s.label("Sensor A");

    SeriesViewModel vm(&s);
    EXPECT_EQ(vm.effective_label(), "Sensor A");
}

TEST(SeriesViewModelTest, EffectiveOpacityForwardsToModel)
{
    LineSeries s;
    s.opacity(0.5f);

    SeriesViewModel vm(&s);
    EXPECT_FLOAT_EQ(vm.effective_opacity(), 0.5f);
}

TEST(SeriesViewModelTest, EffectiveDefaultsWithNoModel)
{
    SeriesViewModel vm;
    EXPECT_TRUE(vm.effective_visible());
    EXPECT_FLOAT_EQ(vm.effective_opacity(), 1.0f);
    EXPECT_TRUE(vm.effective_label().empty());
}

// ─── Override Semantics ──────────────────────────────────────────────────────

TEST(SeriesViewModelTest, VisibleOverrideTakesPrecedence)
{
    LineSeries s;
    s.visible(true);

    SeriesViewModel vm(&s);
    EXPECT_TRUE(vm.effective_visible());

    // Override hides the series in this view
    vm.set_visible_override(false);
    EXPECT_TRUE(vm.has_visible_override());
    EXPECT_FALSE(vm.effective_visible());

    // Model is unchanged
    EXPECT_TRUE(s.visible());

    // Clear override restores model value
    vm.clear_visible_override();
    EXPECT_FALSE(vm.has_visible_override());
    EXPECT_TRUE(vm.effective_visible());
}

TEST(SeriesViewModelTest, ColorOverrideTakesPrecedence)
{
    LineSeries s;
    s.color(colors::blue);

    SeriesViewModel vm(&s);
    vm.set_color_override(colors::green);
    EXPECT_TRUE(vm.has_color_override());

    Color c = vm.effective_color();
    EXPECT_FLOAT_EQ(c.r, colors::green.r);
    EXPECT_FLOAT_EQ(c.g, colors::green.g);
    EXPECT_FLOAT_EQ(c.b, colors::green.b);

    // Model unchanged
    EXPECT_FLOAT_EQ(s.color().r, colors::blue.r);

    vm.clear_color_override();
    EXPECT_FALSE(vm.has_color_override());
    EXPECT_FLOAT_EQ(vm.effective_color().r, colors::blue.r);
}

TEST(SeriesViewModelTest, LabelOverrideTakesPrecedence)
{
    LineSeries s;
    s.label("Original");

    SeriesViewModel vm(&s);
    vm.set_label_override("Custom Name");
    EXPECT_TRUE(vm.has_label_override());
    EXPECT_EQ(vm.effective_label(), "Custom Name");

    // Model unchanged
    EXPECT_EQ(s.label(), "Original");

    vm.clear_label_override();
    EXPECT_EQ(vm.effective_label(), "Original");
}

TEST(SeriesViewModelTest, OpacityOverrideTakesPrecedence)
{
    LineSeries s;
    s.opacity(1.0f);

    SeriesViewModel vm(&s);
    vm.set_opacity_override(0.3f);
    EXPECT_TRUE(vm.has_opacity_override());
    EXPECT_FLOAT_EQ(vm.effective_opacity(), 0.3f);

    vm.clear_opacity_override();
    EXPECT_FLOAT_EQ(vm.effective_opacity(), 1.0f);
}

// ─── Direct Forwarding Setters ───────────────────────────────────────────────

TEST(SeriesViewModelTest, SetVisibleModifiesModel)
{
    LineSeries s;
    s.visible(true);

    SeriesViewModel vm(&s);
    vm.set_visible(false);
    EXPECT_FALSE(s.visible());
}

TEST(SeriesViewModelTest, SetColorModifiesModel)
{
    LineSeries s;
    s.color(colors::blue);

    SeriesViewModel vm(&s);
    vm.set_color(colors::red);
    EXPECT_FLOAT_EQ(s.color().r, colors::red.r);
}

TEST(SeriesViewModelTest, SetLabelModifiesModel)
{
    LineSeries s;
    s.label("Before");

    SeriesViewModel vm(&s);
    vm.set_label("After");
    EXPECT_EQ(s.label(), "After");
}

TEST(SeriesViewModelTest, SetOpacityModifiesModel)
{
    LineSeries s;
    s.opacity(1.0f);

    SeriesViewModel vm(&s);
    vm.set_opacity(0.7f);
    EXPECT_FLOAT_EQ(s.opacity(), 0.7f);
}

TEST(SeriesViewModelTest, ForwardingSettersNoModelIsNoop)
{
    SeriesViewModel vm;
    vm.set_visible(false);   // Should not crash
    vm.set_color(colors::red);
    vm.set_label("test");
    vm.set_opacity(0.5f);
}

// ─── Selection / Highlight ───────────────────────────────────────────────────

TEST(SeriesViewModelTest, SelectionState)
{
    SeriesViewModel vm;
    EXPECT_FALSE(vm.is_selected());

    vm.set_is_selected(true);
    EXPECT_TRUE(vm.is_selected());

    vm.set_is_selected(false);
    EXPECT_FALSE(vm.is_selected());
}

TEST(SeriesViewModelTest, HighlightState)
{
    SeriesViewModel vm;
    EXPECT_FALSE(vm.is_highlighted());

    vm.set_is_highlighted(true);
    EXPECT_TRUE(vm.is_highlighted());

    vm.set_is_highlighted(false);
    EXPECT_FALSE(vm.is_highlighted());
}

// ─── Change Notification ─────────────────────────────────────────────────────

TEST(SeriesViewModelTest, ChangeCallbackOnVisibleOverride)
{
    SeriesViewModel              vm;
    SeriesViewModel::ChangeField last_field{};
    int                          change_count = 0;

    vm.set_on_changed(
        [&](SeriesViewModel&, SeriesViewModel::ChangeField f)
        {
            last_field = f;
            ++change_count;
        });

    vm.set_visible_override(true);
    EXPECT_EQ(change_count, 1);
    EXPECT_EQ(last_field, SeriesViewModel::ChangeField::Visible);
}

TEST(SeriesViewModelTest, ChangeCallbackOnClearOverride)
{
    SeriesViewModel vm;
    vm.set_visible_override(true);

    int change_count = 0;
    vm.set_on_changed([&](SeriesViewModel&, SeriesViewModel::ChangeField) { ++change_count; });

    vm.clear_visible_override();
    EXPECT_EQ(change_count, 1);
}

TEST(SeriesViewModelTest, ChangeCallbackOnModelForwarding)
{
    LineSeries s;
    s.visible(true);

    SeriesViewModel              vm(&s);
    SeriesViewModel::ChangeField last_field{};
    int                          change_count = 0;

    vm.set_on_changed(
        [&](SeriesViewModel&, SeriesViewModel::ChangeField f)
        {
            last_field = f;
            ++change_count;
        });

    vm.set_visible(false);
    EXPECT_EQ(change_count, 1);
    EXPECT_EQ(last_field, SeriesViewModel::ChangeField::Visible);
}

TEST(SeriesViewModelTest, ChangeCallbackOnSelection)
{
    SeriesViewModel              vm;
    SeriesViewModel::ChangeField last_field{};
    int                          change_count = 0;

    vm.set_on_changed(
        [&](SeriesViewModel&, SeriesViewModel::ChangeField f)
        {
            last_field = f;
            ++change_count;
        });

    vm.set_is_selected(true);
    EXPECT_EQ(change_count, 1);
    EXPECT_EQ(last_field, SeriesViewModel::ChangeField::IsSelected);
}

TEST(SeriesViewModelTest, ChangeCallbackOnHighlight)
{
    SeriesViewModel              vm;
    SeriesViewModel::ChangeField last_field{};
    int                          change_count = 0;

    vm.set_on_changed(
        [&](SeriesViewModel&, SeriesViewModel::ChangeField f)
        {
            last_field = f;
            ++change_count;
        });

    vm.set_is_highlighted(true);
    EXPECT_EQ(change_count, 1);
    EXPECT_EQ(last_field, SeriesViewModel::ChangeField::IsHighlighted);
}

TEST(SeriesViewModelTest, NoCallbackOnSameValue)
{
    LineSeries s;
    s.visible(true);

    SeriesViewModel vm(&s);
    int             change_count = 0;

    vm.set_on_changed([&](SeriesViewModel&, SeriesViewModel::ChangeField) { ++change_count; });

    vm.set_visible(true);   // Same as model
    EXPECT_EQ(change_count, 0);

    vm.set_is_selected(false);   // Already false
    EXPECT_EQ(change_count, 0);

    vm.set_is_highlighted(false);   // Already false
    EXPECT_EQ(change_count, 0);
}

// ─── Multiple ViewModels for Same Series (per-window) ────────────────────────

TEST(SeriesViewModelTest, PerWindowVisibility)
{
    LineSeries s;
    s.visible(true);

    SeriesViewModel vm1(&s);   // Window 1
    SeriesViewModel vm2(&s);   // Window 2

    // Window 1 hides the series, window 2 keeps it visible
    vm1.set_visible_override(false);

    EXPECT_FALSE(vm1.effective_visible());
    EXPECT_TRUE(vm2.effective_visible());

    // Model is unchanged
    EXPECT_TRUE(s.visible());
}

TEST(SeriesViewModelTest, PerWindowLabel)
{
    LineSeries s;
    s.label("Shared");

    SeriesViewModel vm1(&s);
    SeriesViewModel vm2(&s);

    vm1.set_label_override("Window 1 Label");

    EXPECT_EQ(vm1.effective_label(), "Window 1 Label");
    EXPECT_EQ(vm2.effective_label(), "Shared");
}

TEST(SeriesViewModelTest, IndependentSelectionPerView)
{
    LineSeries s;

    SeriesViewModel vm1(&s);
    SeriesViewModel vm2(&s);

    vm1.set_is_selected(true);
    vm2.set_is_selected(false);

    EXPECT_TRUE(vm1.is_selected());
    EXPECT_FALSE(vm2.is_selected());
}

}   // namespace spectra
