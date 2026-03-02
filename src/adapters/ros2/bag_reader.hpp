#pragma once

// BagReader — rosbag2 backend for Spectra ROS2 adapter.
//
// Opens .db3 (SQLite) or .mcap bags, exposes:
//   - BagMetadata  : bag-level summary (path, duration, message counts, topics)
//   - BagTopicInfo : per-topic info from the bag index
//   - BagMessage   : one deserialized-raw message (topic + serialized bytes + timestamps)
//   - BagReader    : the main reader class
//
// Gated behind SPECTRA_ROS2_BAG — if that define is absent the entire header
// compiles to empty stubs so callers can include it unconditionally.
//
// Usage:
//   BagReader reader;
//   if (!reader.open("/path/to/my.db3")) {
//       // handle error via reader.last_error()
//   }
//   auto meta = reader.metadata();
//   auto topics = reader.topics();
//
//   // Sequential read (forward only):
//   reader.set_topic_filter({"topic1", "topic2"});
//   BagMessage msg;
//   while (reader.read_next(msg)) {
//       // msg.topic, msg.timestamp_ns, msg.serialized_data
//   }
//
//   // Random seek by timestamp:
//   reader.seek(1234567890LL);   // nanoseconds from bag start
//   while (reader.read_next(msg)) { ... }
//
// Thread-safety:
//   NOT thread-safe.  All methods must be called from the same thread.
//   Create separate BagReader instances per thread.
//
// Error handling:
//   All operations that can fail return bool and set last_error().
//   Exceptions from rosbag2 are caught and stored as error strings.

#ifdef SPECTRA_ROS2_BAG

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include <rosbag2_cpp/reader.hpp>
#include <rosbag2_storage/storage_options.hpp>
#include <rosbag2_storage/topic_metadata.hpp>

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// BagTopicInfo — per-topic metadata from the bag index.
// ---------------------------------------------------------------------------

struct BagTopicInfo
{
    std::string name;             // fully qualified topic name, e.g. "/cmd_vel"
    std::string type;             // ROS2 type string, e.g. "geometry_msgs/msg/Twist"
    std::string serialization_fmt; // usually "cdr"
    uint64_t    message_count{0}; // total messages for this topic in the bag
    int         offered_qos_count{0}; // number of QoS profiles recorded
};

// ---------------------------------------------------------------------------
// BagMetadata — bag-level summary information.
// ---------------------------------------------------------------------------

struct BagMetadata
{
    std::string path;                   // absolute path that was opened
    std::string storage_id;             // "sqlite3" or "mcap"
    int64_t     start_time_ns{0};       // earliest message timestamp (nanoseconds)
    int64_t     end_time_ns{0};         // latest message timestamp (nanoseconds)
    int64_t     duration_ns{0};         // end_time_ns - start_time_ns
    uint64_t    message_count{0};       // total messages across all topics
    uint64_t    compressed_size{0};     // bag file size in bytes (0 if not available)
    std::vector<BagTopicInfo> topics;   // per-topic info (filled by open())

    double duration_sec() const noexcept
    {
        return static_cast<double>(duration_ns) * 1e-9;
    }

    double start_time_sec() const noexcept
    {
        return static_cast<double>(start_time_ns) * 1e-9;
    }

    double end_time_sec() const noexcept
    {
        return static_cast<double>(end_time_ns) * 1e-9;
    }
};

// ---------------------------------------------------------------------------
// BagMessage — one raw message read from the bag.
// ---------------------------------------------------------------------------

struct BagMessage
{
    std::string              topic;            // topic name
    std::string              type;             // ROS2 message type string
    std::string              serialization_fmt; // "cdr"
    int64_t                  timestamp_ns{0};  // message receive timestamp in ns
    std::vector<uint8_t>     serialized_data;  // raw CDR bytes (empty if invalid)

    bool valid() const noexcept { return !topic.empty() && !serialized_data.empty(); }
};

// ---------------------------------------------------------------------------
// BagReader — main reader class.
// ---------------------------------------------------------------------------

class BagReader
{
public:
    BagReader();
    ~BagReader();

    // Non-copyable, movable.
    BagReader(const BagReader&)            = delete;
    BagReader& operator=(const BagReader&) = delete;
    BagReader(BagReader&&)                 = default;
    BagReader& operator=(BagReader&&)      = default;

    // ------------------------------------------------------------------
    // Open / Close
    // ------------------------------------------------------------------

    // Open a bag at `bag_path`.  The path may point to a .db3 file, an .mcap
    // file, or a bag directory containing metadata.yaml.
    // Returns true on success; on failure sets last_error() and returns false.
    // Re-opening is allowed — closes the previous bag first.
    bool open(const std::string& bag_path);

    // Close the currently open bag and release all resources.
    void close();

    // True if a bag is currently open and in a readable state.
    bool is_open() const noexcept;

    // ------------------------------------------------------------------
    // Metadata & Topic listing
    // ------------------------------------------------------------------

    // Returns bag-level metadata.  Valid only after a successful open().
    // Returns an empty BagMetadata if not open.
    const BagMetadata& metadata() const noexcept;

    // Returns the per-topic info extracted from the bag metadata.
    // Equivalent to metadata().topics; provided as a convenience accessor.
    const std::vector<BagTopicInfo>& topics() const noexcept;

    // Returns BagTopicInfo for a specific topic, or nullopt if not found.
    std::optional<BagTopicInfo> topic_info(const std::string& topic_name) const;

    // True if the bag contains the given topic.
    bool has_topic(const std::string& topic_name) const;

