#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <vector>

#include "data/mapped_file.hpp"

using namespace spectra::data;

namespace
{

/// Helper to create a temporary binary file with float data.
std::string write_temp_binary(const std::vector<float>& data, const std::string& name)
{
    auto path =
        (std::filesystem::temp_directory_path() / ("spectra_test_" + name + ".bin")).string();
    std::ofstream ofs(path, std::ios::binary);
    ofs.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size() * sizeof(float)));
    return path;
}

}   // namespace

// ─── Basic mapping ──────────────────────────────────────────────────────────

TEST(MappedFile, DefaultConstruction)
{
    MappedFile mf;
    EXPECT_FALSE(mf.is_open());
    EXPECT_EQ(mf.data(), nullptr);
    EXPECT_EQ(mf.size(), 0u);
}

TEST(MappedFile, MapExistingFile)
{
    std::vector<float> data = {1.0f, 2.0f, 3.0f, 4.0f};
    auto               path = write_temp_binary(data, "map_existing");

    MappedFile mf(path);
    EXPECT_TRUE(mf.is_open());
    EXPECT_EQ(mf.size(), data.size() * sizeof(float));
    EXPECT_NE(mf.data(), nullptr);

    std::remove(path.c_str());
}

TEST(MappedFile, MapNonExistent)
{
    auto path = (std::filesystem::temp_directory_path() / "nonexistent_file_xyz.bin").string();
    EXPECT_THROW(MappedFile mf(path), std::runtime_error);
}

// ─── Subspan access ─────────────────────────────────────────────────────────

TEST(MappedFile, SubspanFloat)
{
    std::vector<float> data = {10.0f, 20.0f, 30.0f, 40.0f, 50.0f};
    auto               path = write_temp_binary(data, "subspan");

    MappedFile mf(path);
    auto       span = mf.subspan_float(0, 5);
    ASSERT_EQ(span.size(), 5u);
    EXPECT_FLOAT_EQ(span[0], 10.0f);
    EXPECT_FLOAT_EQ(span[4], 50.0f);

    // Subspan with offset (skip 2 floats = 8 bytes)
    auto span2 = mf.subspan_float(2 * sizeof(float), 3);
    ASSERT_EQ(span2.size(), 3u);
    EXPECT_FLOAT_EQ(span2[0], 30.0f);
    EXPECT_FLOAT_EQ(span2[2], 50.0f);

    std::remove(path.c_str());
}

TEST(MappedFile, SubspanOutOfBounds)
{
    std::vector<float> data = {1.0f, 2.0f};
    auto               path = write_temp_binary(data, "subspan_oob");

    MappedFile mf(path);

    // Request more than available
    auto span = mf.subspan_float(0, 100);
    EXPECT_EQ(span.size(), 2u);

    // Offset past end
    auto span2 = mf.subspan_float(1000, 1);
    EXPECT_TRUE(span2.empty());

    std::remove(path.c_str());
}

// ─── Move semantics ─────────────────────────────────────────────────────────

TEST(MappedFile, MoveConstruction)
{
    std::vector<float> data = {1.0f, 2.0f, 3.0f};
    auto               path = write_temp_binary(data, "move_ctor");

    MappedFile mf(path);
    EXPECT_TRUE(mf.is_open());

    MappedFile moved(std::move(mf));
    EXPECT_TRUE(moved.is_open());
    EXPECT_FALSE(mf.is_open());   // NOLINT: intentionally testing moved-from state

    auto span = moved.subspan_float(0, 3);
    EXPECT_EQ(span.size(), 3u);
    EXPECT_FLOAT_EQ(span[0], 1.0f);

    std::remove(path.c_str());
}

TEST(MappedFile, MoveAssignment)
{
    std::vector<float> data = {1.0f, 2.0f};
    auto               path = write_temp_binary(data, "move_assign");

    MappedFile mf(path);
    MappedFile target;
    target = std::move(mf);

    EXPECT_TRUE(target.is_open());
    EXPECT_FALSE(mf.is_open());   // NOLINT: intentionally testing moved-from state

    std::remove(path.c_str());
}

// ─── Close ──────────────────────────────────────────────────────────────────

TEST(MappedFile, ExplicitClose)
{
    std::vector<float> data = {1.0f};
    auto               path = write_temp_binary(data, "close");

    MappedFile mf(path);
    EXPECT_TRUE(mf.is_open());

    mf.close();
    EXPECT_FALSE(mf.is_open());
    EXPECT_EQ(mf.data(), nullptr);
    EXPECT_EQ(mf.size(), 0u);

    std::remove(path.c_str());
}

// ─── Empty file ─────────────────────────────────────────────────────────────

TEST(MappedFile, EmptyFile)
{
    auto path = (std::filesystem::temp_directory_path() / "spectra_test_empty.bin").string();
    std::ofstream(path, std::ios::binary);   // Create empty file

    MappedFile mf(path);
    EXPECT_TRUE(mf.is_open());   // Open but no data
    EXPECT_EQ(mf.size(), 0u);

    auto span = mf.subspan_float(0, 1);
    EXPECT_TRUE(span.empty());

    std::remove(path.c_str());
}

// ─── Large file ─────────────────────────────────────────────────────────────

TEST(MappedFile, LargeFile)
{
    const std::size_t  N = 100000;
    std::vector<float> data(N);
    for (std::size_t i = 0; i < N; ++i)
        data[i] = static_cast<float>(i);

    auto path = write_temp_binary(data, "large");

    MappedFile mf(path);
    EXPECT_EQ(mf.size(), N * sizeof(float));

    auto span = mf.subspan_float(0, N);
    ASSERT_EQ(span.size(), N);
    EXPECT_FLOAT_EQ(span[0], 0.0f);
    EXPECT_FLOAT_EQ(span[N - 1], static_cast<float>(N - 1));

    // Read from the middle
    auto mid = mf.subspan_float(50000 * sizeof(float), 10);
    ASSERT_EQ(mid.size(), 10u);
    EXPECT_FLOAT_EQ(mid[0], 50000.0f);

    std::remove(path.c_str());
}
