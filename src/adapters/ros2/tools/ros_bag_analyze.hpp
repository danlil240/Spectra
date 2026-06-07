#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace spectra::adapters::ros2
{

struct BagAnalyzeConfig
{
    std::string              bag_path;
    std::vector<std::string> fields;
    std::string              csv_path;
    char                     separator{','};
    int                      precision{9};
};

struct BagAnalyzeResult
{
    bool        ok{false};
    std::string error;
    size_t      row_count{0};
    size_t      column_count{0};

    explicit operator bool() const { return ok; }
};

// Headless bag → CSV export (no GUI, no rclcpp executor).
BagAnalyzeResult analyze_bag_to_csv(const BagAnalyzeConfig& config);

}   // namespace spectra::adapters::ros2
