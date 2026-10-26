// Regression tests for chunked REQ_SET_DATA (python/spectra/_series.py
// _send_chunked): TLV decode of the chunk tags and daemon-side reassembly.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "daemon/python_message_handler.hpp"
#include "ipc/codec.hpp"
#include "ipc/codec_tlv.hpp"

namespace ipc = spectra::ipc;
using spectra::daemon::SetDataChunkAssembler;
using Status = SetDataChunkAssembler::Status;

namespace
{

void put_field(std::vector<uint8_t>& buf, uint8_t tag, const void* data, uint32_t len)
{
    buf.push_back(tag);
    for (int i = 0; i < 4; ++i)
        buf.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
    const auto* p = static_cast<const uint8_t*>(data);
    buf.insert(buf.end(), p, p + len);
}

void put_u16(std::vector<uint8_t>& buf, uint8_t tag, uint16_t v)
{
    uint8_t b[2] = {static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>(v >> 8)};
    put_field(buf, tag, b, 2);
}

void put_u32(std::vector<uint8_t>& buf, uint8_t tag, uint32_t v)
{
    uint8_t b[4];
    for (int i = 0; i < 4; ++i)
        b[i] = static_cast<uint8_t>((v >> (i * 8)) & 0xFF);
    put_field(buf, tag, b, 4);
}

void put_u64(std::vector<uint8_t>& buf, uint8_t tag, uint64_t v)
{
    uint8_t b[8];
    for (int i = 0; i < 8; ++i)
        b[i] = static_cast<uint8_t>((v >> (i * 8)) & 0xFF);
    put_field(buf, tag, b, 8);
}

// Mirrors python/spectra/_codec.py encode_req_set_data_chunked.
std::vector<uint8_t> encode_chunk(uint64_t                  figure_id,
                                  uint32_t                  series_index,
                                  const std::vector<float>& floats,
                                  uint32_t                  chunk_index,
                                  uint32_t                  chunk_count,
                                  uint32_t                  total_count)
{
    std::vector<uint8_t> buf;
    put_u64(buf, ipc::TAG_FIGURE_ID, figure_id);
    put_u32(buf, ipc::TAG_SERIES_INDEX, series_index);
    put_u16(buf, ipc::TAG_DTYPE, 0);
    put_u32(buf, ipc::TAG_CHUNK_INDEX, chunk_index);
    put_u32(buf, ipc::TAG_CHUNK_COUNT, chunk_count);
    put_u32(buf, ipc::TAG_TOTAL_COUNT, total_count);
    std::vector<uint8_t> blob(4 + floats.size() * sizeof(float));
    const auto           count = static_cast<uint32_t>(floats.size());
    for (int i = 0; i < 4; ++i)
        blob[i] = static_cast<uint8_t>((count >> (i * 8)) & 0xFF);
    if (!floats.empty())
        std::memcpy(blob.data() + 4, floats.data(), floats.size() * sizeof(float));
    put_field(buf, ipc::TAG_BLOB_INLINE, blob.data(), static_cast<uint32_t>(blob.size()));
    return buf;
}

ipc::ReqSetDataPayload chunk(uint64_t           figure_id,
                             uint32_t           series_index,
                             std::vector<float> data,
                             uint32_t           chunk_index,
                             uint32_t           chunk_count,
                             uint32_t           total_count)
{
    ipc::ReqSetDataPayload p;
    p.figure_id    = figure_id;
    p.series_index = series_index;
    p.data         = std::move(data);
    p.chunk_index  = chunk_index;
    p.chunk_count  = chunk_count;
    p.total_count  = total_count;
    return p;
}

}   // namespace

// ─── Decode ─────────────────────────────────────────────────────────────────

TEST(BugfixR2IpcChunk, TlvDecodeReadsChunkFields)
{
    auto bytes = encode_chunk(7, 3, {1.f, 2.f, 3.f, 4.f}, 1, 3, 10);
    auto p     = ipc::decode_req_set_data(bytes);
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->figure_id, 7u);
    EXPECT_EQ(p->series_index, 3u);
    EXPECT_EQ(p->chunk_index, 1u);
    EXPECT_EQ(p->chunk_count, 3u);
    EXPECT_EQ(p->total_count, 10u);
    EXPECT_TRUE(p->is_chunked());
    EXPECT_EQ(p->data, (std::vector<float>{1.f, 2.f, 3.f, 4.f}));
}

