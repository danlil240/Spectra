#include "codec.hpp"

#include <cstring>

#include "codec_fb.hpp"

namespace spectra::ipc
{

// FlatBuffers payloads are prefixed with 0x01; legacy TLV decode remains in
// codec_tlv_decode.cpp for a few message types still read from old clients.

#define DECODE_TRY_FB(decode_fb_func, data)                                                    \
    do                                                                                         \
    {                                                                                          \
        if (detect_payload_format(data) == PayloadFormat::FLATBUFFERS)                         \
        {                                                                                      \
            auto _fb_body = strip_fb_prefix(data);                                             \
            return decode_fb_func(std::span<const uint8_t>(_fb_body.data(), _fb_body.size())); \
        }                                                                                      \
    } while (0)

#define IPC_ENCODE_FB(name, p) return encode_fb_##name(p)

#define IPC_DECODE_FB_ONLY(fb_name, data)         \
    do                                            \
    {                                             \
        DECODE_TRY_FB(decode_fb_##fb_name, data); \
        return std::nullopt;                      \
    } while (0)

#define IPC_DECODE_FB_OR_TLV(fb_name, tlv_fn, data) \
    do                                              \
    {                                               \
        DECODE_TRY_FB(decode_fb_##fb_name, data);   \
        return tlv_fn(data);                        \
    } while (0)

// ─── Little-endian helpers (framing header only) ─────────────────────────────

static void write_u16_le(std::vector<uint8_t>& buf, uint16_t v)
{
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

static void write_u32_le(std::vector<uint8_t>& buf, uint32_t v)
{
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

static void write_u64_le(std::vector<uint8_t>& buf, uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        buf.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
}

static uint16_t read_u16_le(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

static uint32_t read_u32_le(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
           | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

static uint64_t read_u64_le(const uint8_t* p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= static_cast<uint64_t>(p[i]) << (i * 8);
    return v;
}

// ─── Header encode/decode ────────────────────────────────────────────────────

void encode_header(const MessageHeader& hdr, std::vector<uint8_t>& out)
{
    out.reserve(out.size() + HEADER_SIZE);
    out.push_back(MAGIC_0);
    out.push_back(MAGIC_1);
    write_u16_le(out, static_cast<uint16_t>(hdr.type));
    write_u32_le(out, hdr.payload_len);
    write_u64_le(out, hdr.seq);
    write_u64_le(out, hdr.request_id);
    write_u64_le(out, hdr.session_id);
    write_u64_le(out, hdr.window_id);
}

std::optional<MessageHeader> decode_header(std::span<const uint8_t> data)
{
    if (data.size() < HEADER_SIZE)
        return std::nullopt;
    if (data[0] != MAGIC_0 || data[1] != MAGIC_1)
        return std::nullopt;

    MessageHeader hdr;
    hdr.type        = static_cast<MessageType>(read_u16_le(&data[2]));
    hdr.payload_len = read_u32_le(&data[4]);
    hdr.seq         = read_u64_le(&data[8]);
    hdr.request_id  = read_u64_le(&data[16]);
    hdr.session_id  = read_u64_le(&data[24]);
    hdr.window_id   = read_u64_le(&data[32]);
    return hdr;
}

std::vector<uint8_t> encode_message(const Message& msg)
{
    std::vector<uint8_t> out;
    MessageHeader        hdr = msg.header;
    hdr.payload_len          = static_cast<uint32_t>(msg.payload.size());
    encode_header(hdr, out);
    out.insert(out.end(), msg.payload.begin(), msg.payload.end());
    return out;
}

std::optional<Message> decode_message(std::span<const uint8_t> data)
{
    auto hdr_opt = decode_header(data);
    if (!hdr_opt)
        return std::nullopt;

    auto& hdr = *hdr_opt;
    if (hdr.payload_len > MAX_PAYLOAD_SIZE)
        return std::nullopt;
    if (data.size() < HEADER_SIZE + hdr.payload_len)
        return std::nullopt;

    Message msg;
    msg.header = hdr;
    msg.payload.assign(data.begin() + HEADER_SIZE, data.begin() + HEADER_SIZE + hdr.payload_len);
    return msg;
}

// ─── Payload encode/decode (FlatBuffers default) ─────────────────────────────

std::vector<uint8_t> encode_hello(const HelloPayload& p)
{
    IPC_ENCODE_FB(hello, p);
}
std::optional<HelloPayload> decode_hello(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(hello, data);
}

std::vector<uint8_t> encode_welcome(const WelcomePayload& p)
{
    IPC_ENCODE_FB(welcome, p);
}
std::optional<WelcomePayload> decode_welcome(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(welcome, data);
}

std::vector<uint8_t> encode_resp_ok(const RespOkPayload& p)
{
    IPC_ENCODE_FB(resp_ok, p);
}
std::optional<RespOkPayload> decode_resp_ok(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(resp_ok, data);
}

std::vector<uint8_t> encode_resp_err(const RespErrPayload& p)
{
    IPC_ENCODE_FB(resp_err, p);
}
std::optional<RespErrPayload> decode_resp_err(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(resp_err, data);
}

std::vector<uint8_t> encode_cmd_assign_figures(const CmdAssignFiguresPayload& p)
{
    IPC_ENCODE_FB(cmd_assign_figures, p);
}
std::optional<CmdAssignFiguresPayload> decode_cmd_assign_figures(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(cmd_assign_figures, data);
}

std::vector<uint8_t> encode_req_create_window(const ReqCreateWindowPayload& p)
{
    IPC_ENCODE_FB(req_create_window, p);
}
std::optional<ReqCreateWindowPayload> decode_req_create_window(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(req_create_window, data);
}

std::vector<uint8_t> encode_req_close_window(const ReqCloseWindowPayload& p)
{
    IPC_ENCODE_FB(req_close_window, p);
}
std::optional<ReqCloseWindowPayload> decode_req_close_window(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(req_close_window, data);
}

std::vector<uint8_t> encode_cmd_remove_figure(const CmdRemoveFigurePayload& p)
{
    IPC_ENCODE_FB(cmd_remove_figure, p);
}
std::optional<CmdRemoveFigurePayload> decode_cmd_remove_figure(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(cmd_remove_figure, data);
}

std::vector<uint8_t> encode_cmd_set_active(const CmdSetActivePayload& p)
{
    IPC_ENCODE_FB(cmd_set_active, p);
}
std::optional<CmdSetActivePayload> decode_cmd_set_active(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(cmd_set_active, data);
}

std::vector<uint8_t> encode_cmd_close_window(const CmdCloseWindowPayload& p)
{
    IPC_ENCODE_FB(cmd_close_window, p);
}
std::optional<CmdCloseWindowPayload> decode_cmd_close_window(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(cmd_close_window, data);
}

std::vector<uint8_t> encode_req_detach_figure(const ReqDetachFigurePayload& p)
{
    IPC_ENCODE_FB(req_detach_figure, p);
}
std::optional<ReqDetachFigurePayload> decode_req_detach_figure(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(req_detach_figure, data);
}

std::vector<uint8_t> encode_state_snapshot(const StateSnapshotPayload& p)
{
    IPC_ENCODE_FB(state_snapshot, p);
}
std::optional<StateSnapshotPayload> decode_state_snapshot(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(state_snapshot, data);
}

std::vector<uint8_t> encode_state_diff(const StateDiffPayload& p)
{
    IPC_ENCODE_FB(state_diff, p);
}
std::optional<StateDiffPayload> decode_state_diff(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(state_diff, data);
}

std::vector<uint8_t> encode_ack_state(const AckStatePayload& p)
{
    IPC_ENCODE_FB(ack_state, p);
}
std::optional<AckStatePayload> decode_ack_state(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(ack_state, data);
}

std::vector<uint8_t> encode_evt_input(const EvtInputPayload& p)
{
    IPC_ENCODE_FB(evt_input, p);
}
std::optional<EvtInputPayload> decode_evt_input(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(evt_input, data);
}

std::vector<uint8_t> encode_req_create_figure(const ReqCreateFigurePayload& p)
{
    IPC_ENCODE_FB(req_create_figure, p);
}
std::optional<ReqCreateFigurePayload> decode_req_create_figure(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(req_create_figure, data);
}

std::vector<uint8_t> encode_req_destroy_figure(const ReqDestroyFigurePayload& p)
{
    IPC_ENCODE_FB(req_destroy_figure, p);
}
std::optional<ReqDestroyFigurePayload> decode_req_destroy_figure(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(req_destroy_figure, data);
}

std::vector<uint8_t> encode_req_create_axes(const ReqCreateAxesPayload& p)
{
    IPC_ENCODE_FB(req_create_axes, p);
}
std::optional<ReqCreateAxesPayload> decode_req_create_axes(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(req_create_axes, data);
}

std::vector<uint8_t> encode_req_add_series(const ReqAddSeriesPayload& p)
{
    IPC_ENCODE_FB(req_add_series, p);
}
std::optional<ReqAddSeriesPayload> decode_req_add_series(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(req_add_series, data);
}

std::vector<uint8_t> encode_req_remove_series(const ReqRemoveSeriesPayload& p)
{
    IPC_ENCODE_FB(req_remove_series, p);
}
std::optional<ReqRemoveSeriesPayload> decode_req_remove_series(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(req_remove_series, data);
}

std::vector<uint8_t> encode_req_set_data(const ReqSetDataPayload& p)
{
    IPC_ENCODE_FB(req_set_data, p);
}
std::optional<ReqSetDataPayload> decode_req_set_data(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_OR_TLV(req_set_data, tlv_decode_req_set_data, data);
}

std::vector<uint8_t> encode_req_append_data(const ReqAppendDataPayload& p)
{
    IPC_ENCODE_FB(req_append_data, p);
}
std::optional<ReqAppendDataPayload> decode_req_append_data(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_OR_TLV(req_append_data, tlv_decode_req_append_data, data);
}

std::vector<uint8_t> encode_req_update_property(const ReqUpdatePropertyPayload& p)
{
    IPC_ENCODE_FB(req_update_property, p);
}
std::optional<ReqUpdatePropertyPayload> decode_req_update_property(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_OR_TLV(req_update_property, tlv_decode_req_update_property, data);
}

std::vector<uint8_t> encode_req_show(const ReqShowPayload& p)
{
    IPC_ENCODE_FB(req_show, p);
}
std::optional<ReqShowPayload> decode_req_show(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(req_show, data);
}

std::vector<uint8_t> encode_req_close_figure(const ReqCloseFigurePayload& p)
{
    IPC_ENCODE_FB(req_close_figure, p);
}
std::optional<ReqCloseFigurePayload> decode_req_close_figure(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(req_close_figure, data);
}

std::vector<uint8_t> encode_req_update_batch(const ReqUpdateBatchPayload& p)
{
    IPC_ENCODE_FB(req_update_batch, p);
}
std::optional<ReqUpdateBatchPayload> decode_req_update_batch(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_OR_TLV(req_update_batch, tlv_decode_req_update_batch, data);
}

std::vector<uint8_t> encode_req_reconnect(const ReqReconnectPayload& p)
{
    IPC_ENCODE_FB(req_reconnect, p);
}
std::optional<ReqReconnectPayload> decode_req_reconnect(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(req_reconnect, data);
}

std::optional<ReqAnimStartPayload> decode_req_anim_start(std::span<const uint8_t> data)
{
    return tlv_decode_req_anim_start(data);
}

std::vector<uint8_t> encode_resp_figure_created(const RespFigureCreatedPayload& p)
{
    IPC_ENCODE_FB(resp_figure_created, p);
}
std::optional<RespFigureCreatedPayload> decode_resp_figure_created(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(resp_figure_created, data);
}

std::vector<uint8_t> encode_resp_axes_created(const RespAxesCreatedPayload& p)
{
    IPC_ENCODE_FB(resp_axes_created, p);
}
std::optional<RespAxesCreatedPayload> decode_resp_axes_created(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(resp_axes_created, data);
}

std::vector<uint8_t> encode_resp_series_added(const RespSeriesAddedPayload& p)
{
    IPC_ENCODE_FB(resp_series_added, p);
}
std::optional<RespSeriesAddedPayload> decode_resp_series_added(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(resp_series_added, data);
}

std::vector<uint8_t> encode_resp_figure_list(const RespFigureListPayload& p)
{
    IPC_ENCODE_FB(resp_figure_list, p);
}
std::optional<RespFigureListPayload> decode_resp_figure_list(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(resp_figure_list, data);
}

std::vector<uint8_t> encode_evt_window_closed(const EvtWindowClosedPayload& p)
{
    IPC_ENCODE_FB(evt_window_closed, p);
}
std::optional<EvtWindowClosedPayload> decode_evt_window_closed(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(evt_window_closed, data);
}

std::vector<uint8_t> encode_evt_figure_destroyed(const EvtFigureDestroyedPayload& p)
{
    IPC_ENCODE_FB(evt_figure_destroyed, p);
}
std::optional<EvtFigureDestroyedPayload> decode_evt_figure_destroyed(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(evt_figure_destroyed, data);
}

std::vector<uint8_t> encode_req_declare_topic(const ReqDeclareTopicPayload& p)
{
    IPC_ENCODE_FB(req_declare_topic, p);
}
std::optional<ReqDeclareTopicPayload> decode_req_declare_topic(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(req_declare_topic, data);
}

std::vector<uint8_t> encode_req_publish_topic_samples(const ReqPublishTopicSamplesPayload& p)
{
    IPC_ENCODE_FB(req_publish_topic_samples, p);
}
std::optional<ReqPublishTopicSamplesPayload> decode_req_publish_topic_samples(
    std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(req_publish_topic_samples, data);
}

std::vector<uint8_t> encode_req_subscribe_topic(const ReqSubscribeTopicPayload& p)
{
    IPC_ENCODE_FB(req_subscribe_topic, p);
}
std::optional<ReqSubscribeTopicPayload> decode_req_subscribe_topic(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(req_subscribe_topic, data);
}

std::vector<uint8_t> encode_req_unsubscribe_topic(const ReqUnsubscribeTopicPayload& p)
{
    IPC_ENCODE_FB(req_unsubscribe_topic, p);
}
std::optional<ReqUnsubscribeTopicPayload> decode_req_unsubscribe_topic(
    std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(req_unsubscribe_topic, data);
}

std::vector<uint8_t> encode_req_list_topics(const ReqListTopicsPayload& p)
{
    IPC_ENCODE_FB(req_list_topics, p);
}
std::optional<ReqListTopicsPayload> decode_req_list_topics(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(req_list_topics, data);
}

std::vector<uint8_t> encode_resp_topic_list(const RespTopicListPayload& p)
{
    IPC_ENCODE_FB(resp_topic_list, p);
}
std::optional<RespTopicListPayload> decode_resp_topic_list(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(resp_topic_list, data);
}

std::vector<uint8_t> encode_resp_subscribe_topic(const RespSubscribeTopicPayload& p)
{
    IPC_ENCODE_FB(resp_subscribe_topic, p);
}
std::optional<RespSubscribeTopicPayload> decode_resp_subscribe_topic(std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(resp_subscribe_topic, data);
}

std::vector<uint8_t> encode_evt_topic_list_changed(const EvtTopicListChangedPayload& p)
{
    IPC_ENCODE_FB(evt_topic_list_changed, p);
}
std::optional<EvtTopicListChangedPayload> decode_evt_topic_list_changed(
    std::span<const uint8_t> data)
{
    IPC_DECODE_FB_ONLY(evt_topic_list_changed, data);
}

#undef DECODE_TRY_FB
#undef IPC_ENCODE_FB
#undef IPC_DECODE_FB_ONLY
#undef IPC_DECODE_FB_OR_TLV

}   // namespace spectra::ipc
