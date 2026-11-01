#pragma once

#include <string>
#include <vector>

#include "bridges/topic_ingest.hpp"
#include "daemon_server.hpp"

namespace spectra::daemon
{

HandleResult handle_req_declare_topic(DaemonContext&      ctx,
                                      ClientSlot&         slot,
                                      const ipc::Message& msg);

HandleResult handle_req_publish_topic_samples(DaemonContext&      ctx,
                                              ClientSlot&         slot,
                                              const ipc::Message& msg);

HandleResult handle_req_subscribe_topic(DaemonContext&      ctx,
                                        ClientSlot&         slot,
                                        const ipc::Message& msg);

HandleResult handle_req_unsubscribe_topic(DaemonContext&      ctx,
                                          ClientSlot&         slot,
                                          const ipc::Message& msg);

HandleResult handle_req_list_topics(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg);

// Append interleaved topic samples to every subscribed series, adding one
// SET_SERIES_DATA op per live subscriber to `diff` (not broadcast).
void append_topic_samples_to_subscribers(DaemonContext&                        ctx,
                                         const std::string&                    name,
                                         const std::vector<double>&            samples,
                                         const std::vector<TopicSubscription>& subs,
                                         ipc::StateDiffPayload&                diff);

// Publish everything bridges queued since the last call (event loop thread
// only): declares new topics, fans samples out to subscribed series in one
// STATE_DIFF and broadcasts EVT_TOPIC_LIST_CHANGED when topics appeared.
void drain_topic_ingest(DaemonContext& ctx, TopicIngestQueue& queue);

// Broadcast EVT_TOPIC_LIST_CHANGED to all handshaked clients.
void broadcast_topic_list_changed(DaemonContext& ctx);

}   // namespace spectra::daemon
