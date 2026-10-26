#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "ipc/codec_tlv.hpp"

using namespace spectra::ipc;

namespace
{

void put_field(std::vector<uint8_t>& buf, uint8_t tag, const std::vector<uint8_t>& val)
{
    buf.push_back(tag);
    uint32_t len = static_cast<uint32_t>(val.size());
    for (int i = 0; i < 4; ++i)
        buf.push_back(static_cast<uint8_t>(len >> (i * 8)));
    buf.insert(buf.end(), val.begin(), val.end());
}

std::vector<uint8_t> float_blob(uint32_t count, const std::vector<float>& values)
{
    std::vector<uint8_t> blob(4 + values.size() * sizeof(float));
    std::memcpy(blob.data(), &count, 4);
    if (!values.empty())
        std::memcpy(blob.data() + 4, values.data(), values.size() * sizeof(float));
    return blob;
}

std::vector<float> decode_blob(const std::vector<uint8_t>& blob)
{
    std::vector<uint8_t> buf;
    put_field(buf, TAG_BLOB_INLINE, blob);
    PayloadDecoder dec(buf);
    EXPECT_TRUE(dec.next());
    return tlv_payload_as_float_array(dec);
}

}   // namespace

TEST(BugfixIpcTlv, FloatArrayValidCountDecodes)
{
    auto arr = decode_blob(float_blob(3, {1.0f, 2.0f, 3.0f}));
    ASSERT_EQ(arr.size(), 3u);
    EXPECT_FLOAT_EQ(arr[0], 1.0f);
    EXPECT_FLOAT_EQ(arr[2], 3.0f);
}

TEST(BugfixIpcTlv, FloatArrayWrappingCountMaxIsRejected)
{
    // 4 + 0xFFFFFFFF*4 wraps to 0 in 32-bit math.
    auto arr = decode_blob(float_blob(0xFFFFFFFFu, {}));
    EXPECT_TRUE(arr.empty());
}

TEST(BugfixIpcTlv, FloatArrayWrappingCountSmallBlobIsRejected)
{
    // 4 + 0x40000001*4 wraps to 8, matching an 8-byte blob.
    auto arr = decode_blob(float_blob(0x40000001u, {42.0f}));
    EXPECT_TRUE(arr.empty());
}

TEST(BugfixIpcTlv, FloatArrayCountExceedingBlobIsRejected)
{
    auto arr = decode_blob(float_blob(5, {1.0f, 2.0f}));
    EXPECT_TRUE(arr.empty());
}

TEST(BugfixIpcTlv, SetDataWithWrappingCountYieldsEmptyData)
{
    std::vector<uint8_t> buf;
    put_field(buf, TAG_BLOB_INLINE, float_blob(0xFFFFFFFFu, {}));
    auto p = tlv_decode_req_set_data(buf);
    ASSERT_TRUE(p.has_value());
    EXPECT_TRUE(p->data.empty());
}

TEST(BugfixIpcTlv, EmptyTrailingStringFieldDecodesEmpty)
{
    std::vector<uint8_t> buf;
    put_field(buf, TAG_PROPERTY_NAME, {'c', 'o', 'l', 'o', 'r'});
    put_field(buf, TAG_BLOB_INLINE, {});   // zero-length field at the very end
    PayloadDecoder dec(buf);
    ASSERT_TRUE(dec.next());
    EXPECT_EQ(dec.as_string(), "color");
    ASSERT_TRUE(dec.next());
    EXPECT_EQ(dec.field_len(), 0u);
    EXPECT_EQ(dec.as_string(), "");
    EXPECT_TRUE(tlv_payload_as_float_array(dec).empty());
    EXPECT_TRUE(tlv_payload_as_blob(dec).empty());
    EXPECT_FALSE(dec.next());
}

TEST(BugfixIpcTlv, EmptyPayloadDecoderHasNoFields)
{
    std::vector<uint8_t> buf;
    PayloadDecoder       dec(buf);
    EXPECT_FALSE(dec.next());
}
