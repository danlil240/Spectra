// Shareable layout files: binding URIs, layout-only save / load, backward
// compatibility with existing workspaces, and binding resolution with fake
// topic / CSV providers.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <map>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/series.hpp>
#include <sstream>
#include <string>
#ifdef _WIN32
    #include <process.h>
    #define getpid _getpid
#else
    #include <unistd.h>
#endif

#include "daemon/topic_registry.hpp"
#include "math/data_transform.hpp"
#include "ui/workspace/binding_resolver.hpp"
#include "ui/workspace/data_binding.hpp"
#include "ui/workspace/layout.hpp"
#include "ui/workspace/layout_session.hpp"
#include "ui/workspace/workspace.hpp"

using namespace spectra;
namespace fs = std::filesystem;

namespace
{

std::string read_file(const fs::path& p)
{
    std::ifstream      f(p);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void set_env(const char* name, const char* value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    ::setenv(name, value, 1);
#endif
}

void unset_env(const char* name)
{
#ifdef _WIN32
    _putenv_s(name, "");
#else
    ::unsetenv(name);
#endif
}

fs::path temp_path(const std::string& name)
{
    return fs::temp_directory_path() / ("spectra_layout_" + std::to_string(getpid()) + "_" + name);
}

class FakeTopics : public TopicBindingProvider
{
   public:
    std::map<std::string, Result> results;   // topic -> result (default Pending)
    struct Call
    {
        std::string topic;
        FigureId    figure_id;
        uint32_t    axes_index;
        Series*     series;
    };
    std::vector<Call> calls;

    Result bind_topic(const std::string& topic,
                      FigureId           figure_id,
                      uint32_t           axes_index,
                      Series&            series) override
    {
        calls.push_back({topic, figure_id, axes_index, &series});
        auto it = results.find(topic);
        return it == results.end() ? Result::Pending : it->second;
    }
};

class FakeCsv : public CsvBindingProvider
{
   public:
    std::map<std::string, CsvData> files;   // file name -> contents

    std::string locate(const std::string& file_name) override
    {
        return files.count(file_name) ? "/fake/" + file_name : std::string{};
    }
    const CsvData& load(const std::string& path) override
    {
        static const CsvData missing = []
        {
            CsvData d;
            d.error = "missing";
            return d;
        }();
        const std::string name = path.substr(std::string("/fake/").size());
        auto              it   = files.find(name);
        return it == files.end() ? missing : it->second;
    }

    void add(const std::string&              name,
             std::vector<std::string>        headers,
             std::vector<std::vector<float>> cols)
    {
        CsvData d;
        d.headers  = std::move(headers);
        d.columns  = std::move(cols);
        d.num_cols = d.columns.size();
        d.num_rows = d.columns.empty() ? 0 : d.columns[0].size();
        d.column_offsets.assign(d.num_cols, 0.0);
        files[name] = std::move(d);
    }
};

// A dashboard: one figure, two subplots.  Top: a topic-bound line (explicit
// colour, dashed, circle markers).  Bottom: a CSV-bound scatter (palette
// colour) and a CSV-bound line (letter colour).
std::unique_ptr<Figure> make_dashboard()
{
    auto  fig = std::make_unique<Figure>();
    auto& top = fig->subplot(2, 1, 1);
    auto& bot = fig->subplot(2, 1, 2);

    std::vector<float> x = {0.0f, 1.0f, 2.0f};
    std::vector<float> y = {5.0f, 6.0f, 7.0f};

    auto& accel = top.line(x, y);
    accel.label("accel_x");
    accel.color(Color(0.1f, 0.2f, 0.3f, 1.0f));
    accel.width(3.5f);
    accel.line_style(LineStyle::Dashed);
    accel.marker_style(MarkerStyle::Circle);
    accel.opacity(0.75f);
    accel.set_data_binding(topic_binding_uri("imu/accel_x"));
    top.xlim(-1.0f, 4.0f);
    top.ylim(-2.0f, 9.5f);
    top.xlabel("time (s)");
    top.ylabel("accel");
    top.title("IMU");

    auto& temp = bot.scatter(x, y);
    temp.label("temp");
    temp.size(9.0f);
    temp.set_data_binding(csv_binding_uri("flight.csv", "time", "temp"));

    auto& alt = bot.plot(x, y, "r--");
    alt.label("alt");
    alt.set_data_binding(csv_binding_uri("flight.csv", "", "alt"));
    alt.visible(false);
    bot.xlim(0.0f, 100.0f);
    bot.ylim(10.0f, 20.0f);
    return fig;
}

WorkspaceData capture_layout(Figure& fig)
{
    std::vector<Figure*> figs = {&fig};
    auto                 data = Workspace::capture(figs, 0, "dark", true, 300.0f, false);
    data.undo_count           = 7;
    data.plugin_state         = "{\"x\":1}";
    data.last_export_dir      = "/home/me/exports";
    data.shortcut_overrides.push_back({"view.reset", "Ctrl+R", false});
    OverlaySnapshot::MarkerEntry m;
    m.data_x = 1.0;
    data.interaction.markers.push_back(m);
    WorkspaceData::TransformState t;
    t.figure_index = 0;
    t.axes_index   = 1;
    t.steps.push_back({static_cast<int>(TransformType::Scale), 2.0f, true});
    t.steps.push_back({static_cast<int>(TransformType::Offset), 1.0f, true});
    t.steps.push_back({static_cast<int>(TransformType::Negate), 0.0f, false});   // disabled
    data.transforms.push_back(t);
    data.dock_state = "{\"active\":0,\"root\":{\"id\":1,\"leaf\":true,\"figure\":0}}";
    Workspace::make_layout(data);
    return data;
}

}   // namespace

// ─── Binding URIs ───────────────────────────────────────────────────────────

TEST(DataBindingUri, TopicRoundTrip)
{
    auto b = parse_data_binding("topic://imu/accel_x");
    ASSERT_TRUE(b);
    EXPECT_EQ(b->scheme, BindingScheme::Topic);
    EXPECT_EQ(b->source, "imu/accel_x");
    EXPECT_EQ(format_data_binding(*b), "topic://imu/accel_x");
    EXPECT_EQ(topic_binding_uri("imu/accel_x"), "topic://imu/accel_x");
}

TEST(DataBindingUri, CsvWithAndWithoutXColumn)
{
    auto b = parse_data_binding("csv://flight log.csv?x=time#accel x");
    ASSERT_TRUE(b);
    EXPECT_EQ(b->scheme, BindingScheme::Csv);
    EXPECT_EQ(b->source, "flight log.csv");
    EXPECT_EQ(b->x_field, "time");
    EXPECT_EQ(b->field, "accel x");
    EXPECT_EQ(format_data_binding(*b), "csv://flight log.csv?x=time#accel x");

    auto no_x = parse_data_binding("csv://data.csv#y");
    ASSERT_TRUE(no_x);
    EXPECT_TRUE(no_x->x_field.empty());
    EXPECT_EQ(csv_binding_uri("data.csv", "", "y"), "csv://data.csv#y");

    // Unknown query keys are ignored (forward compatibility).
    auto extra = parse_data_binding("csv://d.csv?unit=m&x=t#y");
    ASSERT_TRUE(extra);
    EXPECT_EQ(extra->x_field, "t");
}

TEST(DataBindingUri, SpecialCharactersArePercentEncoded)
{
    DataBinding b;
    b.scheme              = BindingScheme::Csv;
    b.source              = "a#b?.csv";
    b.x_field             = "t=0&1";
    b.field               = "50%#";
    const std::string uri = format_data_binding(b);
    EXPECT_EQ(uri, "csv://a%23b%3F.csv?x=t%3D0%261#50%25%23");
    auto parsed = parse_data_binding(uri);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(*parsed, b);

    DataBinding t;
    t.scheme = BindingScheme::Topic;
    t.source = "odd#topic?";
    auto tp  = parse_data_binding(format_data_binding(t));
    ASSERT_TRUE(tp);
    EXPECT_EQ(*tp, t);
}

TEST(DataBindingUri, UlogIsReserved)
{
    auto b = parse_data_binding("ulog://vehicle_attitude.q[0]");
    ASSERT_TRUE(b);
    EXPECT_EQ(b->scheme, BindingScheme::Ulog);
    EXPECT_EQ(b->source, "vehicle_attitude");
    EXPECT_EQ(b->field, "q[0]");
    EXPECT_EQ(format_data_binding(*b), "ulog://vehicle_attitude.q[0]");

    DataBinding dotted{BindingScheme::Ulog, "a.b", "c.d", ""};
    auto        rt = parse_data_binding(format_data_binding(dotted));
    ASSERT_TRUE(rt);
    EXPECT_EQ(*rt, dotted);
}

TEST(DataBindingUri, MalformedUrisAreRejected)
{
    std::string err;
    EXPECT_FALSE(parse_data_binding("", &err));
    EXPECT_FALSE(err.empty());
    EXPECT_FALSE(parse_data_binding("imu/accel_x", &err));
    EXPECT_FALSE(parse_data_binding("http://example.com", &err));
    EXPECT_NE(err.find("unknown binding scheme"), std::string::npos);
    EXPECT_FALSE(parse_data_binding("topic://", &err));
    EXPECT_FALSE(parse_data_binding("csv://data.csv", &err));   // no column
    EXPECT_FALSE(parse_data_binding("csv://#y", &err));         // no file
    EXPECT_FALSE(parse_data_binding("csv://d.csv#", &err));     // empty column
    EXPECT_FALSE(parse_data_binding("topic://bad%2", &err));    // truncated escape
    EXPECT_FALSE(parse_data_binding("topic://bad%zz", &err));
    EXPECT_FALSE(parse_data_binding("ulog://nofield", &err));
    // Scheme names are case-insensitive.
    EXPECT_TRUE(parse_data_binding("TOPIC://a"));
}

TEST(DataBindingUri, SeriesCopiesKeepBinding)
{
    LineSeries a;
    a.set_data_binding("topic://x");
    LineSeries b(a);
    EXPECT_EQ(b.data_binding(), "topic://x");
    LineSeries c;
    c = a;
    EXPECT_EQ(c.data_binding(), "topic://x");
}

// ─── Layout-only files ──────────────────────────────────────────────────────

TEST(LayoutFile, SaveStoresBindingsAndNoSessionState)
{
    auto fig  = make_dashboard();
    auto data = capture_layout(*fig);

    EXPECT_TRUE(data.layout_only);
    EXPECT_EQ(data.layout_version, WorkspaceData::LAYOUT_VERSION);
    EXPECT_EQ(data.undo_count, 0u);
    EXPECT_TRUE(data.plugin_state.empty());
    EXPECT_TRUE(data.last_export_dir.empty());
    EXPECT_TRUE(data.shortcut_overrides.empty());
    EXPECT_TRUE(data.interaction.markers.empty());

    const auto path = temp_path("save.spectra");
    ASSERT_TRUE(Workspace::save(path.string(), data));
    const std::string text = read_file(path);
    EXPECT_NE(text.find("\"layout_only\": true"), std::string::npos);
    EXPECT_NE(text.find("\"binding\": \"topic://imu/accel_x\""), std::string::npos);
    EXPECT_NE(text.find("\"binding\": \"csv://flight.csv?x=time#temp\""), std::string::npos);
    // No data: every point count is zero and no sample arrays are written.
    EXPECT_EQ(text.find("\"point_count\": 3"), std::string::npos);
    EXPECT_EQ(text.find("x_data"), std::string::npos);
    fs::remove(path);
}

TEST(LayoutFile, RoundTripPreservesStylesLimitsAndTransforms)
{
    auto       fig  = make_dashboard();
    const auto data = capture_layout(*fig);
    const auto path = temp_path("roundtrip.spectra");
    ASSERT_TRUE(Workspace::save(path.string(), data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path.string(), loaded));
    fs::remove(path);

    EXPECT_TRUE(loaded.layout_only);
    EXPECT_EQ(loaded.layout_version, WorkspaceData::LAYOUT_VERSION);
    EXPECT_EQ(loaded.version, WorkspaceData::FORMAT_VERSION);
    ASSERT_EQ(loaded.figures.size(), 1u);
    const auto& fs0 = loaded.figures[0];
    ASSERT_EQ(fs0.axes.size(), 2u);
    ASSERT_EQ(fs0.series.size(), 3u);

    EXPECT_FLOAT_EQ(fs0.axes[0].x_min, -1.0f);
    EXPECT_FLOAT_EQ(fs0.axes[0].y_max, 9.5f);
    EXPECT_EQ(fs0.axes[0].title, "IMU");

    const auto& accel = fs0.series[0];
    EXPECT_EQ(accel.binding, "topic://imu/accel_x");
    EXPECT_EQ(accel.axes_index, 0);
    EXPECT_EQ(accel.point_count, 0u);
    EXPECT_EQ(accel.color_source, "rgb");
    EXPECT_FLOAT_EQ(accel.color_b, 0.3f);
    EXPECT_FLOAT_EQ(accel.line_width, 3.5f);
    EXPECT_EQ(accel.line_style, static_cast<int>(LineStyle::Dashed));
    EXPECT_EQ(accel.marker_style, static_cast<int>(MarkerStyle::Circle));
    EXPECT_FLOAT_EQ(accel.opacity, 0.75f);

    EXPECT_EQ(fs0.series[1].axes_index, 1);
    EXPECT_EQ(fs0.series[1].type, "scatter");
    EXPECT_EQ(fs0.series[1].color_source, "auto");
    EXPECT_FLOAT_EQ(fs0.series[1].marker_size, 9.0f);
    EXPECT_EQ(fs0.series[2].color_source, "letter");
    EXPECT_EQ(fs0.series[2].color_letter, "r");
    EXPECT_FALSE(fs0.series[2].visible);

    ASSERT_EQ(loaded.transforms.size(), 1u);
    EXPECT_EQ(loaded.transforms[0].axes_index, 1u);
    ASSERT_EQ(loaded.transforms[0].steps.size(), 3u);
    EXPECT_FALSE(loaded.transforms[0].steps[2].enabled);
    EXPECT_EQ(loaded.dock_state, data.dock_state);

    // Opening it rebuilds empty, styled series carrying their bindings.
    FigureRegistry  registry;
    FakeTopics      topics;
    FakeCsv         csv;   // no files: CSV bindings stay unresolved
    BindingResolver resolver(&topics, &csv);
    auto            result = instantiate_layout(loaded, registry, resolver);
    ASSERT_EQ(result.figure_ids.size(), 1u);
    Figure* opened = registry.get(result.figure_ids[0]);
    ASSERT_NE(opened, nullptr);
    ASSERT_EQ(opened->axes().size(), 2u);
    Axes& top = *opened->axes()[0];
    Axes& bot = *opened->axes()[1];
    EXPECT_FLOAT_EQ(top.x_limits().min, -1.0f);
    EXPECT_FLOAT_EQ(top.y_limits().max, 9.5f);
    EXPECT_FLOAT_EQ(bot.x_limits().max, 100.0f);
    EXPECT_EQ(top.get_title(), "IMU");
    EXPECT_EQ(top.get_xlabel(), "time (s)");
    ASSERT_EQ(top.series().size(), 1u);
    ASSERT_EQ(bot.series().size(), 2u);

    auto* l = dynamic_cast<LineSeries*>(top.series()[0].get());
    ASSERT_NE(l, nullptr);
    EXPECT_EQ(l->point_count(), 0u);
    EXPECT_EQ(l->data_binding(), "topic://imu/accel_x");
    EXPECT_EQ(l->label(), "accel_x");   // waiting, not unresolved
    EXPECT_FALSE(l->has_auto_color());
    EXPECT_FLOAT_EQ(l->color().g, 0.2f);
    EXPECT_FLOAT_EQ(l->width(), 3.5f);
    EXPECT_EQ(l->line_style(), LineStyle::Dashed);
    EXPECT_EQ(l->marker_style(), MarkerStyle::Circle);
    EXPECT_FLOAT_EQ(l->opacity(), 0.75f);

    auto* sc = dynamic_cast<ScatterSeries*>(bot.series()[0].get());
    ASSERT_NE(sc, nullptr);
    EXPECT_TRUE(sc->has_auto_color());
    EXPECT_FLOAT_EQ(sc->size(), 9.0f);
    auto* alt = bot.series()[1].get();
    EXPECT_EQ(alt->color_letter(), 'r');
    EXPECT_FALSE(alt->visible());

    // Re-capturing the opened layout gives back the same bindings.
    std::vector<Figure*> figs  = {opened};
    auto                 again = Workspace::capture(figs, 0, "dark", true, 300.0f, false);
    ASSERT_EQ(again.figures[0].series.size(), 3u);
    EXPECT_EQ(again.figures[0].series[0].binding, "topic://imu/accel_x");
    EXPECT_EQ(again.figures[0].series[2].binding, "csv://flight.csv#alt");
}

TEST(LayoutFile, BindingsWithJsonCharactersSurviveTheFile)
{
    WorkspaceData data;
    data.layout_only = true;
    WorkspaceData::FigureState fs;
    fs.axes.emplace_back();
    WorkspaceData::SeriesState a;
    a.type    = "line";
    a.binding = "ulog://vehicle_attitude.q[0]";
    WorkspaceData::SeriesState b;
    b.type    = "line";
    b.name    = "after";
    b.binding = "csv://{odd}\"name\".csv#col]";
    fs.series = {a, b};
    data.figures.push_back(fs);
    data.dock_state = "{\"active\":0}";

    const auto path = temp_path("chars.spectra");
    ASSERT_TRUE(Workspace::save(path.string(), data));
    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path.string(), loaded));
    fs::remove(path);
    ASSERT_EQ(loaded.figures.size(), 1u);
    ASSERT_EQ(loaded.figures[0].series.size(), 2u);
    EXPECT_EQ(loaded.figures[0].series[0].binding, a.binding);
    EXPECT_EQ(loaded.figures[0].series[1].binding, b.binding);
    EXPECT_EQ(loaded.figures[0].series[1].name, "after");
    EXPECT_EQ(loaded.dock_state, data.dock_state);
}

