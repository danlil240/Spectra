// State the multiproc window agent (spectra-window) takes from settings and
// from daemon diffs: saved theme / data palette and series colours (P0-10).

#include <gtest/gtest.h>

#include <vector>

#include "agent/live_series_index.hpp"
#include "daemon/figure_model.hpp"
#include "ipc/codec.hpp"
#include "ipc/message.hpp"
#include "ui/settings/settings_store.hpp"
#include "ui/theme/theme.hpp"

using spectra::ui::ThemeManager;
using spectra::ui::settings::SettingsStore;

// ─── Saved settings on a fresh ThemeManager (as in spectra-window) ──────────

TEST(WindowAgentSettings, ApplyToFreshThemeManagerAppliesSavedTheme)
{
    for (const char* theme : {"light", "high_contrast", "dark"})
    {
        ThemeManager tm;   // no themes registered yet
        ThemeManager::set_current(&tm);

        SettingsStore store;
        store.data_mut().default_theme        = theme;
        store.data_mut().default_data_palette = "colorblind";
        store.apply_to(tm);

        EXPECT_EQ(tm.current_theme_name(), theme);
        EXPECT_EQ(tm.current_data_palette_name(), "colorblind");
        // The lazy first-use initialisation must not reset it to "night".
        EXPECT_EQ(ThemeManager::instance().current_theme_name(), theme);
        EXPECT_EQ(tm.current().name, theme);

        ThemeManager::set_current(nullptr);
    }
}

// ─── Figure-wide series index → live (axes, position) ───────────────────────

TEST(WindowAgentSeries, LocateLiveSeriesCountsWithinItsAxes)
{
    spectra::ipc::SnapshotFigureState fig;
    fig.axes.resize(2);   // series only map to axes the window builds
    fig.series.resize(4);
    fig.series[0].axes_index = 0;
    fig.series[1].axes_index = 1;
    fig.series[2].axes_index = 0;
    fig.series[3].axes_index = 1;

    spectra::agent::LiveSeriesIndex where;
    ASSERT_TRUE(spectra::agent::locate_live_series(fig, 2, where));
    EXPECT_EQ(where.axes_index, 0u);
    EXPECT_EQ(where.series_index, 1u);

    ASSERT_TRUE(spectra::agent::locate_live_series(fig, 3, where));
    EXPECT_EQ(where.axes_index, 1u);
    EXPECT_EQ(where.series_index, 1u);

    EXPECT_FALSE(spectra::agent::locate_live_series(fig, 4, where));
}

TEST(WindowAgentSeries, StyleOpsAreSeriesPropertyOps)
{
    using T = spectra::ipc::DiffOp::Type;
    for (T t : {T::SET_SERIES_COLOR,
                T::SET_SERIES_VISIBLE,
                T::SET_LINE_WIDTH,
                T::SET_MARKER_SIZE,
                T::SET_OPACITY,
                T::SET_SERIES_DATA,
                T::SET_SERIES_LABEL})
    {
        EXPECT_TRUE(spectra::agent::is_series_property_op(t)) << static_cast<int>(t);
    }
    for (T t : {T::ADD_SERIES, T::REMOVE_SERIES, T::SET_AXIS_LIMITS, T::ADD_AXES})
    {
        EXPECT_FALSE(spectra::agent::is_series_property_op(t)) << static_cast<int>(t);
    }
}

// ─── Explicit colour from Python reaches the window unchanged ───────────────

TEST(WindowAgentSeries, ExplicitColorDiffRoundTrips)
{
    // python/spectra/_easy.py: color="blue" -> (0.2, 0.4, 1.0, 1.0)
    spectra::daemon::FigureModel model;
    auto                         fid = model.create_figure("f");
    model.add_axes(fid);
    uint32_t i0 = 0;
    uint32_t i1 = 0;
    model.add_series_with_diff(fid, "a", "line", 0, i0);
    model.add_series_with_diff(fid, "b", "line", 0, i1);

    spectra::ipc::StateDiffPayload diff;
    diff.ops.push_back(model.set_series_color(fid, i1, 0.2f, 0.4f, 1.0f, 1.0f));

    auto dec = spectra::ipc::decode_state_diff(spectra::ipc::encode_state_diff(diff));
    ASSERT_TRUE(dec.has_value());
    ASSERT_EQ(dec->ops.size(), 1u);
    const auto& op = dec->ops[0];
    EXPECT_EQ(op.type, spectra::ipc::DiffOp::Type::SET_SERIES_COLOR);
    EXPECT_EQ(op.series_index, i1);
    EXPECT_FLOAT_EQ(static_cast<float>(op.f1), 0.2f);
    EXPECT_FLOAT_EQ(static_cast<float>(op.f2), 0.4f);
    EXPECT_FLOAT_EQ(static_cast<float>(op.f3), 1.0f);
    EXPECT_FLOAT_EQ(static_cast<float>(op.f4), 1.0f);

    // The snapshot a late-joining window receives carries it as explicit.
    auto snap = model.snapshot();
    ASSERT_EQ(snap.figures.size(), 1u);
    const auto& s = snap.figures[0].series[i1];
    EXPECT_EQ(s.color_index, -1);
    EXPECT_FLOAT_EQ(s.color_b, 1.0f);
    EXPECT_EQ(snap.figures[0].series[i0].color_index, 0);
}
