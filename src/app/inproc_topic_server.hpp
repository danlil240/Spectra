// inproc_topic_server.hpp — Topics support for in-process Spectra applications.
//
// Runs a lightweight Unix-socket server inside the process so external
// publishers (spectra::Publisher) can push samples to the running app without
// a separate spectra-backend daemon.
//
// The server:
//   - Listens on $XDG_RUNTIME_DIR/spectra-<pid>.sock (same path pattern as
//     the daemon, so publishers auto-discover it via publisher_client.cpp)
//   - Accepts publisher connections in a background thread
//   - Handles HELLO / REQ_DECLARE_TOPIC / REQ_PUBLISH_TOPIC_SAMPLES /
//     REQ_LIST_TOPICS (all the publisher-side messages)
//   - Queues published samples per subscription.  The worker thread never
//     touches Figure / Axes / Series: the owning (main) thread calls
//     drain_pending() once per frame, which resolves each subscription to its
//     live Series and appends there
//   - Calls a topic_changed callback so the TopicsPanel stays up-to-date.  The
//     callback is invoked under a mutex that stop() and panel destruction also
//     take, so it can never run against a destroyed panel
//
// Subscribe wiring is done from the outside (see wire_topics_panel() below):
//   the panel's subscribe callback looks up the figure/axes by ID, creates a
//   series, and registers the subscription here.  The wiring is undone
//   automatically when the panel is destroyed (e.g. its window is closed).

#pragma once

#ifndef _WIN32

    #include <atomic>
    #include <functional>
    #include <memory>
    #include <mutex>
    #include <string>
    #include <thread>
    #include <unordered_map>
    #include <vector>

    #include "../daemon/topic_registry.hpp"
    #include "../ipc/message.hpp"
    #include "../ipc/transport.hpp"
    #include "../ui/topics/topics_panel.hpp"

namespace spectra
{

class AxesBase;
class FigureRegistry;
class Series;

class InprocTopicServer
{
   public:
    // Called whenever the topic list changes (declare / disconnect / subscribe).
    using TopicChangedFn = std::function<void()>;

    InprocTopicServer();
    ~InprocTopicServer();

    // Start the background listener.  Returns the socket path (empty on error).
    // Typically called once from App::init_inproc().
    std::string start(FigureRegistry* fig_registry);

    // Stop the server and join the background thread.
    void stop();

    bool is_running() const { return running_.load(std::memory_order_relaxed); }

    // Set callback fired on topic list changes (declare / disconnect).
    // May be invoked from the server thread.
    void set_topic_changed_fn(TopicChangedFn fn);

    // Wire a TopicsPanel: sets list_request_callback + subscribe_request_callback.
    // Main thread only.  The panel is unwired automatically when it is
    // destroyed, and by stop().
    void wire_topics_panel(ui::topics::TopicsPanel& panel, FigureRegistry* fig_registry);

    // Deliver samples queued by the server thread to their subscribed series.
    // Must be called on the thread that owns the figures (the main / app
    // thread), typically once per frame before pending series data is
    // committed.  Subscriptions whose figure, axes or series no longer exist
    // are dropped.
    void drain_pending();

    // Number of interleaved (x, y) points waiting for drain_pending().
    size_t pending_point_count() const;

    // Get a snapshot of current topics (for the panel's list_request callback).
    std::vector<ipc::TopicInfoEntry> topic_snapshot() const;

   private:
    void run_loop();
    void handle_message(int fd, ipc::Connection& conn, uint64_t client_id, const ipc::Message& msg);
    void on_client_disconnected(uint64_t client_id);
    void notify_topic_changed();
    void unwire_panel();
    void subscribe_from_panel(const ui::topics::SubscribeRequest& req);

    // Send helpers (no daemon dependency).
    static void send_ok(ipc::Connection& conn, ipc::RequestId req_id);
    static void send_err(ipc::Connection& conn, ipc::RequestId req_id, const std::string& reason);
    static void send_welcome(ipc::Connection& conn);
    void        send_topic_list(ipc::Connection& conn, ipc::RequestId req_id);

    ipc::Server       server_;
    std::thread       thread_;
    std::atomic<bool> running_{false};
    std::string       socket_path_;

    mutable std::mutex    reg_mu_;
    daemon::TopicRegistry registry_;
    uint64_t              next_client_id_{1};

    // Samples published by the server thread, waiting for drain_pending().
    // `sub.series_index` carries the subscription id (see bindings_).
    struct PendingBatch
    {
        daemon::TopicSubscription sub;
        std::vector<float>        xy;   // interleaved x, y
    };
    mutable std::mutex        pending_mu_;
    std::vector<PendingBatch> pending_;
    size_t                    pending_points_ = 0;

    // Subscriptions created through the panel.  Main thread only.  The
    // registry key's series_index is a unique subscription id (not a
    // positional index), so it stays valid when series are added or removed;
    // the target is identified by object identity and re-validated against
    // the live figure before every append.
    struct Binding
    {
        daemon::TopicSubscription sub;
        uint64_t                  figure_id = 0;
        const AxesBase*           axes      = nullptr;
        const Series*             series    = nullptr;
    };
    std::unordered_map<uint32_t /*sub id*/, Binding> bindings_;
    uint32_t                                         next_sub_id_{1};

    // Guards topic_changed_fn_ (and its invocation) and panel_.
    std::mutex               cb_mu_;
    TopicChangedFn           topic_changed_fn_;
    ui::topics::TopicsPanel* panel_ = nullptr;

    FigureRegistry* fig_registry_ = nullptr;   // main thread only
};

}   // namespace spectra

#endif   // !_WIN32
