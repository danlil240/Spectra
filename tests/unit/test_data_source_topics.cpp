// test_data_source_topics.cpp — Plugin data sources published as topics.
//
// DataSourceRegistry adapters are polled by the app each frame and their
// DataPoints published as topics "<source>/<series label>" on the in-process
// topic server, so a subscribed series receives what poll() returned.

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "adapters/adapter_interface.hpp"
#include "adapters/data_source_registry.hpp"
#include "adapters/data_source_topics.hpp"
#include "daemon/bridges/topic_ingest.hpp"
#include "ui/topics/topics_panel.hpp"

#ifndef _WIN32
    #include <cstdlib>
    #include <spectra/axes.hpp>
    #include <spectra/figure.hpp>
    #include <spectra/figure_registry.hpp>
    #include <spectra/series.hpp>
    #include <unistd.h>

    #include "app/inproc_topic_server.hpp"
#endif

using namespace spectra;

namespace
{

struct SourceState
{
    bool                   running = false;
    std::vector<DataPoint> pending;
    int                    starts = 0;
};

class FakeSource : public DataSourceAdapter
{
   public:
    FakeSource(std::string name, SourceState& st) : name_(std::move(name)), st_(st) {}
    const char* name() const override { return name_.c_str(); }
    void        start() override
    {
        st_.running = true;
        ++st_.starts;
    }
    void                   stop() override { st_.running = false; }
    bool                   is_running() const override { return st_.running; }
    std::vector<DataPoint> poll() override
    {
        auto out = std::move(st_.pending);
        st_.pending.clear();
        return out;
    }

   private:
    std::string  name_;
    SourceState& st_;
};

class ThrowingStartSource : public DataSourceAdapter
{
   public:
    const char*            name() const override { return "Broken"; }
    void                   start() override { throw std::runtime_error("boom"); }
    void                   stop() override {}
    bool                   is_running() const override { return false; }
    std::vector<DataPoint> poll() override { return {}; }
};

}   // namespace

TEST(DataSourceTopics, TopicNameJoinsSourceAndLabel)
{
    EXPECT_EQ(data_source_topic_name("Sine Generator", "sin"), "Sine Generator/sin");
    EXPECT_EQ(data_source_topic_name("Sine", ""), "Sine");
    EXPECT_EQ(data_source_topic_name("Sine", "a/b"), "Sine/a/b");
}

TEST(DataSourceTopics, PollAllBySourceKeepsSourcesApart)
{
    DataSourceRegistry reg;
    SourceState        a, b, idle;
    reg.register_source(std::make_unique<FakeSource>("A", a));
    reg.register_source(std::make_unique<FakeSource>("B", b));
    reg.register_source(std::make_unique<FakeSource>("Idle", idle));
    a.running = b.running = idle.running = true;
    a.pending                            = {{"x", 1.0, 10.0f}};
    b.pending                            = {{"y", 2.0, 20.0f}, {"y", 3.0, 30.0f}};

    auto batches = reg.poll_all_by_source();
    ASSERT_EQ(batches.size(), 2u);   // "Idle" returned nothing
    EXPECT_EQ(batches[0].source, "A");
    EXPECT_EQ(batches[1].source, "B");
    EXPECT_EQ(batches[1].points.size(), 2u);
}

TEST(DataSourceTopics, StatusAndSetRunning)
{
    DataSourceRegistry reg;
    SourceState        a;
    reg.register_source(std::make_unique<FakeSource>("A", a));
    reg.register_source(std::make_unique<ThrowingStartSource>());

    auto st = reg.status();
    ASSERT_EQ(st.size(), 2u);
    EXPECT_FALSE(st[0].running);

    EXPECT_TRUE(reg.set_running("A", true));
    EXPECT_TRUE(a.running);
    EXPECT_TRUE(reg.status()[0].running);
    EXPECT_TRUE(reg.set_running("A", false));
    EXPECT_FALSE(a.running);
    EXPECT_FALSE(reg.set_running("missing", true));

    // A source that throws from start() is marked faulted, not propagated.
    EXPECT_TRUE(reg.set_running("Broken", true));
    EXPECT_TRUE(reg.status()[1].faulted);
    EXPECT_FALSE(reg.set_running("Broken", true));
}

