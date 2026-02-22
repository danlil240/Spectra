#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

#include "ui/workspace/workspace.hpp"

using namespace spectra;

// ─── Test fixture ────────────────────────────────────────────────────────────

class WorkspaceV2Test : public ::testing::Test
{
   protected:
    std::string tmp_path;

    void SetUp() override
    {
        tmp_path = (std::filesystem::temp_directory_path() / "spectra_test_ws_v2.spectra").string();
    }

    void TearDown() override
    {
        std::remove(tmp_path.c_str());
        // Clean up autosave if created
        try
        {
            std::filesystem::remove(Workspace::autosave_path());
        }
        catch (...)
        {
        }
    }

    WorkspaceData make_v2_data()
    {
        WorkspaceData data;
        data.theme_name               = "dark";
        data.active_figure_index      = 0;
        data.panels.inspector_visible = true;
        data.panels.inspector_width   = 350.0f;
        data.panels.nav_rail_expanded = true;

        // Interaction state
        data.interaction.crosshair_enabled = true;
        data.interaction.tooltip_enabled   = false;
        WorkspaceData::InteractionState::MarkerEntry m;
        m.data_x       = 3.14f;
        m.data_y       = 2.71f;
        m.series_label = "sin(x)";
        m.point_index  = 42;
        data.interaction.markers.push_back(m);

        // Figure with v2 fields
        WorkspaceData::FigureState fig;
        fig.title            = "Test Figure";
        fig.width            = 1920;
        fig.height           = 1080;
        fig.grid_rows        = 1;
        fig.grid_cols        = 1;
        fig.is_modified      = true;
        fig.custom_tab_title = "My Custom Tab";

        WorkspaceData::AxisState ax;
        ax.x_min        = -10.0f;
        ax.x_max        = 10.0f;
        ax.y_min        = -1.0f;
        ax.y_max        = 1.0f;
        ax.auto_fit     = false;
        ax.grid_visible = false;
        ax.x_label      = "Time (s)";
        ax.y_label      = "Amplitude";
        ax.title        = "Signal Plot";
        fig.axes.push_back(ax);

        WorkspaceData::SeriesState ser;
        ser.name        = "sin(x)";
        ser.type        = "line";
        ser.color_r     = 0.2f;
        ser.color_g     = 0.6f;
        ser.color_b     = 0.9f;
        ser.color_a     = 1.0f;
        ser.line_width  = 3.0f;
        ser.visible     = false;
        ser.point_count = 500;
        ser.opacity     = 0.15f;
        fig.series.push_back(ser);

        data.figures.push_back(fig);

        // Undo metadata
        data.undo_count = 5;
        data.redo_count = 2;

        return data;
    }
};

// ─── V2 format round-trip ───────────────────────────────────────────────────

TEST_F(WorkspaceV2Test, V2RoundTrip)
{
    auto original = make_v2_data();
    ASSERT_TRUE(Workspace::save(tmp_path, original));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(tmp_path, loaded));

    EXPECT_EQ(loaded.version, WorkspaceData::FORMAT_VERSION);
    EXPECT_EQ(loaded.theme_name, "dark");
    EXPECT_EQ(loaded.active_figure_index, 0u);

    // Panels
    EXPECT_TRUE(loaded.panels.inspector_visible);
    EXPECT_FLOAT_EQ(loaded.panels.inspector_width, 350.0f);
    EXPECT_TRUE(loaded.panels.nav_rail_expanded);

    // Interaction state
    EXPECT_TRUE(loaded.interaction.crosshair_enabled);
    EXPECT_FALSE(loaded.interaction.tooltip_enabled);
    ASSERT_EQ(loaded.interaction.markers.size(), 1u);
    EXPECT_NEAR(loaded.interaction.markers[0].data_x, 3.14f, 0.01f);
    EXPECT_NEAR(loaded.interaction.markers[0].data_y, 2.71f, 0.01f);
    EXPECT_EQ(loaded.interaction.markers[0].series_label, "sin(x)");
    EXPECT_EQ(loaded.interaction.markers[0].point_index, 42u);

    // Figure v2 fields
    ASSERT_EQ(loaded.figures.size(), 1u);
    EXPECT_TRUE(loaded.figures[0].is_modified);
    EXPECT_EQ(loaded.figures[0].custom_tab_title, "My Custom Tab");

    // Series opacity
    ASSERT_EQ(loaded.figures[0].series.size(), 1u);
    EXPECT_NEAR(loaded.figures[0].series[0].opacity, 0.15f, 0.01f);
    EXPECT_FALSE(loaded.figures[0].series[0].visible);

    // Undo metadata
    EXPECT_EQ(loaded.undo_count, 5u);
    EXPECT_EQ(loaded.redo_count, 2u);
}

