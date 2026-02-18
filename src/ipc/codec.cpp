#include "codec.hpp"

#include <cstring>

namespace spectra::ipc
{

// ─── Little-endian helpers ───────────────────────────────────────────────────

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
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
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

// ─── Full message encode/decode ──────────────────────────────────────────────

std::vector<uint8_t> encode_message(const Message& msg)
{
    std::vector<uint8_t> out;
    MessageHeader hdr = msg.header;
    hdr.payload_len = static_cast<uint32_t>(msg.payload.size());
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
    msg.payload.assign(data.begin() + HEADER_SIZE,
                       data.begin() + HEADER_SIZE + hdr.payload_len);
    return msg;
}

// ─── PayloadEncoder ──────────────────────────────────────────────────────────

void PayloadEncoder::put_u16(uint8_t tag, uint16_t val)
{
    buf_.push_back(tag);
    write_u32_le(buf_, 2);
    write_u16_le(buf_, val);
}

void PayloadEncoder::put_u32(uint8_t tag, uint32_t val)
{
    buf_.push_back(tag);
    write_u32_le(buf_, 4);
    write_u32_le(buf_, val);
}

void PayloadEncoder::put_u64(uint8_t tag, uint64_t val)
{
    buf_.push_back(tag);
    write_u32_le(buf_, 8);
    write_u64_le(buf_, val);
}

void PayloadEncoder::put_string(uint8_t tag, const std::string& val)
{
    buf_.push_back(tag);
    write_u32_le(buf_, static_cast<uint32_t>(val.size()));
    buf_.insert(buf_.end(), val.begin(), val.end());
}

// ─── PayloadDecoder ──────────────────────────────────────────────────────────

PayloadDecoder::PayloadDecoder(std::span<const uint8_t> data)
    : data_(data)
{
}

bool PayloadDecoder::next()
{
    // Need at least 1 (tag) + 4 (len) bytes
    if (pos_ + 5 > data_.size())
        return false;

    tag_ = data_[pos_];
    len_ = read_u32_le(&data_[pos_ + 1]);
    val_offset_ = pos_ + 5;

    if (val_offset_ + len_ > data_.size())
        return false;

    pos_ = val_offset_ + len_;
    return true;
}

uint16_t PayloadDecoder::as_u16() const
{
    if (len_ < 2) return 0;
    return read_u16_le(&data_[val_offset_]);
}

uint32_t PayloadDecoder::as_u32() const
{
    if (len_ < 4) return 0;
    return read_u32_le(&data_[val_offset_]);
}

uint64_t PayloadDecoder::as_u64() const
{
    if (len_ < 8) return 0;
    return read_u64_le(&data_[val_offset_]);
}

std::string PayloadDecoder::as_string() const
{
    return std::string(reinterpret_cast<const char*>(&data_[val_offset_]), len_);
}

// ─── Handshake payload encode/decode ─────────────────────────────────────────

std::vector<uint8_t> encode_hello(const HelloPayload& p)
{
    PayloadEncoder enc;
    enc.put_u16(TAG_PROTOCOL_MAJOR, p.protocol_major);
    enc.put_u16(TAG_PROTOCOL_MINOR, p.protocol_minor);
    enc.put_string(TAG_AGENT_BUILD, p.agent_build);
    enc.put_u32(TAG_CAPABILITIES, p.capabilities);
    return enc.take();
}

std::optional<HelloPayload> decode_hello(std::span<const uint8_t> data)
{
    HelloPayload p;
    PayloadDecoder dec(data);
    while (dec.next())
    {
        switch (dec.tag())
        {
            case TAG_PROTOCOL_MAJOR: p.protocol_major = dec.as_u16(); break;
            case TAG_PROTOCOL_MINOR: p.protocol_minor = dec.as_u16(); break;
            case TAG_AGENT_BUILD:    p.agent_build    = dec.as_string(); break;
            case TAG_CAPABILITIES:   p.capabilities   = dec.as_u32(); break;
            default: break;  // skip unknown tags (forward compat)
        }
    }
    return p;
}

std::vector<uint8_t> encode_welcome(const WelcomePayload& p)
{
    PayloadEncoder enc;
    enc.put_u64(TAG_SESSION_ID, p.session_id);
    enc.put_u64(TAG_WINDOW_ID, p.window_id);
    enc.put_u64(TAG_PROCESS_ID, p.process_id);
    enc.put_u32(TAG_HEARTBEAT_MS, p.heartbeat_ms);
    enc.put_string(TAG_MODE, p.mode);
    return enc.take();
}

std::optional<WelcomePayload> decode_welcome(std::span<const uint8_t> data)
{
    WelcomePayload p;
    PayloadDecoder dec(data);
    while (dec.next())
    {
        switch (dec.tag())
        {
            case TAG_SESSION_ID:   p.session_id   = dec.as_u64(); break;
            case TAG_WINDOW_ID:    p.window_id    = dec.as_u64(); break;
            case TAG_PROCESS_ID:   p.process_id   = dec.as_u64(); break;
            case TAG_HEARTBEAT_MS: p.heartbeat_ms = dec.as_u32(); break;
            case TAG_MODE:         p.mode         = dec.as_string(); break;
            default: break;
        }
    }
    return p;
}

std::vector<uint8_t> encode_resp_ok(const RespOkPayload& p)
{
    PayloadEncoder enc;
    enc.put_u64(TAG_REQUEST_ID, p.request_id);
    return enc.take();
}

std::optional<RespOkPayload> decode_resp_ok(std::span<const uint8_t> data)
{
    RespOkPayload p;
    PayloadDecoder dec(data);
    while (dec.next())
    {
        switch (dec.tag())
        {
            case TAG_REQUEST_ID: p.request_id = dec.as_u64(); break;
            default: break;
        }
    }
    return p;
}

std::vector<uint8_t> encode_resp_err(const RespErrPayload& p)
{
    PayloadEncoder enc;
    enc.put_u64(TAG_REQUEST_ID, p.request_id);
    enc.put_u32(TAG_ERROR_CODE, p.code);
    enc.put_string(TAG_ERROR_MESSAGE, p.message);
    return enc.take();
}

std::optional<RespErrPayload> decode_resp_err(std::span<const uint8_t> data)
{
    RespErrPayload p;
    PayloadDecoder dec(data);
    while (dec.next())
    {
        switch (dec.tag())
        {
            case TAG_REQUEST_ID:    p.request_id = dec.as_u64(); break;
            case TAG_ERROR_CODE:    p.code       = dec.as_u32(); break;
            case TAG_ERROR_MESSAGE: p.message    = dec.as_string(); break;
            default: break;
        }
    }
    return p;
}

// ─── Control payload encode/decode ───────────────────────────────────────────

std::vector<uint8_t> encode_cmd_assign_figures(const CmdAssignFiguresPayload& p)
{
    PayloadEncoder enc;
    enc.put_u64(TAG_WINDOW_ID, p.window_id);
    enc.put_u32(TAG_FIGURE_COUNT, static_cast<uint32_t>(p.figure_ids.size()));
    for (auto fid : p.figure_ids)
        enc.put_u64(TAG_FIGURE_IDS, fid);
    enc.put_u64(TAG_ACTIVE_FIGURE, p.active_figure_id);
    return enc.take();
}

std::optional<CmdAssignFiguresPayload> decode_cmd_assign_figures(std::span<const uint8_t> data)
{
    CmdAssignFiguresPayload p;
    PayloadDecoder dec(data);
    while (dec.next())
    {
        switch (dec.tag())
        {
            case TAG_WINDOW_ID:     p.window_id = dec.as_u64(); break;
            case TAG_FIGURE_IDS:    p.figure_ids.push_back(dec.as_u64()); break;
            case TAG_ACTIVE_FIGURE: p.active_figure_id = dec.as_u64(); break;
            case TAG_FIGURE_COUNT:  break;  // informational only
            default: break;
        }
    }
    return p;
}

std::vector<uint8_t> encode_req_create_window(const ReqCreateWindowPayload& p)
{
    PayloadEncoder enc;
    enc.put_u64(TAG_TEMPLATE_WINDOW, p.template_window_id);
    return enc.take();
}

std::optional<ReqCreateWindowPayload> decode_req_create_window(std::span<const uint8_t> data)
{
    ReqCreateWindowPayload p;
    PayloadDecoder dec(data);
    while (dec.next())
    {
        switch (dec.tag())
        {
            case TAG_TEMPLATE_WINDOW: p.template_window_id = dec.as_u64(); break;
            default: break;
        }
    }
    return p;
}

std::vector<uint8_t> encode_req_close_window(const ReqCloseWindowPayload& p)
{
    PayloadEncoder enc;
    enc.put_u64(TAG_WINDOW_ID, p.window_id);
    enc.put_string(TAG_REASON, p.reason);
    return enc.take();
}

std::optional<ReqCloseWindowPayload> decode_req_close_window(std::span<const uint8_t> data)
{
    ReqCloseWindowPayload p;
    PayloadDecoder dec(data);
    while (dec.next())
    {
        switch (dec.tag())
        {
            case TAG_WINDOW_ID: p.window_id = dec.as_u64(); break;
            case TAG_REASON:    p.reason    = dec.as_string(); break;
            default: break;
        }
    }
    return p;
}

std::vector<uint8_t> encode_cmd_remove_figure(const CmdRemoveFigurePayload& p)
{
    PayloadEncoder enc;
    enc.put_u64(TAG_WINDOW_ID, p.window_id);
    enc.put_u64(TAG_FIGURE_ID, p.figure_id);
    return enc.take();
}

std::optional<CmdRemoveFigurePayload> decode_cmd_remove_figure(std::span<const uint8_t> data)
{
    CmdRemoveFigurePayload p;
    PayloadDecoder dec(data);
    while (dec.next())
    {
        switch (dec.tag())
        {
            case TAG_WINDOW_ID: p.window_id = dec.as_u64(); break;
            case TAG_FIGURE_ID: p.figure_id = dec.as_u64(); break;
            default: break;
        }
    }
    return p;
}

std::vector<uint8_t> encode_cmd_set_active(const CmdSetActivePayload& p)
{
    PayloadEncoder enc;
    enc.put_u64(TAG_WINDOW_ID, p.window_id);
    enc.put_u64(TAG_ACTIVE_FIGURE, p.figure_id);
    return enc.take();
}

std::optional<CmdSetActivePayload> decode_cmd_set_active(std::span<const uint8_t> data)
{
    CmdSetActivePayload p;
    PayloadDecoder dec(data);
    while (dec.next())
    {
        switch (dec.tag())
        {
            case TAG_WINDOW_ID:     p.window_id = dec.as_u64(); break;
            case TAG_ACTIVE_FIGURE: p.figure_id = dec.as_u64(); break;
            default: break;
        }
    }
    return p;
}

std::vector<uint8_t> encode_cmd_close_window(const CmdCloseWindowPayload& p)
{
    PayloadEncoder enc;
    enc.put_u64(TAG_WINDOW_ID, p.window_id);
    enc.put_string(TAG_REASON, p.reason);
    return enc.take();
}

std::optional<CmdCloseWindowPayload> decode_cmd_close_window(std::span<const uint8_t> data)
{
    CmdCloseWindowPayload p;
    PayloadDecoder dec(data);
    while (dec.next())
    {
        switch (dec.tag())
        {
            case TAG_WINDOW_ID: p.window_id = dec.as_u64(); break;
            case TAG_REASON:    p.reason    = dec.as_string(); break;
            default: break;
        }
    }
    return p;
}

// ─── REQ_DETACH_FIGURE ───────────────────────────────────────────────────────

std::vector<uint8_t> encode_req_detach_figure(const ReqDetachFigurePayload& p)
{
    PayloadEncoder enc;
    enc.put_u64(TAG_SOURCE_WINDOW, p.source_window_id);
    enc.put_u64(TAG_FIGURE_ID, p.figure_id);
    enc.put_u32(TAG_WIDTH, p.width);
    enc.put_u32(TAG_HEIGHT, p.height);
    payload_put_float(enc, TAG_SCREEN_X, static_cast<float>(p.screen_x));
    payload_put_float(enc, TAG_SCREEN_Y, static_cast<float>(p.screen_y));
    return enc.take();
}

std::optional<ReqDetachFigurePayload> decode_req_detach_figure(std::span<const uint8_t> data)
{
    ReqDetachFigurePayload p;
    PayloadDecoder dec(data);
    while (dec.next())
    {
        switch (dec.tag())
        {
            case TAG_SOURCE_WINDOW: p.source_window_id = dec.as_u64(); break;
            case TAG_FIGURE_ID:     p.figure_id = dec.as_u64(); break;
            case TAG_WIDTH:         p.width = dec.as_u32(); break;
            case TAG_HEIGHT:        p.height = dec.as_u32(); break;
            case TAG_SCREEN_X:      p.screen_x = static_cast<int32_t>(payload_as_float(dec)); break;
            case TAG_SCREEN_Y:      p.screen_y = static_cast<int32_t>(payload_as_float(dec)); break;
            default: break;
        }
    }
    return p;
}

// ─── Payload extension helpers ───────────────────────────────────────────────

void payload_put_float(PayloadEncoder& enc, uint8_t tag, float val)
{
    uint32_t bits;
    std::memcpy(&bits, &val, 4);
    enc.put_u32(tag, bits);
}

void payload_put_double(PayloadEncoder& enc, uint8_t tag, double val)
{
    uint64_t bits;
    std::memcpy(&bits, &val, 8);
    enc.put_u64(tag, bits);
}

void payload_put_bool(PayloadEncoder& enc, uint8_t tag, bool val)
{
    enc.put_u16(tag, val ? 1 : 0);
}

void payload_put_blob(PayloadEncoder& enc, uint8_t tag, const std::vector<uint8_t>& blob)
{
    enc.put_string(tag, std::string(reinterpret_cast<const char*>(blob.data()), blob.size()));
}

void payload_put_float_array(PayloadEncoder& enc, uint8_t tag, const std::vector<float>& arr)
{
    // Encode as raw bytes: [count_u32] [float0] [float1] ...
    std::vector<uint8_t> raw;
    uint32_t count = static_cast<uint32_t>(arr.size());
    raw.resize(4 + count * 4);
    std::memcpy(raw.data(), &count, 4);
    if (!arr.empty())
        std::memcpy(raw.data() + 4, arr.data(), count * 4);
    enc.put_string(tag, std::string(reinterpret_cast<const char*>(raw.data()), raw.size()));
}

float payload_as_float(const PayloadDecoder& dec)
{
    uint32_t bits = dec.as_u32();
    float val;
    std::memcpy(&val, &bits, 4);
    return val;
}

double payload_as_double(const PayloadDecoder& dec)
{
    uint64_t bits = dec.as_u64();
    double val;
    std::memcpy(&val, &bits, 8);
    return val;
}

bool payload_as_bool(const PayloadDecoder& dec)
{
    return dec.as_u16() != 0;
}

std::vector<float> payload_as_float_array(const PayloadDecoder& dec)
{
    std::string raw = dec.as_string();
    if (raw.size() < 4)
        return {};
    uint32_t count;
    std::memcpy(&count, raw.data(), 4);
    if (raw.size() < 4 + count * 4)
        return {};
    std::vector<float> arr(count);
    if (count > 0)
        std::memcpy(arr.data(), raw.data() + 4, count * 4);
    return arr;
}

std::vector<uint8_t> payload_as_blob(const PayloadDecoder& dec)
{
    std::string s = dec.as_string();
    return std::vector<uint8_t>(s.begin(), s.end());
}

// ─── Axis blob encode/decode ─────────────────────────────────────────────────

static std::vector<uint8_t> encode_axis_blob(const SnapshotAxisState& ax)
{
    PayloadEncoder enc;
    payload_put_float(enc, TAG_X_MIN, ax.x_min);
    payload_put_float(enc, TAG_X_MAX, ax.x_max);
    payload_put_float(enc, TAG_Y_MIN, ax.y_min);
    payload_put_float(enc, TAG_Y_MAX, ax.y_max);
    payload_put_bool(enc, TAG_GRID_VISIBLE, ax.grid_visible);
    enc.put_string(TAG_X_LABEL, ax.x_label);
    enc.put_string(TAG_Y_LABEL, ax.y_label);
    enc.put_string(TAG_TITLE, ax.title);
    return enc.take();
}

static SnapshotAxisState decode_axis_blob(std::span<const uint8_t> data)
{
    SnapshotAxisState ax;
    PayloadDecoder dec(data);
    while (dec.next())
    {
        switch (dec.tag())
        {
            case TAG_X_MIN:        ax.x_min = payload_as_float(dec); break;
            case TAG_X_MAX:        ax.x_max = payload_as_float(dec); break;
            case TAG_Y_MIN:        ax.y_min = payload_as_float(dec); break;
            case TAG_Y_MAX:        ax.y_max = payload_as_float(dec); break;
            case TAG_GRID_VISIBLE: ax.grid_visible = payload_as_bool(dec); break;
            case TAG_X_LABEL:      ax.x_label = dec.as_string(); break;
            case TAG_Y_LABEL:      ax.y_label = dec.as_string(); break;
            case TAG_TITLE:        ax.title = dec.as_string(); break;
            default: break;
        }
    }
    return ax;
}

// ─── Series blob encode/decode ───────────────────────────────────────────────

static std::vector<uint8_t> encode_series_blob(const SnapshotSeriesState& s)
{
    PayloadEncoder enc;
    enc.put_string(TAG_SERIES_NAME, s.name);
    enc.put_string(TAG_SERIES_TYPE, s.type);
    payload_put_float(enc, TAG_COLOR_R, s.color_r);
    payload_put_float(enc, TAG_COLOR_G, s.color_g);
    payload_put_float(enc, TAG_COLOR_B, s.color_b);
    payload_put_float(enc, TAG_COLOR_A, s.color_a);
    payload_put_float(enc, TAG_LINE_WIDTH, s.line_width);
    payload_put_float(enc, TAG_MARKER_SIZE, s.marker_size);
    payload_put_bool(enc, TAG_VISIBLE, s.visible);
    payload_put_float(enc, TAG_OPACITY_VAL, s.opacity);
    enc.put_u32(TAG_POINT_COUNT, s.point_count);
    if (!s.data.empty())
        payload_put_float_array(enc, TAG_SERIES_DATA, s.data);
    return enc.take();
}

static SnapshotSeriesState decode_series_blob(std::span<const uint8_t> data)
{
    SnapshotSeriesState s;
    PayloadDecoder dec(data);
    while (dec.next())
    {
        switch (dec.tag())
        {
            case TAG_SERIES_NAME:  s.name = dec.as_string(); break;
            case TAG_SERIES_TYPE:  s.type = dec.as_string(); break;
            case TAG_COLOR_R:      s.color_r = payload_as_float(dec); break;
            case TAG_COLOR_G:      s.color_g = payload_as_float(dec); break;
            case TAG_COLOR_B:      s.color_b = payload_as_float(dec); break;
            case TAG_COLOR_A:      s.color_a = payload_as_float(dec); break;
            case TAG_LINE_WIDTH:   s.line_width = payload_as_float(dec); break;
            case TAG_MARKER_SIZE:  s.marker_size = payload_as_float(dec); break;
            case TAG_VISIBLE:      s.visible = payload_as_bool(dec); break;
            case TAG_OPACITY_VAL:  s.opacity = payload_as_float(dec); break;
            case TAG_POINT_COUNT:  s.point_count = dec.as_u32(); break;
            case TAG_SERIES_DATA:  s.data = payload_as_float_array(dec); break;
            default: break;
        }
    }
    return s;
}

// ─── Figure blob encode/decode ───────────────────────────────────────────────

static std::vector<uint8_t> encode_figure_blob(const SnapshotFigureState& fig)
{
    PayloadEncoder enc;
    enc.put_u64(TAG_FIGURE_ID, fig.figure_id);
    enc.put_string(TAG_TITLE, fig.title);
    enc.put_u32(TAG_WIDTH, fig.width);
    enc.put_u32(TAG_HEIGHT, fig.height);
    enc.put_u32(TAG_GRID_ROWS, static_cast<uint32_t>(fig.grid_rows));
    enc.put_u32(TAG_GRID_COLS, static_cast<uint32_t>(fig.grid_cols));
    if (fig.window_group != 0)
        enc.put_u32(TAG_WINDOW_GROUP, fig.window_group);
    for (const auto& ax : fig.axes)
    {
        auto blob = encode_axis_blob(ax);
        payload_put_blob(enc, TAG_AXIS_BLOB, blob);
    }
    for (const auto& s : fig.series)
    {
        auto blob = encode_series_blob(s);
        payload_put_blob(enc, TAG_SERIES_BLOB, blob);
    }
    return enc.take();
}

static SnapshotFigureState decode_figure_blob(std::span<const uint8_t> data)
{
    SnapshotFigureState fig;
    PayloadDecoder dec(data);
    while (dec.next())
    {
        switch (dec.tag())
        {
            case TAG_FIGURE_ID:   fig.figure_id = dec.as_u64(); break;
            case TAG_TITLE:       fig.title = dec.as_string(); break;
            case TAG_WIDTH:       fig.width = dec.as_u32(); break;
            case TAG_HEIGHT:      fig.height = dec.as_u32(); break;
            case TAG_GRID_ROWS:   fig.grid_rows = static_cast<int32_t>(dec.as_u32()); break;
            case TAG_GRID_COLS:   fig.grid_cols = static_cast<int32_t>(dec.as_u32()); break;
            case TAG_WINDOW_GROUP: fig.window_group = dec.as_u32(); break;
            case TAG_AXIS_BLOB:
            {
                auto blob = payload_as_blob(dec);
                fig.axes.push_back(decode_axis_blob(blob));
                break;
            }
            case TAG_SERIES_BLOB:
            {
                auto blob = payload_as_blob(dec);
                fig.series.push_back(decode_series_blob(blob));
                break;
            }
            default: break;
        }
    }
    return fig;
}

// ─── STATE_SNAPSHOT encode/decode ────────────────────────────────────────────

std::vector<uint8_t> encode_state_snapshot(const StateSnapshotPayload& p)
{
    PayloadEncoder enc;
    enc.put_u64(TAG_REVISION, p.revision);
    enc.put_u64(TAG_SESSION_ID, p.session_id);
    for (const auto& fig : p.figures)
    {
        auto blob = encode_figure_blob(fig);
        payload_put_blob(enc, TAG_FIGURE_BLOB, blob);
    }
    return enc.take();
}

std::optional<StateSnapshotPayload> decode_state_snapshot(std::span<const uint8_t> data)
{
    StateSnapshotPayload p;
    PayloadDecoder dec(data);
    while (dec.next())
    {
        switch (dec.tag())
        {
            case TAG_REVISION:    p.revision = dec.as_u64(); break;
            case TAG_SESSION_ID:  p.session_id = dec.as_u64(); break;
            case TAG_FIGURE_BLOB:
            {
                auto blob = payload_as_blob(dec);
                p.figures.push_back(decode_figure_blob(blob));
                break;
            }
            default: break;
        }
    }
    return p;
}

// ─── DiffOp blob encode/decode ───────────────────────────────────────────────

static std::vector<uint8_t> encode_diff_op_blob(const DiffOp& op)
{
    PayloadEncoder enc;
    enc.put_u16(TAG_OP_TYPE, static_cast<uint16_t>(op.type));
    enc.put_u64(TAG_FIGURE_ID, op.figure_id);
    enc.put_u32(TAG_AXES_INDEX, op.axes_index);
    enc.put_u32(TAG_SERIES_INDEX, op.series_index);
    payload_put_float(enc, TAG_F1, op.f1);
    payload_put_float(enc, TAG_F2, op.f2);
    payload_put_float(enc, TAG_F3, op.f3);
    payload_put_float(enc, TAG_F4, op.f4);
    payload_put_bool(enc, TAG_BOOL_VAL, op.bool_val);
    if (!op.str_val.empty())
        enc.put_string(TAG_STR_VAL, op.str_val);
    if (!op.data.empty())
        payload_put_float_array(enc, TAG_OP_DATA, op.data);
    return enc.take();
}

static DiffOp decode_diff_op_blob(std::span<const uint8_t> data)
{
    DiffOp op;
    PayloadDecoder dec(data);
    while (dec.next())
    {
        switch (dec.tag())
        {
            case TAG_OP_TYPE:      op.type = static_cast<DiffOp::Type>(dec.as_u16()); break;
            case TAG_FIGURE_ID:    op.figure_id = dec.as_u64(); break;
            case TAG_AXES_INDEX:   op.axes_index = dec.as_u32(); break;
            case TAG_SERIES_INDEX: op.series_index = dec.as_u32(); break;
            case TAG_F1:           op.f1 = payload_as_float(dec); break;
            case TAG_F2:           op.f2 = payload_as_float(dec); break;
            case TAG_F3:           op.f3 = payload_as_float(dec); break;
            case TAG_F4:           op.f4 = payload_as_float(dec); break;
            case TAG_BOOL_VAL:     op.bool_val = payload_as_bool(dec); break;
            case TAG_STR_VAL:      op.str_val = dec.as_string(); break;
            case TAG_OP_DATA:      op.data = payload_as_float_array(dec); break;
            default: break;
        }
    }
    return op;
}

// ─── STATE_DIFF encode/decode ────────────────────────────────────────────────

std::vector<uint8_t> encode_state_diff(const StateDiffPayload& p)
{
    PayloadEncoder enc;
    enc.put_u64(TAG_BASE_REVISION, p.base_revision);
    enc.put_u64(TAG_NEW_REVISION, p.new_revision);
    for (const auto& op : p.ops)
    {
        auto blob = encode_diff_op_blob(op);
        payload_put_blob(enc, TAG_DIFF_OP_BLOB, blob);
    }
    return enc.take();
}

std::optional<StateDiffPayload> decode_state_diff(std::span<const uint8_t> data)
{
    StateDiffPayload p;
    PayloadDecoder dec(data);
    while (dec.next())
    {
        switch (dec.tag())
        {
            case TAG_BASE_REVISION: p.base_revision = dec.as_u64(); break;
            case TAG_NEW_REVISION:  p.new_revision = dec.as_u64(); break;
            case TAG_DIFF_OP_BLOB:
            {
                auto blob = payload_as_blob(dec);
                p.ops.push_back(decode_diff_op_blob(blob));
                break;
            }
            default: break;
        }
    }
    return p;
}

// ─── ACK_STATE encode/decode ─────────────────────────────────────────────────

std::vector<uint8_t> encode_ack_state(const AckStatePayload& p)
{
    PayloadEncoder enc;
    enc.put_u64(TAG_REVISION, p.revision);
    return enc.take();
}

std::optional<AckStatePayload> decode_ack_state(std::span<const uint8_t> data)
{
    AckStatePayload p;
    PayloadDecoder dec(data);
    while (dec.next())
    {
        switch (dec.tag())
        {
            case TAG_REVISION: p.revision = dec.as_u64(); break;
            default: break;
        }
    }
    return p;
}

// ─── EVT_INPUT encode/decode ─────────────────────────────────────────────────

std::vector<uint8_t> encode_evt_input(const EvtInputPayload& p)
{
    PayloadEncoder enc;
    enc.put_u64(TAG_WINDOW_ID, p.window_id);
    enc.put_u16(TAG_INPUT_TYPE, static_cast<uint16_t>(p.input_type));
    enc.put_u32(TAG_KEY_CODE, static_cast<uint32_t>(p.key));
    enc.put_u32(TAG_MODS, static_cast<uint32_t>(p.mods));
    payload_put_double(enc, TAG_CURSOR_X, p.x);
    payload_put_double(enc, TAG_CURSOR_Y, p.y);
    enc.put_u64(TAG_FIGURE_ID, p.figure_id);
    enc.put_u32(TAG_AXES_INDEX, p.axes_index);
    return enc.take();
}

std::optional<EvtInputPayload> decode_evt_input(std::span<const uint8_t> data)
{
    EvtInputPayload p;
    PayloadDecoder dec(data);
    while (dec.next())
    {
        switch (dec.tag())
        {
            case TAG_WINDOW_ID:  p.window_id = dec.as_u64(); break;
            case TAG_INPUT_TYPE: p.input_type = static_cast<EvtInputPayload::InputType>(dec.as_u16()); break;
            case TAG_KEY_CODE:   p.key = static_cast<int32_t>(dec.as_u32()); break;
            case TAG_MODS:       p.mods = static_cast<int32_t>(dec.as_u32()); break;
            case TAG_CURSOR_X:   p.x = payload_as_double(dec); break;
            case TAG_CURSOR_Y:   p.y = payload_as_double(dec); break;
            case TAG_FIGURE_ID:  p.figure_id = dec.as_u64(); break;
            case TAG_AXES_INDEX: p.axes_index = dec.as_u32(); break;
            default: break;
        }
    }
    return p;
}

}  // namespace spectra::ipc
