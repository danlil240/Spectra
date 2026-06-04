// inproc_topic_server.cpp — InprocTopicServer implementation.

#ifndef _WIN32

    #include "inproc_topic_server.hpp"

    #include <cstdio>
    #include <cstring>
    #include <poll.h>
    #include <spectra/logger.hpp>
    #include <unistd.h>

    #include "../ipc/codec.hpp"
    #include "spectra/axes.hpp"
    #include "spectra/figure.hpp"
    #include "spectra/figure_registry.hpp"
    #include "spectra/series.hpp"

namespace spectra
{

namespace
{

// Resolve $XDG_RUNTIME_DIR or fallback to /tmp.
static std::string runtime_dir()
{
    const char* d = ::getenv("XDG_RUNTIME_DIR");
    return (d && d[0]) ? d : "/tmp";
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
    if (!running_.exchange(false))
        return;
    server_.close();   // wake up poll() and reject new accepts
    if (thread_.joinable())
        thread_.join();
    if (!socket_path_.empty())
    {
        ::unlink(socket_path_.c_str());
        socket_path_.clear();
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
                if (!registry_.publish(req->name, req->samples, &kind, &subs_out))
                {
                    send_err(conn, msg.header.request_id, "Topic not declared");
                    return;
                }
            }

            // Fan out to subscribed series.
            if (!subs_out.empty() && !req->samples.empty() && fig_registry_)
            {
                const size_t stride = (kind == ipc::TopicKind::Scalar3D) ? 3u : 2u;
                const size_t n      = req->samples.size() / stride;
                for (const auto& sub : subs_out)
                {
                    Figure* fig = fig_registry_->get(sub.figure_id);
                    if (!fig)
                        continue;
                    Axes* ab = fig->get_axes(sub.axes_index);
                    if (!ab)
                        continue;
                    auto& series_vec = ab->series();
                    if (sub.series_index >= series_vec.size())
                        continue;
                    Series* s = series_vec[sub.series_index].get();
                    for (size_t k = 0; k < n; ++k)
                    {
                        float x = static_cast<float>(req->samples[k * stride]);
                        float y = static_cast<float>(req->samples[k * stride + 1]);
                        s->append(x, y);
                    }
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
            // no-op
            break;
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
    fig_registry_ = fig_registry;

    // List request: refresh panel from current registry snapshot.
    panel.set_list_request_callback([this, &panel]() { panel.set_topics(topic_snapshot()); });

    // Subscribe request: create a series, enable thread-safe mode, register
    // the subscription in the TopicRegistry so publish() fanout reaches it.
    panel.set_subscribe_request_callback(
        [this, fig_registry](const ui::topics::SubscribeRequest& req)
        {
            SPECTRA_LOG_DEBUG("topics",
                              "subscribe_cb: fig_id={} axes={} topic={}",
                              req.figure_id,
                              req.axes_index,
                              req.topic_name);
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
                SPECTRA_LOG_WARN(
                    "topics",
                    "subscribe_cb: no 2D axes at index {} (axes2d.size={}, all_axes.size={})",
                    req.axes_index,
                    axes2d.size(),
                    all.size());
                return;
            }

            // Create a new line series named after the topic.
            LineSeries& ls = ax->line();
            ls.label(req.topic_name);
            ls.set_thread_safe(true);   // allow append() from the server thread

            // Find the new series index.
            auto&    svec         = ax->series();
            uint32_t series_index = static_cast<uint32_t>(svec.size()) - 1;

            // Register subscription in the registry.
            daemon::TopicSubscription sub;
            sub.figure_id            = req.figure_id;
            sub.axes_index           = req.axes_index;
            sub.series_index         = series_index;
            ipc::TopicKind      kind = ipc::TopicKind::Scalar2D;
            std::vector<double> retained_samples;
            {
                std::lock_guard lock(reg_mu_);
                registry_.subscribe(req.topic_name, sub, &kind, &retained_samples);
            }

            if (!retained_samples.empty())
            {
                const size_t stride = (kind == ipc::TopicKind::Scalar3D) ? 3u : 2u;
                const size_t n      = retained_samples.size() / stride;
                for (size_t k = 0; k < n; ++k)
                {
                    float x = static_cast<float>(retained_samples[k * stride]);
                    float y = static_cast<float>(retained_samples[k * stride + 1]);
                    ls.append(x, y);
                }
            }

            // Enable continuous auto-zoom on the subscribed axes so the user
            // can see all incoming samples without manually pressing "fit".
            // The flag self-disables the first time the user pans or zooms.
            ax->set_topic_auto_zoom(true);

            SPECTRA_LOG_DEBUG("topics",
                              "Subscribed {} -> fig={} axes={} series={}",
                              req.topic_name,
                              req.figure_id,
                              req.axes_index,
                              series_index);
            notify_topic_changed();
        });

    // Fire notify when topics change to keep the panel fresh.
    set_topic_changed_fn([this, &panel]() { panel.set_topics(topic_snapshot()); });
}

}   // namespace spectra

#endif   // !_WIN32