// ─── V1 backward compatibility ──────────────────────────────────────────────

TEST_F(WorkspaceV2Test, V1FileLoadsWithDefaults)
{
    // Write a v1-style JSON manually
    std::string v1_json = R"({
  "version": 1,
  "theme_name": "light",
  "active_figure_index": 0,
  "panels": {
    "inspector_visible": true,
    "inspector_width": 320,
    "nav_rail_expanded": false
  },
  "figures": [
    {
      "title": "Old Figure",
      "width": 1280,
      "height": 720,
      "grid_rows": 1,
      "grid_cols": 1,
      "axes": [],
      "series": []
    }
  ]
})";
    std::ofstream(tmp_path) << v1_json;

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(tmp_path, loaded));

    EXPECT_EQ(loaded.version, 1u);
    EXPECT_EQ(loaded.theme_name, "light");

    // v2 fields should have defaults
    ASSERT_EQ(loaded.figures.size(), 1u);
    EXPECT_FALSE(loaded.figures[0].is_modified);
    EXPECT_TRUE(loaded.figures[0].custom_tab_title.empty());

    // Interaction defaults
    EXPECT_FALSE(loaded.interaction.crosshair_enabled);
    EXPECT_TRUE(loaded.interaction.tooltip_enabled);
    EXPECT_TRUE(loaded.interaction.markers.empty());

    EXPECT_EQ(loaded.undo_count, 0u);
    EXPECT_EQ(loaded.redo_count, 0u);
}

// ─── Multiple figures with tab titles ───────────────────────────────────────

