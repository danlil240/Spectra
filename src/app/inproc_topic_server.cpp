// inproc_topic_server.cpp — InprocTopicServer implementation.

#ifndef _WIN32

    #include "inproc_topic_server.hpp"

    #include <cstdio>
    #include <cstring>
    #include <poll.h>
    #include <spectra/logger.hpp>
    #include <unistd.h>

    #include "../ipc/codec.hpp"
    #include "../ipc/transport.hpp"
    #include "spectra/axes.hpp"
    #include "spectra/figure.hpp"
    #include "spectra/figure_registry.hpp"
    #include "spectra/series.hpp"

namespace spectra
{

namespace
{

// Upper bound on queued-but-undrained points.  Protects memory if the owner
// stops calling drain_pending() (e.g. a stalled main loop): newer batches are
// dropped until the queue is drained.
constexpr size_t MAX_PENDING_POINTS = size_t{1} << 22;

// $XDG_RUNTIME_DIR, or the system temp directory (shared with the daemon and
// publishers so they can discover this socket).
static std::string runtime_dir()
{
    return ipc::runtime_dir();
}

}   // namespace

InprocTopicServer::InprocTopicServer() = default;
InprocTopicServer::~InprocTopicServer()
{
    stop();
}

std::string InprocTopicServer::start(FigureRegistry* fig_registry)
{
    if (running_.load())
        return socket_path_;

    fig_registry_ = fig_registry;

    socket_path_ = runtime_dir() + "/spectra-" + std::to_string(::getpid()) + ".sock";
    if (!server_.listen(socket_path_))
    {
        SPECTRA_LOG_ERROR("topics", "Failed to listen on {}", socket_path_);
        socket_path_.clear();
        return {};
    }

    running_.store(true);
    thread_ = std::thread(&InprocTopicServer::run_loop, this);
    SPECTRA_LOG_INFO("topics", "Listening on {}", socket_path_);
    return socket_path_;
}

void InprocTopicServer::stop()
{
    // Always drop UI wiring, even if the server never started: the panel's
    // callbacks capture `this`.
    unwire_panel();

    if (!running_.exchange(false))
        return;

    // The worker polls with a 100 ms timeout and re-checks running_, so it
    // exits promptly.  Join before closing the listener: the worker reads the
    // listen fd (poll set, try_accept), so closing it first would race.
    if (thread_.joinable())
        thread_.join();
    server_.close();
    if (!socket_path_.empty())
    {
        ::unlink(socket_path_.c_str());
        socket_path_.clear();
    }

    // Main thread: safe to drop figure-side state now that the worker is gone.
    fig_registry_ = nullptr;
    bindings_.clear();
    {
        std::lock_guard lock(pending_mu_);
        pending_.clear();
        pending_points_ = 0;
    }
}

void InprocTopicServer::set_topic_changed_fn(TopicChangedFn fn)
{
    std::lock_guard lock(cb_mu_);
    topic_changed_fn_ = std::move(fn);
}

void InprocTopicServer::unwire_panel()
{
    ui::topics::TopicsPanel* panel = nullptr;
    {
        // Taking cb_mu_ waits for any in-flight notify_topic_changed() on the
        // server thread, so the callback never runs against a freed panel.
        std::lock_guard lock(cb_mu_);
        topic_changed_fn_ = nullptr;
        panel             = panel_;
        panel_            = nullptr;
    }
    if (panel)
    {
        // Main thread; the panel is still alive here (otherwise its destroy
        // callback would already have cleared panel_).
        panel->set_list_request_callback(nullptr);
        panel->set_subscribe_request_callback(nullptr);
        panel->set_destroy_callback(nullptr);
    }
}

// ─── Background thread ───────────────────────────────────────────────────────

void InprocTopicServer::run_loop()
{
    struct ClientState
    {
        std::unique_ptr<ipc::Connection> conn;
        uint64_t                         client_id = 0;
    };

    std::vector<ClientState>   clients;
    std::vector<struct pollfd> fds;

    auto rebuild_fds = [&]()
    {
        fds.clear();
        // [0] = listening socket
        fds.push_back({server_.listen_fd(), POLLIN, 0});
        for (auto& c : clients)
            fds.push_back({c.conn->fd(), POLLIN, 0});
    };

    rebuild_fds();

    while (running_.load())
    {
        int rc = ::poll(fds.data(), static_cast<nfds_t>(fds.size()), 100 /*ms*/);
        if (rc < 0)
            break;
        if (rc == 0)
            continue;

        bool dirty = false;

        // New connection?
        if (fds[0].revents & POLLIN)
        {
            auto conn = server_.try_accept();
            if (conn)
            {
                ClientState cs;
                cs.client_id = next_client_id_++;
                cs.conn      = std::move(conn);
                clients.push_back(std::move(cs));
                dirty = true;
            }
        }

        // Data on existing connections.
        for (size_t i = 0; i < clients.size();)
        {
            int cfd_idx = static_cast<int>(i) + 1;
            if (cfd_idx >= static_cast<int>(fds.size()))
                break;

            bool dead = false;
            if (fds[cfd_idx].revents & (POLLIN | POLLERR | POLLHUP))
            {
                auto msg = clients[i].conn->recv();
                if (!msg)
                {
                    dead = true;
                }
                else
                {
                    handle_message(clients[i].conn->fd(),
                                   *clients[i].conn,
                                   clients[i].client_id,
                                   *msg);
                }
            }

            if (dead)
            {
                on_client_disconnected(clients[i].client_id);
                clients.erase(clients.begin() + static_cast<ptrdiff_t>(i));
                dirty = true;
            }
            else
            {
                ++i;
            }
        }

        if (dirty)
            rebuild_fds();
    }
}

// ─── Message handling ────────────────────────────────────────────────────────

void InprocTopicServer::handle_message(int /*fd*/,
                                       ipc::Connection&    conn,
                                       uint64_t            client_id,
                                       const ipc::Message& msg)
{
    using MT = ipc::MessageType;
    switch (msg.header.type)
    {
        case MT::HELLO:
        {
            send_welcome(conn);
            break;
        }
        case MT::REQ_DECLARE_TOPIC:
        {
            auto req = ipc::decode_req_declare_topic(msg.payload);
            if (!req || req->name.empty())
            {
                send_err(conn, msg.header.request_id, "Bad payload");
                return;
            }
            {
                std::lock_guard lock(reg_mu_);
                registry_.declare(req->name, req->kind, req->unit, req->ring_capacity, client_id);
            }
            SPECTRA_LOG_DEBUG("topics", "Topic declared: {}", req->name);
            send_ok(conn, msg.header.request_id);
            notify_topic_changed();
            break;
        }
        case MT::REQ_PUBLISH_TOPIC_SAMPLES:
        {
            auto req = ipc::decode_req_publish_topic_samples(msg.payload);
            if (!req || req->name.empty())
            {
                send_err(conn, msg.header.request_id, "Bad payload");
                return;
            }

            ipc::TopicKind                         kind = ipc::TopicKind::Scalar2D;
            std::vector<daemon::TopicSubscription> subs_out;
            {
                std::lock_guard lock(reg_mu_);
                // publish() also fails on a stride mismatch: check the declared
                // kind first so the client gets an accurate error.
                const auto declared_kind = registry_.kind_of(req->name);
                if (!declared_kind)
                {
                    send_err(conn, msg.header.request_id, "Topic not declared");
                    return;
                }
                if (req->samples.size() % daemon::TopicRegistry::stride_for(*declared_kind) != 0)
                {
                    send_err(conn,
                             msg.header.request_id,
                             "Sample count is not a multiple of the topic stride");
                    return;
                }
                if (!registry_.publish(req->name, req->samples, &kind, &subs_out))
                {
                    send_err(conn, msg.header.request_id, "Topic not declared");
                    return;
                }
            }

            // Queue for the main thread.  The worker must not touch Figure /
            // Axes / Series: they are owned and mutated by the main thread.
            if (!subs_out.empty() && !req->samples.empty())
            {
                const size_t       stride = daemon::TopicRegistry::stride_for(kind);
                const size_t       n      = req->samples.size() / stride;
                std::vector<float> xy;
                xy.reserve(n * 2);
                for (size_t k = 0; k < n; ++k)
                {
                    xy.push_back(static_cast<float>(req->samples[k * stride]));
                    xy.push_back(static_cast<float>(req->samples[k * stride + 1]));
                }

                std::lock_guard lock(pending_mu_);
                for (const auto& sub : subs_out)
                {
                    if (pending_points_ + n > MAX_PENDING_POINTS)
                    {
                        SPECTRA_LOG_WARN("topics",
                                         "Dropping {} samples for '{}': drain queue full",
                                         n,
                                         req->name);
                        break;
                    }
                    pending_points_ += n;
                    pending_.push_back({sub, xy});
                }
            }

            send_ok(conn, msg.header.request_id);
            break;
        }
        case MT::REQ_LIST_TOPICS:
        {
            send_topic_list(conn, msg.header.request_id);
            break;
        }
        case MT::EVT_HEARTBEAT:
        default:
            break;
    }
}

void InprocTopicServer::on_client_disconnected(uint64_t client_id)
{
    {
        std::lock_guard lock(reg_mu_);
        registry_.on_client_disconnect(client_id);
    }
    notify_topic_changed();
}

void InprocTopicServer::notify_topic_changed()
{
    // Hold cb_mu_ for the whole call: stop() and panel destruction take the
    // same lock before clearing the callback, so the closure can neither be
    // destroyed mid-call nor run after its panel is gone.  Lock order is
    // cb_mu_ -> reg_mu_ (topic_snapshot); never call this with reg_mu_ held.
    std::lock_guard lock(cb_mu_);
    if (topic_changed_fn_)
        topic_changed_fn_();
}

// ─── Send helpers ────────────────────────────────────────────────────────────

void InprocTopicServer::send_ok(ipc::Connection& conn, ipc::RequestId req_id)
{
    ipc::RespOkPayload p;
    p.request_id = req_id;
    ipc::Message m;
    m.header.type        = ipc::MessageType::RESP_OK;
    m.header.session_id  = 1;
    m.header.request_id  = req_id;
    m.payload            = ipc::encode_resp_ok(p);
    m.header.payload_len = static_cast<uint32_t>(m.payload.size());
    conn.send(m);
}

void InprocTopicServer::send_err(ipc::Connection&   conn,
                                 ipc::RequestId     req_id,
                                 const std::string& reason)
{
    ipc::RespErrPayload p;
    p.request_id = req_id;
    p.code       = 400;
    p.message    = reason;
    ipc::Message m;
    m.header.type        = ipc::MessageType::RESP_ERR;
    m.header.session_id  = 1;
    m.header.request_id  = req_id;
    m.payload            = ipc::encode_resp_err(p);
    m.header.payload_len = static_cast<uint32_t>(m.payload.size());
    conn.send(m);
}

void InprocTopicServer::send_welcome(ipc::Connection& conn)
{
    ipc::WelcomePayload wp;
    wp.session_id   = 1;
    wp.window_id    = ipc::INVALID_WINDOW;
    wp.process_id   = static_cast<ipc::ProcessId>(::getpid());
    wp.heartbeat_ms = 0;   // publishers don't need heartbeats
    wp.mode         = "inproc";
    ipc::Message m;
    m.header.type        = ipc::MessageType::WELCOME;
    m.header.session_id  = 1;
    m.payload            = ipc::encode_welcome(wp);
    m.header.payload_len = static_cast<uint32_t>(m.payload.size());
    conn.send(m);
}

void InprocTopicServer::send_topic_list(ipc::Connection& conn, ipc::RequestId req_id)
{
    std::vector<daemon::TopicRegistry::Info> infos;
    {
        std::lock_guard lock(reg_mu_);
        infos = registry_.snapshot();
    }
    ipc::RespTopicListPayload p;
    p.request_id = req_id;
    p.topics.reserve(infos.size());
    for (const auto& i : infos)
    {
        ipc::TopicInfoEntry e;
        e.name             = i.name;
        e.kind             = i.kind;
        e.unit             = i.unit;
        e.estimated_hz     = i.estimated_hz;
        e.total_samples    = i.total_samples;
        e.last_publish_ns  = i.last_publish_ns;
        e.subscriber_count = i.subscriber_count;
        e.publisher_online = i.publisher_online;
        p.topics.push_back(std::move(e));
    }
    ipc::Message m;
    m.header.type        = ipc::MessageType::RESP_TOPIC_LIST;
    m.header.session_id  = 1;
    m.payload            = ipc::encode_resp_topic_list(p);
    m.header.payload_len = static_cast<uint32_t>(m.payload.size());
    conn.send(m);
}

// ─── Snapshot / Panel wiring ─────────────────────────────────────────────────

std::vector<ipc::TopicInfoEntry> InprocTopicServer::topic_snapshot() const
{
    std::vector<daemon::TopicRegistry::Info> infos;
    {
        std::lock_guard lock(reg_mu_);
        infos = registry_.snapshot();
    }
    std::vector<ipc::TopicInfoEntry> out;
    out.reserve(infos.size());
    for (const auto& i : infos)
    {
        ipc::TopicInfoEntry e;
        e.name             = i.name;
        e.kind             = i.kind;
        e.unit             = i.unit;
        e.estimated_hz     = i.estimated_hz;
        e.total_samples    = i.total_samples;
        e.last_publish_ns  = i.last_publish_ns;
        e.subscriber_count = i.subscriber_count;
        e.publisher_online = i.publisher_online;
        out.push_back(std::move(e));
    }
    return out;
}

void InprocTopicServer::wire_topics_panel(ui::topics::TopicsPanel& panel,
                                          FigureRegistry*          fig_registry)
{
    unwire_panel();   // at most one panel is wired at a time
    fig_registry_ = fig_registry;

    // List request: refresh panel from current registry snapshot.
    panel.set_list_request_callback([this, &panel]() { panel.set_topics(topic_snapshot()); });

    // Subscribe request: create a series and register the subscription.
    panel.set_subscribe_request_callback([this](const ui::topics::SubscribeRequest& req)
                                         { subscribe_from_panel(req); });

    // The panel lives in a window's UI context and is destroyed when that
    // window closes, which can be long before stop().  Unwire at that point.
    panel.set_destroy_callback(
        [this]()
        {
            std::lock_guard lock(cb_mu_);
            topic_changed_fn_ = nullptr;
            panel_            = nullptr;
        });

    // Fire notify when topics change to keep the panel fresh.
    std::lock_guard lock(cb_mu_);
    panel_            = &panel;
    topic_changed_fn_ = [this, &panel]() { panel.set_topics(topic_snapshot()); };
}

void InprocTopicServer::subscribe_from_panel(const ui::topics::SubscribeRequest& req)
{
    SPECTRA_LOG_DEBUG("topics",
                      "subscribe_cb: fig_id={} axes={} topic={}",
                      req.figure_id,
                      req.axes_index,
                      req.topic_name);
    FigureRegistry* fig_registry = fig_registry_;
    if (!fig_registry)
    {
        SPECTRA_LOG_ERROR("topics", "subscribe_cb: fig_registry is null");
        return;
    }
    Figure* fig = fig_registry->get(req.figure_id);
    if (!fig)
    {
        SPECTRA_LOG_WARN("topics", "subscribe_cb: figure not found (id={})", req.figure_id);
        for (auto id : fig_registry->all_ids())
            SPECTRA_LOG_DEBUG("topics", "  known fig id={}", id);
        return;
    }
    // Try 2D axes first, then 3D / generic.
    const auto& axes2d = fig->axes();
    const auto& all    = fig->all_axes();
    Axes*       ax     = nullptr;
    if (req.axes_index < axes2d.size() && axes2d[req.axes_index])
    {
        ax = axes2d[req.axes_index].get();
    }
    else if (req.axes_index < all.size() && all[req.axes_index])
    {
        ax = dynamic_cast<Axes*>(all[req.axes_index].get());
    }
    if (!ax)
    {
        SPECTRA_LOG_WARN("topics",
                         "subscribe_cb: no 2D axes at index {} (axes2d.size={}, all_axes.size={})",
                         req.axes_index,
                         axes2d.size(),
                         all.size());
        return;
    }

    // Create a new line series named after the topic.  Samples are appended
    // on this (main) thread by drain_pending(); thread-safe mode keeps them in
    // the pending buffer until the frame-boundary commit, as before.
    LineSeries& ls = ax->line();
    ls.label(req.topic_name);
    ls.set_thread_safe(true);

    // Register the subscription under a unique id (carried in series_index)
    // rather than the series' current position, which shifts whenever a
    // series is added or removed on these axes.
    const uint32_t            sub_id = next_sub_id_++;
    daemon::TopicSubscription sub;
    sub.figure_id            = req.figure_id;
    sub.axes_index           = req.axes_index;
    sub.series_index         = sub_id;
    ipc::TopicKind      kind = ipc::TopicKind::Scalar2D;
    std::vector<double> retained_samples;
    bool                subscribed = false;
    {
        std::lock_guard lock(reg_mu_);
        subscribed = registry_.subscribe(req.topic_name, sub, &kind, &retained_samples);
    }
    if (subscribed)
        bindings_[sub_id] = Binding{sub, req.figure_id, ax, &ls};

    if (!retained_samples.empty())
    {
        const size_t stride = daemon::TopicRegistry::stride_for(kind);
        const size_t n      = retained_samples.size() / stride;
        for (size_t k = 0; k < n; ++k)
        {
            auto x = static_cast<float>(retained_samples[k * stride]);
            auto y = static_cast<float>(retained_samples[k * stride + 1]);
            ls.append(x, y);
        }
    }

    // Enable continuous auto-zoom on the subscribed axes so the user
    // can see all incoming samples without manually pressing "fit".
    // The flag self-disables the first time the user pans or zooms.
    ax->set_topic_auto_zoom(true);

    SPECTRA_LOG_DEBUG("topics",
                      "Subscribed {} -> fig={} axes={} sub_id={}",
                      req.topic_name,
                      req.figure_id,
                      req.axes_index,
                      sub_id);
    notify_topic_changed();
}

size_t InprocTopicServer::pending_point_count() const
{
    std::lock_guard lock(pending_mu_);
    return pending_points_;
}

void InprocTopicServer::drain_pending()
{
    std::vector<PendingBatch> batches;
    {
        std::lock_guard lock(pending_mu_);
        batches.swap(pending_);
        pending_points_ = 0;
    }
    if (batches.empty())
        return;

    // Resolve a binding against the live figure tree.  Only pointer identity
    // is compared, so stale pointers are never dereferenced.
    auto resolve = [this](const Binding& b) -> Series*
    {
        if (!fig_registry_)
            return nullptr;
        Figure* fig = fig_registry_->get(b.figure_id);
        if (!fig)
            return nullptr;
        AxesBase* found_axes = nullptr;
        fig->for_each_axes(
            [&](AxesBase* a)
            {
                if (a == b.axes)
                    found_axes = a;
            });
        if (!found_axes)
            return nullptr;
        for (const auto& s : found_axes->series())
        {
            if (s.get() == b.series)
                return s.get();
        }
        return nullptr;
    };

    bool dropped_any = false;
    for (const auto& batch : batches)
    {
        auto it = bindings_.find(batch.sub.series_index);
        if (it == bindings_.end())
            continue;   // already dropped
        Series* s = resolve(it->second);
        if (!s)
        {
            // Figure, axes or series is gone: stop fanning out to it.
            {
                std::lock_guard lock(reg_mu_);
                registry_.unsubscribe(it->second.sub);
            }
            bindings_.erase(it);
            dropped_any = true;
            continue;
        }
        for (size_t k = 0; k + 1 < batch.xy.size(); k += 2)
            s->append(batch.xy[k], batch.xy[k + 1]);
    }

    if (dropped_any)
        notify_topic_changed();   // subscriber counts changed
}

}   // namespace spectra

#endif   // !_WIN32
