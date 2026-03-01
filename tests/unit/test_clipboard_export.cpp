#include <gtest/gtest.h>
#include <spectra/series.hpp>
#include <string>
#include <vector>

#include "ui/data/clipboard_export.hpp"

using namespace spectra;

TEST(ClipboardExport, EmptyInput)
{
    std::vector<const Series*> empty;
    EXPECT_TRUE(series_to_tsv(empty).empty());
}

TEST(ClipboardExport, SingleLineSeries)
{
    float x[] = {1.0f, 2.0f, 3.0f};
    float y[] = {4.0f, 5.0f, 6.0f};
    LineSeries ls(x, y);
    ls.label("temperature");

    std::vector<const Series*> series = {&ls};
    std::string tsv = series_to_tsv(series);

    EXPECT_NE(tsv.find("temperature_x\ttemperature_y"), std::string::npos);
    EXPECT_NE(tsv.find("1\t4"), std::string::npos);
    EXPECT_NE(tsv.find("3\t6"), std::string::npos);
    EXPECT_EQ(tsv.back(), '\n');
}

TEST(ClipboardExport, SingleScatterSeries)
{
    float x[] = {10.0f, 20.0f};
    float y[] = {30.0f, 40.0f};
    ScatterSeries ss(x, y);
    ss.label("pressure");

    std::vector<const Series*> series = {&ss};
    std::string tsv = series_to_tsv(series);

    EXPECT_NE(tsv.find("pressure_x\tpressure_y"), std::string::npos);
    EXPECT_NE(tsv.find("10\t30"), std::string::npos);
    EXPECT_NE(tsv.find("20\t40"), std::string::npos);
}

TEST(ClipboardExport, MultipleSeriesDifferentLengths)
{
    float x1[] = {1.0f, 2.0f, 3.0f};
    float y1[] = {4.0f, 5.0f, 6.0f};
    LineSeries ls1(x1, y1);
    ls1.label("A");

    float x2[] = {10.0f, 20.0f};
    float y2[] = {30.0f, 40.0f};
    LineSeries ls2(x2, y2);
    ls2.label("B");

    std::vector<const Series*> series = {&ls1, &ls2};
    std::string tsv = series_to_tsv(series);

    EXPECT_NE(tsv.find("A_x\tA_y\tB_x\tB_y"), std::string::npos);

    size_t line_count = 0;
    for (char c : tsv)
        if (c == '\n')
            ++line_count;
    EXPECT_EQ(line_count, 4u);   // header + 3 data rows
}

TEST(ClipboardExport, NullPointersSkipped)
{
    float x[] = {1.0f};
    float y[] = {2.0f};
    LineSeries ls(x, y);
    ls.label("valid");

    std::vector<const Series*> series = {nullptr, &ls, nullptr};
    std::string tsv = series_to_tsv(series);

    EXPECT_NE(tsv.find("valid_x\tvalid_y"), std::string::npos);
    size_t tab_count = 0;
    std::string first_line = tsv.substr(0, tsv.find('\n'));
    for (char c : first_line)
        if (c == '\t')
            ++tab_count;
    EXPECT_EQ(tab_count, 1u);
}

TEST(ClipboardExport, UnlabeledSeriesDefaultName)
{
    float x[] = {1.0f};
    float y[] = {2.0f};
    LineSeries ls(x, y);

    std::vector<const Series*> series = {&ls};
    std::string tsv = series_to_tsv(series);
    EXPECT_NE(tsv.find("series_x\tseries_y"), std::string::npos);
}