    // Number of distinct topics in the bag.
    size_t topic_count() const noexcept;

    // ------------------------------------------------------------------
    // Topic filter (applied before reading)
    // ------------------------------------------------------------------

    // Restrict reading to the given set of topics.
    // Pass an empty set to clear the filter (read all topics).
    // Must be called before read_next() or after seek().
    void set_topic_filter(const std::vector<std::string>& topics);

    // Returns the current topic filter (empty = no filter).
    const std::vector<std::string>& topic_filter() const noexcept;

    // ------------------------------------------------------------------
    // Sequential read
    // ------------------------------------------------------------------

    // Read the next message into `msg`.
    // Returns true if a message was read, false if the bag is exhausted or
    // an error occurred (check last_error()).
    bool read_next(BagMessage& msg);

    // True if there are more messages to read (honoring the current filter).
    bool has_next() const;

    // ------------------------------------------------------------------
    // Random seek
    // ------------------------------------------------------------------

    // Seek to the given timestamp (nanoseconds, absolute bag time).
    // After a successful seek, read_next() resumes from that point.
    // Returns true on success; false if not open or seek fails.
    bool seek(int64_t timestamp_ns);

    // Seek to the beginning of the bag (equivalent to seek(metadata().start_time_ns)).
    bool seek_begin();

    // Seek to the given fraction of the bag duration [0.0, 1.0].
    // 0.0 = start, 1.0 = end.  Clamped to valid range.
    bool seek_fraction(double fraction);

    // ------------------------------------------------------------------
    // State
    // ------------------------------------------------------------------

    // Timestamp of the most recently read message (nanoseconds).
    // Returns 0 if no message has been read yet.
    int64_t current_timestamp_ns() const noexcept;

    // Progress through the bag as a fraction [0.0, 1.0].
    // Based on current_timestamp_ns vs bag duration.
    double progress() const noexcept;

    // ------------------------------------------------------------------
    // Error handling
    // ------------------------------------------------------------------

    // Human-readable description of the last error, or empty string if no error.
    const std::string& last_error() const noexcept;

    // Clear the last error.
    void clear_error() noexcept;

private:
    // Build BagMetadata from the reader's bag metadata after open.
    void build_metadata();

    // Apply topic filter to the reader (called on open and after set_topic_filter).
    void apply_filter();

    // Detect storage ID from file extension or bag directory.
    static std::string detect_storage_id(const std::string& path);

    std::unique_ptr<rosbag2_cpp::Reader> reader_;
    BagMetadata                          metadata_;
    std::vector<std::string>             topic_filter_;
    int64_t                              current_ts_ns_{0};
    bool                                 open_{false};
    bool                                 filter_applied_{false};
    std::string                          last_error_;

    // Type lookup: topic_name → type string (populated in build_metadata).
    std::unordered_map<std::string, std::string> topic_type_map_;
    std::unordered_map<std::string, std::string> topic_fmt_map_;
};

} // namespace spectra::adapters::ros2

#else // SPECTRA_ROS2_BAG not defined — empty stubs so headers compile cleanly.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace spectra::adapters::ros2
{

struct BagTopicInfo
{
    std::string name;
    std::string type;
    std::string serialization_fmt;
    uint64_t    message_count{0};
    int         offered_qos_count{0};
};

struct BagMetadata
{
    std::string              path;
    std::string              storage_id;
    int64_t                  start_time_ns{0};
    int64_t                  end_time_ns{0};
    int64_t                  duration_ns{0};
    uint64_t                 message_count{0};
    uint64_t                 compressed_size{0};
    std::vector<BagTopicInfo> topics;

    double duration_sec()    const noexcept { return 0.0; }
    double start_time_sec()  const noexcept { return 0.0; }
    double end_time_sec()    const noexcept { return 0.0; }
};

struct BagMessage
{
    std::string          topic;
    std::string          type;
    std::string          serialization_fmt;
    int64_t              timestamp_ns{0};
    std::vector<uint8_t> serialized_data;
    bool valid() const noexcept { return false; }
};

class BagReader
{
public:
    bool        open(const std::string&)                     { return false; }
    void        close()                                      {}
    bool        is_open()                        const noexcept { return false; }
    const BagMetadata& metadata()               const noexcept { return meta_; }
    const std::vector<BagTopicInfo>& topics()   const noexcept { return meta_.topics; }
    std::optional<BagTopicInfo> topic_info(const std::string&) const { return std::nullopt; }
    bool        has_topic(const std::string&)    const             { return false; }
    size_t      topic_count()                    const noexcept { return 0; }
    void        set_topic_filter(const std::vector<std::string>&) {}
    const std::vector<std::string>& topic_filter() const noexcept { return filter_; }
    bool        read_next(BagMessage&)                             { return false; }
    bool        has_next()                       const             { return false; }
    bool        seek(int64_t)                                      { return false; }
    bool        seek_begin()                                       { return false; }
    bool        seek_fraction(double)                              { return false; }
    int64_t     current_timestamp_ns()           const noexcept { return 0; }
    double      progress()                        const noexcept { return 0.0; }
    const std::string& last_error()              const noexcept { return err_; }
    void        clear_error()                    noexcept { err_.clear(); }

private:
    BagMetadata              meta_;
    std::vector<std::string> filter_;
    std::string              err_{"BagReader: built without SPECTRA_ROS2_BAG"};
};

} // namespace spectra::adapters::ros2

#endif // SPECTRA_ROS2_BAG
