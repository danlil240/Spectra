#pragma once

// Legacy TLV payload decoding (read-only).  All new IPC writes use FlatBuffers;
// see codec_fb.hpp and encode_fb_* / encode_* in codec.hpp.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "message.hpp"

namespace spectra::ipc
{

// Payload decoder — reads TLV fields from a byte buffer.
// Format per field: [tag: uint8_t] [len: uint32_t LE] [data: len bytes]
class PayloadDecoder
{
   public:
    explicit PayloadDecoder(std::span<const uint8_t> data);

    bool next();

    uint8_t  tag() const { return tag_; }
    uint32_t field_len() const { return len_; }

    uint16_t    as_u16() const;
    uint32_t    as_u32() const;
    uint64_t    as_u64() const;
    std::string as_string() const;

   private:
    std::span<const uint8_t> data_;
    size_t                   pos_        = 0;
    uint8_t                  tag_        = 0;
    uint32_t                 len_        = 0;
    size_t                   val_offset_ = 0;
};

// Field tags (legacy TLV wire format)
static constexpr uint8_t TAG_PROTOCOL_MAJOR = 0x10;
static constexpr uint8_t TAG_PROTOCOL_MINOR = 0x11;
static constexpr uint8_t TAG_AGENT_BUILD    = 0x12;
static constexpr uint8_t TAG_CAPABILITIES   = 0x13;
static constexpr uint8_t TAG_CLIENT_TYPE    = 0x14;

static constexpr uint8_t TAG_SESSION_ID   = 0x20;
static constexpr uint8_t TAG_WINDOW_ID    = 0x21;
static constexpr uint8_t TAG_PROCESS_ID   = 0x22;
static constexpr uint8_t TAG_HEARTBEAT_MS = 0x23;
static constexpr uint8_t TAG_MODE         = 0x24;

static constexpr uint8_t TAG_REQUEST_ID    = 0x30;
static constexpr uint8_t TAG_ERROR_CODE    = 0x31;
static constexpr uint8_t TAG_ERROR_MESSAGE = 0x32;

static constexpr uint8_t TAG_FIGURE_ID       = 0x40;
static constexpr uint8_t TAG_FIGURE_IDS      = 0x41;
static constexpr uint8_t TAG_ACTIVE_FIGURE   = 0x42;
static constexpr uint8_t TAG_TEMPLATE_WINDOW = 0x43;
static constexpr uint8_t TAG_REASON          = 0x44;
static constexpr uint8_t TAG_FIGURE_COUNT    = 0x45;
static constexpr uint8_t TAG_SOURCE_WINDOW   = 0x46;
static constexpr uint8_t TAG_SCREEN_X        = 0x47;
static constexpr uint8_t TAG_SCREEN_Y        = 0x48;

static constexpr uint8_t TAG_REVISION      = 0x50;
static constexpr uint8_t TAG_BASE_REVISION = 0x51;
static constexpr uint8_t TAG_NEW_REVISION  = 0x52;
static constexpr uint8_t TAG_FIGURE_BLOB   = 0x53;
static constexpr uint8_t TAG_AXIS_BLOB     = 0x54;
static constexpr uint8_t TAG_SERIES_BLOB   = 0x55;
static constexpr uint8_t TAG_DIFF_OP_BLOB  = 0x56;

static constexpr uint8_t TAG_TITLE        = 0x60;
static constexpr uint8_t TAG_WIDTH        = 0x61;
static constexpr uint8_t TAG_HEIGHT       = 0x62;
static constexpr uint8_t TAG_GRID_ROWS    = 0x63;
static constexpr uint8_t TAG_GRID_COLS    = 0x64;
static constexpr uint8_t TAG_X_MIN        = 0x65;
static constexpr uint8_t TAG_X_MAX        = 0x66;
static constexpr uint8_t TAG_Y_MIN        = 0x67;
static constexpr uint8_t TAG_Y_MAX        = 0x68;
static constexpr uint8_t TAG_GRID_VISIBLE = 0x69;
static constexpr uint8_t TAG_X_LABEL      = 0x6A;
static constexpr uint8_t TAG_Y_LABEL      = 0x6B;
static constexpr uint8_t TAG_SERIES_NAME  = 0x6C;
static constexpr uint8_t TAG_SERIES_TYPE  = 0x6D;
static constexpr uint8_t TAG_COLOR_R      = 0x6E;
static constexpr uint8_t TAG_COLOR_G      = 0x6F;
static constexpr uint8_t TAG_COLOR_B      = 0x70;
static constexpr uint8_t TAG_COLOR_A      = 0x71;
static constexpr uint8_t TAG_LINE_WIDTH   = 0x72;
static constexpr uint8_t TAG_MARKER_SIZE  = 0x73;
static constexpr uint8_t TAG_VISIBLE      = 0x74;
static constexpr uint8_t TAG_OPACITY_VAL  = 0x75;
static constexpr uint8_t TAG_POINT_COUNT  = 0x76;
static constexpr uint8_t TAG_SERIES_DATA  = 0x77;
static constexpr uint8_t TAG_WINDOW_GROUP = 0x78;
static constexpr uint8_t TAG_LIVE_FPS     = 0xA1;

static constexpr uint8_t TAG_KNOB_BLOB   = 0x79;
static constexpr uint8_t TAG_KNOB_NAME   = 0x7A;
static constexpr uint8_t TAG_KNOB_TYPE   = 0x7B;
static constexpr uint8_t TAG_KNOB_VALUE  = 0x7C;
static constexpr uint8_t TAG_KNOB_MIN    = 0x7D;
static constexpr uint8_t TAG_KNOB_MAX    = 0x7E;
static constexpr uint8_t TAG_KNOB_STEP   = 0x7F;
static constexpr uint8_t TAG_KNOB_CHOICE = 0xA0;

static constexpr uint8_t TAG_OP_TYPE      = 0x80;
static constexpr uint8_t TAG_AXES_INDEX   = 0x81;
static constexpr uint8_t TAG_SERIES_INDEX = 0x82;
static constexpr uint8_t TAG_F1           = 0x83;
static constexpr uint8_t TAG_F2           = 0x84;
static constexpr uint8_t TAG_F3           = 0x85;
static constexpr uint8_t TAG_F4           = 0x86;
static constexpr uint8_t TAG_BOOL_VAL     = 0x87;
static constexpr uint8_t TAG_STR_VAL      = 0x88;
static constexpr uint8_t TAG_OP_DATA      = 0x89;

static constexpr uint8_t TAG_INPUT_TYPE = 0x90;
static constexpr uint8_t TAG_KEY_CODE   = 0x91;
static constexpr uint8_t TAG_MODS       = 0x92;
static constexpr uint8_t TAG_CURSOR_X   = 0x93;
static constexpr uint8_t TAG_CURSOR_Y   = 0x94;

static constexpr uint8_t TAG_GRID_INDEX        = 0xA1;
static constexpr uint8_t TAG_SERIES_LABEL      = 0xA2;
static constexpr uint8_t TAG_DTYPE             = 0xA3;
static constexpr uint8_t TAG_PROPERTY_NAME     = 0xA4;
static constexpr uint8_t TAG_SESSION_TOKEN     = 0xA5;
static constexpr uint8_t TAG_IS_3D             = 0xA6;
static constexpr uint8_t TAG_Z_MIN             = 0xA7;
static constexpr uint8_t TAG_Z_MAX             = 0xA8;
static constexpr uint8_t TAG_BLOB_INLINE       = 0xB0;
static constexpr uint8_t TAG_BATCH_ITEM        = 0xB1;
static constexpr uint8_t TAG_SERIES_AXES_INDEX = 0xB2;

float                tlv_payload_as_float(const PayloadDecoder& dec);
double               tlv_payload_as_double(const PayloadDecoder& dec);
bool                 tlv_payload_as_bool(const PayloadDecoder& dec);
std::vector<float>   tlv_payload_as_float_array(const PayloadDecoder& dec);
std::vector<uint8_t> tlv_payload_as_blob(const PayloadDecoder& dec);

std::optional<ReqSetDataPayload>        tlv_decode_req_set_data(std::span<const uint8_t> data);
std::optional<ReqAppendDataPayload>     tlv_decode_req_append_data(std::span<const uint8_t> data);
std::optional<ReqUpdatePropertyPayload> tlv_decode_req_update_property(
    std::span<const uint8_t> data);
std::optional<ReqUpdateBatchPayload> tlv_decode_req_update_batch(std::span<const uint8_t> data);
std::optional<ReqAnimStartPayload>   tlv_decode_req_anim_start(std::span<const uint8_t> data);

}   // namespace spectra::ipc