TEST(LayoutFile, PlainWorkspaceSaveHasNoLayoutKeys)
{
    auto                 fig  = make_dashboard();
    std::vector<Figure*> figs = {fig.get()};
    auto                 data = Workspace::capture(figs, 0, "dark", true, 300.0f, false);
    data.dock_state           = "{\"active\":3}";   // session ids: not saved in workspaces
    const auto path           = temp_path("plain.spectra");
    ASSERT_TRUE(Workspace::save(path.string(), data));
    const std::string text = read_file(path);
    fs::remove(path);
    EXPECT_EQ(text.find("layout_only"), std::string::npos);
    EXPECT_EQ(text.find("dock_state"), std::string::npos);
    EXPECT_NE(text.find("\"point_count\": 3"), std::string::npos);
}

// A v3 workspace written by an older build: no layout keys, no axes_index,
// no bindings.
TEST(LayoutFile, LoadsExistingV3Workspace)
{
    const char* v3   = R"({
  "version": 3,
  "theme_name": "light",
  "active_figure_index": 0,
  "panels": {
    "inspector_visible": false,
    "inspector_width": 280,
    "nav_rail_expanded": true
  },
  "figures": [
    {
      "title": "",
      "width": 1024,
      "height": 600,
      "grid_rows": 1,
      "grid_cols": 2,
      "is_modified": true,
      "custom_tab_title": "Old tab",
      "axes": [
        {"x_min": 0, "x_max": 10, "y_min": -1, "y_max": 1, "auto_fit": false,
         "grid_visible": true, "x_label": "t", "y_label": "v", "title": "left"},
        {"x_min": 5, "x_max": 6, "y_min": 0, "y_max": 2, "auto_fit": false,
         "grid_visible": false, "x_label": "", "y_label": "", "title": ""}
      ],
      "series": [
        {"name": "sig", "type": "line", "color_r": 1, "color_g": 0, "color_b": 0,
         "color_a": 1, "line_width": 2, "marker_size": 6, "visible": true,
         "point_count": 500, "opacity": 1, "line_style": 1, "marker_style": 0,
         "dash_pattern": []}
      ]
    }
  ],
  "interaction": {"crosshair_enabled": true, "tooltip_enabled": false,
                  "markers": [], "annotations": []},
  "undo_count": 4,
  "redo_count": 0,
  "axis_link_state": "",
  "transforms": [],
  "shortcut_overrides": [],
  "timeline": {"playhead": 0, "duration": 10, "fps": 30, "loop_mode": 0,
               "loop_start": 0, "loop_end": 0, "playing": false},
  "plugin_state": "",
  "data_palette_name": "",
  "last_export_dir": ""
}
)";
    const auto  path = temp_path("v3.spectra");
    {
        std::ofstream f(path);
        f << v3;
    }
    WorkspaceData data;
    ASSERT_TRUE(Workspace::load(path.string(), data));
    fs::remove(path);

    EXPECT_EQ(data.version, 3u);
    EXPECT_FALSE(data.layout_only);
    EXPECT_EQ(data.layout_version, 0u);
    EXPECT_EQ(data.theme_name, "light");
    EXPECT_EQ(data.undo_count, 4u);
    ASSERT_EQ(data.figures.size(), 1u);
    ASSERT_EQ(data.figures[0].series.size(), 1u);
    EXPECT_EQ(data.figures[0].series[0].axes_index, -1);
    EXPECT_TRUE(data.figures[0].series[0].binding.empty());
    EXPECT_EQ(data.figures[0].series[0].point_count, 500u);
    EXPECT_EQ(data.figures[0].custom_tab_title, "Old tab");

    // Opened as a layout: same subplot grid and limits, no series, nothing
    // to resolve.
    FigureRegistry  registry;
    BindingResolver resolver(nullptr, nullptr);
    auto            result = instantiate_layout(data, registry, resolver);
    ASSERT_EQ(result.figure_ids.size(), 1u);
    EXPECT_TRUE(result.slots.empty());
    Figure* fig = registry.get(result.figure_ids[0]);
    ASSERT_NE(fig, nullptr);
    ASSERT_EQ(fig->axes().size(), 2u);
    EXPECT_FLOAT_EQ(fig->axes()[1]->x_limits().min, 5.0f);
    EXPECT_FALSE(fig->axes()[1]->grid_enabled());
    EXPECT_TRUE(fig->axes()[0]->series().empty());
}

