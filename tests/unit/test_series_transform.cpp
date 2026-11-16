#include <chrono>
#include <cmath>
#include <filesystem>
#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <string>
#include <vector>

#include "ui/commands/series_transform.hpp"
#include "ui/commands/undo_manager.hpp"
#include "ui/input/selection_context.hpp"
#include "ui/settings/settings_store.hpp"
#ifdef SPECTRA_USE_IMGUI
    #include "ui/overlay/custom_transform_dialog.hpp"
#endif

using namespace spectra;

namespace
{

const std::string kDot = " \xC2\xB7 ";   // separator between name and suffix

std::vector<float> to_vec(std::span<const float> s)
{
    return {s.begin(), s.end()};
}

LineSeries& add_line(Axes& ax, const std::string& name, std::vector<float> x, std::vector<float> y)
{
    auto& s = ax.line(x, y);
    s.label(name);
    return s;
}

// Two plots: top has "speed" (y = 2x) and "accel" (y = x^2), bottom has "temp".
struct TwoPlots
{
    Figure      fig;
    Axes*       top    = nullptr;
    Axes*       bottom = nullptr;
    LineSeries* speed  = nullptr;
    LineSeries* accel  = nullptr;
    LineSeries* temp   = nullptr;

    TwoPlots()
    {
        top    = &fig.subplot(2, 1, 1);
        bottom = &fig.subplot(2, 1, 2);
        speed  = &add_line(*top, "speed", {0, 1, 2, 3}, {0, 2, 4, 6});
        accel  = &add_line(*top, "accel", {0, 1, 2, 3}, {0, 1, 4, 9});
        temp   = &add_line(*bottom, "temp", {0, 1, 2, 3}, {5, 5, 6, 6});
        top->ylabel("Speed (m/s)");
    }
};

SeriesTransformSpec registry_spec(const std::string& name)
{
    SeriesTransformSpec spec;
    EXPECT_TRUE(make_registry_transform_spec(name, spec));
    return spec;
}

}   // namespace

// ─── Targeting ──────────────────────────────────────────────────────────────

TEST(SeriesTransformTargets, SelectionWins)
{
    TwoPlots             p;
    ui::SelectionContext sel;
    sel.select_series(&p.fig, p.top, 0, p.accel, 1);

    auto t = resolve_transform_targets(sel, &p.fig, p.bottom);
    ASSERT_EQ(t.targets.size(), 1u);
    EXPECT_TRUE(t.from_selection);
    EXPECT_EQ(t.targets[0].series, p.accel);
    EXPECT_EQ(t.targets[0].axes, p.top);
    EXPECT_EQ(describe_transform_targets(t), "Applies to: 1 selected series");

    sel.add_series(&p.fig, p.bottom, p.bottom, 1, p.temp, 0);
    t = resolve_transform_targets(sel, &p.fig, nullptr);
    EXPECT_EQ(t.targets.size(), 2u);
    EXPECT_EQ(describe_transform_targets(t), "Applies to: 2 selected series");
}

TEST(SeriesTransformTargets, NoSelectionUsesVisibleSeriesOfActiveAxesOnly)
{
    TwoPlots p;
    p.accel->visible(false);
    ui::SelectionContext sel;

    auto t = resolve_transform_targets(sel, &p.fig, p.top);
    ASSERT_EQ(t.targets.size(), 1u);   // accel hidden, bottom plot untouched
    EXPECT_FALSE(t.from_selection);
    EXPECT_EQ(t.targets[0].series, p.speed);
    EXPECT_EQ(describe_transform_targets(t), "Applies to: all series in current plot");

    p.bottom->title("Pressure");
    t = resolve_transform_targets(sel, &p.fig, p.bottom);
    ASSERT_EQ(t.targets.size(), 1u);
    EXPECT_EQ(t.targets[0].series, p.temp);
    EXPECT_EQ(t.plot, p.bottom);
    EXPECT_EQ(describe_transform_targets(t), "Applies to: all series in current plot (Pressure)");
}

TEST(SeriesTransformTargets, FallsBackToFirstAxesAndIgnoresForeignSelection)
{
    TwoPlots             p;
    TwoPlots             other;
    ui::SelectionContext sel;
    sel.select_series(&other.fig, other.top, 0, other.speed, 0);

    // Selection of another figure and an active axes of another figure are ignored.
    auto t = resolve_transform_targets(sel, &p.fig, other.bottom);
    EXPECT_FALSE(t.from_selection);
    ASSERT_EQ(t.targets.size(), 2u);
    EXPECT_EQ(t.targets[0].axes, p.top);

    // A selected axes is the current plot.
    sel.select_axes(&p.fig, p.bottom, 1);
    t = resolve_transform_targets(sel, &p.fig, p.top);
    ASSERT_EQ(t.targets.size(), 1u);
    EXPECT_EQ(t.targets[0].series, p.temp);

    EXPECT_TRUE(resolve_transform_targets(sel, nullptr, nullptr).targets.empty());
}

