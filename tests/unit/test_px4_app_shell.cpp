// Unit tests for Px4AppShell — CLI parsing, offline ULog open flow,
// and auto-plot / close-all-plots functionality.

#include <gtest/gtest.h>

#include "px4_app_shell.hpp"

#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/series.hpp>

#include "core/layout.hpp"
#include "px4_recent_files.hpp"
#include "core/config_paths.hpp"
#include "env_guard.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include "ui/commands/command_registry.hpp"
    #include "ui/commands/shortcut_manager.hpp"
#endif

#include <cstdio>
#include <filesystem>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace spectra::adapters::px4;

namespace
{

class ULogBuilder
{
   public:
    ULogBuilder()
    {
        static constexpr uint8_t magic[] = {0x55, 0x4C, 0x6F, 0x67, 0x01, 0x12, 0x35};
        buf_.insert(buf_.end(), magic, magic + sizeof(magic));
        buf_.push_back(1);
        write_u64(1000);
    }

    void add_format(const std::string& content)
    {
        add_message('F',
                    reinterpret_cast<const uint8_t*>(content.data()),
                    static_cast<uint16_t>(content.size()));
    }

    void add_subscription(uint8_t multi_id, uint16_t msg_id, const std::string& name)
    {
        std::vector<uint8_t> payload;
        payload.push_back(multi_id);
        uint8_t id_bytes[2];
        std::memcpy(id_bytes, &msg_id, 2);
        payload.insert(payload.end(), id_bytes, id_bytes + 2);
        payload.insert(payload.end(), name.begin(), name.end());
        add_message('A', payload.data(), static_cast<uint16_t>(payload.size()));
    }

    void add_data(uint16_t msg_id, uint64_t timestamp, const uint8_t* fields, size_t field_len)
    {
        std::vector<uint8_t> payload;
        uint8_t              id_bytes[2];
        std::memcpy(id_bytes, &msg_id, 2);
        payload.insert(payload.end(), id_bytes, id_bytes + 2);
        uint8_t ts_bytes[8];
        std::memcpy(ts_bytes, &timestamp, 8);
        payload.insert(payload.end(), ts_bytes, ts_bytes + 8);
        payload.insert(payload.end(), fields, fields + field_len);
        add_message('D', payload.data(), static_cast<uint16_t>(payload.size()));
    }

    std::string write_to_file(const std::string& path) const
    {
        std::ofstream ofs(path, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(buf_.data()),
                  static_cast<std::streamsize>(buf_.size()));
        return path;
    }

   private:
    void add_message(uint8_t type, const uint8_t* payload, uint16_t len)
    {
        uint8_t hdr[3];
        std::memcpy(hdr, &len, 2);
        hdr[2] = type;
        buf_.insert(buf_.end(), hdr, hdr + 3);
        buf_.insert(buf_.end(), payload, payload + len);
    }

    void write_u64(uint64_t value)
    {
        uint8_t bytes[8];
        std::memcpy(bytes, &value, 8);
        buf_.insert(buf_.end(), bytes, bytes + 8);
    }

    std::vector<uint8_t> buf_;
};

std::string temp_path(const std::string& name)
{
    return "/tmp/spectra_px4_shell_" + name + ".ulg";
}

}   // namespace

TEST(Px4AppShellTest, ParseArgsAcceptsUlogAndConnectOptions)
{
    std::string err;
    const char* argv[] = {"spectra-px4",
                          "--ulog",
                          "/tmp/demo.ulg",
                          "--host",
                          "10.0.0.2",
                          "--port",
                          "14550",
                          "--connect",
                          "--window-s",
                          "45.5"};

    Px4AppConfig cfg =
        parse_px4_args(static_cast<int>(std::size(argv)), const_cast<char**>(argv), err);

    EXPECT_TRUE(err.empty());
    EXPECT_EQ(cfg.ulog_file, "/tmp/demo.ulg");
    EXPECT_EQ(cfg.host, "10.0.0.2");
    EXPECT_EQ(cfg.port, 14550);
    EXPECT_TRUE(cfg.auto_connect);
    EXPECT_DOUBLE_EQ(cfg.time_window_s, 45.5);
}

TEST(Px4AppShellTest, OpenUlogLoadsReaderAndPlotManager)
{
    ULogBuilder builder;
    builder.add_format("vehicle_attitude:uint64_t timestamp;float roll");
    builder.add_subscription(0, 1, "vehicle_attitude");

    float       roll = 1.25f;
    std::string path = builder.write_to_file(temp_path("open_success"));
    builder.add_data(1, 1000000, reinterpret_cast<const uint8_t*>(&roll), sizeof(roll));

    // Rewrite with data included.
    path = builder.write_to_file(path);

    Px4AppShell shell(Px4AppConfig{});
    ASSERT_TRUE(shell.open_ulog(path));

    EXPECT_TRUE(shell.reader().is_open());
    EXPECT_TRUE(shell.plot_manager().has_ulog_data());

    auto topics = shell.plot_manager().available_topics();
    ASSERT_EQ(topics.size(), 1u);
    EXPECT_EQ(topics[0], "vehicle_attitude");

    std::remove(path.c_str());
}

