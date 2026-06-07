#include "tools/ros_bag_analyze.hpp"

#include "bag_field_spec.hpp"
#include "bag_reader.hpp"
#include "message_introspector.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace spectra::adapters::ros2
{

namespace
{

struct SeriesColumn
{
    BagFieldSpec                           spec;
    std::shared_ptr<const MessageSchema>   schema;
    FieldAccessor                          accessor;
    std::vector<std::pair<double, double>> samples;   // bag_time_sec, value
};

BagAnalyzeResult fail(const std::string& msg)
{
    BagAnalyzeResult r;
    r.error = msg;
    return r;
}

bool extract_bag_value(const BagMessage&   msg,
                       const SeriesColumn& col,
                       double              bag_start_ns,
                       double&             bag_time_sec,
                       double&             value)
{
    if (msg.serialized_data.size() < 4 || !col.accessor.valid())
        return false;

    const uint8_t* cdr_body      = msg.serialized_data.data() + 4;
    const size_t   cdr_body_size = msg.serialized_data.size() - 4;
    const size_t   total_offset  = col.accessor.flat_byte_offset();
    const size_t   leaf_sz       = col.accessor.leaf_size();
    if (leaf_sz == 0 || total_offset + leaf_sz > cdr_body_size)
        return false;

    value = col.accessor.extract_double(static_cast<const void*>(cdr_body));
    if (std::isnan(value))
        return false;

    bag_time_sec =
        static_cast<double>(msg.timestamp_ns - static_cast<int64_t>(bag_start_ns)) * 1e-9;
    return true;
}

}   // namespace

BagAnalyzeResult analyze_bag_to_csv(const BagAnalyzeConfig& config)
{
#ifndef SPECTRA_ROS2_BAG
    return fail("rosbag2 support not compiled (SPECTRA_ROS2_BAG=OFF)");
#else
    if (config.bag_path.empty())
        return fail("bag path is required");
    if (config.fields.empty())
        return fail("at least one --fields entry is required");
    if (config.csv_path.empty())
        return fail("csv output path is required");

    BagReader reader;
    if (!reader.open(config.bag_path))
        return fail("failed to open bag: " + reader.last_error());

    std::vector<std::string> bag_topics;
    bag_topics.reserve(reader.topics().size());
    for (const auto& t : reader.topics())
        bag_topics.push_back(t.name);

    MessageIntrospector       intr;
    std::vector<SeriesColumn> columns;
    columns.reserve(config.fields.size());

    for (const auto& token : config.fields)
    {
        BagFieldSpec spec;
        if (!resolve_bag_field_spec(token, bag_topics, spec))
            return fail("could not resolve field spec '" + token + "' against bag topics");

        auto topic_info = reader.topic_info(spec.topic);
        if (!topic_info)
            return fail("topic not found in bag: " + spec.topic);

        auto schema = intr.introspect(topic_info->type);
        if (!schema)
            return fail("failed to introspect type for " + spec.topic + ": " + topic_info->type);

        FieldAccessor acc = intr.make_accessor(*schema, spec.field_path);
        if (!acc.valid())
            return fail("field not found: " + spec.column_name);

        if (acc.requires_deserialized_struct())
            return fail("field requires full deserialization: " + spec.column_name);

        SeriesColumn col;
        col.spec     = spec;
        col.schema   = schema;
        col.accessor = std::move(acc);
        columns.push_back(std::move(col));
    }

    const double bag_start_ns = static_cast<double>(reader.metadata().start_time_ns);

    BagMessage msg;
    while (reader.read_next(msg))
    {
        for (auto& col : columns)
        {
            if (msg.topic != col.spec.topic)
                continue;

            double bag_time_sec = 0.0;
            double value        = 0.0;
            if (!extract_bag_value(msg, col, bag_start_ns, bag_time_sec, value))
                continue;

            col.samples.emplace_back(bag_time_sec, value);
        }
    }

    if (!reader.last_error().empty() && columns.front().samples.empty())
        return fail("bag read error: " + reader.last_error());

    std::vector<double> row_times;
    for (const auto& col : columns)
    {
        for (const auto& [t, _] : col.samples)
            row_times.push_back(t);
    }
    if (row_times.empty())
        return fail("no samples extracted for requested fields");

    std::sort(row_times.begin(), row_times.end());
    row_times.erase(std::unique(row_times.begin(),
                                row_times.end(),
                                [](double a, double b) { return std::abs(a - b) < 1e-12; }),
                    row_times.end());

    std::ofstream out(config.csv_path);
    if (!out)
        return fail("failed to open csv output: " + config.csv_path);

    out << "timestamp_sec" << config.separator << "timestamp_nsec" << config.separator
        << "bag_time_sec";
    for (const auto& col : columns)
        out << config.separator << col.spec.column_name;
    out << '\n';

    out << std::fixed << std::setprecision(config.precision);

    size_t rows_written = 0;
    for (const double bag_t : row_times)
    {
        const int64_t abs_ns = reader.metadata().start_time_ns + static_cast<int64_t>(bag_t * 1e9);
        const int64_t sec    = abs_ns / 1'000'000'000LL;
        const int64_t nsec   = abs_ns % 1'000'000'000LL;

        out << sec << config.separator << nsec << config.separator << bag_t;

        for (const auto& col : columns)
        {
            double value = NAN;
            for (const auto& [t, v] : col.samples)
            {
                if (std::abs(t - bag_t) < 1e-9)
                {
                    value = v;
                    break;
                }
            }
            out << config.separator;
            if (!std::isnan(value))
                out << value;
        }
        out << '\n';
        ++rows_written;
    }

    BagAnalyzeResult result;
    result.ok           = true;
    result.row_count    = rows_written;
    result.column_count = columns.size();
    return result;
#endif
}

}   // namespace spectra::adapters::ros2
