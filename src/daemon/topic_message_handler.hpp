#pragma once

#include "daemon_server.hpp"

namespace spectra::daemon
{

HandleResult handle_req_declare_topic(DaemonContext& ctx, ClientSlot& slot,
                                      const ipc::Message& msg);

HandleResult handle_req_publish_topic_samples(DaemonContext& ctx, ClientSlot& slot,
                                              const ipc::Message& msg);

HandleResult handle_req_subscribe_topic(DaemonContext& ctx, ClientSlot& slot,
                                        const ipc::Message& msg);

HandleResult handle_req_unsubscribe_topic(DaemonContext& ctx, ClientSlot& slot,
                                          const ipc::Message& msg);

HandleResult handle_req_list_topics(DaemonContext& ctx, ClientSlot& slot,
                                    const ipc::Message& msg);

// Broadcast EVT_TOPIC_LIST_CHANGED to all handshaked clients.
void broadcast_topic_list_changed(DaemonContext& ctx);

}   // namespace spectra::daemon
