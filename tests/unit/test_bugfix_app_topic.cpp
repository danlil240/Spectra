// test_bugfix_app_topic.cpp — Regression tests for the inproc topic server.
//
//  - Published samples are queued by the server thread and only applied to
//    series on the owning thread via drain_pending() (no worker-side access to
//    Figure / Axes / Series).
//  - Subscriptions survive series index shifts and are dropped when their
//    series is removed.
//  - A wired TopicsPanel unwires itself on destruction, so later topic
//    changes never call into a freed panel.

#ifndef _WIN32

    #include <gtest/gtest.h>

    #include <atomic>
    #include <chrono>
    #include <cstdlib>
    #include <memory>
    #include <spectra/axes.hpp>
    #include <spectra/figure.hpp>
    #include <spectra/figure_registry.hpp>
    #include <spectra/series.hpp>
    #include <string>
    #include <thread>
    #include <unistd.h>

    #include "app/inproc_topic_server.hpp"
    #include "ipc/codec.hpp"
    #include "ipc/message.hpp"
    #include "ipc/transport.hpp"
    #include "ui/topics/topics_panel.hpp"

using namespace spectra;

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
        char tmpl[] = "/tmp/spx-topic-XXXXXX";
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

// Minimal raw publisher speaking the publisher subset of the protocol.
class RawPublisher
{
   public:
    bool connect(const std::string& path)
    {
        conn_ = ipc::Client::connect(path);
        if (!conn_ || !conn_->is_open())
            return false;
        conn_->set_recv_timeout_ms(2000);
        conn_->set_stall_timeout_ms(2000);
        ipc::HelloPayload hello;
        hello.client_type = "publisher";
        if (!send(ipc::MessageType::HELLO, ipc::encode_hello(hello)))
            return false;
        auto reply = conn_->recv();
        return reply && reply->header.type == ipc::MessageType::WELCOME;
    }

    bool declare(const std::string& name)
    {
        ipc::ReqDeclareTopicPayload p;
        p.name = name;
        return send(ipc::MessageType::REQ_DECLARE_TOPIC, ipc::encode_req_declare_topic(p))
               && expect_ok();
    }

    bool publish(const std::string& name, std::vector<double> samples)
    {
        ipc::ReqPublishTopicSamplesPayload p;
        p.name    = name;
        p.samples = std::move(samples);
        return send(ipc::MessageType::REQ_PUBLISH_TOPIC_SAMPLES,
                    ipc::encode_req_publish_topic_samples(p))
               && expect_ok();
    }

    void close() { conn_.reset(); }

   private:
    bool send(ipc::MessageType type, std::vector<uint8_t> payload)
    {
        ipc::Message m;
        m.header.type        = type;
        m.header.request_id  = ++req_id_;
        m.payload            = std::move(payload);
        m.header.payload_len = static_cast<uint32_t>(m.payload.size());
        return conn_->send(m);
    }

    bool expect_ok()
    {
        auto reply = conn_->recv();
        return reply && reply->header.type == ipc::MessageType::RESP_OK;
    }

    std::unique_ptr<ipc::Connection> conn_;
    ipc::RequestId                   req_id_ = 0;
};

uint32_t subscriber_count(const InprocTopicServer& srv, const std::string& name)
{
    for (const auto& t : srv.topic_snapshot())
        if (t.name == name)
            return t.subscriber_count;
    return 0;
}

LineSeries* line_at(Axes& ax, size_t i)
{
    return dynamic_cast<LineSeries*>(ax.series()[i].get());
}

// Drain into the series and apply the thread-safe pending buffers, as the
// main loop does each frame.
void drain_and_commit(InprocTopicServer& srv, Axes& ax)
{
    srv.drain_pending();
    for (const auto& s : ax.series())
        s->commit_pending();
}

class InprocTopicServerTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        auto fig = std::make_unique<Figure>();
        ax_      = &fig->subplot(1, 1, 1);
        fig_id_  = registry_.register_figure(std::move(fig));
        path_    = server_.start(&registry_);
        ASSERT_FALSE(path_.empty());
        panel_ = std::make_unique<ui::topics::TopicsPanel>();
        server_.wire_topics_panel(*panel_, &registry_);
        ASSERT_TRUE(pub_.connect(path_));
    }

    void TearDown() override
    {
        pub_.close();
        server_.stop();
        panel_.reset();
    }

    ScopedRuntimeDir                         runtime_dir_;
    FigureRegistry                           registry_;
    InprocTopicServer                        server_;
    std::unique_ptr<ui::topics::TopicsPanel> panel_;
    RawPublisher                             pub_;
    Axes*                                    ax_     = nullptr;
    uint64_t                                 fig_id_ = 0;
    std::string                              path_;
};

}   // namespace