TEST_F(WorkspaceV2Test, MultipleFiguresWithTabTitles)
{
    WorkspaceData data;
    data.figures.resize(3);
    data.figures[0].title            = "Fig A";
    data.figures[0].custom_tab_title = "Analysis";
    data.figures[0].is_modified      = true;
    data.figures[1].title            = "Fig B";
    data.figures[1].custom_tab_title = "Comparison";
    data.figures[1].is_modified      = false;
    data.figures[2].title            = "Fig C";
    data.figures[2].custom_tab_title = "";
    data.figures[2].is_modified      = false;
    data.active_figure_index         = 1;

    ASSERT_TRUE(Workspace::save(tmp_path, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(tmp_path, loaded));

    ASSERT_EQ(loaded.figures.size(), 3u);
    EXPECT_EQ(loaded.figures[0].custom_tab_title, "Analysis");
    EXPECT_TRUE(loaded.figures[0].is_modified);
    EXPECT_EQ(loaded.figures[1].custom_tab_title, "Comparison");
    EXPECT_FALSE(loaded.figures[1].is_modified);
    EXPECT_TRUE(loaded.figures[2].custom_tab_title.empty());
    EXPECT_EQ(loaded.active_figure_index, 1u);
}

// ─── Multiple markers ───────────────────────────────────────────────────────

TEST_F(WorkspaceV2Test, MultipleMarkers)
{
    WorkspaceData data;
    for (int i = 0; i < 5; ++i)
    {
        WorkspaceData::InteractionState::MarkerEntry m;
        m.data_x       = static_cast<float>(i) * 1.5f;
        m.data_y       = static_cast<float>(i) * 0.5f;
        m.series_label = "series_" + std::to_string(i);
        m.point_index  = static_cast<size_t>(i * 10);
        data.interaction.markers.push_back(m);
    }

    ASSERT_TRUE(Workspace::save(tmp_path, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(tmp_path, loaded));

    ASSERT_EQ(loaded.interaction.markers.size(), 5u);
    for (int i = 0; i < 5; ++i)
    {
        EXPECT_NEAR(loaded.interaction.markers[i].data_x, i * 1.5f, 0.01f);
        EXPECT_EQ(loaded.interaction.markers[i].series_label, "series_" + std::to_string(i));
        EXPECT_EQ(loaded.interaction.markers[i].point_index, static_cast<size_t>(i * 10));
    }
}

// ─── Series visibility round-trip ───────────────────────────────────────────

TEST_F(WorkspaceV2Test, SeriesVisibilityRoundTrip)
{
    WorkspaceData              data;
    WorkspaceData::FigureState fig;
    fig.title = "Visibility Test";

    WorkspaceData::SeriesState s1;
    s1.name    = "visible_series";
    s1.visible = true;
    s1.opacity = 1.0f;
    fig.series.push_back(s1);

    WorkspaceData::SeriesState s2;
    s2.name    = "hidden_series";
    s2.visible = false;
    s2.opacity = 0.15f;
    fig.series.push_back(s2);

    data.figures.push_back(fig);

    ASSERT_TRUE(Workspace::save(tmp_path, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(tmp_path, loaded));

    ASSERT_EQ(loaded.figures[0].series.size(), 2u);
    EXPECT_TRUE(loaded.figures[0].series[0].visible);
    EXPECT_NEAR(loaded.figures[0].series[0].opacity, 1.0f, 0.01f);
    EXPECT_FALSE(loaded.figures[0].series[1].visible);
    EXPECT_NEAR(loaded.figures[0].series[1].opacity, 0.15f, 0.01f);
}

// ─── Grid visibility in axes ────────────────────────────────────────────────

TEST_F(WorkspaceV2Test, GridVisibilityRoundTrip)
{
    WorkspaceData              data;
    WorkspaceData::FigureState fig;

    WorkspaceData::AxisState ax1;
    ax1.grid_visible = true;
    fig.axes.push_back(ax1);

    WorkspaceData::AxisState ax2;
    ax2.grid_visible = false;
    fig.axes.push_back(ax2);

    data.figures.push_back(fig);

    ASSERT_TRUE(Workspace::save(tmp_path, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(tmp_path, loaded));

    ASSERT_EQ(loaded.figures[0].axes.size(), 2u);
    EXPECT_TRUE(loaded.figures[0].axes[0].grid_visible);
    EXPECT_FALSE(loaded.figures[0].axes[1].grid_visible);
}

// ─── Autosave ───────────────────────────────────────────────────────────────

TEST_F(WorkspaceV2Test, AutosavePathNotEmpty)
{
    std::string path = Workspace::autosave_path();
    EXPECT_FALSE(path.empty());
    EXPECT_NE(path.find("spectra"), std::string::npos);
}

TEST_F(WorkspaceV2Test, MaybeAutosaveWritesFile)
{
    // Clean up first
    Workspace::clear_autosave();
    EXPECT_FALSE(Workspace::has_autosave());

    WorkspaceData data;
    data.theme_name = "dark";

    // First call with 0 interval should always save
    bool saved = Workspace::maybe_autosave(data, 0.0f);
    EXPECT_TRUE(saved);
    EXPECT_TRUE(Workspace::has_autosave());

    // Verify the file is valid
    WorkspaceData loaded;
    EXPECT_TRUE(Workspace::load(Workspace::autosave_path(), loaded));
    EXPECT_EQ(loaded.theme_name, "dark");

    Workspace::clear_autosave();
    EXPECT_FALSE(Workspace::has_autosave());
}

TEST_F(WorkspaceV2Test, MaybeAutosaveRespectsInterval)
{
    Workspace::clear_autosave();
    WorkspaceData data;

    // Save with 0 interval (always)
    EXPECT_TRUE(Workspace::maybe_autosave(data, 0.0f));

    // Immediately try again with large interval — should skip
    bool saved = Workspace::maybe_autosave(data, 9999.0f);
    EXPECT_FALSE(saved);

    Workspace::clear_autosave();
}

TEST_F(WorkspaceV2Test, ClearAutosaveNoError)
{
    // Should not throw even if file doesn't exist
    Workspace::clear_autosave();
    Workspace::clear_autosave();   // Double clear
}

// ─── Version rejection ──────────────────────────────────────────────────────

TEST_F(WorkspaceV2Test, FutureVersionRejected)
{
    std::string future_json = R"({
  "version": 999,
  "theme_name": "dark",
  "figures": []
})";
    std::ofstream(tmp_path) << future_json;

    WorkspaceData loaded;
    EXPECT_FALSE(Workspace::load(tmp_path, loaded));
}

// ─── Empty interaction state ────────────────────────────────────────────────

TEST_F(WorkspaceV2Test, EmptyInteractionState)
{
    WorkspaceData data;
    // Default interaction state — no markers, crosshair off, tooltip on
    ASSERT_TRUE(Workspace::save(tmp_path, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(tmp_path, loaded));

    EXPECT_FALSE(loaded.interaction.crosshair_enabled);
    EXPECT_TRUE(loaded.interaction.tooltip_enabled);
    EXPECT_TRUE(loaded.interaction.markers.empty());
}

// ─── Special characters in tab titles ───────────────────────────────────────

TEST_F(WorkspaceV2Test, SpecialCharsInTabTitle)
{
    WorkspaceData              data;
    WorkspaceData::FigureState fig;
    fig.custom_tab_title = "Test \"quoted\" tab\nwith newline";
    data.figures.push_back(fig);

    ASSERT_TRUE(Workspace::save(tmp_path, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(tmp_path, loaded));

    ASSERT_EQ(loaded.figures.size(), 1u);
    // JSON escaping should preserve content (at least non-empty)
    EXPECT_FALSE(loaded.figures[0].custom_tab_title.empty());
}

// ─── Undo metadata round-trip ───────────────────────────────────────────────

TEST_F(WorkspaceV2Test, UndoMetadataRoundTrip)
{
    WorkspaceData data;
    data.undo_count = 42;
    data.redo_count = 7;

    ASSERT_TRUE(Workspace::save(tmp_path, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(tmp_path, loaded));

    EXPECT_EQ(loaded.undo_count, 42u);
    EXPECT_EQ(loaded.redo_count, 7u);
}

// ─── Large workspace ────────────────────────────────────────────────────────

TEST_F(WorkspaceV2Test, LargeWorkspace)
{
    WorkspaceData data;
    data.active_figure_index = 5;

    for (int fi = 0; fi < 10; ++fi)
    {
        WorkspaceData::FigureState fig;
        fig.title            = "Figure " + std::to_string(fi);
        fig.custom_tab_title = "Tab " + std::to_string(fi);
        fig.is_modified      = (fi % 2 == 0);

        for (int ai = 0; ai < 4; ++ai)
        {
            WorkspaceData::AxisState ax;
            ax.x_min        = static_cast<float>(fi * 10 + ai);
            ax.x_max        = ax.x_min + 10.0f;
            ax.grid_visible = (ai % 2 == 0);
            fig.axes.push_back(ax);
        }

        for (int si = 0; si < 3; ++si)
        {
            WorkspaceData::SeriesState ser;
            ser.name    = "Series " + std::to_string(fi) + "." + std::to_string(si);
            ser.visible = (si != 1);
            ser.opacity = (si == 1) ? 0.15f : 1.0f;
            fig.series.push_back(ser);
        }

        data.figures.push_back(fig);
    }

    ASSERT_TRUE(Workspace::save(tmp_path, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(tmp_path, loaded));

    ASSERT_EQ(loaded.figures.size(), 10u);
    EXPECT_EQ(loaded.active_figure_index, 5u);

    for (int fi = 0; fi < 10; ++fi)
    {
        EXPECT_EQ(loaded.figures[fi].custom_tab_title, "Tab " + std::to_string(fi));
        EXPECT_EQ(loaded.figures[fi].is_modified, (fi % 2 == 0));
        ASSERT_EQ(loaded.figures[fi].axes.size(), 4u);
        ASSERT_EQ(loaded.figures[fi].series.size(), 3u);

        for (int ai = 0; ai < 4; ++ai)
        {
            EXPECT_NEAR(loaded.figures[fi].axes[ai].x_min, static_cast<float>(fi * 10 + ai), 0.01f);
            EXPECT_EQ(loaded.figures[fi].axes[ai].grid_visible, (ai % 2 == 0));
        }

        for (int si = 0; si < 3; ++si)
        {
            EXPECT_EQ(loaded.figures[fi].series[si].visible, (si != 1));
        }
    }
}
