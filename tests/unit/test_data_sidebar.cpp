// Tests for the Data sidebar (src/ui/data_sidebar/): the provider interface,
// search filtering and match counts, keyboard navigation, the plot-field
// action, the built-in log / CSV / topic adapters, the docked left panel
// zone in LayoutManager, and an ImGui draw smoke test.

#include <gtest/gtest.h>
#include <imgui.h>

#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ui/data_sidebar/builtin_providers.hpp"
#include "ui/data_sidebar/data_sidebar.hpp"
#include "ui/data_sidebar/data_sidebar_model.hpp"
#include "ui/figures/figure_manager.hpp"
#include "ui/layout/layout_manager.hpp"
#include "ui/logs/log_browser_panel.hpp"
#include "ui/theme/theme.hpp"
#include "ui/topics/topics_panel.hpp"

using namespace spectra::ui::data_sidebar;
using spectra::Axes;
using spectra::Figure;
using spectra::LayoutManager;

namespace
{

// A provider with fixed sources that records plot calls.
class FakeProvider final : public DataProvider
{
   public:
    explicit FakeProvider(std::string id, std::string title, bool native_drag = false)
        : id_(std::move(id)), title_(std::move(title)), native_drag_(native_drag)
    {
    }

    std::string_view id() const override { return id_; }
    std::string_view title() const override { return title_; }

    void list_sources(std::vector<DataSource>& out) override
    {
        ++list_calls;
        for (const auto& s : sources)
            out.push_back(s);
    }

    PlotResult plot_field(uint64_t source_key, uint64_t field_key, const PlotTarget& t) override
    {
        plotted.push_back({source_key, field_key});
        last_target = t;
        if (fail)
            return {false, "nope"};
        return {true, "plotted " + std::to_string(field_key)};
    }

    DragPayload drag_payload(uint64_t, uint64_t field_key) const override
    {
        if (!native_drag_)
            return {};
        DragPayload p;
        p.type = "FAKE";
        p.data.assign(1, static_cast<unsigned char>(field_key));
        return p;
    }

    std::string empty_hint() const override { return "nothing here"; }

    std::vector<DataSource>                    sources;
    std::vector<std::pair<uint64_t, uint64_t>> plotted;
    PlotTarget                                 last_target;
    int                                        list_calls = 0;
    bool                                       fail       = false;

   private:
    std::string id_;
    std::string title_;
    bool        native_drag_ = false;
};

DataField field(std::string group, std::string name, uint64_t key)
{
    DataField f;
    f.group = std::move(group);
    f.name  = std::move(name);
    f.key   = key;
    return f;
}

// "flight.ulg": attitude/{roll,pitch} + gps/{lat,lon} ; "bench.ulg": alt
FakeProvider* add_logs(DataSidebarModel& model)
{
    auto p = std::make_unique<FakeProvider>("logs", "Log files");
    {
        DataSource s;
        s.name   = "flight.ulg";
        s.key    = 7;
        s.fields = {field("attitude", "roll", 0),
                    field("attitude", "pitch", 1),
                    field("gps", "lat", 2),
                    field("gps", "lon", 3)};
        p->sources.push_back(s);
    }
    {
        DataSource s;
        s.name   = "bench.ulg";
        s.key    = 9;
        s.fields = {field("", "alt", 0)};
        p->sources.push_back(s);
    }
    return static_cast<FakeProvider*>(model.add_provider(std::move(p)));
}

std::vector<std::string> labels(const DataSidebarModel& model)
{
    std::vector<std::string> out;
    for (const auto& r : model.rows())
        out.push_back(std::string(static_cast<size_t>(r.depth), ' ') + r.label);
    return out;
}

int find_row(const DataSidebarModel& model, const std::string& label)
{
    const auto& rows = model.rows();
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].label == label)
            return static_cast<int>(i);
    }
    return -1;
}

std::string temp_path(const std::string& name)
{
    return (std::filesystem::temp_directory_path() / ("spectra_data_sidebar_" + name)).string();
}

}   // namespace

// ─── Matching ────────────────────────────────────────────────────────────────