TEST_F(InprocTopicServerTest, SamplesAreQueuedUntilDrainedOnOwnerThread)
{
    ASSERT_TRUE(pub_.declare("a"));
    ASSERT_TRUE(panel_->submit_subscribe("a", fig_id_, 0));
    ASSERT_EQ(ax_->series().size(), 1u);
    LineSeries* ls = line_at(*ax_, 0);
    ASSERT_NE(ls, nullptr);

    ASSERT_TRUE(pub_.publish("a", {1.0, 10.0, 2.0, 20.0, 3.0, 30.0}));
    // RESP_OK is sent after queueing, so the batch is already pending.
    EXPECT_EQ(server_.pending_point_count(), 3u);
    ls->commit_pending();
    EXPECT_EQ(ls->point_count(), 0u) << "server thread must not append to series";

    drain_and_commit(server_, *ax_);
    EXPECT_EQ(server_.pending_point_count(), 0u);
    ASSERT_EQ(ls->point_count(), 3u);
    EXPECT_FLOAT_EQ(ls->x_data()[2], 3.0f);
    EXPECT_FLOAT_EQ(ls->y_data()[2], 30.0f);
}

TEST_F(InprocTopicServerTest, SubscriptionFollowsSeriesAfterIndexShift)
{
    ASSERT_TRUE(pub_.declare("a"));
    ASSERT_TRUE(pub_.declare("b"));
    ASSERT_TRUE(panel_->submit_subscribe("a", fig_id_, 0));
    ASSERT_TRUE(panel_->submit_subscribe("b", fig_id_, 0));
    ASSERT_EQ(ax_->series().size(), 2u);
    LineSeries* lb = line_at(*ax_, 1);

    // Remove "a"'s series: "b"'s series moves from index 1 to 0.
    ASSERT_TRUE(ax_->remove_series(0));
    ASSERT_EQ(ax_->series().size(), 1u);
    ASSERT_EQ(line_at(*ax_, 0), lb);

    ASSERT_TRUE(pub_.publish("a", {1.0, 1.0}));
    ASSERT_TRUE(pub_.publish("b", {5.0, 50.0, 6.0, 60.0}));
    drain_and_commit(server_, *ax_);

    // Only "b"'s samples land in "b"'s series.
    ASSERT_EQ(lb->point_count(), 2u);
    EXPECT_FLOAT_EQ(lb->y_data()[0], 50.0f);

    // The orphaned "a" subscription was dropped; "b" stays.
    EXPECT_EQ(subscriber_count(server_, "a"), 0u);
    EXPECT_EQ(subscriber_count(server_, "b"), 1u);
}

TEST_F(InprocTopicServerTest, UnregisteredFigureDropsSubscription)
{
    ASSERT_TRUE(pub_.declare("a"));
    ASSERT_TRUE(panel_->submit_subscribe("a", fig_id_, 0));
    EXPECT_EQ(subscriber_count(server_, "a"), 1u);

    registry_.unregister_figure(fig_id_);
    ax_ = nullptr;

    ASSERT_TRUE(pub_.publish("a", {1.0, 1.0}));
    server_.drain_pending();   // must not touch the freed figure
    EXPECT_EQ(subscriber_count(server_, "a"), 0u);
}

TEST_F(InprocTopicServerTest, PanelIsUpdatedAndUnwiredOnDestruction)
{
    std::atomic<int> calls{0};
    auto             count = [&]() { calls.fetch_add(1); };

    ASSERT_TRUE(pub_.declare("a"));
    EXPECT_TRUE(panel_->has_subscribe_callback());

    // Destroy the panel while the server keeps running (window closed).
    panel_.reset();

    // Further topic changes on the worker must not call into the freed panel.
    ASSERT_TRUE(pub_.declare("b"));
    pub_.close();   // disconnect -> notify on the worker thread

    // The server still accepts a new change callback after unwiring.
    server_.set_topic_changed_fn(count);
    RawPublisher p2;
    ASSERT_TRUE(p2.connect(path_));
    ASSERT_TRUE(p2.declare("c"));
    // notify runs on the worker right after RESP_OK; wait for it.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (calls.load() == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_GE(calls.load(), 1);
    p2.close();
    server_.stop();
}

TEST_F(InprocTopicServerTest, StopUnwiresLivePanel)
{
    server_.stop();
    EXPECT_FALSE(panel_->has_subscribe_callback());
    // Destroying the panel after stop() must not call back into the server.
    panel_.reset();
}

#endif   // !_WIN32
