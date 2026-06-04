#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "codec_fb.hpp"
#include "codec_tlv.hpp"
#include "message.hpp"

namespace spectra::ipc
{

// ─── Header serialization ────────────────────────────────────────────────────
// Encodes/decodes the fixed 40-byte message header.

void encode_header(const MessageHeader& hdr, std::vector<uint8_t>& out);

std::optional<MessageHeader> decode_header(std::span<const uint8_t> data);

// ─── Full message serialization ──────────────────────────────────────────────

std::vector<uint8_t> encode_message(const Message& msg);

std::optional<Message> decode_message(std::span<const uint8_t> data);

// ─── Payload encode/decode (FlatBuffers by default) ──────────────────────────
// encode_* always emits a 0x01-prefixed FlatBuffers payload.
// decode_* auto-detects format: FlatBuffers (0x01) or legacy TLV (read-only).

std::vector<uint8_t>        encode_hello(const HelloPayload& p);
std::optional<HelloPayload> decode_hello(std::span<const uint8_t> data);

std::vector<uint8_t>          encode_welcome(const WelcomePayload& p);
std::optional<WelcomePayload> decode_welcome(std::span<const uint8_t> data);

std::vector<uint8_t>         encode_resp_ok(const RespOkPayload& p);
std::optional<RespOkPayload> decode_resp_ok(std::span<const uint8_t> data);

std::vector<uint8_t>          encode_resp_err(const RespErrPayload& p);
std::optional<RespErrPayload> decode_resp_err(std::span<const uint8_t> data);

std::vector<uint8_t>                   encode_cmd_assign_figures(const CmdAssignFiguresPayload& p);
std::optional<CmdAssignFiguresPayload> decode_cmd_assign_figures(std::span<const uint8_t> data);

std::vector<uint8_t>                  encode_req_create_window(const ReqCreateWindowPayload& p);
std::optional<ReqCreateWindowPayload> decode_req_create_window(std::span<const uint8_t> data);

std::vector<uint8_t>                 encode_req_close_window(const ReqCloseWindowPayload& p);
std::optional<ReqCloseWindowPayload> decode_req_close_window(std::span<const uint8_t> data);

std::vector<uint8_t>                  encode_cmd_remove_figure(const CmdRemoveFigurePayload& p);
std::optional<CmdRemoveFigurePayload> decode_cmd_remove_figure(std::span<const uint8_t> data);

std::vector<uint8_t>               encode_cmd_set_active(const CmdSetActivePayload& p);
std::optional<CmdSetActivePayload> decode_cmd_set_active(std::span<const uint8_t> data);

std::vector<uint8_t>                 encode_cmd_close_window(const CmdCloseWindowPayload& p);
std::optional<CmdCloseWindowPayload> decode_cmd_close_window(std::span<const uint8_t> data);

std::vector<uint8_t>                  encode_req_detach_figure(const ReqDetachFigurePayload& p);
std::optional<ReqDetachFigurePayload> decode_req_detach_figure(std::span<const uint8_t> data);

std::vector<uint8_t>                encode_state_snapshot(const StateSnapshotPayload& p);
std::optional<StateSnapshotPayload> decode_state_snapshot(std::span<const uint8_t> data);

std::vector<uint8_t>            encode_state_diff(const StateDiffPayload& p);
std::optional<StateDiffPayload> decode_state_diff(std::span<const uint8_t> data);

std::vector<uint8_t>           encode_ack_state(const AckStatePayload& p);
std::optional<AckStatePayload> decode_ack_state(std::span<const uint8_t> data);

std::vector<uint8_t>           encode_evt_input(const EvtInputPayload& p);
std::optional<EvtInputPayload> decode_evt_input(std::span<const uint8_t> data);

std::vector<uint8_t>                  encode_req_create_figure(const ReqCreateFigurePayload& p);
std::optional<ReqCreateFigurePayload> decode_req_create_figure(std::span<const uint8_t> data);

std::vector<uint8_t>                   encode_req_destroy_figure(const ReqDestroyFigurePayload& p);
std::optional<ReqDestroyFigurePayload> decode_req_destroy_figure(std::span<const uint8_t> data);

std::vector<uint8_t>                encode_req_create_axes(const ReqCreateAxesPayload& p);
std::optional<ReqCreateAxesPayload> decode_req_create_axes(std::span<const uint8_t> data);

std::vector<uint8_t>               encode_req_add_series(const ReqAddSeriesPayload& p);
std::optional<ReqAddSeriesPayload> decode_req_add_series(std::span<const uint8_t> data);

std::vector<uint8_t>                  encode_req_remove_series(const ReqRemoveSeriesPayload& p);
std::optional<ReqRemoveSeriesPayload> decode_req_remove_series(std::span<const uint8_t> data);

std::vector<uint8_t>             encode_req_set_data(const ReqSetDataPayload& p);
std::optional<ReqSetDataPayload> decode_req_set_data(std::span<const uint8_t> data);

std::vector<uint8_t> encode_req_update_property(const ReqUpdatePropertyPayload& p);
std::optional<ReqUpdatePropertyPayload> decode_req_update_property(std::span<const uint8_t> data);

std::vector<uint8_t>          encode_req_show(const ReqShowPayload& p);
std::optional<ReqShowPayload> decode_req_show(std::span<const uint8_t> data);

std::vector<uint8_t>                encode_req_append_data(const ReqAppendDataPayload& p);
std::optional<ReqAppendDataPayload> decode_req_append_data(std::span<const uint8_t> data);

std::vector<uint8_t>                 encode_req_close_figure(const ReqCloseFigurePayload& p);
std::optional<ReqCloseFigurePayload> decode_req_close_figure(std::span<const uint8_t> data);

std::vector<uint8_t>                 encode_req_update_batch(const ReqUpdateBatchPayload& p);
std::optional<ReqUpdateBatchPayload> decode_req_update_batch(std::span<const uint8_t> data);

std::vector<uint8_t>               encode_req_reconnect(const ReqReconnectPayload& p);
std::optional<ReqReconnectPayload> decode_req_reconnect(std::span<const uint8_t> data);

std::optional<ReqAnimStartPayload> decode_req_anim_start(std::span<const uint8_t> data);

std::vector<uint8_t> encode_resp_figure_created(const RespFigureCreatedPayload& p);
std::optional<RespFigureCreatedPayload> decode_resp_figure_created(std::span<const uint8_t> data);

std::vector<uint8_t>                  encode_resp_axes_created(const RespAxesCreatedPayload& p);
std::optional<RespAxesCreatedPayload> decode_resp_axes_created(std::span<const uint8_t> data);

std::vector<uint8_t>                  encode_resp_series_added(const RespSeriesAddedPayload& p);
std::optional<RespSeriesAddedPayload> decode_resp_series_added(std::span<const uint8_t> data);

std::vector<uint8_t>                 encode_resp_figure_list(const RespFigureListPayload& p);
std::optional<RespFigureListPayload> decode_resp_figure_list(std::span<const uint8_t> data);

std::vector<uint8_t>                  encode_evt_window_closed(const EvtWindowClosedPayload& p);
std::optional<EvtWindowClosedPayload> decode_evt_window_closed(std::span<const uint8_t> data);

std::vector<uint8_t> encode_evt_figure_destroyed(const EvtFigureDestroyedPayload& p);
std::optional<EvtFigureDestroyedPayload> decode_evt_figure_destroyed(std::span<const uint8_t> data);

std::vector<uint8_t>                  encode_req_declare_topic(const ReqDeclareTopicPayload& p);
std::optional<ReqDeclareTopicPayload> decode_req_declare_topic(std::span<const uint8_t> data);

std::vector<uint8_t> encode_req_publish_topic_samples(const ReqPublishTopicSamplesPayload& p);
std::optional<ReqPublishTopicSamplesPayload> decode_req_publish_topic_samples(
    std::span<const uint8_t> data);

std::vector<uint8_t> encode_req_subscribe_topic(const ReqSubscribeTopicPayload& p);
std::optional<ReqSubscribeTopicPayload> decode_req_subscribe_topic(std::span<const uint8_t> data);

std::vector<uint8_t> encode_req_unsubscribe_topic(const ReqUnsubscribeTopicPayload& p);
std::optional<ReqUnsubscribeTopicPayload> decode_req_unsubscribe_topic(
    std::span<const uint8_t> data);

std::vector<uint8_t>                encode_req_list_topics(const ReqListTopicsPayload& p);
std::optional<ReqListTopicsPayload> decode_req_list_topics(std::span<const uint8_t> data);

std::vector<uint8_t>                encode_resp_topic_list(const RespTopicListPayload& p);
std::optional<RespTopicListPayload> decode_resp_topic_list(std::span<const uint8_t> data);

std::vector<uint8_t> encode_resp_subscribe_topic(const RespSubscribeTopicPayload& p);
std::optional<RespSubscribeTopicPayload> decode_resp_subscribe_topic(std::span<const uint8_t> data);

std::vector<uint8_t> encode_evt_topic_list_changed(const EvtTopicListChangedPayload& p);
std::optional<EvtTopicListChangedPayload> decode_evt_topic_list_changed(
    std::span<const uint8_t> data);

}   // namespace spectra::ipc