TEST(BugfixR2IpcChunk, UnchunkedDecodeDefaults)
{
    ipc::ReqSetDataPayload in;
    in.figure_id    = 2;
    in.series_index = 1;
    in.data         = {5.f, 6.f};
    auto p          = ipc::decode_req_set_data(ipc::encode_req_set_data(in));
    ASSERT_TRUE(p.has_value());
    EXPECT_FALSE(p->is_chunked());
    EXPECT_EQ(p->chunk_index, 0u);
    EXPECT_EQ(p->data, in.data);
}

TEST(BugfixR2IpcChunk, TlvZeroChunkCountTreatedAsUnchunked)
{
    auto bytes = encode_chunk(1, 0, {1.f, 2.f}, 0, 0, 0);
    auto p     = ipc::tlv_decode_req_set_data(bytes);
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->chunk_count, 1u);
    EXPECT_FALSE(p->is_chunked());
}

// ─── Reassembly ─────────────────────────────────────────────────────────────

TEST(BugfixR2IpcChunk, ReassemblesInOrderChunks)
{
    SetDataChunkAssembler a;
    auto                  r0 = a.add(1, chunk(9, 0, {1.f, 2.f}, 0, 3, 6));
    EXPECT_EQ(r0.status, Status::Pending);
    auto r1 = a.add(1, chunk(9, 0, {3.f, 4.f}, 1, 3, 6));
    EXPECT_EQ(r1.status, Status::Pending);
    EXPECT_EQ(a.pending_count(), 1u);
    auto r2 = a.add(1, chunk(9, 0, {5.f, 6.f}, 2, 3, 6));
    ASSERT_EQ(r2.status, Status::Complete);
    EXPECT_EQ(r2.data, (std::vector<float>{1.f, 2.f, 3.f, 4.f, 5.f, 6.f}));
    EXPECT_EQ(a.pending_count(), 0u);
    EXPECT_EQ(a.pending_floats(), 0u);
}

TEST(BugfixR2IpcChunk, DecodedPythonChunksReassemble)
{
    SetDataChunkAssembler a;
    auto c0 = ipc::decode_req_set_data(encode_chunk(4, 2, {1.f, 2.f, 3.f, 4.f}, 0, 2, 6));
    auto c1 = ipc::decode_req_set_data(encode_chunk(4, 2, {5.f, 6.f}, 1, 2, 6));
    ASSERT_TRUE(c0 && c1);
    EXPECT_EQ(a.add(1, std::move(*c0)).status, Status::Pending);
    auto r = a.add(1, std::move(*c1));
    ASSERT_EQ(r.status, Status::Complete);
    EXPECT_EQ(r.data.size(), 6u);
    EXPECT_FLOAT_EQ(r.data.back(), 6.f);
}

TEST(BugfixR2IpcChunk, UnchunkedCompletesImmediately)
{
    SetDataChunkAssembler a;
    auto                  r = a.add(1, chunk(1, 0, {1.f, 2.f}, 0, 1, 0));
    ASSERT_EQ(r.status, Status::Complete);
    EXPECT_EQ(r.data, (std::vector<float>{1.f, 2.f}));
}

TEST(BugfixR2IpcChunk, OutOfOrderChunkRejectedAndDropped)
{
    SetDataChunkAssembler a;
    EXPECT_EQ(a.add(1, chunk(1, 0, {1.f, 2.f}, 0, 3, 6)).status, Status::Pending);
    auto r = a.add(1, chunk(1, 0, {5.f, 6.f}, 2, 3, 6));
    EXPECT_EQ(r.status, Status::Error);
    EXPECT_FALSE(r.error.empty());
    EXPECT_EQ(a.pending_count(), 0u);
    // A follow-up chunk without a new chunk 0 is rejected too.
    EXPECT_EQ(a.add(1, chunk(1, 0, {3.f, 4.f}, 1, 3, 6)).status, Status::Error);
}

TEST(BugfixR2IpcChunk, ChunkWithoutStartRejected)
{
    SetDataChunkAssembler a;
    EXPECT_EQ(a.add(1, chunk(1, 0, {1.f}, 1, 2, 2)).status, Status::Error);
}