// ─── Resolution ─────────────────────────────────────────────────────────────

TEST(LayoutResolve, TopicAndCsvBindingsWithFakeProviders)
{
    auto fig  = make_dashboard();
    auto data = capture_layout(*fig);

    FakeTopics topics;
    topics.results["imu/accel_x"] = TopicBindingProvider::Result::Subscribed;
    FakeCsv csv;
    csv.add("flight.csv",
            {"time", "temp", "alt"},
            {{0.0f, 1.0f, 2.0f, 3.0f}, {20.0f, 21.0f, 22.0f, 23.0f}, {5.0f, 6.0f, 7.0f, 8.0f}});

    FigureRegistry  registry;
    BindingResolver resolver(&topics, &csv);
    auto            result = instantiate_layout(data, registry, resolver);
    ASSERT_EQ(result.slots.size(), 3u);
    EXPECT_EQ(result.count(BindingStatus::Bound), 3u);

    ASSERT_EQ(topics.calls.size(), 1u);
    EXPECT_EQ(topics.calls[0].topic, "imu/accel_x");
    EXPECT_EQ(topics.calls[0].figure_id, result.figure_ids[0]);
    EXPECT_EQ(topics.calls[0].axes_index, 0u);

    Figure* opened = registry.get(result.figure_ids[0]);
    ASSERT_NE(opened, nullptr);
    EXPECT_EQ(topics.calls[0].series, opened->axes()[0]->series()[0].get());

    // CSV on axes 1, which has the saved transform: y * 2 + 1 (third step disabled).
    auto* temp = dynamic_cast<ScatterSeries*>(opened->axes()[1]->series()[0].get());
    ASSERT_NE(temp, nullptr);
    ASSERT_EQ(temp->point_count(), 4u);
    EXPECT_FLOAT_EQ(temp->x_data()[3], 3.0f);
    EXPECT_FLOAT_EQ(temp->y_data()[0], 41.0f);
    EXPECT_FLOAT_EQ(temp->y_data()[3], 47.0f);
    // No x column: row index.
    auto* alt = dynamic_cast<LineSeries*>(opened->axes()[1]->series()[1].get());
    ASSERT_NE(alt, nullptr);
    ASSERT_EQ(alt->point_count(), 4u);
    EXPECT_FLOAT_EQ(alt->x_data()[2], 2.0f);
    EXPECT_FLOAT_EQ(alt->y_data()[2], 15.0f);
    EXPECT_EQ(alt->label(), "alt");
}

