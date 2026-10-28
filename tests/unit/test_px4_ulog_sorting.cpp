// test_px4_ulog_sorting.cpp — PX4 viewer topic / parameter ordering, driven
// through the real ULogFilePanel and Dear ImGui on the committed ULog fixture
// tests/data/ulog/px4_fixture.ulg (regenerate with make_px4_fixture.py).
//
// The Parameters table is click-to-sort: the test clicks its column headers
// with synthesized mouse events (no display or GPU needed) and asserts the
// row order the panel draws after each click.

#include <gtest/gtest.h>

#include "px4_plot_manager.hpp"
#include "ui/ulog_file_panel.hpp"
#include "ulog_reader.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef SPECTRA_SOURCE_DIR
    #define SPECTRA_SOURCE_DIR "."
#endif

namespace fs = std::filesystem;
using namespace spectra::adapters::px4;

namespace
{

std::string fixture_path()
{
    return std::string(SPECTRA_SOURCE_DIR) + "/tests/data/ulog/px4_fixture.ulg";
}

// Parameter names in the fixture's file order (see make_px4_fixture.py).
const std::vector<std::string> kFileOrder = {"MC_ROLL_P",
                                             "SYS_AUTOSTART",
                                             "BAT_LOW_THR",
                                             "COM_ARM_WO_GPS",
                                             "MPC_XY_VEL_MAX",
                                             "SYS_HAS_MAG",
                                             "MC_PITCH_P",
                                             "EKF2_GPS_DELAY"};

std::vector<std::string> names_in(const ULogReader& reader, const std::vector<size_t>& order)
{
    std::vector<std::string> out;
    for (size_t i : order)
        out.push_back(i < reader.parameters().size() ? reader.parameters()[i].key : "<bad>");
    return out;
}

// Headless Dear ImGui driver for one ULogFilePanel.
class PanelDriver
{
   public:
    explicit PanelDriver(ULogFilePanel& panel) : panel_(panel)
    {
        ctx_           = ImGui::CreateContext();
        ImGuiIO& io    = ImGui::GetIO();
        io.DisplaySize = ImVec2(900.0f, 700.0f);
        io.DeltaTime   = 1.0f / 60.0f;
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.Fonts->AddFontDefault();
        unsigned char* px = nullptr;
        int            w = 0, h = 0;
        io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
        frame();
        frame();
    }
    ~PanelDriver() { ImGui::DestroyContext(ctx_); }

    PanelDriver(const PanelDriver&)            = delete;
    PanelDriver& operator=(const PanelDriver&) = delete;

    void frame(ImVec2 mouse = ImVec2(-1.0f, -1.0f), bool down = false)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(mouse.x, mouse.y);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
        ImGui::NewFrame();
        // Dock the panel into a host dockspace, as spectra-px4's layout does:
        // a floating panel is treated as torn off and moved to an OS window.
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(880.0f, 680.0f), ImGuiCond_Always);
        ImGui::Begin(
            "Px4TestHost",
            nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        const ImGuiID dockspace = ImGui::GetID("Px4TestDockspace");
        ImGui::DockSpace(dockspace);
        ImGui::End();
        ImGui::SetNextWindowDockID(dockspace, ImGuiCond_Always);
        panel_.draw();
        ImGui::Render();
    }

    // Press and release the left button at `p`, then settle one frame.
    void click(ImVec2 p)
    {
        frame(p, false);
        frame(p, true);
        frame(p, false);
        frame();
    }

    // Centre of the tab named `label` in the panel's tab bar (NaN if absent).
    ImVec2 tab_center(const char* label) const
    {
        ImGuiContext& g = *GImGui;
        for (int i = 0; i < g.TabBars.GetMapSize(); ++i)
        {
            ImGuiTabBar* bar = g.TabBars.TryGetMapData(i);
            if (!bar)
                continue;
            for (ImGuiTabItem& tab : bar->Tabs)
            {
                if (std::strcmp(ImGui::TabBarGetTabName(bar, &tab), label) != 0)
                    continue;
                const float x = bar->BarRect.Min.x + tab.Offset - bar->ScrollingAnim;
                return ImVec2(x + tab.Width * 0.5f,
                              (bar->BarRect.Min.y + bar->BarRect.Max.y) * 0.5f);
            }
        }
        return ImVec2(NAN, NAN);
    }

    // The sortable table drawn in the last frame (the Parameters table).
    ImGuiTable* sortable_table() const
    {
        ImGuiContext& g = *GImGui;
        for (int i = 0; i < g.Tables.GetMapSize(); ++i)
        {
            ImGuiTable* t = g.Tables.TryGetMapData(i);
            if (t && (t->Flags & ImGuiTableFlags_Sortable) && t->LastFrameActive == g.FrameCount)
                return t;
        }
        return nullptr;
    }