TEST(DataSidebarQuery, CaseInsensitiveSubstringOverSourceAndField)
{
    const DataField f = field("vehicle_attitude", "q[0]", 0);
    EXPECT_TRUE(field_matches_query("flight.ulg", f, ""));
    EXPECT_TRUE(field_matches_query("flight.ulg", f, "   "));
    EXPECT_TRUE(field_matches_query("flight.ulg", f, "ATTITUDE"));
    EXPECT_TRUE(field_matches_query("flight.ulg", f, "q[0"));
    EXPECT_TRUE(field_matches_query("flight.ulg", f, "FLIGHT"));        // source name
    EXPECT_TRUE(field_matches_query("flight.ulg", f, "attitude/q"));    // across segments
    EXPECT_TRUE(field_matches_query("flight.ulg", f, "flight q[0]"));   // every term
    EXPECT_FALSE(field_matches_query("flight.ulg", f, "flight gps"));
    EXPECT_FALSE(field_matches_query("flight.ulg", f, "q[1]"));
}

// ─── Tree / filtering ────────────────────────────────────────────────────────

TEST(DataSidebarModel, BuildsTreeWithCollapsedGroups)
{
    DataSidebarModel model;
    auto*            logs = add_logs(model);
    model.add_provider(std::make_unique<FakeProvider>("empty", "Nothing"));
    model.refresh();
    EXPECT_EQ(logs->list_calls, 1);
    EXPECT_EQ(model.field_count(), 5u);
    EXPECT_EQ(model.source_count(), 2u);
    EXPECT_EQ(model.match_count(), 5u);
    EXPECT_FALSE(model.filtering());

    EXPECT_EQ(labels(model),
              (std::vector<std::string>{"Log files",
                                        " flight.ulg",
                                        "  attitude",
                                        "  gps",
                                        " bench.ulg",
                                        "  alt",
                                        "Nothing",
                                        " nothing here"}));
    const auto& rows = model.rows();
    EXPECT_EQ(rows[0].kind, RowKind::Provider);
    EXPECT_EQ(rows[0].count, 5u);
    EXPECT_EQ(rows[2].kind, RowKind::Group);
    EXPECT_EQ(rows[2].count, 2u);
    EXPECT_FALSE(rows[2].expanded);
    EXPECT_EQ(rows[5].kind, RowKind::Field);
    EXPECT_EQ(rows[7].kind, RowKind::Empty);

    // Expanding a group shows its fields one level deeper.
    model.toggle_expanded(2);
    EXPECT_EQ(labels(model)[3], "   roll");
    EXPECT_EQ(labels(model)[4], "   pitch");
    // Collapsing the provider hides everything under it.
    model.toggle_expanded(0);
    EXPECT_EQ(labels(model), (std::vector<std::string>{"Log files", "Nothing", " nothing here"}));
}

TEST(DataSidebarModel, SearchFiltersLiveAndCountsMatches)
{
    DataSidebarModel model;
    add_logs(model);
    model.add_provider(std::make_unique<FakeProvider>("empty", "Nothing"));
    model.refresh();

    model.set_query("LA");   // gps/lat only ("alt" has no "la")
    EXPECT_TRUE(model.filtering());
    EXPECT_EQ(model.match_count(), 1u);
    // Providers / sources without matches are hidden; groups open while searching.
    EXPECT_EQ(labels(model),
              (std::vector<std::string>{"Log files", " flight.ulg", "  gps", "   lat"}));
    EXPECT_EQ(model.rows()[0].count, 1u);
    EXPECT_EQ(model.rows()[2].count, 1u);
    // The first match is selected, so Enter plots it.
    ASSERT_GE(model.selected(), 0);
    EXPECT_EQ(model.rows()[static_cast<size_t>(model.selected())].label, "lat");

    model.set_query("bench");   // source name matches all of its fields
    EXPECT_EQ(model.match_count(), 1u);
    EXPECT_EQ(labels(model), (std::vector<std::string>{"Log files", " bench.ulg", "  alt"}));

    model.set_query("zzz");
    EXPECT_EQ(model.match_count(), 0u);
    EXPECT_TRUE(model.rows().empty());

    model.set_query("");
    EXPECT_EQ(model.match_count(), 5u);
    EXPECT_EQ(model.rows().size(), 8u);
    // Collapse state survives a search.
    EXPECT_FALSE(model.rows()[2].expanded);
}

// ─── Keyboard navigation ─────────────────────────────────────────────────────