// ─── Copy vs overwrite ──────────────────────────────────────────────────────

TEST(SeriesTransformApply, CopyAddsSeriesNextToSourceAndKeepsSource)
{
    TwoPlots  p;
    const int slot = p.speed->auto_color_index();
    auto      r    = apply_series_transform({{p.top, p.speed}},
                                    registry_spec("Derivative"),
                                    SeriesTransformMode::Copy,
                                    nullptr);
    EXPECT_EQ(r.applied, 1u);
    ASSERT_EQ(r.created.size(), 1u);

    ASSERT_EQ(p.top->series().size(), 3u);
    EXPECT_EQ(p.top->series()[0].get(), p.speed);
    auto* copy = dynamic_cast<LineSeries*>(p.top->series()[1].get());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy, r.created[0]);
    EXPECT_EQ(p.top->series()[2].get(), p.accel);

    EXPECT_EQ(copy->label(), "speed" + kDot + "d/dt");
    EXPECT_EQ(copy->auto_color_index(), slot);          // same colour family...
    EXPECT_EQ(copy->line_style(), LineStyle::Dashed);   // ...but dashed
    for (float v : copy->y_data())
        EXPECT_FLOAT_EQ(v, 2.0f);

    // Source untouched; copy mode never changes the y label.
    EXPECT_EQ(p.speed->label(), "speed");
    EXPECT_EQ(to_vec(p.speed->y_data()), (std::vector<float>{0, 2, 4, 6}));
    EXPECT_EQ(p.top->ylabel(), "Speed (m/s)");
    EXPECT_TRUE(r.relabeled.empty());
}

TEST(SeriesTransformApply, OverwriteReplacesDataAndRenames)
{
    TwoPlots p;
    auto     r = apply_series_transform({{p.top, p.accel}},
                                    registry_spec("Negate"),
                                    SeriesTransformMode::Overwrite,
                                    nullptr);
    EXPECT_EQ(r.applied, 1u);
    EXPECT_TRUE(r.created.empty());
    ASSERT_EQ(p.top->series().size(), 2u);
    EXPECT_EQ(p.accel->label(), "accel" + kDot + "neg");
    EXPECT_EQ(to_vec(p.accel->y_data()), (std::vector<float>{0, -1, -4, -9}));
    EXPECT_EQ(to_vec(p.speed->y_data()), (std::vector<float>{0, 2, 4, 6}));
}

TEST(SeriesTransformApply, UnnamedSeriesGetsPositionalName)
{
    EXPECT_EQ(transformed_series_name("", "FFT", 2), "series 3" + kDot + "FFT");
    EXPECT_EQ(transformed_series_name("speed", "d/dt", 0), "speed" + kDot + "d/dt");
    EXPECT_EQ(transform_suffix("Derivative"), "d/dt");
    EXPECT_EQ(transform_suffix("my_custom"), "my_custom");
}

TEST(SeriesTransformApply, YLabelSuffixOnlyWhenWholeAxesOverwritten)
{
    TwoPlots p;
    // Only one of two series: label stays.
    apply_series_transform({{p.top, p.speed}},
                           registry_spec("Derivative"),
                           SeriesTransformMode::Overwrite,
                           nullptr);
    EXPECT_EQ(p.top->ylabel(), "Speed (m/s)");

    // Unit-preserving transform on the whole axes: label stays.
    apply_series_transform({{p.top, p.speed}, {p.top, p.accel}},
                           registry_spec("Negate"),
                           SeriesTransformMode::Overwrite,
                           nullptr);
    EXPECT_EQ(p.top->ylabel(), "Speed (m/s)");

    // Unit-changing transform on the whole axes: suffix added.
    auto r = apply_series_transform({{p.top, p.speed}, {p.top, p.accel}},
                                    registry_spec("Derivative"),
                                    SeriesTransformMode::Overwrite,
                                    nullptr);
    EXPECT_EQ(p.top->ylabel(), "Speed (m/s)" + kDot + "d/dt");
    ASSERT_EQ(r.relabeled.size(), 1u);
}

TEST(SeriesTransformApply, UsesLogicalYWithOffset)
{
    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);
    auto&  s  = add_line(ax, "p", {0, 1}, {0, 90});
    s.y_offset(10.0);   // logical y = {10, 100}
    apply_series_transform({{&ax, &s}},
                           registry_spec("Log10"),
                           SeriesTransformMode::Overwrite,
                           nullptr);
    EXPECT_DOUBLE_EQ(s.y_offset(), 0.0);
    ASSERT_EQ(s.y_data().size(), 2u);
    EXPECT_NEAR(s.y_data()[0], 1.0f, 1e-5);
    EXPECT_NEAR(s.y_data()[1], 2.0f, 1e-5);
}

