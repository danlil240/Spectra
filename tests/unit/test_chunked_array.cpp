#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <numeric>
#include <vector>

#include "data/chunked_array.hpp"

using namespace spectra::data;

// ─── Construction ───────────────────────────────────────────────────────────

TEST(ChunkedArray, DefaultConstruction)
{
    ChunkedArray arr;
    EXPECT_EQ(arr.size(), 0u);
    EXPECT_TRUE(arr.empty());
    EXPECT_EQ(arr.chunk_count(), 0u);
    EXPECT_EQ(arr.chunk_size(), ChunkedArray::DEFAULT_CHUNK_SIZE);
}

TEST(ChunkedArray, CustomChunkSize)
{
    ChunkedArray arr(128);
    EXPECT_EQ(arr.chunk_size(), 128u);
}

TEST(ChunkedArray, ConstructFromSpan)
{
    std::vector<float> data(500);
    std::iota(data.begin(), data.end(), 0.0f);

    ChunkedArray arr(data, 128);
    EXPECT_EQ(arr.size(), 500u);
    EXPECT_EQ(arr.chunk_size(), 128u);
    EXPECT_EQ(arr.chunk_count(), 4u);   // 500/128 = 3.9 → 4 chunks
}

// ─── Push back ──────────────────────────────────────────────────────────────

TEST(ChunkedArray, PushBackSingle)
{
    ChunkedArray arr(4);
    arr.push_back(1.0f);
    arr.push_back(2.0f);
    arr.push_back(3.0f);

    EXPECT_EQ(arr.size(), 3u);
    EXPECT_FLOAT_EQ(arr[0], 1.0f);
    EXPECT_FLOAT_EQ(arr[1], 2.0f);
    EXPECT_FLOAT_EQ(arr[2], 3.0f);
}

TEST(ChunkedArray, PushBackCrossesChunkBoundary)
{
    ChunkedArray arr(4);
    for (int i = 0; i < 10; ++i)
        arr.push_back(static_cast<float>(i));

    EXPECT_EQ(arr.size(), 10u);
    EXPECT_EQ(arr.chunk_count(), 3u);   // 10/4 = 2.5 → 3 chunks

    for (int i = 0; i < 10; ++i)
        EXPECT_FLOAT_EQ(arr[static_cast<std::size_t>(i)], static_cast<float>(i));
}

// ─── Append ─────────────────────────────────────────────────────────────────

TEST(ChunkedArray, AppendSpan)
{
    ChunkedArray       arr(4);
    std::vector<float> data = {10.0f, 20.0f, 30.0f, 40.0f, 50.0f};
    arr.append(data);

    EXPECT_EQ(arr.size(), 5u);
    EXPECT_FLOAT_EQ(arr[0], 10.0f);
    EXPECT_FLOAT_EQ(arr[4], 50.0f);
}

TEST(ChunkedArray, AppendMultiple)
{
    ChunkedArray       arr(4);
    std::vector<float> a = {1.0f, 2.0f, 3.0f};
    std::vector<float> b = {4.0f, 5.0f, 6.0f, 7.0f};
    arr.append(a);
    arr.append(b);

    EXPECT_EQ(arr.size(), 7u);
    for (std::size_t i = 0; i < 7; ++i)
        EXPECT_FLOAT_EQ(arr[i], static_cast<float>(i + 1));
}

// ─── Read ───────────────────────────────────────────────────────────────────

TEST(ChunkedArray, ReadWithinSingleChunk)
{
    ChunkedArray arr(8);
    for (int i = 0; i < 5; ++i)
        arr.push_back(static_cast<float>(i * 10));

    float buf[3];
    auto  n = arr.read(1, 3, buf);
    EXPECT_EQ(n, 3u);
    EXPECT_FLOAT_EQ(buf[0], 10.0f);
    EXPECT_FLOAT_EQ(buf[1], 20.0f);
    EXPECT_FLOAT_EQ(buf[2], 30.0f);
}

TEST(ChunkedArray, ReadAcrossChunkBoundary)
{
    ChunkedArray arr(4);
    for (int i = 0; i < 10; ++i)
        arr.push_back(static_cast<float>(i));

    float buf[6];
    auto  n = arr.read(2, 6, buf);
    EXPECT_EQ(n, 6u);
    for (std::size_t i = 0; i < 6; ++i)
        EXPECT_FLOAT_EQ(buf[i], static_cast<float>(i + 2));
}

TEST(ChunkedArray, ReadPastEnd)
{
    ChunkedArray arr(4);
    for (int i = 0; i < 5; ++i)
        arr.push_back(static_cast<float>(i));

    float buf[10];
    auto  n = arr.read(3, 10, buf);
    EXPECT_EQ(n, 2u);   // Only 2 elements from index 3
    EXPECT_FLOAT_EQ(buf[0], 3.0f);
    EXPECT_FLOAT_EQ(buf[1], 4.0f);
}

TEST(ChunkedArray, ReadOutOfRange)
{
    ChunkedArray arr(4);
    arr.push_back(1.0f);

    float buf[1];
    auto  n = arr.read(5, 1, buf);
    EXPECT_EQ(n, 0u);
}

