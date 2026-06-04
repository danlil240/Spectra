#include "codec_tlv.hpp"

#include <cstring>

#include "codec_fb.hpp"

namespace spectra::ipc
{

namespace
{

uint16_t read_u16_le(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t read_u32_le(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
           | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint64_t read_u64_le(const uint8_t* p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= static_cast<uint64_t>(p[i]) << (i * 8);
    return v;
}

}   // namespace

PayloadDecoder::PayloadDecoder(std::span<const uint8_t> data) : data_(data) {}

bool PayloadDecoder::next()
{
    if (pos_ + 5 > data_.size())
        return false;

    tag_        = data_[pos_];
    len_        = read_u32_le(&data_[pos_ + 1]);
    val_offset_ = pos_ + 5;

    if (val_offset_ + len_ > data_.size())
        return false;

    pos_ = val_offset_ + len_;
    return true;
}

uint16_t PayloadDecoder::as_u16() const
{
    if (len_ < 2)
        return 0;
    return read_u16_le(&data_[val_offset_]);
}

uint32_t PayloadDecoder::as_u32() const
{
    if (len_ < 4)
        return 0;
    return read_u32_le(&data_[val_offset_]);
}

uint64_t PayloadDecoder::as_u64() const
{
    if (len_ < 8)
        return 0;
    return read_u64_le(&data_[val_offset_]);
}

std::string PayloadDecoder::as_string() const
{
    return std::string(reinterpret_cast<const char*>(&data_[val_offset_]), len_);
}

float tlv_payload_as_float(const PayloadDecoder& dec)
{
    uint32_t bits = dec.as_u32();
    float    val;
    std::memcpy(&val, &bits, 4);
    return val;
}

double tlv_payload_as_double(const PayloadDecoder& dec)
{
    uint64_t bits = dec.as_u64();
    double   val;
    std::memcpy(&val, &bits, 8);
    return val;
}

bool tlv_payload_as_bool(const PayloadDecoder& dec)
{
    return dec.as_u16() != 0;
}

std::vector<float> tlv_payload_as_float_array(const PayloadDecoder& dec)
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

std::vector<uint8_t> tlv_payload_as_blob(const PayloadDecoder& dec)
{
    std::string s = dec.as_string();
    return std::vector<uint8_t>(s.begin(), s.end());
}

std::optional<ReqSetDataPayload> tlv_decode_req_set_data(std::span<const uint8_t> data)
{
    ReqSetDataPayload p;
    PayloadDecoder    dec(data);
    while (dec.next())
    {
        switch (dec.tag())
        {
            case TAG_FIGURE_ID:
                p.figure_id = dec.as_u64();
                break;
            case TAG_SERIES_INDEX:
                p.series_index = dec.as_u32();
                break;
            case TAG_DTYPE:
                p.dtype = static_cast<uint8_t>(dec.as_u16());
                break;
            case TAG_BLOB_INLINE:
                p.data = tlv_payload_as_float_array(dec);
                break;
            default:
                break;
        }
    }
    return p;
}

std::optional<ReqAppendDataPayload> tlv_decode_req_append_data(std::span<const uint8_t> data)
{
    ReqAppendDataPayload p;
    PayloadDecoder       dec(data);
    while (dec.next())
    {
        switch (dec.tag())
        {
            case TAG_FIGURE_ID:
                p.figure_id = dec.as_u64();
                break;
            case TAG_SERIES_INDEX:
                p.series_index = dec.as_u32();
                break;
            case TAG_BLOB_INLINE:
                p.data = tlv_payload_as_float_array(dec);
                break;
            default:
                break;
        }
    }
    return p;
}

std::optional<ReqUpdatePropertyPayload> tlv_decode_req_update_property(std::span<const uint8_t> data)
{
    ReqUpdatePropertyPayload p;
    PayloadDecoder           dec(data);
    while (dec.next())
    {
        switch (dec.tag())
        {
            case TAG_FIGURE_ID:
                p.figure_id = dec.as_u64();
                break;
            case TAG_AXES_INDEX:
                p.axes_index = dec.as_u32();
                break;
            case TAG_SERIES_INDEX:
                p.series_index = dec.as_u32();
                break;
            case TAG_PROPERTY_NAME:
                p.property = dec.as_string();
                break;
            case TAG_F1:
                p.f1 = tlv_payload_as_float(dec);
                break;
            case TAG_F2:
                p.f2 = tlv_payload_as_float(dec);
                break;
            case TAG_F3:
                p.f3 = tlv_payload_as_float(dec);
                break;
            case TAG_F4:
                p.f4 = tlv_payload_as_float(dec);
                break;
            case TAG_BOOL_VAL:
                p.bool_val = tlv_payload_as_bool(dec);
                break;
            case TAG_STR_VAL:
                p.str_val = dec.as_string();
                break;
            default:
                break;
        }
    }
    return p;
}

std::optional<ReqUpdateBatchPayload> tlv_decode_req_update_batch(std::span<const uint8_t> data)
{
    ReqUpdateBatchPayload p;
    PayloadDecoder        dec(data);
    while (dec.next())
    {
        if (dec.tag() != TAG_BATCH_ITEM)
            continue;
        auto blob = tlv_payload_as_blob(dec);
        auto span = std::span<const uint8_t>(blob.data(), blob.size());
        std::optional<ReqUpdatePropertyPayload> item;
        if (detect_payload_format(span) == PayloadFormat::FLATBUFFERS)
        {
            auto body = strip_fb_prefix(span);
            item      = decode_fb_req_update_property(
                std::span<const uint8_t>(body.data(), body.size()));
        }
        else
        {
            item = tlv_decode_req_update_property(span);
        }
        if (item)
            p.updates.push_back(*item);
    }
    return p;
}

std::optional<ReqAnimStartPayload> tlv_decode_req_anim_start(std::span<const uint8_t> data)
{
    ReqAnimStartPayload p;
    PayloadDecoder      dec(data);
    while (dec.next())
    {
        switch (dec.tag())
        {
            case TAG_FIGURE_ID:
                p.figure_id = dec.as_u64();
                break;
            case TAG_F1:
                p.fps = tlv_payload_as_float(dec);
                break;
            case TAG_F2:
                p.duration = tlv_payload_as_float(dec);
                break;
            default:
                break;
        }
    }
    return p;
}

}   // namespace spectra::ipc