TEST(SeriesTransformApply, AutoscalesOnlyWhenRangeChangesALot)
{
    TwoPlots p;
    p.top->xlim(0.0, 3.0);
    p.top->ylim(-1.0, 10.0);

    // Normalize keeps data inside the view but shrinks it to < 1/4: fit.
    auto r = apply_series_transform({{p.top, p.speed}, {p.top, p.accel}},
                                    registry_spec("Normalize"),
                                    SeriesTransformMode::Overwrite,
                                    nullptr);
    ASSERT_EQ(r.rescaled.size(), 1u);

    // Small change inside the current view: no fit, the user's zoom stays.
    p.bottom->xlim(0.0, 3.0);
    p.bottom->ylim(4.0, 7.0);
    r = apply_series_transform({{p.bottom, p.temp}},
                               registry_spec("Abs"),
                               SeriesTransformMode::Overwrite,
                               nullptr);
    EXPECT_TRUE(r.rescaled.empty());
    EXPECT_DOUBLE_EQ(p.bottom->y_limits().max, 7.0);
}

// ─── Undo / redo ────────────────────────────────────────────────────────────

TEST(SeriesTransformUndo, OverwriteUndoRestoresDataNamesLabelAndView)
{
    TwoPlots    p;
    UndoManager undo;
    p.top->xlim(0.0, 3.0);
    p.top->ylim(-1.0, 10.0);

    apply_series_transform({{p.top, p.speed}, {p.top, p.accel}},
                           registry_spec("Derivative"),
                           SeriesTransformMode::Overwrite,
                           &undo);
    ASSERT_EQ(undo.undo_count(), 1u);   // one action for the whole apply
    EXPECT_EQ(undo.undo_description(), "Transform: Derivative");
    EXPECT_EQ(p.speed->y_data().size(), 3u);

    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(to_vec(p.speed->x_data()), (std::vector<float>{0, 1, 2, 3}));
    EXPECT_EQ(to_vec(p.speed->y_data()), (std::vector<float>{0, 2, 4, 6}));
    EXPECT_EQ(to_vec(p.accel->y_data()), (std::vector<float>{0, 1, 4, 9}));
    EXPECT_EQ(p.speed->label(), "speed");
    EXPECT_EQ(p.accel->label(), "accel");
    EXPECT_EQ(p.top->ylabel(), "Speed (m/s)");
    EXPECT_DOUBLE_EQ(p.top->y_limits().min, -1.0);
    EXPECT_DOUBLE_EQ(p.top->y_limits().max, 10.0);

    ASSERT_TRUE(undo.redo());
    EXPECT_EQ(p.speed->label(), "speed" + kDot + "d/dt");
    EXPECT_EQ(p.speed->y_data().size(), 3u);
    for (float v : p.speed->y_data())
        EXPECT_FLOAT_EQ(v, 2.0f);
    EXPECT_EQ(p.top->ylabel(), "Speed (m/s)" + kDot + "d/dt");
}

TEST(SeriesTransformUndo, CopyUndoRemovesCopiesAndRedoRecreatesThem)
{
    TwoPlots    p;
    UndoManager undo;
    apply_series_transform({{p.top, p.speed}, {p.top, p.accel}},
                           registry_spec("Derivative"),
                           SeriesTransformMode::Copy,
                           &undo);
    ASSERT_EQ(p.top->series().size(), 4u);
    EXPECT_EQ(p.top->series()[1]->label(), "speed" + kDot + "d/dt");
    EXPECT_EQ(p.top->series()[3]->label(), "accel" + kDot + "d/dt");
    ASSERT_EQ(undo.undo_count(), 1u);

    ASSERT_TRUE(undo.undo());
    ASSERT_EQ(p.top->series().size(), 2u);
    EXPECT_EQ(p.top->series()[0].get(), p.speed);
    EXPECT_EQ(p.top->series()[1].get(), p.accel);
    EXPECT_EQ(to_vec(p.speed->y_data()), (std::vector<float>{0, 2, 4, 6}));

    ASSERT_TRUE(undo.redo());
    ASSERT_EQ(p.top->series().size(), 4u);
    EXPECT_EQ(p.top->series()[1]->label(), "speed" + kDot + "d/dt");
    EXPECT_EQ(p.top->series()[3]->label(), "accel" + kDot + "d/dt");

    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(p.top->series().size(), 2u);
}

