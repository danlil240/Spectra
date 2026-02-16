#include <cstdio>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

#include "ui/workspace.hpp"

using namespace spectra;

// ─── WorkspaceData defaults ──────────────────────────────────────────────────

TEST(WorkspaceData, DefaultValues)
{
    WorkspaceData data;
    EXPECT_EQ(data.version, WorkspaceData::FORMAT_VERSION);
    EXPECT_EQ(data.theme_name, "dark");
    EXPECT_EQ(data.active_figure_index, 0u);
    EXPECT_TRUE(data.panels.inspector_visible);
    EXPECT_FLOAT_EQ(data.panels.inspector_width, 320.0f);
    EXPECT_FALSE(data.panels.nav_rail_expanded);
    EXPECT_TRUE(data.figures.empty());
}

// ─── Serialization round-trip ────────────────────────────────────────────────

class WorkspaceTest : public ::testing::Test
{
   protected:
    std::string tmp_path;

    void SetUp() override
    {
        tmp_path =
            (std::filesystem::temp_directory_path() / "plotix_test_workspace.spectra").string();
    }

    void TearDown() override { std::remove(tmp_path.c_str()); }

    WorkspaceData make_sample_data()
    {
        WorkspaceData data;
        data.theme_name = "light";
        data.active_figure_index = 1;
        data.panels.inspector_visible = false;
        data.panels.inspector_width = 400.0f;
        data.panels.nav_rail_expanded = true;

        WorkspaceData::FigureState fig;
        fig.title = "Test Figure";
        fig.width = 1920;
        fig.height = 1080;
        fig.grid_rows = 2;
        fig.grid_cols = 3;

        WorkspaceData::AxisState ax;
        ax.x_min = -5.0f;
        ax.x_max = 5.0f;
        ax.y_min = -1.0f;
        ax.y_max = 1.0f;
        ax.auto_fit = false;
        ax.grid_visible = true;
        ax.x_label = "Time (s)";
        ax.y_label = "Amplitude";
        ax.title = "Signal";
        fig.axes.push_back(ax);

        WorkspaceData::SeriesState ser;
        ser.name = "sin(x)";
        ser.type = "line";
        ser.color_r = 0.3f;
        ser.color_g = 0.5f;
        ser.color_b = 0.8f;
        ser.color_a = 1.0f;
        ser.line_width = 3.0f;
        ser.visible = true;
        ser.point_count = 1000;
        fig.series.push_back(ser);

        data.figures.push_back(fig);
        return data;
    }
};

TEST_F(WorkspaceTest, SaveAndLoadRoundTrip)
{
    auto original = make_sample_data();
    ASSERT_TRUE(Workspace::save(tmp_path, original));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(tmp_path, loaded));

    EXPECT_EQ(loaded.version, original.version);
    EXPECT_EQ(loaded.theme_name, "light");
    EXPECT_EQ(loaded.active_figure_index, 1u);
    EXPECT_FALSE(loaded.panels.inspector_visible);
    EXPECT_FLOAT_EQ(loaded.panels.inspector_width, 400.0f);
    EXPECT_TRUE(loaded.panels.nav_rail_expanded);

    ASSERT_EQ(loaded.figures.size(), 1u);
    const auto& fig = loaded.figures[0];
    EXPECT_EQ(fig.title, "Test Figure");
    EXPECT_EQ(fig.width, 1920u);
    EXPECT_EQ(fig.height, 1080u);
    EXPECT_EQ(fig.grid_rows, 2);
    EXPECT_EQ(fig.grid_cols, 3);

    ASSERT_EQ(fig.axes.size(), 1u);
    const auto& ax = fig.axes[0];
    EXPECT_FLOAT_EQ(ax.x_min, -5.0f);
    EXPECT_FLOAT_EQ(ax.x_max, 5.0f);
    EXPECT_FLOAT_EQ(ax.y_min, -1.0f);
    EXPECT_FLOAT_EQ(ax.y_max, 1.0f);
    EXPECT_FALSE(ax.auto_fit);
    EXPECT_TRUE(ax.grid_visible);
    EXPECT_EQ(ax.x_label, "Time (s)");
    EXPECT_EQ(ax.y_label, "Amplitude");
    EXPECT_EQ(ax.title, "Signal");

    ASSERT_EQ(fig.series.size(), 1u);
    const auto& ser = fig.series[0];
    EXPECT_EQ(ser.name, "sin(x)");
    EXPECT_EQ(ser.type, "line");
    EXPECT_NEAR(ser.color_r, 0.3f, 0.01f);
    EXPECT_NEAR(ser.color_g, 0.5f, 0.01f);
    EXPECT_NEAR(ser.color_b, 0.8f, 0.01f);
    EXPECT_TRUE(ser.visible);
}

