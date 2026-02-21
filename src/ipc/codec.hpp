#pragma once

#include "message.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace spectra::ipc
{

// ─── Header serialization ────────────────────────────────────────────────────
// Encodes/decodes the fixed 40-byte message header.

// Encode header into exactly HEADER_SIZE bytes (appended to `out`).
void encode_header(const MessageHeader& hdr, std::vector<uint8_t>& out);

// Decode header from exactly HEADER_SIZE bytes.
// Returns std::nullopt if magic bytes are wrong or buffer too small.
std::optional<MessageHeader> decode_header(std::span<const uint8_t> data);

// ─── Full message serialization ──────────────────────────────────────────────

// Encode a complete message (header + payload) into a byte buffer.
std::vector<uint8_t> encode_message(const Message& msg);

// Decode a complete message from a byte buffer.
// Returns std::nullopt on any framing/size error.
std::optional<Message> decode_message(std::span<const uint8_t> data);

// ─── Payload serialization (simple TLV-style binary) ─────────────────────────
// Format for each field: [tag: uint8_t] [len: uint32_t LE] [data: len bytes]
// Tags:
//   0x01 = uint16_t (2 bytes)
//   0x02 = uint32_t (4 bytes)
//   0x03 = uint64_t (8 bytes)
//   0x04 = string   (len bytes, no null terminator)

// Payload encoder — builds a TLV byte buffer.
class PayloadEncoder
{
   public:
    void put_u16(uint8_t tag, uint16_t val);
    void put_u32(uint8_t tag, uint32_t val);
    void put_u64(uint8_t tag, uint64_t val);
    void put_string(uint8_t tag, const std::string& val);

    const std::vector<uint8_t>& data() const { return buf_; }
    std::vector<uint8_t> take() { return std::move(buf_); }

   private:
    std::vector<uint8_t> buf_;
};

// Payload decoder — reads TLV fields from a byte buffer.
class PayloadDecoder
{
   public:
    explicit PayloadDecoder(std::span<const uint8_t> data);

    // Advance to the next field. Returns false when no more fields.
    bool next();

    uint8_t  tag() const { return tag_; }
    uint32_t field_len() const { return len_; }

    // Read the current field's value (caller must check tag first).
    uint16_t    as_u16() const;
    uint32_t    as_u32() const;
    uint64_t    as_u64() const;
    std::string as_string() const;

   private:
    std::span<const uint8_t> data_;
    size_t   pos_ = 0;
    uint8_t  tag_ = 0;
    uint32_t len_ = 0;
    size_t   val_offset_ = 0;
};

// ─── Convenience: encode/decode handshake payloads ───────────────────────────

// Field tags for HelloPayload
static constexpr uint8_t TAG_PROTOCOL_MAJOR = 0x10;
static constexpr uint8_t TAG_PROTOCOL_MINOR = 0x11;
static constexpr uint8_t TAG_AGENT_BUILD    = 0x12;
static constexpr uint8_t TAG_CAPABILITIES   = 0x13;

// Field tags for WelcomePayload
static constexpr uint8_t TAG_SESSION_ID     = 0x20;
static constexpr uint8_t TAG_WINDOW_ID      = 0x21;
static constexpr uint8_t TAG_PROCESS_ID     = 0x22;
static constexpr uint8_t TAG_HEARTBEAT_MS   = 0x23;
static constexpr uint8_t TAG_MODE           = 0x24;

// Field tags for RespErr
static constexpr uint8_t TAG_REQUEST_ID     = 0x30;
static constexpr uint8_t TAG_ERROR_CODE     = 0x31;
static constexpr uint8_t TAG_ERROR_MESSAGE  = 0x32;

std::vector<uint8_t> encode_hello(const HelloPayload& p);
std::optional<HelloPayload> decode_hello(std::span<const uint8_t> data);

std::vector<uint8_t> encode_welcome(const WelcomePayload& p);
std::optional<WelcomePayload> decode_welcome(std::span<const uint8_t> data);

std::vector<uint8_t> encode_resp_ok(const RespOkPayload& p);
std::optional<RespOkPayload> decode_resp_ok(std::span<const uint8_t> data);

std::vector<uint8_t> encode_resp_err(const RespErrPayload& p);
std::optional<RespErrPayload> decode_resp_err(std::span<const uint8_t> data);

// ─── Convenience: encode/decode control payloads ─────────────────────────────

// Field tags for control payloads
static constexpr uint8_t TAG_FIGURE_ID       = 0x40;
static constexpr uint8_t TAG_FIGURE_IDS      = 0x41;  // repeated u64
static constexpr uint8_t TAG_ACTIVE_FIGURE   = 0x42;
static constexpr uint8_t TAG_TEMPLATE_WINDOW = 0x43;
static constexpr uint8_t TAG_REASON          = 0x44;
static constexpr uint8_t TAG_FIGURE_COUNT    = 0x45;
static constexpr uint8_t TAG_SOURCE_WINDOW   = 0x46;
static constexpr uint8_t TAG_SCREEN_X        = 0x47;
static constexpr uint8_t TAG_SCREEN_Y        = 0x48;

std::vector<uint8_t> encode_cmd_assign_figures(const CmdAssignFiguresPayload& p);
std::optional<CmdAssignFiguresPayload> decode_cmd_assign_figures(std::span<const uint8_t> data);

std::vector<uint8_t> encode_req_create_window(const ReqCreateWindowPayload& p);
std::optional<ReqCreateWindowPayload> decode_req_create_window(std::span<const uint8_t> data);

std::vector<uint8_t> encode_req_close_window(const ReqCloseWindowPayload& p);
std::optional<ReqCloseWindowPayload> decode_req_close_window(std::span<const uint8_t> data);

std::vector<uint8_t> encode_cmd_remove_figure(const CmdRemoveFigurePayload& p);
std::optional<CmdRemoveFigurePayload> decode_cmd_remove_figure(std::span<const uint8_t> data);

std::vector<uint8_t> encode_cmd_set_active(const CmdSetActivePayload& p);
std::optional<CmdSetActivePayload> decode_cmd_set_active(std::span<const uint8_t> data);

std::vector<uint8_t> encode_cmd_close_window(const CmdCloseWindowPayload& p);
std::optional<CmdCloseWindowPayload> decode_cmd_close_window(std::span<const uint8_t> data);

std::vector<uint8_t> encode_req_detach_figure(const ReqDetachFigurePayload& p);
std::optional<ReqDetachFigurePayload> decode_req_detach_figure(std::span<const uint8_t> data);

// ─── Convenience: encode/decode state sync payloads ──────────────────────────

// Field tags for state sync
static constexpr uint8_t TAG_REVISION        = 0x50;
static constexpr uint8_t TAG_BASE_REVISION   = 0x51;
static constexpr uint8_t TAG_NEW_REVISION    = 0x52;
static constexpr uint8_t TAG_FIGURE_BLOB     = 0x53;  // nested TLV for a figure
static constexpr uint8_t TAG_AXIS_BLOB       = 0x54;  // nested TLV for an axis
static constexpr uint8_t TAG_SERIES_BLOB     = 0x55;  // nested TLV for a series
static constexpr uint8_t TAG_DIFF_OP_BLOB    = 0x56;  // nested TLV for a diff op

// Sub-tags within figure/axis/series blobs
static constexpr uint8_t TAG_TITLE           = 0x60;
static constexpr uint8_t TAG_WIDTH           = 0x61;
static constexpr uint8_t TAG_HEIGHT          = 0x62;
static constexpr uint8_t TAG_GRID_ROWS       = 0x63;
static constexpr uint8_t TAG_GRID_COLS       = 0x64;
static constexpr uint8_t TAG_X_MIN           = 0x65;
static constexpr uint8_t TAG_X_MAX           = 0x66;
static constexpr uint8_t TAG_Y_MIN           = 0x67;
static constexpr uint8_t TAG_Y_MAX           = 0x68;
static constexpr uint8_t TAG_GRID_VISIBLE    = 0x69;
static constexpr uint8_t TAG_X_LABEL         = 0x6A;
static constexpr uint8_t TAG_Y_LABEL         = 0x6B;
static constexpr uint8_t TAG_SERIES_NAME     = 0x6C;
static constexpr uint8_t TAG_SERIES_TYPE     = 0x6D;
static constexpr uint8_t TAG_COLOR_R         = 0x6E;
static constexpr uint8_t TAG_COLOR_G         = 0x6F;
static constexpr uint8_t TAG_COLOR_B         = 0x70;
static constexpr uint8_t TAG_COLOR_A         = 0x71;
static constexpr uint8_t TAG_LINE_WIDTH      = 0x72;
static constexpr uint8_t TAG_MARKER_SIZE     = 0x73;
static constexpr uint8_t TAG_VISIBLE         = 0x74;
static constexpr uint8_t TAG_OPACITY_VAL     = 0x75;
static constexpr uint8_t TAG_POINT_COUNT     = 0x76;
static constexpr uint8_t TAG_SERIES_DATA     = 0x77;  // raw float array
static constexpr uint8_t TAG_WINDOW_GROUP    = 0x78;  // figure window grouping

// Knob blob tags
static constexpr uint8_t TAG_KNOB_BLOB       = 0x79;  // nested TLV for a knob
static constexpr uint8_t TAG_KNOB_NAME       = 0x7A;
static constexpr uint8_t TAG_KNOB_TYPE       = 0x7B;
static constexpr uint8_t TAG_KNOB_VALUE      = 0x7C;
static constexpr uint8_t TAG_KNOB_MIN        = 0x7D;
static constexpr uint8_t TAG_KNOB_MAX        = 0x7E;
static constexpr uint8_t TAG_KNOB_STEP       = 0x7F;
static constexpr uint8_t TAG_KNOB_CHOICE     = 0xA0;  // repeated string

// Sub-tags for DiffOp
static constexpr uint8_t TAG_OP_TYPE         = 0x80;
static constexpr uint8_t TAG_AXES_INDEX      = 0x81;
static constexpr uint8_t TAG_SERIES_INDEX    = 0x82;
static constexpr uint8_t TAG_F1              = 0x83;
static constexpr uint8_t TAG_F2              = 0x84;
static constexpr uint8_t TAG_F3              = 0x85;
static constexpr uint8_t TAG_F4              = 0x86;
static constexpr uint8_t TAG_BOOL_VAL        = 0x87;
static constexpr uint8_t TAG_STR_VAL         = 0x88;
static constexpr uint8_t TAG_OP_DATA         = 0x89;  // raw float array for diff

// Field tags for EVT_INPUT
static constexpr uint8_t TAG_INPUT_TYPE      = 0x90;
static constexpr uint8_t TAG_KEY_CODE        = 0x91;
static constexpr uint8_t TAG_MODS            = 0x92;
static constexpr uint8_t TAG_CURSOR_X        = 0x93;
static constexpr uint8_t TAG_CURSOR_Y        = 0x94;

// PayloadEncoder extension for floats and raw byte blobs
// (added as free functions to avoid modifying the class)
void payload_put_float(PayloadEncoder& enc, uint8_t tag, float val);
void payload_put_blob(PayloadEncoder& enc, uint8_t tag, const std::vector<uint8_t>& blob);
void payload_put_float_array(PayloadEncoder& enc, uint8_t tag, const std::vector<float>& arr);
void payload_put_double(PayloadEncoder& enc, uint8_t tag, double val);
void payload_put_bool(PayloadEncoder& enc, uint8_t tag, bool val);

float payload_as_float(const PayloadDecoder& dec);
double payload_as_double(const PayloadDecoder& dec);
bool payload_as_bool(const PayloadDecoder& dec);
std::vector<float> payload_as_float_array(const PayloadDecoder& dec);
std::vector<uint8_t> payload_as_blob(const PayloadDecoder& dec);

std::vector<uint8_t> encode_state_snapshot(const StateSnapshotPayload& p);
std::optional<StateSnapshotPayload> decode_state_snapshot(std::span<const uint8_t> data);

std::vector<uint8_t> encode_state_diff(const StateDiffPayload& p);
std::optional<StateDiffPayload> decode_state_diff(std::span<const uint8_t> data);

std::vector<uint8_t> encode_ack_state(const AckStatePayload& p);
std::optional<AckStatePayload> decode_ack_state(std::span<const uint8_t> data);

std::vector<uint8_t> encode_evt_input(const EvtInputPayload& p);
std::optional<EvtInputPayload> decode_evt_input(std::span<const uint8_t> data);

}  // namespace spectra::ipc
