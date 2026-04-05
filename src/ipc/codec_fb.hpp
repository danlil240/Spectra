#pragma once

// FlatBuffers codec adapter for the Spectra IPC protocol (v2).
//
// Provides encode_fb_* / decode_fb_* functions that convert between
// the existing spectra::ipc payload structs and FlatBuffers-encoded
// byte vectors.  The 40-byte framing header is unchanged.
//
// Migration approach:
//   - encode_fb_* serializes a payload struct into a FlatBuffers byte vector.
//   - decode_fb_* deserializes a FlatBuffers byte vector back into the struct.
//   - The codec.cpp functions delegate to these when the payload format is FB.

#include <optional>
#include <span>
#include <vector>

#include "message.hpp"

namespace spectra::ipc
{

// ─── Payload format negotiation ──────────────────────────────────────────────

// CAPABILITY_FLATBUFFERS is defined in message.hpp.

// Payload format indicator byte prepended to every payload (1 byte overhead).
// This enables per-message format detection without connection-level state.
enum class PayloadFormat : uint8_t
{
    TLV         = 0x00,   // Legacy TLV encoding
    FLATBUFFERS = 0x01,   // FlatBuffers encoding
};

// ─── FlatBuffers encode functions ────────────────────────────────────────────
// Each returns a byte vector with a 1-byte format prefix (0x01) followed
// by the FlatBuffers buffer.  Callers use this as the message payload.

std::vector<uint8_t> encode_fb_hello(const HelloPayload& p);
std::vector<uint8_t> encode_fb_welcome(const WelcomePayload& p);
std::vector<uint8_t> encode_fb_resp_ok(const RespOkPayload& p);
std::vector<uint8_t> encode_fb_resp_err(const RespErrPayload& p);

std::vector<uint8_t> encode_fb_cmd_assign_figures(const CmdAssignFiguresPayload& p);
std::vector<uint8_t> encode_fb_req_create_window(const ReqCreateWindowPayload& p);
std::vector<uint8_t> encode_fb_req_close_window(const ReqCloseWindowPayload& p);
std::vector<uint8_t> encode_fb_req_detach_figure(const ReqDetachFigurePayload& p);
std::vector<uint8_t> encode_fb_cmd_remove_figure(const CmdRemoveFigurePayload& p);
std::vector<uint8_t> encode_fb_cmd_set_active(const CmdSetActivePayload& p);
std::vector<uint8_t> encode_fb_cmd_close_window(const CmdCloseWindowPayload& p);

std::vector<uint8_t> encode_fb_state_snapshot(const StateSnapshotPayload& p);
std::vector<uint8_t> encode_fb_state_diff(const StateDiffPayload& p);
std::vector<uint8_t> encode_fb_ack_state(const AckStatePayload& p);

std::vector<uint8_t> encode_fb_evt_input(const EvtInputPayload& p);

std::vector<uint8_t> encode_fb_req_create_figure(const ReqCreateFigurePayload& p);
std::vector<uint8_t> encode_fb_req_destroy_figure(const ReqDestroyFigurePayload& p);
std::vector<uint8_t> encode_fb_req_create_axes(const ReqCreateAxesPayload& p);
std::vector<uint8_t> encode_fb_req_add_series(const ReqAddSeriesPayload& p);
std::vector<uint8_t> encode_fb_req_remove_series(const ReqRemoveSeriesPayload& p);
std::vector<uint8_t> encode_fb_req_set_data(const ReqSetDataPayload& p);
std::vector<uint8_t> encode_fb_req_update_property(const ReqUpdatePropertyPayload& p);
std::vector<uint8_t> encode_fb_req_show(const ReqShowPayload& p);
std::vector<uint8_t> encode_fb_req_close_figure(const ReqCloseFigurePayload& p);
std::vector<uint8_t> encode_fb_req_append_data(const ReqAppendDataPayload& p);
std::vector<uint8_t> encode_fb_req_update_batch(const ReqUpdateBatchPayload& p);
std::vector<uint8_t> encode_fb_req_reconnect(const ReqReconnectPayload& p);

std::vector<uint8_t> encode_fb_resp_figure_created(const RespFigureCreatedPayload& p);
std::vector<uint8_t> encode_fb_resp_axes_created(const RespAxesCreatedPayload& p);
std::vector<uint8_t> encode_fb_resp_series_added(const RespSeriesAddedPayload& p);
std::vector<uint8_t> encode_fb_resp_figure_list(const RespFigureListPayload& p);

std::vector<uint8_t> encode_fb_evt_window_closed(const EvtWindowClosedPayload& p);
std::vector<uint8_t> encode_fb_evt_figure_destroyed(const EvtFigureDestroyedPayload& p);

// ─── FlatBuffers decode functions ────────────────────────────────────────────
// Each expects the raw payload bytes (without the 1-byte format prefix —
// the caller strips it).  Returns nullopt on verification failure.

std::optional<HelloPayload>   decode_fb_hello(std::span<const uint8_t> data);
std::optional<WelcomePayload> decode_fb_welcome(std::span<const uint8_t> data);
std::optional<RespOkPayload>  decode_fb_resp_ok(std::span<const uint8_t> data);
std::optional<RespErrPayload> decode_fb_resp_err(std::span<const uint8_t> data);

std::optional<CmdAssignFiguresPayload> decode_fb_cmd_assign_figures(std::span<const uint8_t> data);
std::optional<ReqCreateWindowPayload>  decode_fb_req_create_window(std::span<const uint8_t> data);
std::optional<ReqCloseWindowPayload>   decode_fb_req_close_window(std::span<const uint8_t> data);
std::optional<ReqDetachFigurePayload>  decode_fb_req_detach_figure(std::span<const uint8_t> data);
std::optional<CmdRemoveFigurePayload>  decode_fb_cmd_remove_figure(std::span<const uint8_t> data);
std::optional<CmdSetActivePayload>     decode_fb_cmd_set_active(std::span<const uint8_t> data);
std::optional<CmdCloseWindowPayload>   decode_fb_cmd_close_window(std::span<const uint8_t> data);

std::optional<StateSnapshotPayload> decode_fb_state_snapshot(std::span<const uint8_t> data);
std::optional<StateDiffPayload>     decode_fb_state_diff(std::span<const uint8_t> data);
std::optional<AckStatePayload>      decode_fb_ack_state(std::span<const uint8_t> data);

std::optional<EvtInputPayload> decode_fb_evt_input(std::span<const uint8_t> data);

std::optional<ReqCreateFigurePayload>   decode_fb_req_create_figure(std::span<const uint8_t> data);
std::optional<ReqDestroyFigurePayload>  decode_fb_req_destroy_figure(std::span<const uint8_t> data);
std::optional<ReqCreateAxesPayload>     decode_fb_req_create_axes(std::span<const uint8_t> data);
std::optional<ReqAddSeriesPayload>      decode_fb_req_add_series(std::span<const uint8_t> data);
std::optional<ReqRemoveSeriesPayload>   decode_fb_req_remove_series(std::span<const uint8_t> data);
std::optional<ReqSetDataPayload>        decode_fb_req_set_data(std::span<const uint8_t> data);
std::optional<ReqUpdatePropertyPayload> decode_fb_req_update_property(
    std::span<const uint8_t> data);
std::optional<ReqShowPayload>        decode_fb_req_show(std::span<const uint8_t> data);
std::optional<ReqCloseFigurePayload> decode_fb_req_close_figure(std::span<const uint8_t> data);
std::optional<ReqAppendDataPayload>  decode_fb_req_append_data(std::span<const uint8_t> data);
std::optional<ReqUpdateBatchPayload> decode_fb_req_update_batch(std::span<const uint8_t> data);
std::optional<ReqReconnectPayload>   decode_fb_req_reconnect(std::span<const uint8_t> data);

std::optional<RespFigureCreatedPayload> decode_fb_resp_figure_created(
    std::span<const uint8_t> data);
std::optional<RespAxesCreatedPayload> decode_fb_resp_axes_created(std::span<const uint8_t> data);
std::optional<RespSeriesAddedPayload> decode_fb_resp_series_added(std::span<const uint8_t> data);
std::optional<RespFigureListPayload>  decode_fb_resp_figure_list(std::span<const uint8_t> data);

std::optional<EvtWindowClosedPayload>    decode_fb_evt_window_closed(std::span<const uint8_t> data);
std::optional<EvtFigureDestroyedPayload> decode_fb_evt_figure_destroyed(
    std::span<const uint8_t> data);

// ─── Format detection ────────────────────────────────────────────────────────

// Check the first byte of a payload to determine its format.
inline PayloadFormat detect_payload_format(std::span<const uint8_t> payload)
{
    if (payload.empty())
        return PayloadFormat::TLV;
    if (payload[0] == static_cast<uint8_t>(PayloadFormat::FLATBUFFERS))
        return PayloadFormat::FLATBUFFERS;
    return PayloadFormat::TLV;
}

// Strip the 1-byte format prefix from a FlatBuffers payload.
// Returns a heap-allocated copy so the buffer is properly aligned for
// FlatBuffers access (the original span at offset +1 may be misaligned).
inline std::vector<uint8_t> strip_fb_prefix(std::span<const uint8_t> payload)
{
    auto stripped = payload.subspan(1);
    return {stripped.begin(), stripped.end()};
}

}   // namespace spectra::ipc
