// spectra-ros-analyze — headless rosbag2 field export for CI / reports.
//
// Usage:
//   spectra-ros-analyze --bag recording.db3 \
//       --fields /imu/data.linear_acceleration.z \
//       --csv out.csv

#include "tools/ros_bag_analyze.hpp"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using spectra::adapters::ros2::BagAnalyzeConfig;
using spectra::adapters::ros2::analyze_bag_to_csv;

namespace
{

void print_usage(const char* argv0)
{
    std::fprintf(stderr,
                 "Usage: %s --bag PATH --fields SPEC [--fields SPEC ...] --csv OUT.csv\n"
                 "\n"
                 "Field spec formats:\n"
                 "  /topic:field.path\n"
                 "  /topic.field.path   (topic resolved against bag metadata)\n"
                 "\n"
                 "Options:\n"
                 "  --bag PATH          Input .db3 / .mcap bag (required)\n"
                 "  --fields SPEC       Numeric field to export (repeatable)\n"
                 "  --csv PATH          Output CSV path (required)\n"
                 "  --precision N       Decimal precision (default 9)\n"
                 "  -h, --help          Show this help\n",
                 argv0);
}

}   // namespace

int main(int argc, char** argv)
{
    BagAnalyzeConfig cfg;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg(argv[i]);
        if (arg == "--help" || arg == "-h")
        {
            print_usage(argv[0]);
            return 0;
        }
        if (arg == "--bag" && i + 1 < argc)
        {
            cfg.bag_path = argv[++i];
            continue;
        }
        if (arg == "--fields" && i + 1 < argc)
        {
            cfg.fields.emplace_back(argv[++i]);
            continue;
        }
        if (arg == "--csv" && i + 1 < argc)
        {
            cfg.csv_path = argv[++i];
            continue;
        }
        if (arg == "--precision" && i + 1 < argc)
        {
            cfg.precision = std::stoi(argv[++i]);
            continue;
        }

        std::fprintf(stderr, "Unknown argument: %s\n", arg.c_str());
        print_usage(argv[0]);
        return 2;
    }

    const auto result = analyze_bag_to_csv(cfg);
    if (!result)
    {
        std::fprintf(stderr, "spectra-ros-analyze: %s\n", result.error.c_str());
        return 1;
    }

    std::printf("Wrote %zu rows x %zu field columns to %s\n",
                result.row_count,
                result.column_count,
                cfg.csv_path.c_str());
    return 0;
}