TEST_F(WorkspaceTest, SaveCreatesFile)
{
    auto data = make_sample_data();
    ASSERT_TRUE(Workspace::save(tmp_path, data));
    EXPECT_TRUE(std::filesystem::exists(tmp_path));
    EXPECT_GT(std::filesystem::file_size(tmp_path), 0u);
}

TEST_F(WorkspaceTest, LoadNonExistentReturnsFalse)
{
    WorkspaceData data;
    EXPECT_FALSE(Workspace::load("/nonexistent/path/workspace.spectra", data));
}

TEST_F(WorkspaceTest, LoadEmptyFileReturnsFalse)
{
    std::ofstream(tmp_path).close();  // Create empty file
    WorkspaceData data;
    EXPECT_FALSE(Workspace::load(tmp_path, data));
}

TEST_F(WorkspaceTest, SaveToInvalidPathReturnsFalse)
{
    auto data = make_sample_data();
    EXPECT_FALSE(Workspace::save("/nonexistent/dir/workspace.spectra", data));
}

TEST_F(WorkspaceTest, MultipleFigures)
{
    WorkspaceData data;
    data.figures.resize(3);
    data.figures[0].title = "Fig A";
    data.figures[1].title = "Fig B";
    data.figures[2].title = "Fig C";

    ASSERT_TRUE(Workspace::save(tmp_path, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(tmp_path, loaded));
    ASSERT_EQ(loaded.figures.size(), 3u);
    EXPECT_EQ(loaded.figures[0].title, "Fig A");
    EXPECT_EQ(loaded.figures[1].title, "Fig B");
    EXPECT_EQ(loaded.figures[2].title, "Fig C");
}

TEST_F(WorkspaceTest, EmptyFiguresArray)
{
    WorkspaceData data;
    // No figures
    ASSERT_TRUE(Workspace::save(tmp_path, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(tmp_path, loaded));
    EXPECT_TRUE(loaded.figures.empty());
}

TEST_F(WorkspaceTest, SpecialCharactersInStrings)
{
    WorkspaceData data;
    WorkspaceData::FigureState fig;
    fig.title = "Test \"quoted\" figure";

    WorkspaceData::AxisState ax;
    ax.x_label = "Time\\n(seconds)";
    fig.axes.push_back(ax);
    data.figures.push_back(fig);

    ASSERT_TRUE(Workspace::save(tmp_path, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(tmp_path, loaded));
    ASSERT_EQ(loaded.figures.size(), 1u);
    // JSON escaping should preserve the content
    EXPECT_FALSE(loaded.figures[0].title.empty());
}

// ─── Paths ───────────────────────────────────────────────────────────────────

TEST(WorkspacePaths, DefaultPathNotEmpty)
{
    std::string path = Workspace::default_path();
    EXPECT_FALSE(path.empty());
    EXPECT_NE(path.find("spectra"), std::string::npos);
}

TEST(WorkspacePaths, AutosavePathNotEmpty)
{
    std::string path = Workspace::autosave_path();
    EXPECT_FALSE(path.empty());
    EXPECT_NE(path.find("spectra"), std::string::npos);
}

// ─── JSON format ─────────────────────────────────────────────────────────────

TEST_F(WorkspaceTest, OutputIsValidJson)
{
    auto data = make_sample_data();
    ASSERT_TRUE(Workspace::save(tmp_path, data));

    std::ifstream file(tmp_path);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    // Basic JSON structure checks
    EXPECT_EQ(content.front(), '{');
    EXPECT_NE(content.find("\"version\""), std::string::npos);
    EXPECT_NE(content.find("\"figures\""), std::string::npos);
    EXPECT_NE(content.find("\"theme_name\""), std::string::npos);
}