TEST(LayoutResolve, UnresolvedBindingsAreLabelledAndCanBeReplaced)
{
    auto fig  = make_dashboard();
    auto data = capture_layout(*fig);

    FakeTopics topics;
    topics.results["imu/accel_x"] = TopicBindingProvider::Result::Unavailable;
    FakeCsv csv;   // flight.csv missing

    FigureRegistry  registry;
    BindingResolver resolver(&topics, &csv);
    auto            result = instantiate_layout(data, registry, resolver);
    ASSERT_EQ(result.slots.size(), 3u);
    EXPECT_EQ(result.count(BindingStatus::Unresolved), 3u);

    Figure* opened = registry.get(result.figure_ids[0]);
    Series* accel  = opened->axes()[0]->series()[0].get();
    EXPECT_EQ(accel->label(), "accel_x [unresolved: topic://imu/accel_x]");
    auto* temp = dynamic_cast<ScatterSeries*>(opened->axes()[1]->series()[0].get());
    EXPECT_EQ(temp->point_count(), 0u);
    EXPECT_NE(result.slots[1].result.message.find("not found"), std::string::npos);

    // Replacement picked: another CSV that has the column.
    csv.add("other.csv", {"temp"}, {{1.0f, 2.0f}});
    ASSERT_TRUE(rebind_slot(result.slots[1], "csv://other.csv#temp", registry, resolver));
    EXPECT_EQ(result.slots[1].result.status, BindingStatus::Bound);
    EXPECT_EQ(temp->label(), "temp");
    EXPECT_EQ(temp->data_binding(), "csv://other.csv#temp");
    ASSERT_EQ(temp->point_count(), 2u);
    EXPECT_FLOAT_EQ(temp->y_data()[1], 5.0f);   // the axes' transform still applies

    // A column that does not exist stays unresolved with a reason.
    ASSERT_TRUE(rebind_slot(result.slots[2], "csv://other.csv#nope", registry, resolver));
    EXPECT_EQ(result.slots[2].result.status, BindingStatus::Unresolved);
    EXPECT_NE(result.slots[2].result.message.find("nope"), std::string::npos);

    // ULog bindings are reserved, not resolved yet.
    ASSERT_TRUE(rebind_slot(result.slots[0], "ulog://vehicle_attitude.roll", registry, resolver));
    EXPECT_EQ(result.slots[0].result.status, BindingStatus::Unresolved);

    // Once the series is removed the slot can no longer be rebound.
    opened->axes_mut()[0]->clear_series();
    EXPECT_FALSE(rebind_slot(result.slots[0], "topic://a", registry, resolver));
}

