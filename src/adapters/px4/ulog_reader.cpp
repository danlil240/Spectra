#include "ulog_reader.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace spectra::adapters::px4
{

// ---------------------------------------------------------------------------
// ULog binary format constants
// ---------------------------------------------------------------------------

static constexpr uint8_t  k_magic[] = {0x55, 0x4C, 0x6F, 0x67, 0x01, 0x12, 0x35};
static constexpr size_t   k_magic_size = sizeof(k_magic);
static constexpr size_t   k_header_size = k_magic_size + 1 + 8;  // magic + version + timestamp

// Message types (ULog spec).
static constexpr uint8_t MSG_FORMAT          = 'F';
static constexpr uint8_t MSG_DATA            = 'D';
static constexpr uint8_t MSG_INFO            = 'I';
static constexpr uint8_t MSG_INFO_MULTIPLE   = 'M';
static constexpr uint8_t MSG_PARAMETER       = 'P';
static constexpr uint8_t MSG_PARAMETER_DEF   = 'Q';
static constexpr uint8_t MSG_ADD_LOGGED_MSG  = 'A';
static constexpr uint8_t MSG_REMOVE_LOGGED   = 'R';
static constexpr uint8_t MSG_SYNC            = 'S';
static constexpr uint8_t MSG_DROPOUT         = 'O';
static constexpr uint8_t MSG_LOGGING         = 'L';
static constexpr uint8_t MSG_LOGGING_TAGGED  = 'C';
static constexpr uint8_t MSG_FLAG_BITS       = 'B';

// Message header: 2-byte size + 1-byte type.
static constexpr size_t MSG_HEADER_SIZE = 3;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static uint16_t read_u16(const uint8_t* p)
{
    uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}

static uint32_t read_u32(const uint8_t* p)
{
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

static uint64_t read_u64(const uint8_t* p)
{
    uint64_t v;
    std::memcpy(&v, p, 8);
    return v;
}

static int32_t read_i32(const uint8_t* p)
{
    int32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

static float read_f32(const uint8_t* p)
{
    float v;
    std::memcpy(&v, p, 4);
    return v;
}

// ---------------------------------------------------------------------------
// ULogFieldType helpers
// ---------------------------------------------------------------------------

size_t ulog_field_size(ULogFieldType t)
{
    switch (t)
    {
    case ULogFieldType::Int8:   return 1;
    case ULogFieldType::UInt8:  return 1;
    case ULogFieldType::Int16:  return 2;
    case ULogFieldType::UInt16: return 2;
    case ULogFieldType::Int32:  return 4;
    case ULogFieldType::UInt32: return 4;
    case ULogFieldType::Int64:  return 8;
    case ULogFieldType::UInt64: return 8;
    case ULogFieldType::Float:  return 4;
    case ULogFieldType::Double: return 8;
    case ULogFieldType::Bool:   return 1;
    case ULogFieldType::Char:   return 1;
    case ULogFieldType::Nested: return 0;  // depends on nested format
    }
    return 0;
}

ULogFieldType parse_ulog_type(const std::string& type_str, int& array_size)
{
    // Strip array suffix: "float[3]" → "float", array_size = 3.
    std::string base = type_str;
    array_size = 1;

    auto bracket = base.find('[');
    if (bracket != std::string::npos)
    {
        auto close = base.find(']', bracket);
        if (close != std::string::npos)
        {
            std::string num_str = base.substr(bracket + 1, close - bracket - 1);
            array_size = std::max(1, std::atoi(num_str.c_str()));
        }
        base = base.substr(0, bracket);
    }

    if (base == "int8_t")    return ULogFieldType::Int8;
    if (base == "uint8_t")   return ULogFieldType::UInt8;
    if (base == "int16_t")   return ULogFieldType::Int16;
    if (base == "uint16_t")  return ULogFieldType::UInt16;
    if (base == "int32_t")   return ULogFieldType::Int32;
    if (base == "uint32_t")  return ULogFieldType::UInt32;
    if (base == "int64_t")   return ULogFieldType::Int64;
    if (base == "uint64_t")  return ULogFieldType::UInt64;
    if (base == "float")     return ULogFieldType::Float;
    if (base == "double")    return ULogFieldType::Double;
    if (base == "bool")      return ULogFieldType::Bool;
    if (base == "char")      return ULogFieldType::Char;

    // Anything else is a nested type reference.
    return ULogFieldType::Nested;
}

// ---------------------------------------------------------------------------
// ULogLogMessage
// ---------------------------------------------------------------------------

const char* ULogLogMessage::level_name() const
{
    switch (log_level)
    {
    case '0': return "EMERG";
    case '1': return "ALERT";
    case '2': return "CRIT";
    case '3': return "ERROR";
    case '4': return "WARN";
    case '5': return "NOTICE";
    case '6': return "INFO";
    case '7': return "DEBUG";
    default:  return "UNKNOWN";
    }
}

// ---------------------------------------------------------------------------
// ULogInfoMessage helpers
// ---------------------------------------------------------------------------

std::string ULogInfoMessage::as_string() const
{
    if (auto* s = std::get_if<std::string>(&value))
        return *s;
    if (auto* i = std::get_if<int32_t>(&value))
        return std::to_string(*i);
    if (auto* u = std::get_if<uint32_t>(&value))
        return std::to_string(*u);
    if (auto* i64 = std::get_if<int64_t>(&value))
        return std::to_string(*i64);
    if (auto* u64 = std::get_if<uint64_t>(&value))
        return std::to_string(*u64);
    if (auto* f = std::get_if<float>(&value))
        return std::to_string(*f);
    if (auto* d = std::get_if<double>(&value))
        return std::to_string(*d);
    if (auto* b = std::get_if<bool>(&value))
        return *b ? "true" : "false";
    return {};
}

int64_t ULogInfoMessage::as_int() const
{
    if (auto* i = std::get_if<int32_t>(&value))
        return *i;
    if (auto* u = std::get_if<uint32_t>(&value))
        return static_cast<int64_t>(*u);
    if (auto* i64 = std::get_if<int64_t>(&value))
        return *i64;
    if (auto* u64 = std::get_if<uint64_t>(&value))
        return static_cast<int64_t>(*u64);
    return 0;
}

double ULogInfoMessage::as_double() const
{
    if (auto* f = std::get_if<float>(&value))
        return *f;
    if (auto* d = std::get_if<double>(&value))
        return *d;
    if (auto* i = std::get_if<int32_t>(&value))
        return *i;
    if (auto* u = std::get_if<uint32_t>(&value))
        return *u;
    return 0.0;
}

// ---------------------------------------------------------------------------
// ULogTimeSeries
// ---------------------------------------------------------------------------

std::pair<std::vector<float>, std::vector<float>>
ULogTimeSeries::extract_field(const std::string& field_name) const
{
    if (!format)
        return {};

    // Find the field.
    const ULogField* target = nullptr;
    for (auto& f : format->fields)
    {
        if (f.name == field_name)
        {
            target = &f;
            break;
        }
    }
    if (!target)
        return {};

    std::vector<float> times;
    std::vector<float> values;
    times.reserve(rows.size());
    values.reserve(rows.size());

    for (auto& row : rows)
    {
        times.push_back(static_cast<float>(row.timestamp_us) * 1e-6f);

        switch (target->base_type)
        {
        case ULogFieldType::Float:
            values.push_back(row.field_at<float>(target->offset));
            break;
        case ULogFieldType::Double:
            values.push_back(static_cast<float>(row.field_at<double>(target->offset)));
            break;
        case ULogFieldType::Int32:
            values.push_back(static_cast<float>(row.field_at<int32_t>(target->offset)));
            break;
        case ULogFieldType::UInt32:
            values.push_back(static_cast<float>(row.field_at<uint32_t>(target->offset)));
            break;
        case ULogFieldType::Int16:
            values.push_back(static_cast<float>(row.field_at<int16_t>(target->offset)));
            break;
        case ULogFieldType::UInt16:
            values.push_back(static_cast<float>(row.field_at<uint16_t>(target->offset)));
            break;
        case ULogFieldType::Int8:
            values.push_back(static_cast<float>(row.field_at<int8_t>(target->offset)));
            break;
        case ULogFieldType::UInt8:
            values.push_back(static_cast<float>(row.field_at<uint8_t>(target->offset)));
            break;
        case ULogFieldType::Int64:
            values.push_back(static_cast<float>(row.field_at<int64_t>(target->offset)));
            break;
        case ULogFieldType::UInt64:
            values.push_back(static_cast<float>(row.field_at<uint64_t>(target->offset)));
            break;
        case ULogFieldType::Bool:
            values.push_back(row.field_at<uint8_t>(target->offset) ? 1.0f : 0.0f);
            break;
        default:
            values.push_back(0.0f);
            break;
        }
    }

    return {std::move(times), std::move(values)};
}

std::pair<std::vector<double>, std::vector<double>>
ULogTimeSeries::extract_field_double(const std::string& field_name) const
{
    if (!format)
        return {};

    const ULogField* target = nullptr;
    for (auto& f : format->fields)
    {
        if (f.name == field_name)
        {
            target = &f;
            break;
        }
    }
    if (!target)
        return {};

    std::vector<double> times;
    std::vector<double> values;
    times.reserve(rows.size());
    values.reserve(rows.size());

    for (auto& row : rows)
    {
        times.push_back(static_cast<double>(row.timestamp_us) * 1e-6);

        switch (target->base_type)
        {
        case ULogFieldType::Float:
            values.push_back(row.field_at<float>(target->offset));
            break;
        case ULogFieldType::Double:
            values.push_back(row.field_at<double>(target->offset));
            break;
        case ULogFieldType::Int32:
            values.push_back(row.field_at<int32_t>(target->offset));
            break;
        case ULogFieldType::UInt32:
            values.push_back(row.field_at<uint32_t>(target->offset));
            break;
        default:
            values.push_back(0.0);
            break;
        }
    }

    return {std::move(times), std::move(values)};
}

std::pair<std::vector<float>, std::vector<float>>
ULogTimeSeries::extract_array_element(const std::string& field_name, int index) const
{
    if (!format || index < 0)
        return {};

    const ULogField* target = nullptr;
    for (auto& f : format->fields)
    {
        if (f.name == field_name && index < f.array_size)
        {
            target = &f;
            break;
        }
    }
    if (!target)
        return {};

    size_t elem_size = ulog_field_size(target->base_type);
    size_t elem_offset = target->offset + static_cast<size_t>(index) * elem_size;

    std::vector<float> times;
    std::vector<float> values;
    times.reserve(rows.size());
    values.reserve(rows.size());

    for (auto& row : rows)
    {
        times.push_back(static_cast<float>(row.timestamp_us) * 1e-6f);

        switch (target->base_type)
        {
        case ULogFieldType::Float:
            values.push_back(row.field_at<float>(elem_offset));
            break;
        case ULogFieldType::Double:
            values.push_back(static_cast<float>(row.field_at<double>(elem_offset)));
            break;
        case ULogFieldType::Int32:
            values.push_back(static_cast<float>(row.field_at<int32_t>(elem_offset)));
            break;
        case ULogFieldType::UInt32:
            values.push_back(static_cast<float>(row.field_at<uint32_t>(elem_offset)));
            break;
        default:
            values.push_back(0.0f);
            break;
        }
    }

    return {std::move(times), std::move(values)};
}

// ---------------------------------------------------------------------------
// ULogReader — construction / destruction
// ---------------------------------------------------------------------------

ULogReader::ULogReader() = default;
ULogReader::~ULogReader() = default;

// ---------------------------------------------------------------------------
// open
// ---------------------------------------------------------------------------

bool ULogReader::open(const std::string& file_path)
{
    close();

    // Read entire file into memory.
    std::ifstream ifs(file_path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open())
    {
        last_error_ = "ULogReader: cannot open file: " + file_path;
        return false;
    }

    auto file_size = static_cast<size_t>(ifs.tellg());
    if (file_size < k_header_size)
    {
        last_error_ = "ULogReader: file too small to be a valid ULog: " + file_path;
        return false;
    }

    std::vector<uint8_t> buffer(file_size);
    ifs.seekg(0, std::ios::beg);
    ifs.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(file_size));
    if (!ifs.good())
    {
        last_error_ = "ULogReader: failed to read file: " + file_path;
        return false;
    }
    ifs.close();

    metadata_.path = file_path;

    // Parse header.
    if (!parse_header(buffer.data(), file_size))
        return false;

    // Parse definitions section (starts after header).
    size_t data_offset = k_header_size;
    if (!parse_definitions(buffer.data(), file_size, data_offset))
        return false;

    // Resolve field offsets now that all formats are known.
    for (auto& [name, fmt] : formats_)
        resolve_field_offsets(fmt);

    // Parse data section.
    if (!parse_data_section(buffer.data(), file_size, data_offset))
        return false;

    // Compute metadata summary.
    metadata_.message_count = 0;
    metadata_.start_time_us = UINT64_MAX;
    metadata_.end_time_us = 0;

    for (auto& [key, ts] : data_)
    {
        metadata_.message_count += ts.rows.size();
        if (!ts.rows.empty())
        {
            auto first_ts = ts.rows.front().timestamp_us;
            auto last_ts  = ts.rows.back().timestamp_us;
            if (first_ts < metadata_.start_time_us)
                metadata_.start_time_us = first_ts;
            if (last_ts > metadata_.end_time_us)
                metadata_.end_time_us = last_ts;
        }
    }

    if (metadata_.start_time_us == UINT64_MAX)
        metadata_.start_time_us = metadata_.timestamp_us;
    if (metadata_.end_time_us == 0)
        metadata_.end_time_us = metadata_.start_time_us;

    metadata_.duration_us = metadata_.end_time_us - metadata_.start_time_us;
    metadata_.dropout_count = dropouts_.size();
    metadata_.total_dropout_ms = 0;
    for (auto& d : dropouts_)
        metadata_.total_dropout_ms += d.duration_ms;

    open_ = true;
    return true;
}

// ---------------------------------------------------------------------------
// close
// ---------------------------------------------------------------------------

void ULogReader::close()
{
    metadata_ = {};
    formats_.clear();
    subscriptions_.clear();
    sub_index_.clear();
    data_.clear();
    info_messages_.clear();
    initial_params_.clear();
    changed_params_.clear();
    dropouts_.clear();
    log_messages_.clear();
    open_ = false;
    last_error_.clear();
}

bool ULogReader::is_open() const noexcept { return open_; }

const ULogMetadata& ULogReader::metadata() const noexcept { return metadata_; }

const std::unordered_map<std::string, ULogMessageFormat>&
ULogReader::message_formats() const noexcept { return formats_; }

const ULogMessageFormat* ULogReader::format(const std::string& name) const
{
    auto it = formats_.find(name);
    return it != formats_.end() ? &it->second : nullptr;
}

const std::vector<ULogSubscription>& ULogReader::subscriptions() const noexcept
{
    return subscriptions_;
}

std::vector<std::string> ULogReader::topic_names() const
{
    std::vector<std::string> names;
    names.reserve(data_.size());
    for (auto& [key, ts] : data_)
        names.push_back(key);
    return names;
}

bool ULogReader::has_topic(const std::string& name) const
{
    return data_.find(name) != data_.end() ||
           data_.find(make_data_key(name, 0)) != data_.end();
}

size_t ULogReader::topic_count() const noexcept { return data_.size(); }

const ULogTimeSeries* ULogReader::data_for(const std::string& topic, uint8_t multi_id) const
{
    auto key = make_data_key(topic, multi_id);
    auto it = data_.find(key);
    return it != data_.end() ? &it->second : nullptr;
}

const std::map<std::string, ULogTimeSeries>& ULogReader::all_data() const noexcept
{
    return data_;
}

const std::vector<ULogInfoMessage>& ULogReader::info_messages() const noexcept
{
    return info_messages_;
}

const std::vector<ULogParameter>& ULogReader::parameters() const noexcept
{
    return initial_params_;
}

const std::vector<ULogParameter>& ULogReader::changed_parameters() const noexcept
{
    return changed_params_;
}

const std::vector<ULogDropout>& ULogReader::dropouts() const noexcept { return dropouts_; }

const std::vector<ULogLogMessage>& ULogReader::log_messages() const noexcept
{
    return log_messages_;
}

std::optional<ULogInfoMessage> ULogReader::info(const std::string& key) const
{
    for (auto& im : info_messages_)
    {
        if (im.key == key)
            return im;
    }
    return std::nullopt;
}

std::string ULogReader::info_string(const std::string& key) const
{
    auto im = info(key);
    return im ? im->as_string() : std::string{};
}

const std::string& ULogReader::last_error() const noexcept { return last_error_; }
void ULogReader::clear_error() noexcept { last_error_.clear(); }

// ---------------------------------------------------------------------------
// parse_header
// ---------------------------------------------------------------------------

bool ULogReader::parse_header(const uint8_t* data, size_t size)
{
    if (size < k_header_size)
    {
        last_error_ = "ULogReader: file too small for header";
        return false;
    }

    // Check magic bytes.
    if (std::memcmp(data, k_magic, k_magic_size) != 0)
    {
        last_error_ = "ULogReader: invalid ULog magic bytes";
        return false;
    }

    metadata_.version = data[k_magic_size];
    metadata_.timestamp_us = read_u64(data + k_magic_size + 1);

    return true;
}

// ---------------------------------------------------------------------------
// parse_definitions
// ---------------------------------------------------------------------------

bool ULogReader::parse_definitions(const uint8_t* data, size_t size, size_t& offset)
{
    // The definitions section contains FLAG_BITS, FORMAT, INFO, INFO_MULTIPLE,
    // and PARAMETER messages.  It ends when we hit a DATA, ADD_LOGGED_MSG,
    // or any other data-section message type.
    while (offset + MSG_HEADER_SIZE <= size)
    {
        uint16_t msg_size = read_u16(data + offset);
        uint8_t  msg_type = data[offset + 2];

        if (offset + MSG_HEADER_SIZE + msg_size > size)
        {
            last_error_ = "ULogReader: truncated message in definitions section";
            return false;
        }

        const uint8_t* payload = data + offset + MSG_HEADER_SIZE;

        switch (msg_type)
        {
        case MSG_FLAG_BITS:
            // Skip flag bits for now (v2 feature).
            break;

        case MSG_FORMAT:
            if (!parse_format_message(payload, msg_size))
                return false;
            break;

        case MSG_INFO:
            if (!parse_info_message(payload, msg_size))
                return false;
            break;

        case MSG_INFO_MULTIPLE:
            if (!parse_multi_info_message(payload, msg_size))
                return false;
            break;

        case MSG_PARAMETER:
            if (!parse_parameter_message(payload, msg_size, true))
                return false;
            break;

        case MSG_PARAMETER_DEF:
            if (!parse_parameter_message(payload, msg_size, true))
                return false;
            break;

        default:
            // Non-definition message — data section starts here.
            return true;
        }

        offset += MSG_HEADER_SIZE + msg_size;
    }

    return true;
}

// ---------------------------------------------------------------------------
// parse_data_section
// ---------------------------------------------------------------------------

bool ULogReader::parse_data_section(const uint8_t* data, size_t size, size_t offset)
{
    while (offset + MSG_HEADER_SIZE <= size)
    {
        uint16_t msg_size = read_u16(data + offset);
        uint8_t  msg_type = data[offset + 2];

        if (offset + MSG_HEADER_SIZE + msg_size > size)
            break;  // truncated message at EOF is tolerated

        const uint8_t* payload = data + offset + MSG_HEADER_SIZE;

        switch (msg_type)
        {
        case MSG_ADD_LOGGED_MSG:
            parse_subscription_message(payload, msg_size);
            break;

        case MSG_REMOVE_LOGGED:
            parse_unsubscription_message(payload, msg_size);
            break;

        case MSG_DATA:
            parse_data_message(payload, msg_size);
            break;

        case MSG_LOGGING:
        case MSG_LOGGING_TAGGED:
            parse_logging_message(payload, msg_size);
            break;

        case MSG_DROPOUT:
            parse_dropout_message(payload, msg_size);
            break;

        case MSG_SYNC:
            parse_sync_message(payload, msg_size);
            break;

        case MSG_PARAMETER:
            parse_parameter_message(payload, msg_size, false);
            break;

        case MSG_INFO:
            parse_info_message(payload, msg_size);
            break;

        case MSG_INFO_MULTIPLE:
            parse_multi_info_message(payload, msg_size);
            break;

        default:
            // Unknown message type — skip.
            break;
        }

        offset += MSG_HEADER_SIZE + msg_size;
    }

    return true;
}

// ---------------------------------------------------------------------------
// parse_format_message
// ---------------------------------------------------------------------------

bool ULogReader::parse_format_message(const uint8_t* payload, uint16_t len)
{
    // Format: "name:field1_type field1_name;field2_type field2_name;..."
    std::string content(reinterpret_cast<const char*>(payload), len);

    auto colon = content.find(':');
    if (colon == std::string::npos)
    {
        last_error_ = "ULogReader: FORMAT message missing colon separator";
        return false;
    }

    ULogMessageFormat fmt;
    fmt.name = content.substr(0, colon);

    std::string fields_str = content.substr(colon + 1);

    // Split by ';'
    std::istringstream ss(fields_str);
    std::string field_def;

    while (std::getline(ss, field_def, ';'))
    {
        if (field_def.empty())
            continue;

        // Trim whitespace.
        while (!field_def.empty() && field_def.front() == ' ')
            field_def.erase(field_def.begin());
        while (!field_def.empty() && field_def.back() == ' ')
            field_def.pop_back();

        if (field_def.empty())
            continue;

        // Split into type and name: "float[4] q" or "uint64_t timestamp"
        auto space = field_def.find(' ');
        if (space == std::string::npos)
            continue;

        ULogField f;
        f.type_str = field_def.substr(0, space);
        f.name = field_def.substr(space + 1);

        // Trim padding fields (start with '_padding').
        if (f.name.find("_padding") == 0)
            continue;

        int arr_size = 1;
        f.base_type = parse_ulog_type(f.type_str, arr_size);
        f.array_size = arr_size;

        if (f.base_type == ULogFieldType::Nested)
            f.nested_type = f.type_str.substr(0, f.type_str.find('['));

        fmt.fields.push_back(std::move(f));
    }

    formats_[fmt.name] = std::move(fmt);
    return true;
}

// ---------------------------------------------------------------------------
// parse_info_message
// ---------------------------------------------------------------------------

bool ULogReader::parse_info_message(const uint8_t* payload, uint16_t len)
{
    if (len < 2)
        return true;  // empty info message, skip

    // Format: key_len (1 byte) + key (key_len bytes) + value (remaining)
    uint8_t key_len = payload[0];
    if (static_cast<uint16_t>(1 + key_len) > len)
        return true;

    // Key includes type prefix: "char[40] sys_name"
    std::string key_with_type(reinterpret_cast<const char*>(payload + 1), key_len);

    // Split type and key name.
    auto space = key_with_type.find(' ');
    if (space == std::string::npos)
        return true;

    std::string type_str = key_with_type.substr(0, space);
    std::string key_name = key_with_type.substr(space + 1);

    const uint8_t* val_ptr = payload + 1 + key_len;
    uint16_t val_len = len - 1 - key_len;

    ULogInfoMessage info;
    info.key = key_name;
    info.type_str = type_str;

    // Parse value based on type.
    if (type_str.find("char[") == 0 || type_str == "char")
    {
        info.value = std::string(reinterpret_cast<const char*>(val_ptr), val_len);
    }
    else if (type_str == "int32_t" && val_len >= 4)
    {
        info.value = read_i32(val_ptr);
    }
    else if (type_str == "uint32_t" && val_len >= 4)
    {
        info.value = read_u32(val_ptr);
    }
    else if (type_str == "int64_t" && val_len >= 8)
    {
        info.value = static_cast<int64_t>(read_u64(val_ptr));
    }
    else if (type_str == "uint64_t" && val_len >= 8)
    {
        info.value = read_u64(val_ptr);
    }
    else if (type_str == "float" && val_len >= 4)
    {
        info.value = read_f32(val_ptr);
    }
    else if (type_str == "bool" && val_len >= 1)
    {
        info.value = (val_ptr[0] != 0);
    }
    else
    {
        // Unknown type — store as string of raw bytes.
        info.value = std::string(reinterpret_cast<const char*>(val_ptr), val_len);
    }

    info_messages_.push_back(std::move(info));
    return true;
}

// ---------------------------------------------------------------------------
// parse_multi_info_message
// ---------------------------------------------------------------------------

bool ULogReader::parse_multi_info_message(const uint8_t* payload, uint16_t len)
{
    if (len < 3)
        return true;

    // Format: is_continued (1 byte) + key_len (1 byte) + key + value
    // The is_continued flag signals multi-part values; for read-only parsing
    // we store each part as a separate info entry, so the flag is not needed.
    // uint8_t is_continued = payload[0];
    uint8_t key_len = payload[1];

    if (static_cast<uint16_t>(2 + key_len) > len)
        return true;

    std::string key_with_type(reinterpret_cast<const char*>(payload + 2), key_len);

    auto space = key_with_type.find(' ');
    if (space == std::string::npos)
        return true;

    std::string type_str = key_with_type.substr(0, space);
    std::string key_name = key_with_type.substr(space + 1);

    const uint8_t* val_ptr = payload + 2 + key_len;
    uint16_t val_len = len - 2 - key_len;

    ULogInfoMessage info;
    info.key = key_name;
    info.type_str = type_str;

    if (type_str.find("char[") == 0 || type_str == "char")
        info.value = std::string(reinterpret_cast<const char*>(val_ptr), val_len);
    else
        info.value = std::string(reinterpret_cast<const char*>(val_ptr), val_len);

    info_messages_.push_back(std::move(info));
    return true;
}

// ---------------------------------------------------------------------------
// parse_parameter_message
// ---------------------------------------------------------------------------

bool ULogReader::parse_parameter_message(const uint8_t* payload, uint16_t len, bool is_initial)
{
    if (len < 2)
        return true;

    uint8_t key_len = payload[0];
    if (static_cast<uint16_t>(1 + key_len) > len)
        return true;

    std::string key_with_type(reinterpret_cast<const char*>(payload + 1), key_len);

    auto space = key_with_type.find(' ');
    if (space == std::string::npos)
        return true;

    std::string type_str = key_with_type.substr(0, space);
    std::string key_name = key_with_type.substr(space + 1);

    const uint8_t* val_ptr = payload + 1 + key_len;
    uint16_t val_len = len - 1 - key_len;

    ULogParameter param;
    param.key = key_name;

    if (type_str == "float" && val_len >= 4)
    {
        param.is_float = true;
        param.value_float = read_f32(val_ptr);
    }
    else if (type_str == "int32_t" && val_len >= 4)
    {
        param.is_float = false;
        param.value_int = read_i32(val_ptr);
    }

    if (is_initial)
        initial_params_.push_back(std::move(param));
    else
        changed_params_.push_back(std::move(param));

    return true;
}

// ---------------------------------------------------------------------------
// parse_subscription_message
// ---------------------------------------------------------------------------

bool ULogReader::parse_subscription_message(const uint8_t* payload, uint16_t len)
{
    // Format: multi_id (1 byte) + msg_id (2 bytes) + message_name (remaining)
    if (len < 4)
        return true;

    ULogSubscription sub;
    sub.multi_id = payload[0];
    sub.msg_id = read_u16(payload + 1);

    // Message name is null-terminated.
    size_t name_len = len - 3;
    sub.message_name = std::string(reinterpret_cast<const char*>(payload + 3), name_len);

    // Strip trailing null bytes.
    while (!sub.message_name.empty() && sub.message_name.back() == '\0')
        sub.message_name.pop_back();

    // Register subscription.
    sub_index_[sub.msg_id] = subscriptions_.size();
    subscriptions_.push_back(sub);

    // Create time series entry.
    auto key = make_data_key(sub.message_name, sub.multi_id);
    auto& ts = data_[key];
    ts.message_name = sub.message_name;
    ts.multi_id = sub.multi_id;

    // Link to format definition.
    auto fmt_it = formats_.find(sub.message_name);
    if (fmt_it != formats_.end())
        ts.format = &fmt_it->second;

    return true;
}

// ---------------------------------------------------------------------------
// parse_unsubscription_message
// ---------------------------------------------------------------------------

bool ULogReader::parse_unsubscription_message(const uint8_t* payload, uint16_t len)
{
    if (len < 2)
        return true;

    uint16_t msg_id = read_u16(payload);
    sub_index_.erase(msg_id);
    return true;
}

// ---------------------------------------------------------------------------
// parse_data_message
// ---------------------------------------------------------------------------

bool ULogReader::parse_data_message(const uint8_t* payload, uint16_t len)
{
    if (len < 10)   // msg_id (2) + timestamp (8) minimum
        return true;

    uint16_t msg_id = read_u16(payload);
    uint64_t timestamp = read_u64(payload + 2);

    auto sub_it = sub_index_.find(msg_id);
    if (sub_it == sub_index_.end())
        return true;  // unknown subscription, skip

    auto& sub = subscriptions_[sub_it->second];
    auto key = make_data_key(sub.message_name, sub.multi_id);
    auto data_it = data_.find(key);
    if (data_it == data_.end())
        return true;

    ULogDataRow row;
    row.timestamp_us = timestamp;

    // Store the full field data (starting after msg_id), including the
    // timestamp field.  This way, field offsets from the FORMAT definition
    // map directly into the payload.
    size_t data_len = len - 2;   // everything after msg_id
    if (data_len > 0)
    {
        row.payload.resize(data_len);
        std::memcpy(row.payload.data(), payload + 2, data_len);
    }

    data_it->second.rows.push_back(std::move(row));
    return true;
}

// ---------------------------------------------------------------------------
// parse_logging_message
// ---------------------------------------------------------------------------

bool ULogReader::parse_logging_message(const uint8_t* payload, uint16_t len)
{
    if (len < 9)   // log_level (1) + timestamp (8) minimum
        return true;

    ULogLogMessage msg;
    msg.log_level = payload[0];
    msg.timestamp_us = read_u64(payload + 1);

    size_t text_len = len - 9;
    if (text_len > 0)
        msg.message = std::string(reinterpret_cast<const char*>(payload + 9), text_len);

    // Strip trailing null bytes.
    while (!msg.message.empty() && msg.message.back() == '\0')
        msg.message.pop_back();

    log_messages_.push_back(std::move(msg));
    return true;
}

// ---------------------------------------------------------------------------
// parse_dropout_message
// ---------------------------------------------------------------------------

bool ULogReader::parse_dropout_message(const uint8_t* payload, uint16_t len)
{
    if (len < 2)
        return true;

    ULogDropout dropout;
    dropout.duration_ms = read_u16(payload);
    dropouts_.push_back(dropout);
    return true;
}

// ---------------------------------------------------------------------------
// parse_sync_message
// ---------------------------------------------------------------------------

bool ULogReader::parse_sync_message(const uint8_t* /*payload*/, uint16_t /*len*/)
{
    // Sync messages are used for seeking — no processing needed for read-only.
    return true;
}

// ---------------------------------------------------------------------------
// resolve_field_offsets
// ---------------------------------------------------------------------------

void ULogReader::resolve_field_offsets(ULogMessageFormat& fmt)
{
    size_t offset = 0;
    for (auto& f : fmt.fields)
    {
        f.offset = offset;

        if (f.base_type == ULogFieldType::Nested)
        {
            // Lookup nested format to determine size.
            auto it = formats_.find(f.nested_type);
            if (it != formats_.end())
                offset += it->second.byte_size * static_cast<size_t>(f.array_size);
            else
                offset += 0;  // unknown nested type
        }
        else
        {
            offset += ulog_field_size(f.base_type) * static_cast<size_t>(f.array_size);
        }
    }
    fmt.byte_size = offset;
}

// ---------------------------------------------------------------------------
// make_data_key
// ---------------------------------------------------------------------------

std::string ULogReader::make_data_key(const std::string& name, uint8_t multi_id)
{
    if (multi_id == 0)
        return name;
    return name + "[" + std::to_string(multi_id) + "]";
}

}   // namespace spectra::adapters::px4