    // Centre of column `col`'s header cell in the sortable table.
    ImVec2 header_center(int col) const
    {
        ImGuiTable* t = sortable_table();
        if (!t || col >= t->ColumnsCount)
            return ImVec2(NAN, NAN);
        const ImGuiTableColumn& c = t->Columns[col];
        return ImVec2((c.MinX + c.MaxX) * 0.5f, t->OuterRect.Min.y + ImGui::GetFontSize() * 0.6f);
    }

    // Current (column, ascending) sort state of the sortable table.
    bool sort_state(int& column, bool& ascending) const
    {
        ImGuiTable* t = sortable_table();
        if (!t)
            return false;
        for (int n = 0; n < t->ColumnsCount; ++n)
        {
            const ImGuiTableColumn& c = t->Columns[n];
            if (c.SortOrder == 0)
            {
                column    = n;
                ascending = c.SortDirection == ImGuiSortDirection_Ascending;
                return true;
            }
        }
        return false;
    }

    void open_parameters_tab()
    {
        ASSERT_TRUE(panel_.detach_controller().is_docked()) << "panel left the dockspace";
        const ImVec2 p = tab_center("Parameters");
        ASSERT_FALSE(std::isnan(p.x)) << "Parameters tab not found";
        click(p);
        ASSERT_NE(sortable_table(), nullptr)
            << "Parameters table not drawn after selecting its tab";
    }

   private:
    ULogFilePanel& panel_;
    ImGuiContext*  ctx_ = nullptr;
};

class Px4UlogSortingTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        ASSERT_TRUE(fs::exists(fixture_path())) << fixture_path();
        ASSERT_TRUE(reader_.open(fixture_path())) << reader_.last_error();
        plot_mgr_.load_ulog(reader_);
    }

    ULogReader     reader_;
    Px4PlotManager plot_mgr_;
};

}   // namespace

// The fixture is a real ULog the reader parses completely.
TEST_F(Px4UlogSortingTest, FixtureParsesWithExpectedContent)
{
    ASSERT_EQ(reader_.parameters().size(), kFileOrder.size());
    std::vector<std::string> keys;
    for (const auto& p : reader_.parameters())
        keys.push_back(p.key);
    EXPECT_EQ(keys, kFileOrder) << "parameters() keeps the file order";

    EXPECT_EQ(reader_.topic_count(), 3u);
    EXPECT_EQ(reader_.info_string("sys_name"), "PX4");
    EXPECT_EQ(reader_.log_messages().size(), 3u);

    const auto* bat = reader_.data_for("battery_status");
    ASSERT_NE(bat, nullptr);
    ASSERT_EQ(bat->rows.size(), 10u);
    auto [t, volts] = bat->extract_field("voltage_v");
    ASSERT_EQ(volts.size(), 10u);
    ASSERT_EQ(t.size(), 10u);
    EXPECT_FLOAT_EQ(volts.front(), 16.0f);
    EXPECT_NEAR(volts.back(), 15.1f, 1e-5f);
    EXPECT_NEAR(t.back() - t.front(), 0.9f, 1e-5f);   // 10 Hz
}

// Topic tree: topics alphabetical, fields in format order, arrays expanded.
TEST_F(Px4UlogSortingTest, TopicTreeOrder)
{
    EXPECT_EQ(plot_mgr_.available_topics(),
              (std::vector<std::string>{"battery_status", "sensor_combined", "vehicle_attitude"}));
    EXPECT_EQ(plot_mgr_.topic_fields("vehicle_attitude"),
              (std::vector<std::string>{"timestamp", "q[0]", "q[1]", "q[2]", "q[3]"}));
    EXPECT_EQ(plot_mgr_.topic_fields("sensor_combined"),
              (std::vector<std::string>{"timestamp", "gyro_rad[0]", "gyro_rad[1]", "gyro_rad[2]"}));
}