TEST(ChunkedArray, ReadVec)
{
    ChunkedArray arr(4);
    for (int i = 0; i < 10; ++i)
        arr.push_back(static_cast<float>(i));

    auto vec = arr.read_vec(2, 5);
    ASSERT_EQ(vec.size(), 5u);
    for (std::size_t i = 0; i < 5; ++i)
        EXPECT_FLOAT_EQ(vec[i], static_cast<float>(i + 2));
}

// ─── Erase front ────────────────────────────────────────────────────────────

TEST(ChunkedArray, EraseFrontPartialChunk)
{
    ChunkedArray arr(4);
    for (int i = 0; i < 10; ++i)
        arr.push_back(static_cast<float>(i));

    auto removed = arr.erase_front(2);
    EXPECT_EQ(removed, 2u);
    EXPECT_EQ(arr.size(), 8u);
    EXPECT_FLOAT_EQ(arr[0], 2.0f);
}

TEST(ChunkedArray, EraseFrontFullChunks)
{
    ChunkedArray arr(4);
    for (int i = 0; i < 12; ++i)
        arr.push_back(static_cast<float>(i));

    auto removed = arr.erase_front(8);
    EXPECT_EQ(removed, 8u);
    EXPECT_EQ(arr.size(), 4u);
    EXPECT_FLOAT_EQ(arr[0], 8.0f);
    EXPECT_FLOAT_EQ(arr[3], 11.0f);
}

TEST(ChunkedArray, EraseFrontAll)
{
    ChunkedArray arr(4);
    for (int i = 0; i < 5; ++i)
        arr.push_back(static_cast<float>(i));

    auto removed = arr.erase_front(10);   // More than size
    EXPECT_EQ(removed, 5u);
    EXPECT_EQ(arr.size(), 0u);
    EXPECT_TRUE(arr.empty());
}

TEST(ChunkedArray, EraseFrontZero)
{
    ChunkedArray arr(4);
    arr.push_back(1.0f);

    auto removed = arr.erase_front(0);
    EXPECT_EQ(removed, 0u);
    EXPECT_EQ(arr.size(), 1u);
}

// ─── Clear ──────────────────────────────────────────────────────────────────

TEST(ChunkedArray, Clear)
{
    ChunkedArray arr(4);
    for (int i = 0; i < 10; ++i)
        arr.push_back(static_cast<float>(i));

    arr.clear();
    EXPECT_EQ(arr.size(), 0u);
    EXPECT_TRUE(arr.empty());
    EXPECT_EQ(arr.chunk_count(), 0u);
}

// ─── Chunk data access ──────────────────────────────────────────────────────

TEST(ChunkedArray, ChunkDataAccess)
{
    ChunkedArray arr(4);
    for (int i = 0; i < 6; ++i)
        arr.push_back(static_cast<float>(i));

    // Two chunks: first has 4 elements, second has 2
    EXPECT_EQ(arr.chunk_count(), 2u);

    auto chunk0 = arr.chunk_data(0);
    EXPECT_EQ(chunk0.size(), 4u);
    EXPECT_FLOAT_EQ(chunk0[0], 0.0f);
    EXPECT_FLOAT_EQ(chunk0[3], 3.0f);

    auto chunk1 = arr.chunk_data(1);
    EXPECT_EQ(chunk1.size(), 2u);   // Partial chunk
    EXPECT_FLOAT_EQ(chunk1[0], 4.0f);
    EXPECT_FLOAT_EQ(chunk1[1], 5.0f);
}

// ─── Memory accounting ─────────────────────────────────────────────────────

TEST(ChunkedArray, MemoryBytes)
{
    ChunkedArray arr(1024);
    // Empty → 0 bytes
    EXPECT_EQ(arr.memory_bytes(), 0u);

    arr.push_back(1.0f);
    // One chunk allocated with capacity 1024
    EXPECT_EQ(arr.memory_bytes(), 1024 * sizeof(float));
}

// ─── Move semantics ─────────────────────────────────────────────────────────

TEST(ChunkedArray, MoveConstruction)
{
    ChunkedArray arr(4);
    for (int i = 0; i < 6; ++i)
        arr.push_back(static_cast<float>(i));

    ChunkedArray moved(std::move(arr));
    EXPECT_EQ(moved.size(), 6u);
    EXPECT_FLOAT_EQ(moved[0], 0.0f);
    EXPECT_FLOAT_EQ(moved[5], 5.0f);
}

// ─── Large dataset ──────────────────────────────────────────────────────────

TEST(ChunkedArray, LargeDataset)
{
    const std::size_t N = 100000;
    ChunkedArray      arr(1024);

    std::vector<float> data(N);
    std::iota(data.begin(), data.end(), 0.0f);
    arr.append(data);

    EXPECT_EQ(arr.size(), N);

    // Verify random access across many chunks
    EXPECT_FLOAT_EQ(arr[0], 0.0f);
    EXPECT_FLOAT_EQ(arr[1023], 1023.0f);
    EXPECT_FLOAT_EQ(arr[1024], 1024.0f);   // Second chunk start
    EXPECT_FLOAT_EQ(arr[N - 1], static_cast<float>(N - 1));

    // Verify read across chunk boundaries
    auto vec = arr.read_vec(1020, 10);
    ASSERT_EQ(vec.size(), 10u);
    for (std::size_t i = 0; i < 10; ++i)
        EXPECT_FLOAT_EQ(vec[i], static_cast<float>(1020 + i));
}