TEST(DataSidebarModel, KeyboardNavigationAndEnterPlots)
{
    DataSidebarModel model;
    auto*            logs = add_logs(model);
    model.add_provider(std::make_unique<FakeProvider>("empty", "Nothing"));
    Figure fig({.width = 320, .height = 240});
    Axes&  ax = fig.subplot(1, 1, 1);
    model.set_target_provider(
        [&]()
        {
            PlotTarget t;
            t.axes      = &ax;
            t.figure_id = 42;
            return t;
        });
    model.refresh();

    EXPECT_EQ(model.selected(), -1);
    model.move_selection(1);   // first row
    EXPECT_EQ(model.selected(), 0);
    model.move_selection(-5);   // clamped
    EXPECT_EQ(model.selected(), 0);
    model.move_selection(2);   // "attitude" group
    EXPECT_EQ(model.rows()[2].label, "attitude");
    EXPECT_EQ(model.selected(), 2);

    model.expand_selected();   // Right opens the group...
    EXPECT_TRUE(model.rows()[2].expanded);
    model.expand_selected();   // ...then steps into it
    EXPECT_EQ(model.rows()[static_cast<size_t>(model.selected())].label, "roll");

    model.move_selection(1);
    const PlotResult r = model.activate_selected();   // Enter
    EXPECT_TRUE(r.ok);
    ASSERT_EQ(logs->plotted.size(), 1u);
    EXPECT_EQ(logs->plotted[0], (std::pair<uint64_t, uint64_t>{7, 1}));   // flight.ulg / pitch
    EXPECT_EQ(logs->last_target.axes, &ax);
    EXPECT_EQ(logs->last_target.figure_id, 42u);
    EXPECT_EQ(model.status(), "plotted 1");
    EXPECT_FALSE(model.status_is_error());

    model.collapse_selected();   // Left on a field steps to its group
    EXPECT_EQ(model.rows()[static_cast<size_t>(model.selected())].label, "attitude");
    model.collapse_selected();   // ...then collapses it
    EXPECT_FALSE(model.rows()[2].expanded);
    model.activate_selected();   // Enter on a container toggles it
    EXPECT_TRUE(model.rows()[2].expanded);

    // The empty-hint row is skipped.
    model.move_selection(100);
    EXPECT_EQ(model.rows()[static_cast<size_t>(model.selected())].label, "Nothing");

    // Selection follows the row id across refreshes.
    model.select(static_cast<size_t>(find_row(model, "lon") >= 0 ? find_row(model, "lon") : 0));
    const std::string before = model.rows()[static_cast<size_t>(model.selected())].id;
    model.refresh();
    EXPECT_EQ(model.rows()[static_cast<size_t>(model.selected())].id, before);
}

TEST(DataSidebarModel, PlotFailuresSetErrorStatus)
{
    DataSidebarModel model;
    auto*            logs = add_logs(model);
    logs->fail            = true;
    model.refresh();
    const int alt = find_row(model, "alt");
    ASSERT_GE(alt, 0);
    EXPECT_FALSE(model.plot_row(static_cast<size_t>(alt)).ok);
    EXPECT_TRUE(model.status_is_error());
    EXPECT_EQ(model.status(), "nope");
    // Non-field rows and unknown providers do not reach the provider.
    EXPECT_FALSE(model.plot_row(0).ok);
    EXPECT_FALSE(model.plot_field(99, 0, 0, {}).ok);
    EXPECT_EQ(logs->plotted.size(), 1u);
}

TEST(DataSidebarModel, DragPayloadsNativeOrGeneric)
{
    DataSidebarModel model;
    auto*            generic = add_logs(model);
    auto*            native  = static_cast<FakeProvider*>(
        model.add_provider(std::make_unique<FakeProvider>("native", "Native", true)));
    DataSource s;
    s.name   = "src";
    s.key    = 3;
    s.fields = {field("", "x", 5)};
    native->sources.push_back(s);
    model.refresh();

    // Generic: plotted back through the model.
    const int alt = find_row(model, "alt");
    ASSERT_GE(alt, 0);
    const DragPayload g = model.drag_payload(static_cast<size_t>(alt));
    ASSERT_EQ(g.type, DATA_FIELD_DRAG_TYPE);
    ASSERT_EQ(g.data.size(), sizeof(DataFieldDragPayload));
    DataFieldDragPayload dp;
    std::memcpy(&dp, g.data.data(), sizeof(dp));
    EXPECT_EQ(dp.provider_index, 0u);
    EXPECT_EQ(dp.source_key, 9u);
    EXPECT_EQ(dp.field_key, 0u);
    EXPECT_TRUE(model.plot_payload(dp, {}).ok);
    ASSERT_EQ(generic->plotted.size(), 1u);
    EXPECT_EQ(generic->plotted[0], (std::pair<uint64_t, uint64_t>{9, 0}));

    // Native payload passes through; non-field rows have none.
    const int x = find_row(model, "x");
    ASSERT_GE(x, 0);
    EXPECT_EQ(model.drag_payload(static_cast<size_t>(x)).type, "FAKE");
    EXPECT_TRUE(model.drag_payload(0).empty());
}

