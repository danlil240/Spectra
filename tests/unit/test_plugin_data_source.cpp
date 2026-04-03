// test_plugin_data_source.cpp — Unit tests for DataSourceRegistry and Plugin C ABI (C2 / C3)

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "adapters/adapter_interface.hpp"
#include "adapters/data_source_registry.hpp"
#include "ui/workspace/plugin_api.hpp"

using namespace spectra;

// ─── Mock concrete adapter ───────────────────────────────────────────────────

struct MockDataSourceState
{
    bool                   running = false;
    std::vector<DataPoint> pending_points;
    bool                   build_ui_called = false;
};

class MockDataSourceAdapter : public DataSourceAdapter
{
   public:
    explicit MockDataSourceAdapter(const std::string& name, MockDataSourceState& state)
        : name_(name), state_(state)
    {
    }

    const char* name() const override { return name_.c_str(); }
    void        start() override { state_.running = true; }
    void        stop() override { state_.running = false; }
    bool        is_running() const override { return state_.running; }

    std::vector<DataPoint> poll() override
    {
        auto pts = state_.pending_points;
        state_.pending_points.clear();
        return pts;
    }

    void build_ui() override { state_.build_ui_called = true; }

   private:
    std::string          name_;
    MockDataSourceState& state_;
};

// ─── DataSourceRegistry tests ─────────────────────────────────────────────────

TEST(DataSourceRegistry, RegisterAndCount)
{
    DataSourceRegistry  reg;
    MockDataSourceState state;

    EXPECT_EQ(reg.count(), 0u);
    reg.register_source(std::make_unique<MockDataSourceAdapter>("Sensor A", state));
    EXPECT_EQ(reg.count(), 1u);
}

TEST(DataSourceRegistry, RegisterDuplicateIsIgnored)
{
    DataSourceRegistry  reg;
    MockDataSourceState stateA;
    MockDataSourceState stateB;

    reg.register_source(std::make_unique<MockDataSourceAdapter>("Sensor A", stateA));
    reg.register_source(std::make_unique<MockDataSourceAdapter>("Sensor A", stateB));
    EXPECT_EQ(reg.count(), 1u);
}

TEST(DataSourceRegistry, SourceNames)
{
    DataSourceRegistry  reg;
    MockDataSourceState s1, s2;

    reg.register_source(std::make_unique<MockDataSourceAdapter>("Alpha", s1));
    reg.register_source(std::make_unique<MockDataSourceAdapter>("Beta", s2));

    auto names = reg.source_names();
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "Alpha");
    EXPECT_EQ(names[1], "Beta");
}

TEST(DataSourceRegistry, Find)
{
    DataSourceRegistry  reg;
    MockDataSourceState state;

    reg.register_source(std::make_unique<MockDataSourceAdapter>("MySensor", state));

    DataSourceAdapter* found = reg.find("MySensor");
    ASSERT_NE(found, nullptr);
    EXPECT_STREQ(found->name(), "MySensor");

    EXPECT_EQ(reg.find("NoSuchSensor"), nullptr);
}

TEST(DataSourceRegistry, UnregisterRemovesSource)
{
    DataSourceRegistry  reg;
    MockDataSourceState state;

    reg.register_source(std::make_unique<MockDataSourceAdapter>("TempSensor", state));
    EXPECT_EQ(reg.count(), 1u);

    reg.unregister_source("TempSensor");
    EXPECT_EQ(reg.count(), 0u);
    EXPECT_EQ(reg.find("TempSensor"), nullptr);
}

TEST(DataSourceRegistry, UnregisterStopsRunningAdapter)
{
    DataSourceRegistry  reg;
    MockDataSourceState state;

    reg.register_source(std::make_unique<MockDataSourceAdapter>("LiveSensor", state));
    reg.find("LiveSensor")->start();
    EXPECT_TRUE(state.running);

    reg.unregister_source("LiveSensor");
    EXPECT_FALSE(state.running);
}

TEST(DataSourceRegistry, PollAllCollectsFromRunningSourcesOnly)
{
    DataSourceRegistry  reg;
    MockDataSourceState stateA, stateB;

    reg.register_source(std::make_unique<MockDataSourceAdapter>("A", stateA));
    reg.register_source(std::make_unique<MockDataSourceAdapter>("B", stateB));

    // Only start A; B is idle
    reg.find("A")->start();

    stateA.pending_points.push_back({"series1", 1.0, 42.0f});
    stateA.pending_points.push_back({"series2", 2.0, 7.5f});
    stateB.pending_points.push_back({"series3", 3.0, 0.0f});   // should NOT appear

    auto pts = reg.poll_all();
    ASSERT_EQ(pts.size(), 2u);
    EXPECT_EQ(pts[0].series_label, "series1");
    EXPECT_FLOAT_EQ(pts[0].value, 42.0f);
    EXPECT_EQ(pts[1].series_label, "series2");
    EXPECT_FLOAT_EQ(pts[1].value, 7.5f);
}

