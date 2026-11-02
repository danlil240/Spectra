#include "ulog_log_reader.hpp"

namespace spectra::logs
{

namespace
{

constexpr int MAX_NESTING = 8;

const char* ulog_type_name(ULogFieldType t)
{
    switch (t)
    {
        case ULogFieldType::Int8:
            return "int8_t";
        case ULogFieldType::UInt8:
            return "uint8_t";
        case ULogFieldType::Int16:
            return "int16_t";
        case ULogFieldType::UInt16:
            return "uint16_t";
        case ULogFieldType::Int32:
            return "int32_t";
        case ULogFieldType::UInt32:
            return "uint32_t";
        case ULogFieldType::Int64:
            return "int64_t";
        case ULogFieldType::UInt64:
            return "uint64_t";
        case ULogFieldType::Float:
            return "float";
        case ULogFieldType::Double:
            return "double";
        case ULogFieldType::Bool:
            return "bool";
        case ULogFieldType::Char:
            return "char";
        case ULogFieldType::Nested:
            break;
    }
    return "";
}

float value_at(const ULogDataRow& row, size_t offset, ULogFieldType type)
{
    switch (type)
    {
        case ULogFieldType::Int8:
            return static_cast<float>(row.field_at<int8_t>(offset));
        case ULogFieldType::UInt8:
            return static_cast<float>(row.field_at<uint8_t>(offset));
        case ULogFieldType::Int16:
            return static_cast<float>(row.field_at<int16_t>(offset));
        case ULogFieldType::UInt16:
            return static_cast<float>(row.field_at<uint16_t>(offset));
        case ULogFieldType::Int32:
            return static_cast<float>(row.field_at<int32_t>(offset));
        case ULogFieldType::UInt32:
            return static_cast<float>(row.field_at<uint32_t>(offset));
        case ULogFieldType::Int64:
            return static_cast<float>(row.field_at<int64_t>(offset));
        case ULogFieldType::UInt64:
            return static_cast<float>(row.field_at<uint64_t>(offset));
        case ULogFieldType::Float:
            return row.field_at<float>(offset);
        case ULogFieldType::Double:
            return static_cast<float>(row.field_at<double>(offset));
        case ULogFieldType::Bool:
            return row.field_at<uint8_t>(offset) ? 1.0f : 0.0f;
        case ULogFieldType::Char:
        case ULogFieldType::Nested:
            break;
    }
    return 0.0f;
}

}   // namespace

bool ULogLogReader::has_magic(std::span<const uint8_t> head)
{
    static constexpr uint8_t MAGIC[] = {0x55, 0x4C, 0x6F, 0x67, 0x01, 0x12, 0x35};
    if (head.size() < sizeof(MAGIC))
        return false;
    for (size_t i = 0; i < sizeof(MAGIC); ++i)
    {
        if (head[i] != MAGIC[i])
            return false;
    }
    return true;
}

bool ULogLogReader::open(const std::string& path)
{
    fields_.clear();
    slots_.clear();
    warnings_.clear();
    error_.clear();
    path_ = path;

    if (!reader_.open(path))
        return fail(reader_.last_error());

    // all_data() is a std::map, so topics come out sorted by name.
    for (const auto& [key, series] : reader_.all_data())
    {
        if (!series.format || series.rows.empty())
            continue;
        add_fields(key, series, *series.format, "", 0, 0);
    }
    return true;
}

void ULogLogReader::add_fields(const std::string&       topic,
                               const ULogTimeSeries&    series,
                               const ULogMessageFormat& format,
                               const std::string&       prefix,
                               size_t                   base_offset,
                               int                      depth)
{
    for (const auto& f : format.fields)
    {
        if (f.name.rfind("_padding", 0) == 0 || f.base_type == ULogFieldType::Char)
            continue;
        // The top-level timestamp is the time axis, not a value.
        if (depth == 0 && f.name == "timestamp")
            continue;

        const size_t offset = base_offset + f.offset;
        const int    count  = f.array_size > 0 ? f.array_size : 1;

        if (f.base_type == ULogFieldType::Nested)
        {
            const ULogMessageFormat* nested = reader_.format(f.nested_type);
            if (!nested || depth >= MAX_NESTING)
                continue;
            for (int i = 0; i < count; ++i)
            {
                std::string name = prefix + f.name;
                if (count > 1)
                    name += "[" + std::to_string(i) + "]";
                add_fields(topic,
                           series,
                           *nested,
                           name + ".",
                           offset + static_cast<size_t>(i) * nested->byte_size,
                           depth + 1);
            }
            continue;
        }

        const size_t elem = ulog_field_size(f.base_type);
        for (int i = 0; i < count; ++i)
        {
            LogField lf;
            lf.topic = topic;
            lf.path  = prefix + f.name;
            if (count > 1)
                lf.path += "[" + std::to_string(i) + "]";
            lf.type         = ulog_type_name(f.base_type);
            lf.sample_count = series.rows.size();
            fields_.push_back(std::move(lf));
            slots_.push_back(Slot{&series, offset + static_cast<size_t>(i) * elem, f.base_type});
        }
    }
}

bool ULogLogReader::read(size_t field_index, LogSeries& out) const
{
    if (field_index >= slots_.size())
        return fail("Field index out of range");

    const Slot& slot = slots_[field_index];
    const auto& rows = slot.series->rows;
    out.time.resize(rows.size());
    out.values.resize(rows.size());
    for (size_t i = 0; i < rows.size(); ++i)
    {
        out.time[i]   = static_cast<double>(rows[i].timestamp_us) * 1e-6;
        out.values[i] = value_at(rows[i], slot.offset, slot.type);
    }
    return true;
}

}   // namespace spectra::logs