TEST(LayoutResolve, NoProvidersMeansUnresolved)
{
    LineSeries      s;
    BindingResolver resolver(nullptr, nullptr);
    EXPECT_EQ(resolver.resolve("topic://a", s, 1, 0).status, BindingStatus::Unresolved);
    EXPECT_EQ(resolver.resolve("csv://a.csv#y", s, 1, 0).status, BindingStatus::Unresolved);
    EXPECT_EQ(resolver.resolve("garbage", s, 1, 0).status, BindingStatus::Unresolved);
}

TEST(LayoutResolve, FileCsvProviderSearchesDirsAndPickedFiles)
{
    const fs::path dir = temp_path("csvdir");
    fs::create_directories(dir);
    {
        std::ofstream f(dir / "log.csv");
        f << "time,value\n0,1\n1,4\n2,9\n";
    }
    FileCsvBindingProvider csv;
    EXPECT_TRUE(csv.locate("log.csv").empty());
    csv.add_search_dir(dir.string());
    csv.add_search_dir(dir.string());   // de-duplicated
    EXPECT_EQ(csv.search_dirs().size(), 1u);
    EXPECT_EQ(fs::path(csv.locate("log.csv")), dir / "log.csv");

    LineSeries      s;
    BindingResolver resolver(nullptr, &csv);
    auto            r = resolver.resolve("csv://log.csv?x=time#value", s, 1, 0);
    EXPECT_EQ(r.status, BindingStatus::Bound) << r.message;
    ASSERT_EQ(s.point_count(), 3u);
    EXPECT_FLOAT_EQ(s.y_data()[2], 9.0f);

    // A file the user picked for a missing name.
    const fs::path other = dir / "renamed.csv";
    fs::copy_file(dir / "log.csv", other);
    EXPECT_TRUE(csv.locate("teammate.csv").empty());
    csv.set_file("teammate.csv", other.string());
    EXPECT_EQ(fs::path(csv.locate("teammate.csv")), other);
    fs::remove_all(dir);
}