TEST(DataSourceTopics, PumpPublishesPolledPointsAsTopics)
{
    DataSourceRegistry reg;
    SourceState        st;
    reg.register_source(std::make_unique<FakeSource>("Sine", st));

    daemon::TopicIngestQueue queue;
    // Not running: nothing is polled.
    st.pending = {{"sin", 0.0, 1.0f}};
    EXPECT_EQ(pump_data_sources_to_topics(reg, queue), 0u);
    EXPECT_EQ(queue.size(), 0u);

    ASSERT_TRUE(reg.set_running("Sine", true));
    st.pending = {{"sin", 0.5, 2.0f}, {"cos", 0.5, -1.0f}, {"sin", 1.0, 3.0f}};
    EXPECT_EQ(pump_data_sources_to_topics(reg, queue), 3u);

    daemon::TopicRegistry topics;
    auto                  r = daemon::apply_ingest_batches(topics, queue.drain());
    EXPECT_TRUE(r.topic_list_changed);
    EXPECT_EQ(r.samples_published, 3u);

    std::vector<double>       sin_ring;
    daemon::TopicSubscription probe{1, 0, 0};
    ASSERT_TRUE(topics.subscribe("Sine/sin", probe, nullptr, &sin_ring));
    EXPECT_EQ(sin_ring, (std::vector<double>{0.5, 2.0, 1.0, 3.0}));
    EXPECT_TRUE(topics.exists("Sine/cos"));
}

TEST(DataSourceTopics, PanelTogglesDataSources)
{
    DataSourceRegistry      reg;
    SourceState             st;
    ui::topics::TopicsPanel panel;
    reg.register_source(std::make_unique<FakeSource>("Sine", st));

    EXPECT_FALSE(panel.has_data_source_callbacks());
    EXPECT_FALSE(panel.submit_data_source_toggle("Sine", true));

    panel.set_data_source_callbacks(
        [&reg]()
        {
            std::vector<ui::topics::DataSourceRow> rows;
            for (auto& s : reg.status())
                rows.push_back({s.name, s.running, s.faulted});
            return rows;
        },
        [&reg](const std::string& name, bool start) { reg.set_running(name, start); });
    EXPECT_TRUE(panel.has_data_source_callbacks());
    EXPECT_TRUE(panel.submit_data_source_toggle("Sine", true));
    EXPECT_TRUE(st.running);
    EXPECT_EQ(st.starts, 1);
    EXPECT_TRUE(panel.submit_data_source_toggle("Sine", false));
    EXPECT_FALSE(st.running);
}

#ifndef _WIN32

namespace
{

class ScopedRuntimeDir
{
   public:
    ScopedRuntimeDir()
    {
        if (const char* old = std::getenv("XDG_RUNTIME_DIR"))
        {
            had_old_ = true;
            old_     = old;
        }
        char tmpl[] = "/tmp/spx-dstopic-XXXXXX";
        if (char* d = ::mkdtemp(tmpl))
            dir_ = d;
        ::setenv("XDG_RUNTIME_DIR", dir_.c_str(), 1);
    }
    ~ScopedRuntimeDir()
    {
        if (had_old_)
            ::setenv("XDG_RUNTIME_DIR", old_.c_str(), 1);
        else
            ::unsetenv("XDG_RUNTIME_DIR");
        if (!dir_.empty())
            ::rmdir(dir_.c_str());
    }

   private:
    bool        had_old_ = false;
    std::string old_;
    std::string dir_;
};

}   // namespace

TEST(DataSourceTopics, PolledPointsReachSubscribedSeries)
{
    ScopedRuntimeDir  runtime_dir;
    FigureRegistry    figures;
    auto              fig    = std::make_unique<Figure>();
    Axes&             ax     = fig->subplot(1, 1, 1);
    const uint64_t    fig_id = figures.register_figure(std::move(fig));
    InprocTopicServer server;
    ASSERT_FALSE(server.start(&figures).empty());
    ui::topics::TopicsPanel panel;
    server.wire_topics_panel(panel, &figures);

    DataSourceRegistry reg;
    SourceState        st;
    reg.register_source(std::make_unique<FakeSource>("Sine", st));
    ASSERT_TRUE(reg.set_running("Sine", true));

    // First frame: the topic appears (declared on first sight).
    st.pending = {{"sin", 0.0, 0.0f}};
    EXPECT_EQ(pump_data_sources_to_topics(reg, server), 1u);
    auto topics = server.topic_snapshot();
    ASSERT_EQ(topics.size(), 1u);
    EXPECT_EQ(topics[0].name, "Sine/sin");
    EXPECT_TRUE(topics[0].publisher_online);

    // The user drags it onto the axes: the retained sample primes the series.
    ASSERT_TRUE(panel.submit_subscribe("Sine/sin", fig_id, 0));
    ASSERT_EQ(ax.series().size(), 1u);
    auto* line = dynamic_cast<LineSeries*>(ax.series()[0].get());
    ASSERT_NE(line, nullptr);

    // Later frames: poll() output flows into the series on the owner thread.
    st.pending = {{"sin", 1.0, 0.5f}, {"sin", 2.0, 1.0f}};
    EXPECT_EQ(pump_data_sources_to_topics(reg, server), 2u);
    server.drain_pending();
    line->commit_pending();
    ASSERT_EQ(line->point_count(), 3u);
    EXPECT_FLOAT_EQ(line->x_data()[2], 2.0f);
    EXPECT_FLOAT_EQ(line->y_data()[2], 1.0f);

    server.stop();
}

#endif   // !_WIN32