TEST(DataSourceRegistry, PollAllReturnsEmptyWhenNoSourcesRunning)
{
    DataSourceRegistry  reg;
    MockDataSourceState state;
    reg.register_source(std::make_unique<MockDataSourceAdapter>("Idle", state));

    auto pts = reg.poll_all();
    EXPECT_TRUE(pts.empty());
}

TEST(DataSourceAdapter, StartStopLifecycle)
{
    MockDataSourceState   state;
    MockDataSourceAdapter adapter("Test", state);

    EXPECT_FALSE(adapter.is_running());
    adapter.start();
    EXPECT_TRUE(adapter.is_running());
    adapter.stop();
    EXPECT_FALSE(adapter.is_running());
}

TEST(DataSourceAdapter, BuildUI)
{
    MockDataSourceState   state;
    MockDataSourceAdapter adapter("Test", state);

    EXPECT_FALSE(state.build_ui_called);
    adapter.build_ui();
    EXPECT_TRUE(state.build_ui_called);
}

// ─── C ABI data source tests ──────────────────────────────────────────────────

struct CAPISourceState
{
    bool running         = false;
    bool start_called    = false;
    bool stop_called     = false;
    bool build_ui_called = false;
    // Points to return on next poll
    std::vector<std::pair<const char*, float>> points;
};

static void capi_start(void* ud)
{
    auto* s         = static_cast<CAPISourceState*>(ud);
    s->running      = true;
    s->start_called = true;
}
static void capi_stop(void* ud)
{
    auto* s        = static_cast<CAPISourceState*>(ud);
    s->running     = false;
    s->stop_called = true;
}
static int capi_is_running(void* ud)
{
    return static_cast<CAPISourceState*>(ud)->running ? 1 : 0;
}
static size_t capi_poll(SpectraDataPoint* out_pts, size_t max_pts, void* ud)
{
    auto*  s = static_cast<CAPISourceState*>(ud);
    size_t n = std::min(s->points.size(), max_pts);
    for (size_t i = 0; i < n; ++i)
    {
        out_pts[i].series_label = s->points[i].first;
        out_pts[i].timestamp    = static_cast<double>(i) * 0.1;
        out_pts[i].value        = s->points[i].second;
    }
    s->points.erase(s->points.begin(), s->points.begin() + static_cast<ptrdiff_t>(n));
    return n;
}
static void capi_build_ui(void* ud)
{
    static_cast<CAPISourceState*>(ud)->build_ui_called = true;
}

static SpectraDataSourceDesc make_desc(const char* name, CAPISourceState* state)
{
    SpectraDataSourceDesc desc{};
    desc.name          = name;
    desc.start_fn      = capi_start;
    desc.stop_fn       = capi_stop;
    desc.is_running_fn = capi_is_running;
    desc.poll_fn       = capi_poll;
    desc.build_ui_fn   = capi_build_ui;
    desc.user_data     = state;
    return desc;
}

TEST(PluginDataSourceCAPI, RegisterDataSource)
{
    DataSourceRegistry reg;
    CAPISourceState    state;
    auto               desc = make_desc("CAPI Sensor", &state);

    int result = spectra_register_data_source(&reg, &desc);
    EXPECT_EQ(result, 0);
    EXPECT_EQ(reg.count(), 1u);

    auto names = reg.source_names();
    ASSERT_EQ(names.size(), 1u);
    EXPECT_EQ(names[0], "CAPI Sensor");
}

TEST(PluginDataSourceCAPI, RegisterNullRegistryReturnsError)
{
    CAPISourceState state;
    auto            desc = make_desc("X", &state);
    EXPECT_EQ(spectra_register_data_source(nullptr, &desc), -1);
}

TEST(PluginDataSourceCAPI, RegisterNullDescReturnsError)
{
    DataSourceRegistry reg;
    EXPECT_EQ(spectra_register_data_source(&reg, nullptr), -1);
}

