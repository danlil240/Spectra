#pragma once

#include "daemon_server.hpp"

namespace spectra::daemon
{

// Handles messages from render agents and the source app client:
// HELLO, EVT_HEARTBEAT, REQ_CREATE_WINDOW, REQ_CLOSE_WINDOW, REQ_DETACH_FIGURE,
// EVT_WINDOW, EVT_INPUT, STATE_SNAPSHOT, STATE_DIFF, ACK_STATE.

HandleResult handle_hello(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg);
HandleResult handle_heartbeat(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg);
HandleResult handle_req_create_window(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg);
HandleResult handle_req_close_window(DaemonContext& ctx,
                                     ClientSlot&    slot,
                                     const ipc::Message& msg);
HandleResult handle_req_detach_figure(DaemonContext& ctx,
                                      ClientSlot&    slot,
                                      const ipc::Message& msg);
HandleResult handle_evt_window(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg);
HandleResult handle_evt_input(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg);
HandleResult handle_state_snapshot(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg);
HandleResult handle_state_diff(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg);

}   // namespace spectra::daemon