// ─── Built-in providers ──────────────────────────────────────────────────────

TEST(DataSidebarProviders, LogFilesListAndPlot)
{
    spectra::ui::logs::LogBrowserPanel panel;
    ASSERT_TRUE(
        panel.open_file(std::string(SPECTRA_SOURCE_DIR) + "/tests/data/ulog/px4_fixture.ulg"))
        << panel.status();
    panel.set_visible(false);

    DataSidebarModel     model;
    int                  opened = 0;
    BuiltinProviderHooks hooks;
    hooks.log_panel       = &panel;
    hooks.open_log_dialog = [&]() { ++opened; };
    CsvProvider* csv      = install_builtin_providers(model, hooks);
    ASSERT_NE(csv, nullptr);
    ASSERT_EQ(model.provider_count(), 2u);   // logs, csv (no topics panel)
    DataProvider* logs = model.find_provider("logs");
    ASSERT_NE(logs, nullptr);
    model.refresh();

    const auto& srcs = model.sources(0);
    ASSERT_EQ(srcs.size(), 1u);
    EXPECT_EQ(srcs[0].name, "px4_fixture.ulg");
    EXPECT_EQ(srcs[0].detail, "ULog");

    // Search finds the field; Enter plots it into the target axes.
    Figure fig({.width = 640, .height = 480});
    Axes&  ax = fig.subplot(1, 1, 1);
    model.set_target_provider(
        [&]()
        {
            PlotTarget t;
            t.axes = &ax;
            return t;
        });
    model.set_query("sensor gyro_rad[2]");
    EXPECT_EQ(model.match_count(), 1u);
    ASSERT_GE(model.selected(), 0);
    const DataRow& row = model.rows()[static_cast<size_t>(model.selected())];
    EXPECT_EQ(row.label, "gyro_rad[2]");
    EXPECT_EQ(model.sources(0)[row.source].fields[row.field].group, "sensor_combined");

    // Dragging uses the Log Browser's own payload, so its drop target plots it.
    const DragPayload dp = model.drag_payload(static_cast<size_t>(model.selected()));
    EXPECT_EQ(dp.type, spectra::ui::logs::LOG_FIELD_DRAG_TYPE);

    EXPECT_TRUE(model.activate_selected().ok) << model.status();
    ASSERT_EQ(ax.series().size(), 1u);
    EXPECT_EQ(ax.series()[0]->label(), "sensor_combined/gyro_rad[2]");

    // Provider actions: open dialog, details window, close.
    EXPECT_EQ(logs->open_label(), "Open Log...");
    logs->open();
    EXPECT_EQ(opened, 1);
    logs->open_details();
    EXPECT_TRUE(panel.is_visible());
    EXPECT_TRUE(logs->close_source(srcs[0].key));
    model.refresh();
    EXPECT_TRUE(model.sources(0).empty());
    EXPECT_TRUE(panel.files().empty());
}