TEST(PluginDataSourceCAPI, RegisterMissingRequiredFnsReturnsError)
{
    DataSourceRegistry    reg;
    SpectraDataSourceDesc desc{};
    desc.name     = "Broken";
    desc.start_fn = capi_start;
    // Missing stop_fn, is_running_fn, poll_fn
    EXPECT_EQ(spectra_register_data_source(&reg, &desc), -1);
}

TEST(PluginDataSourceCAPI, RegisterMissingNameReturnsError)
{
    DataSourceRegistry    reg;
    CAPISourceState       state;
    SpectraDataSourceDesc desc{};
    // Both name and name_fn are null
    desc.start_fn      = capi_start;
    desc.stop_fn       = capi_stop;
    desc.is_running_fn = capi_is_running;
    desc.poll_fn       = capi_poll;
    desc.user_data     = &state;
    EXPECT_EQ(spectra_register_data_source(&reg, &desc), -1);
}

TEST(PluginDataSourceCAPI, StartStopIsRunning)
{
    DataSourceRegistry reg;
    CAPISourceState    state;
    auto               desc = make_desc("CAPI Sensor", &state);
    spectra_register_data_source(&reg, &desc);

    DataSourceAdapter* adapter = reg.find("CAPI Sensor");
    ASSERT_NE(adapter, nullptr);

    EXPECT_FALSE(adapter->is_running());
    adapter->start();
    EXPECT_TRUE(state.start_called);
    EXPECT_TRUE(adapter->is_running());

    adapter->stop();
    EXPECT_TRUE(state.stop_called);
    EXPECT_FALSE(adapter->is_running());
}

TEST(PluginDataSourceCAPI, PollReturnsDataPoints)
{
    DataSourceRegistry reg;
    CAPISourceState    state;
    state.points = {{"ch1", 1.5f}, {"ch2", 3.0f}};

    auto desc = make_desc("CAPI Sensor", &state);
    spectra_register_data_source(&reg, &desc);

    DataSourceAdapter* adapter = reg.find("CAPI Sensor");
    ASSERT_NE(adapter, nullptr);

    adapter->start();
    auto pts = adapter->poll();
    adapter->stop();

    ASSERT_EQ(pts.size(), 2u);
    EXPECT_EQ(pts[0].series_label, "ch1");
    EXPECT_FLOAT_EQ(pts[0].value, 1.5f);
    EXPECT_EQ(pts[1].series_label, "ch2");
    EXPECT_FLOAT_EQ(pts[1].value, 3.0f);
}

TEST(PluginDataSourceCAPI, BuildUI)
{
    DataSourceRegistry reg;
    CAPISourceState    state;
    auto               desc = make_desc("CAPI Sensor", &state);
    spectra_register_data_source(&reg, &desc);

    DataSourceAdapter* adapter = reg.find("CAPI Sensor");
    ASSERT_NE(adapter, nullptr);

    EXPECT_FALSE(state.build_ui_called);
    adapter->build_ui();
    EXPECT_TRUE(state.build_ui_called);
}

TEST(PluginDataSourceCAPI, UnregisterRemovesSource)
{
    DataSourceRegistry reg;
    CAPISourceState    state;
    auto               desc = make_desc("TempSource", &state);
    spectra_register_data_source(&reg, &desc);
    ASSERT_EQ(reg.count(), 1u);

    int result = spectra_unregister_data_source(&reg, "TempSource");
    EXPECT_EQ(result, 0);
    EXPECT_EQ(reg.count(), 0u);
}

TEST(PluginDataSourceCAPI, UnregisterNullRegistryReturnsError)
{
    EXPECT_EQ(spectra_unregister_data_source(nullptr, "X"), -1);
}

TEST(PluginDataSourceCAPI, UnregisterNullNameReturnsError)
{
    DataSourceRegistry reg;
    EXPECT_EQ(spectra_unregister_data_source(&reg, nullptr), -1);
}

TEST(PluginDataSourceCAPI, PollAllViaRegistryWithCABIAdapter)
{
    DataSourceRegistry reg;
    CAPISourceState    state;
    state.points = {{"accel", 9.81f}};

    auto desc = make_desc("IMU", &state);
    spectra_register_data_source(&reg, &desc);

    reg.find("IMU")->start();
    auto pts = reg.poll_all();

    ASSERT_EQ(pts.size(), 1u);
    EXPECT_EQ(pts[0].series_label, "accel");
    EXPECT_FLOAT_EQ(pts[0].value, 9.81f);
}