TEST(BugfixR2IpcChunk, ChunkZeroRestartsTransfer)
{
    SetDataChunkAssembler a;
    EXPECT_EQ(a.add(1, chunk(1, 0, {9.f, 9.f}, 0, 2, 4)).status, Status::Pending);
    EXPECT_EQ(a.add(1, chunk(1, 0, {1.f, 2.f}, 0, 2, 4)).status, Status::Pending);
    auto r = a.add(1, chunk(1, 0, {3.f, 4.f}, 1, 2, 4));
    ASSERT_EQ(r.status, Status::Complete);
    EXPECT_EQ(r.data, (std::vector<float>{1.f, 2.f, 3.f, 4.f}));
}

TEST(BugfixR2IpcChunk, OversizeAndMismatchRejected)
{
    SetDataChunkAssembler a(8, 16);
    // total_count over the per-assembly limit.
    EXPECT_EQ(a.add(1, chunk(1, 0, {1.f}, 0, 2, 9)).status, Status::Error);
    // Chunk data overruns the declared total.
    EXPECT_EQ(a.add(1, chunk(1, 0, {1.f, 2.f, 3.f}, 0, 2, 4)).status, Status::Pending);
    EXPECT_EQ(a.add(1, chunk(1, 0, {4.f, 5.f}, 1, 2, 4)).status, Status::Error);
    EXPECT_EQ(a.pending_count(), 0u);
    // Last chunk leaves the array short of total_count.
    EXPECT_EQ(a.add(1, chunk(1, 0, {1.f}, 0, 2, 4)).status, Status::Pending);
    EXPECT_EQ(a.add(1, chunk(1, 0, {2.f}, 1, 2, 4)).status, Status::Error);
    // Metadata changing mid-transfer.
    EXPECT_EQ(a.add(1, chunk(1, 0, {1.f}, 0, 3, 3)).status, Status::Pending);
    EXPECT_EQ(a.add(1, chunk(1, 0, {2.f}, 1, 3, 4)).status, Status::Error);
    EXPECT_EQ(a.pending_floats(), 0u);
}

TEST(BugfixR2IpcChunk, PendingMemoryIsBounded)
{
    SetDataChunkAssembler a(8, 10);
    EXPECT_EQ(a.add(1, chunk(1, 0, {1.f}, 0, 2, 8)).status, Status::Pending);
    EXPECT_EQ(a.pending_floats(), 8u);
    // A second concurrent transfer would exceed the global pending budget.
    EXPECT_EQ(a.add(1, chunk(1, 1, {1.f}, 0, 2, 8)).status, Status::Error);
    EXPECT_EQ(a.pending_count(), 1u);
}

TEST(BugfixR2IpcChunk, KeysAreIndependentAndClientsCanBeDropped)
{
    SetDataChunkAssembler a;
    EXPECT_EQ(a.add(1, chunk(1, 0, {1.f}, 0, 2, 2)).status, Status::Pending);
    EXPECT_EQ(a.add(2, chunk(1, 0, {7.f}, 0, 2, 2)).status, Status::Pending);
    EXPECT_EQ(a.add(1, chunk(1, 1, {3.f}, 0, 2, 2)).status, Status::Pending);
    EXPECT_EQ(a.pending_count(), 3u);

    auto r = a.add(2, chunk(1, 0, {8.f}, 1, 2, 2));
    ASSERT_EQ(r.status, Status::Complete);
    EXPECT_EQ(r.data, (std::vector<float>{7.f, 8.f}));

    a.drop_client(1);
    EXPECT_EQ(a.pending_count(), 0u);
    EXPECT_EQ(a.pending_floats(), 0u);

    EXPECT_EQ(a.add(3, chunk(1, 0, {1.f}, 0, 2, 2)).status, Status::Pending);
    EXPECT_EQ(a.add(4, chunk(1, 0, {1.f}, 0, 2, 2)).status, Status::Pending);
    a.retain_clients({4});
    EXPECT_EQ(a.pending_count(), 1u);
}

TEST(BugfixR2IpcChunk, UnchunkedSupersedesPendingTransfer)
{
    SetDataChunkAssembler a;
    EXPECT_EQ(a.add(1, chunk(1, 0, {1.f}, 0, 2, 2)).status, Status::Pending);
    EXPECT_EQ(a.add(1, chunk(1, 0, {5.f, 6.f}, 0, 1, 0)).status, Status::Complete);
    EXPECT_EQ(a.pending_count(), 0u);
    EXPECT_EQ(a.pending_floats(), 0u);
}