TEST(DataSidebarProviders, CsvColumnsPlotAgainstFirstColumn)
{
    const std::string path = temp_path("data.csv");
    {
        std::ofstream out(path);
        out << "time,speed,temp\n0,1,20\n1,2,21\n2,4,22\n";
    }

    DataSidebarModel     model;
    BuiltinProviderHooks hooks;
    CsvProvider*         csv = install_builtin_providers(model, hooks);
    ASSERT_NE(csv, nullptr);
    spectra::CsvData bad;
    bad.error = "broken";
    EXPECT_EQ(csv->add_file("bad.csv", bad), 0u);
    const uint64_t id = csv->open_file(path);
    ASSERT_NE(id, 0u);
    // Re-opening replaces the entry.
    const uint64_t id2 = csv->open_file(path);
    EXPECT_NE(id2, id);
    EXPECT_EQ(csv->files().size(), 1u);
    model.refresh();

    const auto& srcs = model.sources(0);
    ASSERT_EQ(srcs.size(), 1u);
    EXPECT_EQ(srcs[0].name, "spectra_data_sidebar_data.csv");
    EXPECT_EQ(srcs[0].detail, "3 rows");
    ASSERT_EQ(srcs[0].fields.size(), 3u);
    EXPECT_EQ(srcs[0].fields[1].name, "speed");
    EXPECT_EQ(srcs[0].fields[0].detail, "x");

    Figure     fig({.width = 640, .height = 480});
    Axes&      ax = fig.subplot(1, 1, 1);
    PlotTarget t;
    t.axes = &ax;

    // No target axes: error, nothing added.
    EXPECT_FALSE(model.plot_field(0, id2, 1, {}).ok);
    EXPECT_TRUE(model.status_is_error());

    ASSERT_TRUE(model.plot_field(0, id2, 1, t).ok) << model.status();
    ASSERT_EQ(ax.series().size(), 1u);
    auto* line = dynamic_cast<spectra::LineSeries*>(ax.series()[0].get());
    ASSERT_NE(line, nullptr);
    EXPECT_EQ(line->label(), "speed");
    EXPECT_EQ(ax.xlabel(), "time");
    ASSERT_EQ(line->x_data().size(), 3u);
    EXPECT_FLOAT_EQ(line->x_data()[2], 2.0f);
    EXPECT_FLOAT_EQ(line->y_data()[2], 4.0f);
    EXPECT_FALSE(line->data_binding().empty());

    // The x column itself is plotted against row number.
    ASSERT_TRUE(model.plot_field(0, id2, 0, t).ok);
    auto* idx = dynamic_cast<spectra::LineSeries*>(ax.series()[1].get());
    ASSERT_NE(idx, nullptr);
    EXPECT_FLOAT_EQ(idx->x_data()[1], 1.0f);

    EXPECT_FALSE(model.plot_field(0, id2, 9, t).ok);   // unknown column
    EXPECT_FALSE(model.plot_field(0, 999, 0, t).ok);   // unknown file
    // CSV fields have no native drop target: generic payload.
    model.set_query("temp");
    ASSERT_GE(model.selected(), 0);
    EXPECT_EQ(model.drag_payload(static_cast<size_t>(model.selected())).type, DATA_FIELD_DRAG_TYPE);

    EXPECT_TRUE(csv->close_source(id2));
    EXPECT_TRUE(csv->files().empty());
    std::filesystem::remove(path);
}

