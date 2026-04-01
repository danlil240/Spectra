#pragma once

#include "daemon_server.hpp"

namespace spectra::daemon
{

// Handles Python client requests (REQ_CREATE_FIGURE, REQ_ADD_SERIES, REQ_SET_DATA,
// REQ_UPDATE_PROPERTY, REQ_SHOW, REQ_APPEND_DATA, REQ_REMOVE_SERIES, REQ_CLOSE_FIGURE,
// REQ_UPDATE_BATCH, REQ_DESTROY_FIGURE, REQ_LIST_FIGURES, REQ_RECONNECT, REQ_DISCONNECT,
// REQ_GET_SNAPSHOT, REQ_CREATE_AXES).

HandleResult handle_req_create_figure(DaemonContext& ctx,
                                      ClientSlot&    slot,
                                      const ipc::Message& msg);

HandleResult handle_req_create_axes(DaemonContext& ctx,
                                    ClientSlot&    slot,
                                    const ipc::Message& msg);

HandleResult handle_req_add_series(DaemonContext& ctx,
                                   ClientSlot&    slot,
                                   const ipc::Message& msg);

HandleResult handle_req_set_data(DaemonContext& ctx,
                                 ClientSlot&    slot,
                                 const ipc::Message& msg);

HandleResult handle_req_update_property(DaemonContext& ctx,
                                        ClientSlot&    slot,
                                        const ipc::Message& msg);

HandleResult handle_req_show(DaemonContext& ctx,
                             ClientSlot&    slot,
                             const ipc::Message& msg);

HandleResult handle_req_append_data(DaemonContext& ctx,
                                    ClientSlot&    slot,
                                    const ipc::Message& msg);

HandleResult handle_req_remove_series(DaemonContext& ctx,
                                      ClientSlot&    slot,
                                      const ipc::Message& msg);

HandleResult handle_req_close_figure(DaemonContext& ctx,
                                     ClientSlot&    slot,
                                     const ipc::Message& msg);

HandleResult handle_req_update_batch(DaemonContext& ctx,
                                     ClientSlot&    slot,
                                     const ipc::Message& msg);

HandleResult handle_req_destroy_figure(DaemonContext& ctx,
                                       ClientSlot&    slot,
                                       const ipc::Message& msg);

HandleResult handle_req_list_figures(DaemonContext& ctx,
                                     ClientSlot&    slot,
                                     const ipc::Message& msg);

HandleResult handle_req_reconnect(DaemonContext& ctx,
                                  ClientSlot&    slot,
                                  const ipc::Message& msg);

HandleResult handle_req_disconnect(DaemonContext& ctx,
                                   ClientSlot&    slot,
                                   const ipc::Message& msg);

HandleResult handle_req_get_snapshot(DaemonContext& ctx,
                                     ClientSlot&    slot,
                                     const ipc::Message& msg);

}   // namespace spectra::daemon
