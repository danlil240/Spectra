#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../ipc/message.hpp"
#include "../ipc/transport.hpp"
#include "client_router.hpp"
#include "figure_model.hpp"
#include "process_manager.hpp"
#include "session_graph.hpp"

namespace spectra::daemon
{

// Per-connection bookkeeping.
struct ClientSlot
{
    std::unique_ptr<ipc::Connection> conn;
    ipc::WindowId                    window_id      = ipc::INVALID_WINDOW;
    bool                             handshake_done = false;
    bool       is_source_client = false;   // true = app pushing figures (not a render agent)
    ClientType client_type      = ClientType::UNKNOWN;
};

// Return value from message handlers — tells the event loop how to advance.
enum class HandleResult
{
    Continue,           // advance iterator normally (++it)
    EraseAndContinue,   // erase current slot, iterator already advanced
    Shutdown,           // source client disconnected, shut down
};

// Shared context passed to all message handlers.
struct DaemonContext
{
    SessionGraph&            graph;
    FigureModel&             fig_model;
    ProcessManager&          proc_mgr;
    std::vector<ClientSlot>& clients;
    std::atomic<bool>&       running;
};

// ─── Helper functions (shared across handlers) ─────────────────────────────

bool send_assign_figures(ipc::Connection&             conn,
                         ipc::WindowId                wid,
                         ipc::SessionId               sid,
                         const std::vector<uint64_t>& figure_ids,
                         uint64_t                     active_figure_id);

bool send_state_snapshot(ipc::Connection&                 conn,
                         ipc::WindowId                    wid,
                         ipc::SessionId                   sid,
                         const ipc::StateSnapshotPayload& snap);

bool send_state_diff(ipc::Connection&             conn,
                     ipc::WindowId                wid,
                     ipc::SessionId               sid,
                     const ipc::StateDiffPayload& diff);

bool send_close_window(ipc::Connection&   conn,
                       ipc::WindowId      wid,
                       ipc::SessionId     sid,
                       const std::string& reason);

bool send_python_response(ipc::Connection&            conn,
                          ipc::MessageType            type,
                          ipc::SessionId              sid,
                          ipc::RequestId              req_id,
                          const std::vector<uint8_t>& payload);

bool send_resp_err(ipc::Connection&   conn,
                   ipc::SessionId     sid,
                   ipc::RequestId     req_id,
                   uint32_t           code,
                   const std::string& message);

bool send_resp_ok(ipc::Connection& conn,
                  ipc::SessionId   sid,
                  ipc::RequestId   req_id,
                  ipc::WindowId    wid = ipc::INVALID_WINDOW);

// Broadcast STATE_DIFF to all agents.
void broadcast_diff_to_agents(DaemonContext& ctx, const ipc::StateDiffPayload& diff);

// Broadcast STATE_DIFF to all handshaked clients.
void broadcast_diff_to_all(DaemonContext& ctx, const ipc::StateDiffPayload& diff);

// Notify all Python clients about a window closed event.
void notify_python_window_closed(DaemonContext&     ctx,
                                 uint64_t           figure_id,
                                 ipc::WindowId      window_id,
                                 const std::string& reason);

// Redistribute orphaned figures to the first remaining agent.
void redistribute_orphaned_figures(DaemonContext& ctx, const std::vector<uint64_t>& orphaned);

}   // namespace spectra::daemon