TEST(DataSidebarProviders, TopicsGroupBySourceAndSubscribe)
{
    using spectra::ui::topics::TopicsPanel;
    TopicsPanel                                        panel;
    std::vector<spectra::ui::topics::SubscribeRequest> subs;
    std::vector<std::pair<std::string, bool>>          toggles;
    int                                                list_requests = 0;
    panel.set_list_request_callback([&]() { ++list_requests; });
    panel.set_subscribe_request_callback([&](const spectra::ui::topics::SubscribeRequest& r)
                                         { subs.push_back(r); });
    panel.set_data_source_callbacks(
        []()
        {
            return std::vector<spectra::ui::topics::DataSourceRow>{{"sine", true, false},
                                                                   {"idle", false, false}};
        },
        [&](const std::string& name, bool start) { toggles.emplace_back(name, start); });

    std::vector<spectra::ipc::TopicInfoEntry> topics(4);
    topics[0].name             = "sine/a";
    topics[0].estimated_hz     = 50.0;
    topics[0].publisher_online = true;
    topics[1].name             = "imu/accel_x";
    topics[2].name             = "imu/accel_y";
    topics[3].name             = "temperature";
    panel.set_topics(topics);

    DataSidebarModel     model;
    BuiltinProviderHooks hooks;
    hooks.topics_panel = &panel;
    install_builtin_providers(model, hooks);
    ASSERT_EQ(model.provider_count(), 2u);   // csv, topics
    model.refresh();
    EXPECT_EQ(list_requests, 1);   // the sidebar keeps the topic list fresh

    const auto& srcs = model.sources(1);
    ASSERT_EQ(srcs.size(), 3u);   // sine, idle, Topics
    EXPECT_EQ(srcs[0].name, "sine");
    EXPECT_EQ(srcs[0].state, DataSource::State::Running);
    ASSERT_EQ(srcs[0].fields.size(), 1u);
    EXPECT_EQ(srcs[0].fields[0].name, "a");
    EXPECT_EQ(srcs[0].fields[0].detail, "50 Hz");
    EXPECT_EQ(srcs[1].state, DataSource::State::Stopped);
    EXPECT_TRUE(srcs[1].fields.empty());
    EXPECT_EQ(srcs[2].name, "Topics");
    ASSERT_EQ(srcs[2].fields.size(), 3u);
    EXPECT_EQ(srcs[2].fields[0].group, "");   // "temperature" (no parent) sorts first
    EXPECT_EQ(srcs[2].fields[1].group, "imu");
    EXPECT_EQ(srcs[2].fields[1].name, "accel_x");

    // Plot subscribes the target figure / axes.
    model.set_target_provider(
        []()
        {
            PlotTarget t;
            t.figure_id  = 5;
            t.axes_index = 1;
            return t;
        });
    model.set_query("accel_y");
    ASSERT_GE(model.selected(), 0);
    EXPECT_EQ(model.drag_payload(static_cast<size_t>(model.selected())).type,
              spectra::ui::topics::TOPIC_DRAG_TYPE);
    ASSERT_TRUE(model.activate_selected().ok) << model.status();
    ASSERT_EQ(subs.size(), 1u);
    EXPECT_EQ(subs[0].topic_name, "imu/accel_y");
    EXPECT_EQ(subs[0].figure_id, 5u);
    EXPECT_EQ(subs[0].axes_index, 1u);

    // Start / stop a plugin source.
    DataProvider* prov = model.find_provider("topics");
    ASSERT_NE(prov, nullptr);
    EXPECT_TRUE(prov->set_source_running(srcs[1].key, true));
    ASSERT_EQ(toggles.size(), 1u);
    EXPECT_EQ(toggles[0], (std::pair<std::string, bool>{"idle", true}));
    EXPECT_FALSE(prov->set_source_running(12345, true));
}

TEST(DataSidebarProviders, TopicsWithoutTransportShowHint)
{
    spectra::ui::topics::TopicsPanel panel;
    TopicProvider                    prov(&panel);
    std::vector<DataSource>          out;
    prov.list_sources(out);
    EXPECT_TRUE(out.empty());
    EXPECT_NE(prov.empty_hint().find("not connected"), std::string::npos);
    EXPECT_FALSE(prov.plot_field(0, 1, {}).ok);
}

// ─── Layout ──────────────────────────────────────────────────────────────────

TEST(DataSidebarLayout, LeftPanelDocksBetweenRailAndCanvas)
{
    LayoutManager lm;
    lm.update(1280.0f, 800.0f);
    const float canvas_x = lm.canvas_rect().x;
    const float canvas_w = lm.canvas_rect().w;
    EXPECT_FLOAT_EQ(lm.left_panel_rect().w, 0.0f);

    lm.set_left_panel_visible(true);
    lm.update(1280.0f, 800.0f);
    const auto lp = lm.left_panel_rect();
    EXPECT_FLOAT_EQ(lp.x, lm.nav_rail_rect().w);
    EXPECT_FLOAT_EQ(lp.w, LayoutManager::LEFT_PANEL_DEFAULT_WIDTH);
    EXPECT_FLOAT_EQ(lp.y, LayoutManager::COMMAND_BAR_HEIGHT);
    EXPECT_FLOAT_EQ(lm.canvas_rect().x, canvas_x + lp.w);
    EXPECT_FLOAT_EQ(lm.canvas_rect().w, canvas_w - lp.w);

    lm.set_left_panel_width(10000.0f);   // clamped
    EXPECT_FLOAT_EQ(lm.left_panel_width(), LayoutManager::LEFT_PANEL_MAX_WIDTH);
    lm.set_left_panel_width(LayoutManager::LEFT_PANEL_DEFAULT_WIDTH);

    // Too narrow: the panel hides instead of squeezing the canvas.
    lm.update(
        LayoutManager::NAV_RAIL_COLLAPSED_WIDTH + LayoutManager::WINDOW_MIN_CANVAS_WIDTH + 40.0f,
        600.0f);
    EXPECT_TRUE(lm.is_left_panel_auto_hidden());
    EXPECT_FLOAT_EQ(lm.left_panel_rect().w, 0.0f);
    EXPECT_GE(lm.canvas_rect().w, LayoutManager::WINDOW_MIN_CANVAS_WIDTH);

    lm.set_left_panel_visible(false);
    lm.update(1280.0f, 800.0f);
    EXPECT_FLOAT_EQ(lm.canvas_rect().x, canvas_x);
}

