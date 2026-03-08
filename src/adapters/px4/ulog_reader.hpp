#pragma once

// ULogReader — PX4 ULog binary file reader for Spectra.
//
// Parses the ULog binary format (v1/v2) used by PX4 autopilot for flight log
// recording.  Extracts message definitions, parameters, info messages, logged
// data, and dropout records.
//
// ULog format reference:
//   https://docs.px4.io/main/en/dev_log/ulog_file_format.html
//
// Usage:
//   ULogReader reader;
//   if (!reader.open("/path/to/log.ulg")) {
//       // handle error via reader.last_error()
//   }
//   auto& meta = reader.metadata();
//   auto& msgs = reader.message_formats();
//
//   // Iterate logged data for a specific topic:
//   auto data = reader.data_for("vehicle_attitude");
//   for (auto& ts_row : data) {
//       double roll = ts_row.field<float>("roll");
//   }
//
// Thread-safety:
//   NOT thread-safe.  All methods must be called from the same thread.

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace spectra::adapters::px4
{

// ---------------------------------------------------------------------------
// ULog field type descriptors
// ---------------------------------------------------------------------------

enum class ULogFieldType : uint8_t
{
    Int8,
    UInt8,
    Int16,
    UInt16,
    Int32,
    UInt32,
    Int64,
    UInt64,
    Float,
    Double,
    Bool,
    Char,
    Nested,   // another ULog message type
};

// Size in bytes of a primitive ULog field type.
size_t ulog_field_size(ULogFieldType t);

// Parse a ULog type string (e.g. "float", "uint32_t", "float[3]").
// Returns the base type and array_size (1 for scalars).
ULogFieldType parse_ulog_type(const std::string& type_str, int& array_size);

// ---------------------------------------------------------------------------
// ULogField — one field in a message format definition.
// ---------------------------------------------------------------------------

struct ULogField
{
    std::string   name;
    std::string   type_str;       // original type string, e.g. "float[3]"
    ULogFieldType base_type{ULogFieldType::Float};
    int           array_size{1};  // 1 for scalars, >1 for arrays
    size_t        offset{0};      // byte offset within the message payload
    std::string   nested_type;    // non-empty if base_type == Nested
};

// ---------------------------------------------------------------------------
// ULogMessageFormat — parsed FORMAT message.
// ---------------------------------------------------------------------------

struct ULogMessageFormat
{
    std::string            name;       // e.g. "vehicle_attitude"
    std::vector<ULogField> fields;
    size_t                 byte_size{0}; // total size of one record (excluding timestamp)
};

// ---------------------------------------------------------------------------
// ULogSubscription — maps a msg_id to a message name + multi_id.
// ---------------------------------------------------------------------------

struct ULogSubscription
{
    uint16_t    msg_id{0};
    uint8_t     multi_id{0};   // instance ID for multi-instance messages
    std::string message_name;  // references a ULogMessageFormat
};

// ---------------------------------------------------------------------------
// ULogDataRow — one timestamped data record.
// ---------------------------------------------------------------------------

struct ULogDataRow
{
    uint64_t             timestamp_us{0};   // microseconds since boot
    std::vector<uint8_t> payload;           // raw field data (excluding timestamp)

    // Extract a typed field value at byte offset within payload.
    template <typename T>
    T field_at(size_t byte_offset) const
    {
        T val{};
        if (byte_offset + sizeof(T) <= payload.size())
            std::memcpy(&val, payload.data() + byte_offset, sizeof(T));
        return val;
    }
};

// ---------------------------------------------------------------------------
// ULogTimeSeries — all data rows for one subscription (topic + multi_id).
// ---------------------------------------------------------------------------

struct ULogTimeSeries
{
    std::string      message_name;
    uint8_t          multi_id{0};
    const ULogMessageFormat* format{nullptr};   // non-owning pointer
    std::vector<ULogDataRow> rows;

    // Convenience: extract a named float field as parallel arrays.
    // Returns {timestamps_sec, values}.  Empty if field not found.
    std::pair<std::vector<float>, std::vector<float>>
    extract_field(const std::string& field_name) const;

    // Extract a named field as doubles for higher precision.
    std::pair<std::vector<double>, std::vector<double>>
    extract_field_double(const std::string& field_name) const;

    // Extract an array field element (e.g. "q[0]" from float[4] q).
    std::pair<std::vector<float>, std::vector<float>>
    extract_array_element(const std::string& field_name, int index) const;
};

// ---------------------------------------------------------------------------
// ULogInfoMessage — key-value info from the log header.
// ---------------------------------------------------------------------------

using ULogValue = std::variant<int32_t, uint32_t, int64_t, uint64_t,
                               float, double, bool, std::string>;

struct ULogInfoMessage
{
    std::string key;
    std::string type_str;   // e.g. "char[40]", "uint32_t"
    ULogValue   value;

    // Convenience accessors.
    std::string as_string() const;
    int64_t     as_int() const;
    double      as_double() const;
};

// ---------------------------------------------------------------------------
// ULogParameter — logged parameter (initial or changed).
// ---------------------------------------------------------------------------

struct ULogParameter
{
    std::string key;
    float       value_float{0.0f};
    int32_t     value_int{0};
    bool        is_float{true};
    uint64_t    timestamp_us{0};   // 0 for initial parameters
};

// ---------------------------------------------------------------------------
// ULogDropout — logged data dropout event.
// ---------------------------------------------------------------------------

struct ULogDropout
{
    uint16_t duration_ms{0};
};

// ---------------------------------------------------------------------------
// ULogLogMessage — text log entry from the flight controller.
// ---------------------------------------------------------------------------

struct ULogLogMessage
{
    uint64_t    timestamp_us{0};
    uint8_t     log_level{0};   // '0'=EMERG .. '7'=DEBUG
    std::string message;

    // Severity name.
    const char* level_name() const;
};

// ---------------------------------------------------------------------------
// ULogMetadata — summary information about the log file.
// ---------------------------------------------------------------------------

struct ULogMetadata
{
    std::string path;
    uint8_t     version{0};             // ULog version (1 or 2)
    uint64_t    timestamp_us{0};        // logging start time (microseconds)
    uint64_t    start_time_us{0};       // first data timestamp
    uint64_t    end_time_us{0};         // last data timestamp
    uint64_t    duration_us{0};         // end - start

    size_t      message_count{0};       // total data messages
    size_t      dropout_count{0};       // number of dropout events
    uint32_t    total_dropout_ms{0};    // cumulative dropout time

    double duration_sec() const noexcept
    {
        return static_cast<double>(duration_us) * 1e-6;
    }

    double start_time_sec() const noexcept
    {
        return static_cast<double>(start_time_us) * 1e-6;
    }

    double end_time_sec() const noexcept
    {
        return static_cast<double>(end_time_us) * 1e-6;
    }
};

// ---------------------------------------------------------------------------
// ULogReader — main ULog file reader.
// ---------------------------------------------------------------------------

class ULogReader
{
public:
    ULogReader();
    ~ULogReader();

    // Non-copyable, movable.
    ULogReader(const ULogReader&)            = delete;
    ULogReader& operator=(const ULogReader&) = delete;
    ULogReader(ULogReader&&)                 = default;
    ULogReader& operator=(ULogReader&&)      = default;

    // ------------------------------------------------------------------
    // Open / Close
    // ------------------------------------------------------------------

    // Open a ULog file at `file_path`.
    // Returns true on success; on failure sets last_error() and returns false.
    bool open(const std::string& file_path);

    // Close the currently open log and release all resources.
    void close();

    // True if a log file is currently open and parsed.
    bool is_open() const noexcept;

    // ------------------------------------------------------------------
    // Metadata
    // ------------------------------------------------------------------

    const ULogMetadata& metadata() const noexcept;

    // ------------------------------------------------------------------
    // Message formats
    // ------------------------------------------------------------------

    // All parsed FORMAT definitions, keyed by message name.
    const std::unordered_map<std::string, ULogMessageFormat>& message_formats() const noexcept;

    // Lookup a specific format by name.
    const ULogMessageFormat* format(const std::string& name) const;

    // ------------------------------------------------------------------
    // Subscriptions (topics)
    // ------------------------------------------------------------------

    // All subscription records (msg_id → topic + multi_id).
    const std::vector<ULogSubscription>& subscriptions() const noexcept;

    // List of unique topic names that have logged data.
    std::vector<std::string> topic_names() const;

    // True if the log contains data for the given topic name.
    bool has_topic(const std::string& name) const;

    // Number of distinct topics.
    size_t topic_count() const noexcept;

    // ------------------------------------------------------------------
    // Data access
    // ------------------------------------------------------------------

    // Get the time series for a given topic + multi_id (default 0).
    const ULogTimeSeries* data_for(const std::string& topic, uint8_t multi_id = 0) const;

    // Get all time series (keyed by "topic_name" or "topic_name[multi_id]").
    const std::map<std::string, ULogTimeSeries>& all_data() const noexcept;

    // ------------------------------------------------------------------
    // Info messages / parameters
    // ------------------------------------------------------------------

    const std::vector<ULogInfoMessage>& info_messages() const noexcept;
    const std::vector<ULogParameter>&   parameters() const noexcept;
    const std::vector<ULogParameter>&   changed_parameters() const noexcept;
    const std::vector<ULogDropout>&     dropouts() const noexcept;
    const std::vector<ULogLogMessage>&  log_messages() const noexcept;

    // Lookup a specific info key (e.g. "sys_name", "ver_hw").
    std::optional<ULogInfoMessage> info(const std::string& key) const;

    // Convenience: get info key as string (empty if not found).
    std::string info_string(const std::string& key) const;

    // ------------------------------------------------------------------
    // Error handling
    // ------------------------------------------------------------------

    const std::string& last_error() const noexcept;
    void clear_error() noexcept;

private:
    // Internal parsing stages.
    bool parse_header(const uint8_t* data, size_t size);
    bool parse_definitions(const uint8_t* data, size_t size, size_t& offset);
    bool parse_data_section(const uint8_t* data, size_t size, size_t offset);

    bool parse_format_message(const uint8_t* payload, uint16_t len);
    bool parse_info_message(const uint8_t* payload, uint16_t len);
    bool parse_multi_info_message(const uint8_t* payload, uint16_t len);
    bool parse_parameter_message(const uint8_t* payload, uint16_t len, bool is_default);
    bool parse_subscription_message(const uint8_t* payload, uint16_t len);
    bool parse_unsubscription_message(const uint8_t* payload, uint16_t len);
    bool parse_data_message(const uint8_t* payload, uint16_t len);
    bool parse_logging_message(const uint8_t* payload, uint16_t len);
    bool parse_dropout_message(const uint8_t* payload, uint16_t len);
    bool parse_sync_message(const uint8_t* payload, uint16_t len);

    // Resolve field offsets for a message format after all formats are parsed.
    void resolve_field_offsets(ULogMessageFormat& fmt);

    // Build a lookup key for data access.
    static std::string make_data_key(const std::string& name, uint8_t multi_id);

    ULogMetadata metadata_;

    // Parsed content.
    std::unordered_map<std::string, ULogMessageFormat> formats_;
    std::vector<ULogSubscription>                      subscriptions_;
    std::unordered_map<uint16_t, size_t>               sub_index_;  // msg_id → subscriptions_ index
    std::map<std::string, ULogTimeSeries>              data_;

    std::vector<ULogInfoMessage>  info_messages_;
    std::vector<ULogParameter>    initial_params_;
    std::vector<ULogParameter>    changed_params_;
    std::vector<ULogDropout>      dropouts_;
    std::vector<ULogLogMessage>   log_messages_;

    bool        open_{false};
    std::string last_error_;
};

}   // namespace spectra::adapters::px4