TEST(Px4AppShellTest, FailedOpenClearsLoadedPlotState)
{
    ULogBuilder builder;
    builder.add_format("vehicle_local_position:uint64_t timestamp;float x");
    builder.add_subscription(0, 1, "vehicle_local_position");

    float x = 3.0f;
    builder.add_data(1, 1000000, reinterpret_cast<const uint8_t*>(&x), sizeof(x));
    std::string path = builder.write_to_file(temp_path("open_then_fail"));

    Px4AppShell shell(Px4AppConfig{});
    ASSERT_TRUE(shell.open_ulog(path));
    ASSERT_TRUE(shell.plot_manager().has_ulog_data());

    EXPECT_FALSE(shell.open_ulog("/tmp/this_file_should_not_exist_ulog.ulg"));
    EXPECT_FALSE(shell.reader().is_open());
    EXPECT_FALSE(shell.plot_manager().has_ulog_data());
    EXPECT_TRUE(shell.plot_manager().available_topics().empty());

    std::remove(path.c_str());
}

TEST(Px4AppShellTest, PollSyncsSelectedUlogFieldToCanvasFigure)
{
    ULogBuilder builder;
    builder.add_format("vehicle_attitude:uint64_t timestamp;float roll");
    builder.add_subscription(0, 1, "vehicle_attitude");

    float roll1 = 1.25f;
    float roll2 = 2.50f;
    builder.add_data(1, 1000000, reinterpret_cast<const uint8_t*>(&roll1), sizeof(roll1));
    builder.add_data(1, 2000000, reinterpret_cast<const uint8_t*>(&roll2), sizeof(roll2));

    std::string path = builder.write_to_file(temp_path("canvas_sync"));

    Px4AppShell     shell(Px4AppConfig{});
    spectra::Figure fig({.width = 800, .height = 600});
    shell.set_canvas_figure(&fig);

    ASSERT_TRUE(shell.open_ulog(path));
    shell.plot_manager().add_field("vehicle_attitude", "roll");
    shell.poll();

    ASSERT_EQ(fig.axes().size(), 1u);
    ASSERT_NE(fig.axes()[0], nullptr);
    ASSERT_EQ(fig.axes()[0]->series().size(), 1u);

    auto* line = dynamic_cast<spectra::LineSeries*>(fig.axes()[0]->series()[0].get());
    ASSERT_NE(line, nullptr);
    EXPECT_EQ(line->label(), "vehicle_attitude.roll");
    ASSERT_EQ(line->point_count(), 2u);
    EXPECT_FLOAT_EQ(line->x_data()[0], 1.0f);
    EXPECT_FLOAT_EQ(line->x_data()[1], 2.0f);
    EXPECT_FLOAT_EQ(line->y_data()[0], 1.25f);
    EXPECT_FLOAT_EQ(line->y_data()[1], 2.50f);

    std::remove(path.c_str());
}