TEST(SeriesTransformUndo, CopyUndoUsesRemovalHook)
{
    TwoPlots                                   p;
    UndoManager                                undo;
    std::vector<std::pair<AxesBase*, Series*>> removed;
    SeriesTransformHooks                       hooks;
    hooks.remove_series = [&](AxesBase* ax, Series* s) { removed.emplace_back(ax, s); };

    auto r = apply_series_transform({{p.bottom, p.temp}},
                                    registry_spec("Abs"),
                                    SeriesTransformMode::Copy,
                                    &undo,
                                    hooks);
    ASSERT_EQ(r.created.size(), 1u);
    ASSERT_TRUE(undo.undo());
    ASSERT_EQ(removed.size(), 1u);
    EXPECT_EQ(removed[0].first, p.bottom);
    EXPECT_EQ(removed[0].second, r.created[0]);
}

TEST(SeriesTransformUndo, SkipsSeriesRemovedSinceApply)
{
    TwoPlots    p;
    UndoManager undo;
    apply_series_transform({{p.top, p.accel}},
                           registry_spec("Negate"),
                           SeriesTransformMode::Overwrite,
                           &undo);
    p.top->remove_series(1);    // accel deleted afterwards
    EXPECT_TRUE(undo.undo());   // must not touch the dead pointer
    EXPECT_TRUE(undo.redo());
    EXPECT_EQ(p.top->series().size(), 1u);
}

TEST(SeriesTransformApply, NothingToDoPushesNoUndo)
{
    TwoPlots    p;
    UndoManager undo;
    auto r = apply_series_transform({}, registry_spec("Abs"), SeriesTransformMode::Copy, &undo);
    EXPECT_EQ(r.applied, 0u);
    EXPECT_EQ(undo.undo_count(), 0u);
    SeriesTransformSpec unknown;
    EXPECT_FALSE(make_registry_transform_spec("no-such-transform", unknown));
}

// ─── Persisted mode ─────────────────────────────────────────────────────────

TEST(SeriesTransformSetting, DefaultsToCopyAndRoundTrips)
{
    using ui::settings::SettingsStore;
    EXPECT_FALSE(ui::settings::SettingsData{}.transform_overwrite);

    const auto path =
        std::filesystem::temp_directory_path()
        / ("spectra_transform_setting_"
           + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
    {
        SettingsStore store;
        store.data_mut().transform_overwrite = true;
        ASSERT_TRUE(store.save(path.string()));
    }
    SettingsStore loaded;
    ASSERT_TRUE(loaded.load(path.string()));
    EXPECT_TRUE(loaded.data().transform_overwrite);
    std::filesystem::remove(path);
}

// ─── Custom formula dialog ──────────────────────────────────────────────────

#ifdef SPECTRA_USE_IMGUI
TEST(SeriesTransformDialog, FormulaAppliesToPreselectedSeriesWithUndo)
{
    TwoPlots                  p;
    UndoManager               undo;
    ui::CustomTransformDialog dlg;
    bool                      overwrite = false;
    dlg.set_undo_manager(&undo);
    dlg.set_overwrite_binding([&]() { return overwrite; }, [&](bool v) { overwrite = v; });
    dlg.open(&p.fig);

    ui::SelectionContext sel;
    sel.select_series(&p.fig, p.bottom, 1, p.temp, 0);
    dlg.preselect(resolve_transform_targets(sel, &p.fig, nullptr));
    auto targets = dlg.current_targets();
    ASSERT_EQ(targets.size(), 1u);
    EXPECT_EQ(targets[0].series, p.temp);

    dlg.set_formula("y * 2");
    ASSERT_TRUE(dlg.formula_valid());
    ASSERT_TRUE(dlg.apply());   // copy mode
    ASSERT_EQ(p.bottom->series().size(), 2u);
    EXPECT_EQ(p.bottom->series()[1]->label(), "temp" + kDot + "y * 2");
    EXPECT_EQ(to_vec(p.temp->y_data()), (std::vector<float>{5, 5, 6, 6}));
    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(p.bottom->series().size(), 1u);

    overwrite = true;
    ASSERT_TRUE(dlg.apply());
    EXPECT_EQ(to_vec(p.temp->y_data()), (std::vector<float>{10, 10, 12, 12}));
    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(to_vec(p.temp->y_data()), (std::vector<float>{5, 5, 6, 6}));
    EXPECT_EQ(p.temp->label(), "temp");
}

TEST(SeriesTransformDialog, PreselectAxesWhenNoSelection)
{
    TwoPlots                  p;
    ui::CustomTransformDialog dlg;
    dlg.open(&p.fig);
    ui::SelectionContext sel;
    dlg.preselect(resolve_transform_targets(sel, &p.fig, p.bottom));
    auto targets = dlg.current_targets();
    ASSERT_EQ(targets.size(), 1u);
    EXPECT_EQ(targets[0].series, p.temp);
}
#endif