// Clicking the Parameters headers re-sorts the rows the panel draws.
TEST_F(Px4UlogSortingTest, ClickToSortParametersTable)
{
    ULogFilePanel panel(reader_, plot_mgr_);
    PanelDriver   ui(panel);
    ui.open_parameters_tab();
    if (HasFatalFailure())
        return;

    int  col = -1;
    bool asc = false;

    // Default: Name ascending.
    ASSERT_TRUE(ui.sort_state(col, asc));
    EXPECT_EQ(col, 0);
    EXPECT_TRUE(asc);
    EXPECT_EQ(names_in(reader_, panel.parameter_row_order()),
              (std::vector<std::string>{"BAT_LOW_THR",
                                        "COM_ARM_WO_GPS",
                                        "EKF2_GPS_DELAY",
                                        "MC_PITCH_P",
                                        "MC_ROLL_P",
                                        "MPC_XY_VEL_MAX",
                                        "SYS_AUTOSTART",
                                        "SYS_HAS_MAG"}));

    // Name again: descending.
    ui.click(ui.header_center(0));
    ASSERT_TRUE(ui.sort_state(col, asc));
    EXPECT_EQ(col, 0);
    EXPECT_FALSE(asc);
    EXPECT_EQ(names_in(reader_, panel.parameter_row_order()),
              (std::vector<std::string>{"SYS_HAS_MAG",
                                        "SYS_AUTOSTART",
                                        "MPC_XY_VEL_MAX",
                                        "MC_ROLL_P",
                                        "MC_PITCH_P",
                                        "EKF2_GPS_DELAY",
                                        "COM_ARM_WO_GPS",
                                        "BAT_LOW_THR"}));

    // Value: ascending, numeric across float and int32; ties keep file order.
    ui.click(ui.header_center(2));
    ASSERT_TRUE(ui.sort_state(col, asc));
    EXPECT_EQ(col, 2);
    EXPECT_TRUE(asc);
    EXPECT_EQ(names_in(reader_, panel.parameter_row_order()),
              (std::vector<std::string>{"BAT_LOW_THR",        // 0.15
                                        "COM_ARM_WO_GPS",     // 1 (file index 3)
                                        "SYS_HAS_MAG",        // 1 (file index 5)
                                        "MC_ROLL_P",          // 6.5 (file index 0)
                                        "MC_PITCH_P",         // 6.5 (file index 6)
                                        "MPC_XY_VEL_MAX",     // 12
                                        "EKF2_GPS_DELAY",     // 110
                                        "SYS_AUTOSTART"}));   // 4001

    // Value again: descending; ties still in file order (stable).
    ui.click(ui.header_center(2));
    ASSERT_TRUE(ui.sort_state(col, asc));
    EXPECT_EQ(col, 2);
    EXPECT_FALSE(asc);
    EXPECT_EQ(names_in(reader_, panel.parameter_row_order()),
              (std::vector<std::string>{"SYS_AUTOSTART",
                                        "EKF2_GPS_DELAY",
                                        "MPC_XY_VEL_MAX",
                                        "MC_ROLL_P",
                                        "MC_PITCH_P",
                                        "COM_ARM_WO_GPS",
                                        "SYS_HAS_MAG",
                                        "BAT_LOW_THR"}));

    // Type: "float" before "int32", file order within a type.
    ui.click(ui.header_center(1));
    ASSERT_TRUE(ui.sort_state(col, asc));
    EXPECT_EQ(col, 1);
    EXPECT_TRUE(asc);
    EXPECT_EQ(names_in(reader_, panel.parameter_row_order()),
              (std::vector<std::string>{"MC_ROLL_P",
                                        "BAT_LOW_THR",
                                        "MPC_XY_VEL_MAX",
                                        "MC_PITCH_P",
                                        "EKF2_GPS_DELAY",
                                        "SYS_AUTOSTART",
                                        "COM_ARM_WO_GPS",
                                        "SYS_HAS_MAG"}));
}

// Re-opening the same path after the log changed on disk re-sorts with the
// new values (the cached order must not outlive the parameter set it indexes).
TEST_F(Px4UlogSortingTest, ReopeningChangedLogResorts)
{
    const fs::path tmp = fs::temp_directory_path() / "spectra_px4_sort_reopen.ulg";
    fs::copy_file(fixture_path(), tmp, fs::copy_options::overwrite_existing);
    ASSERT_TRUE(reader_.open(tmp.string()));
    plot_mgr_.load_ulog(reader_);

    ULogFilePanel panel(reader_, plot_mgr_);
    PanelDriver   ui(panel);
    ui.open_parameters_tab();
    if (HasFatalFailure())
        return;
    ui.click(ui.header_center(2));   // Value ascending
    ASSERT_EQ(names_in(reader_, panel.parameter_row_order()).front(), "BAT_LOW_THR");

    // Rewrite BAT_LOW_THR's value in place (0.15 -> 9000) and reopen the same path.
    {
        std::vector<char> bytes;
        {
            std::ifstream in(tmp, std::ios::binary);
            bytes.assign(std::istreambuf_iterator<char>(in), {});
        }
        const std::string key = "float BAT_LOW_THR";
        auto              it  = std::search(bytes.begin(), bytes.end(), key.begin(), key.end());
        ASSERT_NE(it, bytes.end());
        const float v = 9000.0f;
        std::memcpy(&*(it + static_cast<std::ptrdiff_t>(key.size())), &v, sizeof(v));
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    ASSERT_TRUE(reader_.open(tmp.string()));
    plot_mgr_.load_ulog(reader_);
    ui.frame();
    ui.frame();

    const auto order = names_in(reader_, panel.parameter_row_order());
    ASSERT_EQ(order.size(), kFileOrder.size());
    EXPECT_EQ(order.front(), "COM_ARM_WO_GPS");
    EXPECT_EQ(order.back(), "BAT_LOW_THR") << "9000 now sorts last by value";

    std::error_code ec;
    fs::remove(tmp, ec);
}