TEST(Px4AppShellTest, FailedOpenClearsCanvasSeries)
{
    ULogBuilder builder;
    builder.add_format("vehicle_local_position:uint64_t timestamp;float x");
    builder.add_subscription(0, 1, "vehicle_local_position");

    float x = 3.0f;
    builder.add_data(1, 1000000, reinterpret_cast<const uint8_t*>(&x), sizeof(x));
    std::string path = builder.write_to_file(temp_path("canvas_clear"));

    Px4AppShell     shell(Px4AppConfig{});
    spectra::Figure fig({.width = 800, .height = 600});
    shell.set_canvas_figure(&fig);

    ASSERT_TRUE(shell.open_ulog(path));
    shell.plot_manager().add_field("vehicle_local_position", "x");
    shell.poll();

    ASSERT_EQ(fig.axes().size(), 1u);
    ASSERT_EQ(fig.axes()[0]->series().size(), 1u);

    EXPECT_FALSE(shell.open_ulog("/tmp/this_file_should_not_exist_canvas_clear.ulg"));
    shell.poll();

    // Nothing is loaded any more: the canvas falls back to the empty state,
    // which has no axes (and therefore no stale series).
    EXPECT_TRUE(shell.empty_state_visible());
    EXPECT_TRUE(fig.axes().empty());

    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// Empty state: with nothing loaded the canvas figure has no axes, so the
// viewer shows an "Open ULog" empty state instead of an empty 0-1 plot.
// ---------------------------------------------------------------------------

TEST(Px4AppShellTest, EmptyStateWhenNothingLoaded)
{
    Px4AppShell     shell(Px4AppConfig{});
    spectra::Figure fig({.width = 800, .height = 600});
    fig.subplot(1, 1, 1);   // a default axes, as a fresh figure may have
    shell.set_canvas_figure(&fig);

    EXPECT_TRUE(shell.empty_state_visible());
    shell.poll();
    EXPECT_TRUE(fig.axes().empty());

    // Repeated polls keep the figure empty.
    shell.poll();
    EXPECT_TRUE(fig.axes().empty());
}

TEST(Px4AppShellTest, EmptyStateHiddenOnceUlogIsOpen)
{
    ULogBuilder builder;
    builder.add_format("vehicle_attitude:uint64_t timestamp;float roll");
    builder.add_subscription(0, 1, "vehicle_attitude");
    float roll = 0.5f;
    builder.add_data(1, 1000000, reinterpret_cast<const uint8_t*>(&roll), sizeof(roll));
    std::string path = builder.write_to_file(temp_path("empty_state_hidden"));

    Px4AppShell     shell(Px4AppConfig{});
    spectra::Figure fig({.width = 800, .height = 600});
    shell.set_canvas_figure(&fig);
    shell.poll();
    ASSERT_TRUE(fig.axes().empty());

    ASSERT_TRUE(shell.open_ulog(path));
    shell.poll();
    EXPECT_FALSE(shell.empty_state_visible());
    // No known auto-plot topics: a single (empty) axes ready for manual fields.
    ASSERT_EQ(fig.axes().size(), 1u);

    shell.plot_manager().add_field("vehicle_attitude", "roll");
    shell.poll();
    ASSERT_EQ(fig.axes().size(), 1u);
    EXPECT_EQ(fig.axes()[0]->series().size(), 1u);

    std::remove(path.c_str());
}

#ifdef SPECTRA_USE_IMGUI
// ---------------------------------------------------------------------------
// Nav rail: each PX4 panel appears once (custom items, no registry duplicates).
// ---------------------------------------------------------------------------

TEST(Px4AppShellTest, NavRailListsEachPanelOnce)
{
    Px4AppShell shell(Px4AppConfig{});
    ASSERT_TRUE(shell.init());
    EXPECT_TRUE(shell.nav_rail_visible());

    std::vector<spectra::ui::shell::NavItem> items;
    shell.nav_rail().build_items(items);

    int ulog_items = 0;
    int live_items = 0;
    for (const auto& item : items)
    {
        if (item.is_section_header)
            continue;
        if (item.label == "ULog File")
            ++ulog_items;
        if (item.label == "PX4 Live")
            ++live_items;
    }
    EXPECT_EQ(ulog_items, 1);
    EXPECT_EQ(live_items, 1);
}
#endif

// ---------------------------------------------------------------------------
// AutoPlotUlog: verifies that opening a ULog with known interesting topics
// triggers multi-subplot auto-plot mode.
// ---------------------------------------------------------------------------

TEST(Px4AppShellTest, AutoPlotUlogCreatesSubplotsForKnownTopics)
{
    // Build a ULog with battery_status (voltage_v, current_a) — one of the
    // predefined auto-plot groups.
    ULogBuilder builder;
    // Format: timestamp + two floats
    builder.add_format("battery_status:uint64_t timestamp;float voltage_v;float current_a");
    builder.add_subscription(0, 1, "battery_status");

    struct BatRow
    {
        float voltage_v;
        float current_a;
    };
    BatRow row1{12.0f, 5.0f};
    BatRow row2{11.9f, 5.2f};
    BatRow row3{11.8f, 5.3f};
    builder.add_data(1, 1000000, reinterpret_cast<const uint8_t*>(&row1), sizeof(row1));
    builder.add_data(1, 2000000, reinterpret_cast<const uint8_t*>(&row2), sizeof(row2));
    builder.add_data(1, 3000000, reinterpret_cast<const uint8_t*>(&row3), sizeof(row3));

    std::string path = builder.write_to_file(temp_path("auto_plot_battery"));

    Px4AppShell     shell(Px4AppConfig{});
    spectra::Figure fig({.width = 800, .height = 1200});
    shell.set_canvas_figure(&fig);

    ASSERT_TRUE(shell.open_ulog(path));

    // Auto-plot should be active since battery_status is a known topic.
    // There should be at least one subplot (for battery).
    EXPECT_GE(fig.axes().size(), 1u);

    // Verify at least one axes has a non-empty series (battery voltage/current).
    bool has_battery_series = false;
    for (const auto& ax : fig.axes())
    {
        if (ax && ax->series().size() >= 1)
        {
            has_battery_series = true;
            break;
        }
    }
    EXPECT_TRUE(has_battery_series);

    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// AutoPlotUlog: a ULog with no known topics should NOT produce any subplots
// beyond the placeholder empty axes.
// ---------------------------------------------------------------------------

TEST(Px4AppShellTest, AutoPlotUlogNoMatchingTopicsGivesEmptyPlot)
{
    ULogBuilder builder;
    builder.add_format("unknown_topic:uint64_t timestamp;float x");
    builder.add_subscription(0, 1, "unknown_topic");

    float x = 1.0f;
    builder.add_data(1, 1000000, reinterpret_cast<const uint8_t*>(&x), sizeof(x));

    std::string path = builder.write_to_file(temp_path("auto_plot_no_match"));

    Px4AppShell     shell(Px4AppConfig{});
    spectra::Figure fig({.width = 800, .height = 600});
    shell.set_canvas_figure(&fig);

    ASSERT_TRUE(shell.open_ulog(path));

    // No auto-plot groups should be created; figure should have 1 placeholder axes.
    ASSERT_EQ(fig.axes().size(), 1u);
    // The placeholder axes has no data series.
    EXPECT_TRUE(fig.axes()[0]->series().empty());

    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// CloseAllPlots: verifies that close_all_plots() clears auto-plot state and
// reverts to a single empty subplot.
// ---------------------------------------------------------------------------

TEST(Px4AppShellTest, CloseAllPlotsRevertsToSingleEmptySubplot)
{
    ULogBuilder builder;
    builder.add_format("battery_status:uint64_t timestamp;float voltage_v;float current_a");
    builder.add_subscription(0, 1, "battery_status");

    struct BatRow
    {
        float voltage_v;
        float current_a;
    };
    BatRow row{12.0f, 5.0f};
    builder.add_data(1, 1000000, reinterpret_cast<const uint8_t*>(&row), sizeof(row));

    std::string path = builder.write_to_file(temp_path("close_all"));

    Px4AppShell     shell(Px4AppConfig{});
    spectra::Figure fig({.width = 800, .height = 1200});
    shell.set_canvas_figure(&fig);

    ASSERT_TRUE(shell.open_ulog(path));
    // After auto-plot there should be at least 1 axes with data.
    EXPECT_GE(fig.axes().size(), 1u);

    // Close all plots.
    shell.close_all_plots();
    shell.poll();

    // Figure should be back to a single empty axes.
    ASSERT_EQ(fig.axes().size(), 1u);
    EXPECT_TRUE(fig.axes()[0]->series().empty());
    // Manual fields should also be gone.
    EXPECT_EQ(shell.plot_manager().field_count(), 0u);

    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// CloseAllPlots: manually added fields are also removed.
// ---------------------------------------------------------------------------

TEST(Px4AppShellTest, CloseAllPlotsRemovesManuallyAddedFields)
{
    ULogBuilder builder;
    builder.add_format("vehicle_attitude:uint64_t timestamp;float roll");
    builder.add_subscription(0, 1, "vehicle_attitude");

    float roll = 1.0f;
    builder.add_data(1, 1000000, reinterpret_cast<const uint8_t*>(&roll), sizeof(roll));

    std::string path = builder.write_to_file(temp_path("close_all_manual"));

    Px4AppShell     shell(Px4AppConfig{});
    spectra::Figure fig({.width = 800, .height = 600});
    shell.set_canvas_figure(&fig);

    ASSERT_TRUE(shell.open_ulog(path));
    shell.plot_manager().add_field("vehicle_attitude", "roll");
    shell.poll();

    // There should be at least 1 series after adding the field.
    ASSERT_FALSE(fig.axes().empty());
    bool has_series = false;
    for (const auto& ax : fig.axes())
        if (ax && !ax->series().empty())
        {
            has_series = true;
            break;
        }
    EXPECT_TRUE(has_series);

    shell.close_all_plots();
    shell.poll();

    // All fields and series should be gone.
    EXPECT_EQ(shell.plot_manager().field_count(), 0u);
    ASSERT_EQ(fig.axes().size(), 1u);
    EXPECT_TRUE(fig.axes()[0]->series().empty());

    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// Auto-plot layout: four or more groups are tiled so they fit the canvas.
// ---------------------------------------------------------------------------

namespace
{

// ULog with battery_status, vehicle_local_position (altitude + velocity) and
// airspeed: four auto-plot groups.
std::string write_four_group_ulog(const std::string& name)
{
    ULogBuilder builder;
    builder.add_format("battery_status:uint64_t timestamp;float voltage_v;float current_a");
    builder.add_format(
        "vehicle_local_position:uint64_t timestamp;float z;float vx;float vy;float vz");
    builder.add_format(
        "airspeed:uint64_t timestamp;float indicated_airspeed_m_s;float true_airspeed_m_s");
    builder.add_subscription(0, 1, "battery_status");
    builder.add_subscription(0, 2, "vehicle_local_position");
    builder.add_subscription(0, 3, "airspeed");

    for (uint64_t t = 1; t <= 3; ++t)
    {
        const float bat[2] = {12.0f - 0.1f * static_cast<float>(t), 5.0f};
        const float pos[4] = {-1.0f * static_cast<float>(t), 0.5f, 0.25f, -0.1f};
        const float air[2] = {10.0f + static_cast<float>(t), 11.0f + static_cast<float>(t)};
        builder.add_data(1, t * 1000000, reinterpret_cast<const uint8_t*>(bat), sizeof(bat));
        builder.add_data(2, t * 1000000, reinterpret_cast<const uint8_t*>(pos), sizeof(pos));
        builder.add_data(3, t * 1000000, reinterpret_cast<const uint8_t*>(air), sizeof(air));
    }
    return builder.write_to_file(temp_path(name));
}

std::string temp_dir(const std::string& name)
{
    auto dir = std::filesystem::temp_directory_path() / ("spectra_px4_test_" + name);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir.string();
}

}   // namespace

TEST(Px4AppShellTest, AutoPlotGridTilesFourOrMoreGroups)
{
    EXPECT_EQ(Px4AppShell::auto_plot_grid(1), std::make_pair(1, 1));
    EXPECT_EQ(Px4AppShell::auto_plot_grid(3), std::make_pair(3, 1));
    EXPECT_EQ(Px4AppShell::auto_plot_grid(4), std::make_pair(2, 2));
    EXPECT_EQ(Px4AppShell::auto_plot_grid(6), std::make_pair(3, 2));
    EXPECT_EQ(Px4AppShell::auto_plot_grid(8), std::make_pair(3, 3));
    for (int n = 1; n <= 9; ++n)
    {
        const auto [rows, cols] = Px4AppShell::auto_plot_grid(n);
        EXPECT_GE(rows * cols, n) << n;
        EXPECT_LE(rows, 3) << n;
    }
}

TEST(Px4AppShellTest, FourAutoPlotGroupsFitTheCanvas)
{
    const std::string path = write_four_group_ulog("four_groups");

    Px4AppShell     shell(Px4AppConfig{});
    spectra::Figure fig({.width = 1000, .height = 640});
    shell.set_canvas_figure(&fig);
    ASSERT_TRUE(shell.open_ulog(path));

    ASSERT_EQ(fig.axes().size(), 4u);
    EXPECT_EQ(fig.grid_rows(), 2);
    EXPECT_EQ(fig.grid_cols(), 2);
    for (const auto& ax : fig.axes())
    {
        ASSERT_TRUE(ax);
        EXPECT_FALSE(ax->series().empty());
    }

    // Same layout the window runtime computes for the canvas: the grid must
    // fit the canvas height without scrolling.
    const auto&      st = fig.style();
    spectra::Margins m{st.margin_left, st.margin_right, st.margin_bottom, st.margin_top};
    float            content_h = 0.0f;
    const float      canvas_h  = 600.0f;
    const auto       rects     = spectra::compute_subplot_layout(1000.0f,
                                                       canvas_h,
                                                       fig.grid_rows(),
                                                       fig.grid_cols(),
                                                       m,
                                                       st.min_subplot_height,
                                                       &content_h);
    EXPECT_LE(content_h, canvas_h);
    ASSERT_GE(rects.size(), 4u);
    for (size_t i = 0; i < 4; ++i)
    {
        EXPECT_GT(rects[i].h, 0.0f);
        EXPECT_LE(rects[i].y + rects[i].h, canvas_h);
    }

    // Closing the plots goes back to a single 1x1 axes.
    shell.close_all_plots();
    shell.poll();
    EXPECT_EQ(fig.grid_rows(), 1);
    EXPECT_EQ(fig.grid_cols(), 1);
    EXPECT_EQ(fig.axes().size(), 1u);

    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// Recent files store.
// ---------------------------------------------------------------------------

// The store keeps absolute, normalised paths; on Windows that adds the drive
// letter and backslashes, so expectations go through the same transform.
static std::string stored_form(const std::string& p)
{
    return std::filesystem::absolute(std::filesystem::path(p)).lexically_normal().string();
}

TEST(Px4RecentFilesTest, PushMovesToFrontDedupesAndCaps)
{
    Px4RecentFiles recent({}, 3);
    recent.push("/logs/a.ulg");
    recent.push("/logs/b.ulg");
    recent.push("/logs/a.ulg");
    ASSERT_EQ(recent.entries().size(), 2u);
    EXPECT_EQ(recent.entries()[0], stored_form("/logs/a.ulg"));
    EXPECT_EQ(recent.entries()[1], stored_form("/logs/b.ulg"));

    recent.push("/logs/c.ulg");
    recent.push("/logs/d.ulg");
    ASSERT_EQ(recent.entries().size(), 3u);
    EXPECT_EQ(recent.entries()[0], stored_form("/logs/d.ulg"));
    EXPECT_EQ(recent.entries()[2], stored_form("/logs/a.ulg"));

    recent.remove("/logs/c.ulg");
    EXPECT_EQ(recent.entries().size(), 2u);
    recent.clear();
    EXPECT_TRUE(recent.empty());
}

TEST(Px4RecentFilesTest, DefaultsToTenEntriesInSpectraConfigDir)
{
    Px4RecentFiles recent;
    EXPECT_EQ(recent.max_entries(), 10u);
    // Follows the user config dir, which SPECTRA_CONFIG_DIR may redirect
    // (QA runs set it to keep the real config untouched).
    const std::filesystem::path p(Px4RecentFiles::default_path());
    EXPECT_EQ(p, spectra::user_config_dir() / "px4_recent.json");

    // Without overrides that is <home>/.config/spectra.
    spectra::test::EnvGuard spectra_dir{"SPECTRA_CONFIG_DIR"};
    spectra::test::EnvGuard xdg{"XDG_CONFIG_HOME"};
    spectra::test::EnvGuard home{"HOME"};
    spectra_dir.unset();
    xdg.unset();
    home.set((std::filesystem::temp_directory_path() / "px4_recent_home").string());
    const std::filesystem::path d(Px4RecentFiles::default_path());
    EXPECT_EQ(d.filename(), "px4_recent.json");
    EXPECT_EQ(d.parent_path().filename(), "spectra");
    EXPECT_EQ(d.parent_path().parent_path().filename(), ".config");
}

TEST(Px4RecentFilesTest, RelativePathsAreStoredAbsolute)
{
    Px4RecentFiles recent;
    recent.push("flight.ulg");
    ASSERT_EQ(recent.entries().size(), 1u);
    EXPECT_TRUE(std::filesystem::path(recent.entries()[0]).is_absolute());
    EXPECT_EQ(std::filesystem::path(recent.entries()[0]).filename(), "flight.ulg");
}

TEST(Px4RecentFilesTest, PersistsAcrossInstancesWithEscapedPaths)
{
    const std::string dir  = temp_dir("recent_roundtrip");
    const std::string file = dir + "/sub/px4_recent.json";   // parent created on save

    const std::string odd = "/logs/with \"quotes\" and \\backslash\\/x.ulg";
    {
        Px4RecentFiles recent(file);
        recent.push("/logs/one.ulg");
        recent.push(odd);
    }
    Px4RecentFiles loaded(file);
    ASSERT_TRUE(loaded.load());
    ASSERT_EQ(loaded.entries().size(), 2u);
    EXPECT_EQ(loaded.entries()[0], stored_form(odd));
    EXPECT_EQ(loaded.entries()[1], stored_form("/logs/one.ulg"));

    Px4RecentFiles missing(dir + "/does_not_exist.json");
    EXPECT_FALSE(missing.load());
    EXPECT_TRUE(missing.entries().empty());

    std::filesystem::remove_all(dir);
}

TEST(Px4RecentFilesTest, ParseArgsUsesDefaultRecentPath)
{
    const char* argv[] = {"spectra-px4"};
    std::string err;
    const auto  cfg = parse_px4_args(1, const_cast<char**>(argv), err);
    EXPECT_EQ(cfg.recent_files_path, Px4RecentFiles::default_path());
}

TEST(Px4AppShellTest, OpeningUlogRecordsAndPersistsRecentFile)
{
    const std::string dir    = temp_dir("shell_recent");
    const std::string store  = dir + "/px4_recent.json";
    const std::string ulog_a = write_four_group_ulog("recent_a");
    const std::string ulog_b = write_four_group_ulog("recent_b");

    {
        Px4AppConfig cfg;
        cfg.recent_files_path = store;
        Px4AppShell shell(cfg);
        ASSERT_TRUE(shell.init());
        EXPECT_TRUE(shell.recent_files().empty());

        ASSERT_TRUE(shell.open_ulog(ulog_a));
        ASSERT_TRUE(shell.open_ulog(ulog_b));
        EXPECT_FALSE(shell.open_ulog(dir + "/missing.ulg"));   // failures are not recorded
        ASSERT_EQ(shell.recent_files().entries().size(), 2u);
        EXPECT_EQ(shell.recent_files().entries()[0], stored_form(ulog_b));
    }

    // A new session (e.g. after restart) sees the same list.
    Px4AppConfig cfg;
    cfg.recent_files_path = store;
    Px4AppShell shell(cfg);
    ASSERT_TRUE(shell.init());
    ASSERT_EQ(shell.recent_files().entries().size(), 2u);
    EXPECT_EQ(shell.recent_files().entries()[0], stored_form(ulog_b));
    EXPECT_EQ(shell.recent_files().entries()[1], stored_form(ulog_a));

    // Re-opening from the list promotes it; an entry whose file is gone is dropped.
    ASSERT_TRUE(shell.open_recent(ulog_a));
    EXPECT_EQ(shell.recent_files().entries()[0], stored_form(ulog_a));
    std::remove(ulog_b.c_str());
    EXPECT_FALSE(shell.open_recent(ulog_b));
    ASSERT_EQ(shell.recent_files().entries().size(), 1u);
    EXPECT_EQ(shell.recent_files().entries()[0], stored_form(ulog_a));

    std::remove(ulog_a.c_str());
    std::filesystem::remove_all(dir);
}

#ifdef SPECTRA_USE_IMGUI
namespace
{
const spectra::ui::shell::MenuAction* find_action(
    const std::vector<spectra::ui::shell::MenuAction>& items,
    const std::string&                                 label)
{
    for (const auto& a : items)
        if (a.label == label)
            return &a;
    return nullptr;
}
}   // namespace

TEST(Px4AppShellTest, FileMenuListsRecentFiles)
{
    const std::string dir  = temp_dir("menu_recent");
    const std::string ulog = write_four_group_ulog("menu_recent");

    Px4AppConfig cfg;
    cfg.recent_files_path = dir + "/px4_recent.json";
    Px4AppShell shell(cfg);
    ASSERT_TRUE(shell.init());

    auto&       file   = shell.menu_bar().menu("File");
    const auto* recent = find_action(file.items(), "Open Recent");
    ASSERT_NE(recent, nullptr);
    ASSERT_EQ(recent->submenu.size(), 3u);   // "No Recent Files", separator, "Clear"
    EXPECT_EQ(recent->submenu[0].label, "No Recent Files");
    EXPECT_FALSE(recent->submenu[0].enabled());

    ASSERT_TRUE(shell.open_ulog(ulog));
    shell.poll();   // menu is rebuilt outside of menu drawing
    recent = find_action(file.items(), "Open Recent");
    ASSERT_NE(recent, nullptr);
    ASSERT_EQ(recent->submenu.size(), 3u);
    EXPECT_EQ(recent->submenu[0].label, std::filesystem::path(ulog).filename().string());
    EXPECT_TRUE(static_cast<bool>(recent->submenu[0].on_click));
    EXPECT_EQ(recent->submenu[2].label, "Clear Recent Files");
    EXPECT_TRUE(recent->submenu[2].enabled());

    recent->submenu[2].on_click();
    shell.poll();
    EXPECT_TRUE(shell.recent_files().empty());
    recent = find_action(file.items(), "Open Recent");
    EXPECT_EQ(recent->submenu[0].label, "No Recent Files");

    std::remove(ulog.c_str());
    std::filesystem::remove_all(dir);
}

TEST(Px4AppShellTest, CtrlOOpensUlogAndCtrlQQuits)
{
    constexpr int kKeyO = 79, kKeyQ = 81, kPress = 1, kModCtrl = 0x2;

    const std::string ulog = write_four_group_ulog("shortcut_open");

    Px4AppShell shell(Px4AppConfig{});
    ASSERT_TRUE(shell.init());
    int chooser_calls = 0;
    shell.set_file_chooser(
        [&]()
        {
            ++chooser_calls;
            return ulog;
        });

    spectra::CommandRegistry registry;
    spectra::ShortcutManager shortcuts;
    shortcuts.set_command_registry(&registry);
    shortcuts.register_defaults();

    // Before registration the menu must not advertise unbound shortcuts.
    const auto& resolve = shell.menu_bar().shortcut_resolver();
    const auto* open    = find_action(shell.menu_bar().menu("File").items(), "Open ULog...");
    const auto* quit    = find_action(shell.menu_bar().menu("File").items(), "Exit");
    ASSERT_NE(open, nullptr);
    ASSERT_NE(quit, nullptr);
    EXPECT_TRUE(open->shortcut.empty());
    EXPECT_EQ(resolve(open->command_id), "");
    EXPECT_FALSE(shortcuts.on_key(kKeyO, kPress, kModCtrl));

    shell.register_commands(registry, shortcuts);
    EXPECT_EQ(resolve(open->command_id), "Ctrl+O");
    EXPECT_EQ(resolve(quit->command_id), "Ctrl+Q");

    EXPECT_TRUE(shortcuts.on_key(kKeyO, kPress, kModCtrl));
    EXPECT_EQ(chooser_calls, 1);
    EXPECT_TRUE(shell.reader().is_open());

    EXPECT_FALSE(shell.shutdown_requested());
    EXPECT_TRUE(shortcuts.on_key(kKeyQ, kPress, kModCtrl));
    EXPECT_TRUE(shell.shutdown_requested());

    shell.unregister_commands();
    EXPECT_FALSE(shortcuts.on_key(kKeyO, kPress, kModCtrl));
    EXPECT_EQ(registry.find("px4.file.open_ulog"), nullptr);

    std::remove(ulog.c_str());
}

// Closing the window frees its command registry and shortcut manager before
// the px4 shell shuts down; forget_commands() must drop them untouched
// (unregister_commands() on them was a use-after-free, a crash on Windows).
TEST(Px4AppShellTest, ForgetCommandsAfterTheWindowContextIsGone)
{
    Px4AppShell shell(Px4AppConfig{});
    ASSERT_TRUE(shell.init());
    {
        auto registry  = std::make_unique<spectra::CommandRegistry>();
        auto shortcuts = std::make_unique<spectra::ShortcutManager>();
        shortcuts->set_command_registry(registry.get());
        shell.register_commands(*registry, *shortcuts);
        EXPECT_NE(registry->find("px4.file.open_ulog"), nullptr);
    }   // the window's context goes away
    shell.forget_commands();
    const auto& resolve = shell.menu_bar().shortcut_resolver();
    EXPECT_EQ(resolve("px4.file.open_ulog"), "");
    shell.run_command("px4.app.quit");   // no registry: a no-op
    EXPECT_FALSE(shell.shutdown_requested());
    shell.unregister_commands();   // nothing left to unregister
}
#endif

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

// New figures and split panes: each figure keeps its own plots and new fields
// go to the figure the user is looking at.
TEST(Px4AppShellTest, FieldsGoToTheActiveFigureAndClosedFiguresDropTheirPlots)
{
    ULogBuilder builder;
    builder.add_format("vehicle_attitude:uint64_t timestamp;float roll;float pitch");
    builder.add_subscription(0, 1, "vehicle_attitude");
    const float row1[2] = {1.0f, 2.0f};
    const float row2[2] = {3.0f, 4.0f};
    builder.add_data(1, 1000000, reinterpret_cast<const uint8_t*>(row1), sizeof(row1));
    builder.add_data(1, 2000000, reinterpret_cast<const uint8_t*>(row2), sizeof(row2));
    const std::string path = builder.write_to_file(temp_path("multi_figure"));

    spectra::FigureRegistry registry;
    const spectra::FigureId id1 =
        registry.register_figure(std::make_unique<spectra::Figure>(spectra::FigureConfig{}));
    const spectra::FigureId id2 =
        registry.register_figure(std::make_unique<spectra::Figure>(spectra::FigureConfig{}));
    spectra::Figure* fig1 = registry.get(id1);
    spectra::Figure* fig2 = registry.get(id2);
    fig2->subplot(1, 1, 1);   // "New Figure" starts with one empty axes

    spectra::FigureId active = id1;
    Px4AppShell       shell(Px4AppConfig{});
    shell.set_canvas_figure(fig1);
    shell.set_figure_source(&registry, [&active]() { return active; });

    ASSERT_TRUE(shell.open_ulog(path));
    shell.close_all_plots();   // the fixture has no auto-plot topics
    shell.plot_manager().add_field("vehicle_attitude", "roll");
    shell.poll();
    EXPECT_EQ(shell.target_figure(), fig1);

    // Switch to the second figure: its plot set starts empty and the first
    // figure's series are left alone.
    active = id2;
    shell.poll();
    EXPECT_EQ(shell.target_figure(), fig2);
    EXPECT_EQ(shell.plotted_figure_count(), 2u);
    EXPECT_TRUE(shell.plot_manager().has_ulog_data());
    EXPECT_EQ(shell.plot_manager().field_count(), 0u);
    ASSERT_EQ(fig2->axes().size(), 1u);
    EXPECT_TRUE(fig2->axes()[0]->series().empty());

    shell.plot_manager().add_field("vehicle_attitude", "pitch");
    shell.poll();

    ASSERT_EQ(fig1->axes().size(), 1u);
    ASSERT_EQ(fig1->axes()[0]->series().size(), 1u);
    EXPECT_EQ(fig1->axes()[0]->series()[0]->label(), "vehicle_attitude.roll");
    ASSERT_EQ(fig2->axes().size(), 1u);
    ASSERT_EQ(fig2->axes()[0]->series().size(), 1u);
    EXPECT_EQ(fig2->axes()[0]->series()[0]->label(), "vehicle_attitude.pitch");

    // Close All Plots only clears the active figure.
    shell.close_all_plots();
    shell.poll();
    EXPECT_TRUE(fig2->axes()[0]->series().empty());
    EXPECT_EQ(fig1->axes()[0]->series().size(), 1u);

    // Closing the second figure drops its plot set; plots go back to the first.
    registry.unregister_figure(id2);
    active = id1;
    shell.poll();
    EXPECT_EQ(shell.plotted_figure_count(), 1u);
    EXPECT_EQ(shell.target_figure(), fig1);
    EXPECT_EQ(shell.plot_manager().field_count(), 1u);

    std::remove(path.c_str());
}

// Drag and drop from the ULog panel: a field or a whole topic lands in the
// dropped-on figure; on an auto-plot it joins the subplot under the cursor.
TEST(Px4AppShellTest, DroppedFieldsAndTopicsArePlottedWhereTheyLand)
{
    ULogBuilder builder;
    builder.add_format("vehicle_attitude:uint64_t timestamp;float roll;float pitch");
    builder.add_format("battery_status:uint64_t timestamp;float voltage_v;float current_a");
    builder.add_subscription(0, 1, "vehicle_attitude");
    builder.add_subscription(0, 2, "battery_status");
    const float att[2] = {1.0f, 2.0f};
    const float bat[2] = {16.0f, 3.0f};
    builder.add_data(1, 1000000, reinterpret_cast<const uint8_t*>(att), sizeof(att));
    builder.add_data(1, 2000000, reinterpret_cast<const uint8_t*>(att), sizeof(att));
    builder.add_data(2, 1000000, reinterpret_cast<const uint8_t*>(bat), sizeof(bat));
    builder.add_data(2, 2000000, reinterpret_cast<const uint8_t*>(bat), sizeof(bat));
    const std::string path = builder.write_to_file(temp_path("drop_fields"));

    spectra::FigureRegistry registry;
    const spectra::FigureId id1 =
        registry.register_figure(std::make_unique<spectra::Figure>(spectra::FigureConfig{}));
    const spectra::FigureId id2 =
        registry.register_figure(std::make_unique<spectra::Figure>(spectra::FigureConfig{}));
    spectra::Figure* fig1 = registry.get(id1);
    spectra::Figure* fig2 = registry.get(id2);
    fig2->subplot(1, 1, 1);

    Px4AppShell shell(Px4AppConfig{});
    shell.set_canvas_figure(fig1);
    shell.set_figure_source(&registry, [id1]() { return id1; });
    ASSERT_TRUE(shell.open_ulog(path));   // auto-plots the Battery group into fig1
    shell.poll();
    ASSERT_EQ(fig1->axes().size(), 1u);
    ASSERT_EQ(fig1->axes()[0]->series().size(), 2u);   // voltage, current

    // A field dropped on the auto-plot subplot joins it.
    EXPECT_TRUE(shell.drop_field(make_field_drag_payload("vehicle_attitude", "roll"), fig1, 0));
    shell.poll();
    ASSERT_EQ(fig1->axes()[0]->series().size(), 3u);
    EXPECT_EQ(fig1->axes()[0]->series()[2]->label(), "vehicle_attitude.roll");

    // A whole topic dropped on the second (non-active) figure adds every
    // field but the timestamp, without touching the first figure.
    EXPECT_TRUE(shell.drop_field(make_field_drag_payload("vehicle_attitude", ""), fig2, 0));
    shell.poll();
    ASSERT_EQ(fig2->axes().size(), 1u);
    ASSERT_EQ(fig2->axes()[0]->series().size(), 2u);
    EXPECT_EQ(fig2->axes()[0]->series()[0]->label(), "vehicle_attitude.roll");
    EXPECT_EQ(fig2->axes()[0]->series()[1]->label(), "vehicle_attitude.pitch");
    EXPECT_EQ(fig1->axes()[0]->series().size(), 3u);

    // Unknown topics plot nothing.
    EXPECT_FALSE(shell.drop_field(make_field_drag_payload("no_such_topic", ""), fig2, 0));

    std::remove(path.c_str());
}

// Opening another log while a different figure is active must not leave the
// first figure's auto-plot showing the previous flight's samples.
TEST(Px4AppShellTest, OpeningAnotherLogRefreshesAutoPlotsInOtherFigures)
{
    auto write_battery_log = [](const std::string& name, float voltage)
    {
        ULogBuilder builder;
        builder.add_format("battery_status:uint64_t timestamp;float voltage_v;float current_a");
        builder.add_subscription(0, 1, "battery_status");
        const float row[2] = {voltage, 3.0f};
        builder.add_data(1, 1000000, reinterpret_cast<const uint8_t*>(row), sizeof(row));
        builder.add_data(1, 2000000, reinterpret_cast<const uint8_t*>(row), sizeof(row));
        return builder.write_to_file(temp_path(name));
    };
    const std::string first  = write_battery_log("reload_first", 16.0f);
    const std::string second = write_battery_log("reload_second", 12.0f);

    spectra::FigureRegistry registry;
    const spectra::FigureId id1 =
        registry.register_figure(std::make_unique<spectra::Figure>(spectra::FigureConfig{}));
    const spectra::FigureId id2 =
        registry.register_figure(std::make_unique<spectra::Figure>(spectra::FigureConfig{}));
    spectra::Figure* fig1 = registry.get(id1);
    registry.get(id2)->subplot(1, 1, 1);

    spectra::FigureId active = id1;
    Px4AppShell       shell(Px4AppConfig{});
    shell.set_canvas_figure(fig1);
    shell.set_figure_source(&registry, [&active]() { return active; });

    ASSERT_TRUE(shell.open_ulog(first));   // auto-plots the Battery group into fig1
    shell.poll();
    ASSERT_EQ(fig1->axes().size(), 1u);
    ASSERT_FALSE(fig1->axes()[0]->series().empty());
    auto* voltage = dynamic_cast<spectra::LineSeries*>(fig1->axes()[0]->series()[0].get());
    ASSERT_NE(voltage, nullptr);
    ASSERT_FALSE(voltage->y_data().empty());
    EXPECT_FLOAT_EQ(voltage->y_data()[0], 16.0f);

    active = id2;
    shell.poll();
    ASSERT_TRUE(shell.open_ulog(second));
    shell.poll();

    ASSERT_EQ(fig1->axes().size(), 1u);
    ASSERT_FALSE(fig1->axes()[0]->series().empty());
    voltage = dynamic_cast<spectra::LineSeries*>(fig1->axes()[0]->series()[0].get());
    ASSERT_NE(voltage, nullptr);
    ASSERT_FALSE(voltage->y_data().empty());
    EXPECT_FLOAT_EQ(voltage->y_data()[0], 12.0f);

    std::remove(first.c_str());
    std::remove(second.c_str());
}