TEST(LayoutResolve, TransformStepsMatchDataTransforms)
{
    std::vector<float>               x = {0, 1, 2};
    std::vector<float>               y = {-3, 1, 5};
    std::vector<LayoutTransformStep> steps;
    steps.push_back({static_cast<int>(TransformType::Abs), 0.0f, true});
    steps.push_back({static_cast<int>(TransformType::Clamp), 4.0f, true});
    steps.push_back({999, 1.0f, true});                                       // unknown: skipped
    steps.push_back({static_cast<int>(TransformType::Custom), 1.0f, true});   // skipped
    apply_transform_steps(steps, x, y);
    ASSERT_EQ(y.size(), 3u);
    EXPECT_FLOAT_EQ(y[0], 3.0f);
    EXPECT_FLOAT_EQ(y[2], 4.0f);
}

// ─── Session helpers ────────────────────────────────────────────────────────

TEST(LayoutSession, SavedLabelsAndCsvReplacement)
{
    const fs::path cfg = temp_path("cfg");
    fs::create_directories(cfg);
    set_env("SPECTRA_CONFIG_DIR", cfg.string().c_str());

    auto fig  = make_dashboard();
    auto data = capture_layout(*fig);

    FigureRegistry registry;
    LayoutSession  session;
    session.set_figure_registry(&registry);
    auto result = instantiate_layout(data, registry, session.resolver());
    session.adopt(std::move(result));
    EXPECT_EQ(session.unresolved_count(), 3u);   // no topic provider, no CSV dirs
    EXPECT_TRUE(session.is_visible());

    Series* accel = nullptr;
    for (const auto& slot : session.slots())
    {
        if (slot.uri.rfind("topic://", 0) == 0)
            accel = find_series(registry, slot.figure_id, slot.series);
    }
    ASSERT_NE(accel, nullptr);
    EXPECT_NE(accel->label().find("[unresolved"), std::string::npos);
    session.with_saved_labels([&]() { EXPECT_EQ(accel->label(), "accel_x"); });
    EXPECT_NE(accel->label().find("[unresolved"), std::string::npos);

    // Locating flight.csv binds both CSV series.
    {
        std::ofstream f(cfg / "my_flight.csv");
        f << "time,temp,alt\n0,1,2\n1,3,4\n";
    }
    EXPECT_EQ(session.use_csv_file("flight.csv", (cfg / "my_flight.csv").string()), 2u);
    EXPECT_EQ(session.unresolved_count(), 1u);

    // Recent layouts live in the config dir.
    session.recent().push((cfg / "a.spectra").string());
    session.recent().push((cfg / "b.spectra").string());
    session.recent().push((cfg / "a.spectra").string());
    RecentLayouts reloaded(RecentLayouts::default_path());
    ASSERT_TRUE(reloaded.load());
    ASSERT_EQ(reloaded.entries().size(), 2u);
    EXPECT_EQ(fs::path(reloaded.entries()[0]).filename(), "a.spectra");

    // Requested opens run once a handler is wired.
    std::vector<std::string> opened;
    session.request_open("x.spectra");
    session.poll();   // no handler yet: kept
    EXPECT_TRUE(session.has_pending_open());
    session.set_open_handler(
        [&](const std::string& p)
        {
            opened.push_back(p);
            return true;
        });
    session.poll();
    ASSERT_EQ(opened.size(), 1u);
    EXPECT_FALSE(session.has_pending_open());

    // Removing a figure prunes its slots.
    for (auto id : registry.all_ids())
        registry.unregister_figure(id);
    session.prune();
    EXPECT_TRUE(session.slots().empty());

    unset_env("SPECTRA_CONFIG_DIR");
    fs::remove_all(cfg);
}