TEST(DataSidebarLayout, HiddenInspectorReservesNoSpace)
{
    // 700 px window: rail 72 + min canvas 160 leaves 468 px. With the inspector
    // closed all of it is available to the sidebar.
    LayoutManager lm;
    lm.set_inspector_visible(false);
    lm.set_left_panel_visible(true);
    lm.update(700.0f, 600.0f);
    EXPECT_FALSE(lm.is_left_panel_auto_hidden());
    EXPECT_FLOAT_EQ(lm.left_panel_rect().w, LayoutManager::LEFT_PANEL_DEFAULT_WIDTH);

    // Opening the inspector takes its share first; the sidebar gets the rest
    // (or hides when that is below LEFT_PANEL_MIN_FIT_WIDTH).
    lm.set_inspector_visible(true);
    lm.update(700.0f, 600.0f);
    const float space = 700.0f - lm.nav_rail_rect().w - LayoutManager::WINDOW_MIN_CANVAS_WIDTH;
    const float left  = space - lm.inspector_fitted_width();
    EXPECT_FLOAT_EQ(lm.left_panel_rect().w,
                    left >= LayoutManager::LEFT_PANEL_MIN_FIT_WIDTH
                        ? std::min(left, LayoutManager::LEFT_PANEL_DEFAULT_WIDTH)
                        : 0.0f);
    EXPECT_LT(lm.left_panel_rect().w, LayoutManager::LEFT_PANEL_DEFAULT_WIDTH);
}

TEST(DataSidebarProviders, ActiveFigureTargetUsesOrCreatesFigure)
{
    spectra::FigureRegistry registry;
    spectra::FigureManager  figures(registry);

    // No figure yet: one is created with a 1x1 subplot.
    PlotTarget created = active_figure_target(figures);
    ASSERT_NE(created.axes, nullptr);
    Figure* fig = registry.get(created.figure_id);
    ASSERT_NE(fig, nullptr);
    EXPECT_EQ(created.axes, fig->axes().front().get());
    EXPECT_EQ(created.axes_index, 0u);

    // Existing figure: same axes, no new figure.
    const size_t     count = registry.count();
    const PlotTarget again = active_figure_target(figures);
    EXPECT_EQ(again.axes, created.axes);
    EXPECT_EQ(again.figure_id, created.figure_id);
    EXPECT_EQ(registry.count(), count);
}

// ─── Drawing ─────────────────────────────────────────────────────────────────

TEST(DataSidebar, DrawsWithImGui)
{
    spectra::ui::ThemeManager theme;
    spectra::ui::ThemeManager::set_current(&theme);
    ImGui::CreateContext();
    ImGuiIO& io    = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 800.0f);
    io.DeltaTime   = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    unsigned char* px = nullptr;
    int            tw = 0, th = 0;
    io.Fonts->GetTexDataAsRGBA32(&px, &tw, &th);

    LayoutManager lm;
    DataSidebar   sidebar;
    sidebar.set_layout_manager(&lm);
    add_logs(sidebar.model());
    sidebar.model().add_provider(std::make_unique<FakeProvider>("empty", "Nothing"));

    // Hidden: draw is a no-op and does not list sources.
    ImGui::NewFrame();
    sidebar.draw();
    ImGui::Render();
    EXPECT_TRUE(sidebar.model().rows().empty());

    lm.set_left_panel_visible(true);
    lm.update(1280.0f, 800.0f);
    for (const char* query : {"", "gps", "no-such-field"})
    {
        sidebar.model().set_query(query);
        ImGui::NewFrame();
        sidebar.draw();
        ImGui::Render();
    }
    EXPECT_EQ(sidebar.model().source_count(), 2u);
    ImGui::DestroyContext();
    spectra::ui::ThemeManager::set_current(nullptr);
}
