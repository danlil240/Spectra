#include "daemon_server.hpp"

#include "../ipc/codec.hpp"

namespace spectra::daemon
{

bool send_assign_figures(ipc::Connection&             conn,
                         ipc::WindowId                wid,
                         ipc::SessionId               sid,
                         const std::vector<uint64_t>& figure_ids,
                         uint64_t                     active_figure_id)
{
    ipc::CmdAssignFiguresPayload payload;
    payload.window_id        = wid;
    payload.figure_ids       = figure_ids;
    payload.active_figure_id = active_figure_id;

    ipc::Message msg;
    msg.header.type        = ipc::MessageType::CMD_ASSIGN_FIGURES;
    msg.header.session_id  = sid;
    msg.header.window_id   = wid;
    msg.payload            = ipc::encode_cmd_assign_figures(payload);
    msg.header.payload_len = static_cast<uint32_t>(msg.payload.size());
    return conn.send(msg);
}

bool send_state_snapshot(ipc::Connection&                 conn,
                         ipc::WindowId                    wid,
                         ipc::SessionId                   sid,
                         const ipc::StateSnapshotPayload& snap)
{
    ipc::Message msg;
    msg.header.type        = ipc::MessageType::STATE_SNAPSHOT;
    msg.header.session_id  = sid;
    msg.header.window_id   = wid;
    msg.payload            = ipc::encode_state_snapshot(snap);
    msg.header.payload_len = static_cast<uint32_t>(msg.payload.size());
    return conn.send(msg);
}

bool send_state_diff(ipc::Connection&             conn,
                     ipc::WindowId                wid,
                     ipc::SessionId               sid,
                     const ipc::StateDiffPayload& diff)
{
    ipc::Message msg;
    msg.header.type        = ipc::MessageType::STATE_DIFF;
    msg.header.session_id  = sid;
    msg.header.window_id   = wid;
    msg.payload            = ipc::encode_state_diff(diff);
    msg.header.payload_len = static_cast<uint32_t>(msg.payload.size());
    return conn.send(msg);
}

bool send_close_window(ipc::Connection&   conn,
                       ipc::WindowId      wid,
                       ipc::SessionId     sid,
                       const std::string& reason)
{
    ipc::CmdCloseWindowPayload payload;
    payload.window_id = wid;
    payload.reason    = reason;

    ipc::Message msg;
    msg.header.type        = ipc::MessageType::CMD_CLOSE_WINDOW;
    msg.header.session_id  = sid;
    msg.header.window_id   = wid;
    msg.payload            = ipc::encode_cmd_close_window(payload);
    msg.header.payload_len = static_cast<uint32_t>(msg.payload.size());
    return conn.send(msg);
}

bool send_python_response(ipc::Connection&            conn,
                          ipc::MessageType            type,
                          ipc::SessionId              sid,
                          ipc::RequestId              req_id,
                          const std::vector<uint8_t>& payload)
{
    ipc::Message msg;
    msg.header.type        = type;
    msg.header.session_id  = sid;
    msg.header.request_id  = req_id;
    msg.payload            = payload;
    msg.header.payload_len = static_cast<uint32_t>(msg.payload.size());
    return conn.send(msg);
}

bool send_resp_err(ipc::Connection&   conn,
                   ipc::SessionId     sid,
                   ipc::RequestId     req_id,
                   uint32_t           code,
                   const std::string& message)
{
    ipc::Message msg;
    msg.header.type        = ipc::MessageType::RESP_ERR;
    msg.header.session_id  = sid;
    msg.header.request_id  = req_id;
    msg.payload            = ipc::encode_resp_err({req_id, code, message});
    msg.header.payload_len = static_cast<uint32_t>(msg.payload.size());
    return conn.send(msg);
}

bool send_resp_ok(ipc::Connection& conn,
                  ipc::SessionId   sid,
                  ipc::RequestId   req_id,
                  ipc::WindowId    wid)
{
    ipc::Message msg;
    msg.header.type        = ipc::MessageType::RESP_OK;
    msg.header.session_id  = sid;
    msg.header.request_id  = req_id;
    msg.header.window_id   = wid;
    msg.payload            = ipc::encode_resp_ok({req_id});
    msg.header.payload_len = static_cast<uint32_t>(msg.payload.size());
    return conn.send(msg);
}

void broadcast_diff_to_agents(DaemonContext& ctx, const ipc::StateDiffPayload& diff)
{
    for (auto& c : ctx.clients)
    {
        if (c.conn && c.handshake_done && c.client_type == ClientType::AGENT)
        {
            send_state_diff(*c.conn, c.window_id, ctx.graph.session_id(), diff);
        }
    }
}

void broadcast_diff_to_all(DaemonContext& ctx, const ipc::StateDiffPayload& diff)
{
    for (auto& c : ctx.clients)
    {
        if (c.conn && c.handshake_done)
        {
            send_state_diff(*c.conn, c.window_id, ctx.graph.session_id(), diff);
        }
    }
}

void notify_python_window_closed(DaemonContext&     ctx,
                                 uint64_t           figure_id,
                                 ipc::WindowId      window_id,
                                 const std::string& reason)
{
    ipc::EvtWindowClosedPayload evt;
    evt.figure_id    = figure_id;
    evt.window_id    = window_id;
    evt.reason       = reason;
    auto evt_payload = ipc::encode_evt_window_closed(evt);

    for (auto& c : ctx.clients)
    {
        if (c.conn && c.conn->is_open() && c.handshake_done && c.client_type == ClientType::PYTHON)
        {
            ipc::Message evt_msg;
            evt_msg.header.type        = ipc::MessageType::EVT_WINDOW_CLOSED;
            evt_msg.header.session_id  = ctx.graph.session_id();
            evt_msg.payload            = evt_payload;
            evt_msg.header.payload_len = static_cast<uint32_t>(evt_msg.payload.size());
            c.conn->send(evt_msg);
        }
    }
}

void redistribute_orphaned_figures(DaemonContext& ctx, const std::vector<uint64_t>& orphaned)
{
    if (orphaned.empty())
        return;

    auto remaining = ctx.graph.all_window_ids();
    if (remaining.empty())
        return;

    auto target = remaining[0];
    for (auto fid : orphaned)
        ctx.graph.assign_figure(fid, target);

    auto figs = ctx.graph.figures_for_window(target);
    for (auto& c : ctx.clients)
    {
        if (c.window_id == target && c.conn)
        {
            send_assign_figures(*c.conn,
                                target,
                                ctx.graph.session_id(),
                                figs,
                                figs.empty() ? 0 : figs[0]);
            break;
        }
    }
}

}   // namespace spectra::daemon