TEST(LayoutRecent, KeepsMostRecentFirstAndCaps)
{
    RecentLayouts r({}, 3);
    r.push("/a");
    r.push("/b");
    r.push("/c");
    r.push("/d");
    ASSERT_EQ(r.entries().size(), 3u);
    EXPECT_EQ(r.entries()[0], "/d");
    r.push("/c");
    EXPECT_EQ(r.entries()[0], "/c");
    EXPECT_EQ(r.entries().size(), 3u);
    r.remove("/c");
    EXPECT_EQ(r.entries()[0], "/d");
    r.clear();
    EXPECT_TRUE(r.empty());
}

TEST(LayoutDock, FigureIdsAreRemapped)
{
    const std::string dock =
        R"({"active":42,"root":{"id":1,"leaf":false,"dir":"h","ratio":0.5,)"
        R"("first":{"id":2,"leaf":true,"figure":42},"second":{"id":3,"leaf":true,"figure":7}}})";
    auto out = remap_dock_figures(dock,
                                  [](FigureId id) -> std::optional<FigureId>
                                  {
                                      if (id == 42)
                                          return 0;
                                      if (id == 7)
                                          return 1;
                                      return std::nullopt;
                                  });
    EXPECT_EQ(
        out,
        R"({"active":0,"root":{"id":1,"leaf":false,"dir":"h","ratio":0.5,)"
        R"("first":{"id":2,"leaf":true,"figure":0},"second":{"id":3,"leaf":true,"figure":1}}})");
    // Unknown ids are kept.
    EXPECT_EQ(remap_dock_figures(R"({"figure":5})", [](FigureId) { return std::nullopt; }),
              R"({"figure":5})");
}

