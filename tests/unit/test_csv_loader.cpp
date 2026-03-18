#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "ui/data/csv_loader.hpp"

namespace
{

class TempCsvFile
{
   public:
    explicit TempCsvFile(const std::string& contents)
        : path_(std::filesystem::temp_directory_path()
                / ("spectra_csv_loader_" + std::to_string(unique_suffix_++) + ".csv"))
    {
        std::ofstream out(path_);
        out << contents;
    }

    ~TempCsvFile()
    {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    TempCsvFile(const TempCsvFile&)            = delete;
    TempCsvFile& operator=(const TempCsvFile&) = delete;
    TempCsvFile(TempCsvFile&&)                 = delete;
    TempCsvFile& operator=(TempCsvFile&&)      = delete;

    std::string path() const { return path_.string(); }

   private:
    std::filesystem::path  path_;
    static inline uint64_t unique_suffix_ =
        static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
};

}   // namespace

TEST(CsvLoader, ParsesDatetimeColumnAsRelativeSeconds)
{
    TempCsvFile file("time,value\n"
                     "2026-03-15T20:47:00Z,1\n"
                     "2026-03-15T20:47:01.500Z,2\n"
                     "2026-03-15T20:47:03Z,3\n");

    const spectra::CsvData data = spectra::parse_csv(file.path());

    ASSERT_TRUE(data.error.empty());
    ASSERT_EQ(data.num_cols, 2u);
    ASSERT_EQ(data.num_rows, 3u);
    ASSERT_EQ(data.headers.size(), 2u);
    EXPECT_EQ(data.headers[0], "time");
    EXPECT_EQ(data.headers[1], "value");
    ASSERT_EQ(data.columns[0].size(), 3u);
    ASSERT_EQ(data.columns[1].size(), 3u);
    EXPECT_NEAR(data.columns[0][0], 0.0f, 1e-5f);
    EXPECT_NEAR(data.columns[0][1], 1.5f, 1e-5f);
    EXPECT_NEAR(data.columns[0][2], 3.0f, 1e-5f);
    EXPECT_FLOAT_EQ(data.columns[1][0], 1.0f);
    EXPECT_FLOAT_EQ(data.columns[1][1], 2.0f);
    EXPECT_FLOAT_EQ(data.columns[1][2], 3.0f);
}

TEST(CsvLoader, DoesNotTreatDatetimeFirstRowAsHeader)
{
    TempCsvFile file("2026-03-15T20:47:00+02:00,1\n"
                     "2026-03-15T18:47:02Z,2\n");

    const spectra::CsvData data = spectra::parse_csv(file.path());

    ASSERT_TRUE(data.error.empty());
    ASSERT_EQ(data.num_cols, 2u);
    ASSERT_EQ(data.num_rows, 2u);
    ASSERT_EQ(data.headers.size(), 2u);
    EXPECT_EQ(data.headers[0], "Column 1");
    EXPECT_EQ(data.headers[1], "Column 2");
    ASSERT_EQ(data.columns[0].size(), 2u);
    ASSERT_EQ(data.columns[1].size(), 2u);
    EXPECT_NEAR(data.columns[0][0], 0.0f, 1e-5f);
    EXPECT_NEAR(data.columns[0][1], 2.0f, 1e-5f);
    EXPECT_FLOAT_EQ(data.columns[1][0], 1.0f);
    EXPECT_FLOAT_EQ(data.columns[1][1], 2.0f);
}

TEST(CsvLoader, SupportsAdditionalCommonDatetimeFormats)
{
    TempCsvFile file("time;value\n"
                     "2026-03-04_15-09-55_718414;1\n"
                     "2026-03-04T15:09:56.718414;2\n"
                     "2026/03/04 15:09:57,718414;3\n"
                     "20260304 150958.718414;4\n"
                     "20260304150959.718414;5\n"
                     "2026.03.04 15:10:00;6\n");

    const spectra::CsvData data = spectra::parse_csv(file.path());

    ASSERT_TRUE(data.error.empty());
    ASSERT_EQ(data.num_rows, 6u);
    ASSERT_EQ(data.columns[0].size(), 6u);
    EXPECT_NEAR(data.columns[0][0], 0.0f, 1e-5f);
    EXPECT_NEAR(data.columns[0][1], 1.0f, 1e-5f);
    EXPECT_NEAR(data.columns[0][2], 2.0f, 1e-5f);
    EXPECT_NEAR(data.columns[0][3], 3.0f, 1e-5f);
    EXPECT_NEAR(data.columns[0][4], 4.0f, 1e-5f);
    EXPECT_NEAR(data.columns[0][5], 4.281586f, 1e-5f);
}

TEST(CsvLoader, SupportsTimezoneVariants)
{
    TempCsvFile file("time,value\n"
                     "2026-03-04_15-09-55_718414+02:00,1\n"
                     "2026-03-04T13:09:56.718414Z,2\n"
                     "2026-03-04 13:09:57.718414-00:00,3\n"
                     "2026-03-04 15:09:58.718414+0200,4\n"
                     "2026-03-04 15:09:59.718414+02_00,5\n");

    const spectra::CsvData data = spectra::parse_csv(file.path());

    ASSERT_TRUE(data.error.empty());
    ASSERT_EQ(data.num_rows, 5u);
    ASSERT_EQ(data.columns[0].size(), 5u);
    EXPECT_NEAR(data.columns[0][0], 0.0f, 1e-5f);
    EXPECT_NEAR(data.columns[0][1], 1.0f, 1e-5f);
    EXPECT_NEAR(data.columns[0][2], 2.0f, 1e-5f);
    EXPECT_NEAR(data.columns[0][3], 3.0f, 1e-5f);
    EXPECT_NEAR(data.columns[0][4], 4.0f, 1e-5f);
}