// ─── Pending topic subscriptions ────────────────────────────────────────────

TEST(TopicRegistryPending, SubscriptionBeforeDeclareIsPromoted)
{
    daemon::TopicRegistry     reg;
    daemon::TopicSubscription sub{1, 0, 5};
    EXPECT_FALSE(reg.subscribe_or_defer("imu/a", sub, nullptr, nullptr));
    EXPECT_EQ(reg.pending_count("imu/a"), 1u);
    EXPECT_FALSE(reg.subscribe_or_defer("imu/a", sub, nullptr, nullptr));   // no duplicate
    EXPECT_EQ(reg.pending_count("imu/a"), 1u);

    reg.declare("imu/a", ipc::TopicKind::Scalar2D, "", 0, 9);
    EXPECT_EQ(reg.pending_count("imu/a"), 0u);
    std::vector<daemon::TopicSubscription> subs;
    ASSERT_TRUE(reg.publish("imu/a", {1.0, 2.0}, nullptr, &subs));
    ASSERT_EQ(subs.size(), 1u);
    EXPECT_EQ(subs[0], sub);

    // Existing topic: subscribed immediately.
    daemon::TopicSubscription sub2{1, 0, 6};
    EXPECT_TRUE(reg.subscribe_or_defer("imu/a", sub2, nullptr, nullptr));

    // Pending subscriptions can be dropped before the topic exists.
    daemon::TopicSubscription sub3{2, 0, 1};
    reg.subscribe_or_defer("later", sub3, nullptr, nullptr);
    reg.unsubscribe(sub3);
    EXPECT_EQ(reg.pending_count("later"), 0u);
    reg.subscribe_or_defer("later", sub3, nullptr, nullptr);
    reg.on_series_removed(2, 1);
    EXPECT_EQ(reg.pending_count("later"), 0u);
}
