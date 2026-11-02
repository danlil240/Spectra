#pragma once

// LogReader — format-neutral access to recorded log files (ULog, MCAP, ...).
//
// A reader opens one file and exposes every plottable scalar it contains as a
// LogField: a topic (message / channel name) plus a field path inside it,
// using "." for nested messages and "[i]" for array elements, e.g.
//   topic "vehicle_attitude", path "q[0]"
//   topic "/imu",             path "angular_velocity.x"
// read() returns the field's samples as timestamps in seconds (double, so
// epoch and boot times keep full precision) and values as float.
//
// Readers report failures through return values and last_error(); they never
// throw.  Not thread-safe: use one reader from one thread at a time.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace spectra::logs
{

struct LogField
{
    std::string topic;              // message / channel name (first tree level)
    std::string path;               // field inside the topic: "a.b", "arr[2]"
    std::string type;               // source type name, e.g. "float", "uint32_t", "float64"
    std::string unit;               // empty when the format carries no units
    size_t      sample_count = 0;   // number of samples read() returns

    // "topic/path" — used as the default series label.
    std::string full_name() const;
};

struct LogSeries
{
    std::vector<double> time;     // seconds; non-decreasing per field
    std::vector<float>  values;   // same length as time
};

class LogReader
{
   public:
    virtual ~LogReader() = default;

    // Short format name, e.g. "ULog".
    virtual std::string_view format_name() const = 0;

    // Open and index `path`.  Returns false and sets last_error() on failure.
    virtual bool open(const std::string& path) = 0;

    // Every plottable field, grouped by topic (topics appear contiguously).
    virtual const std::vector<LogField>& fields() const = 0;

    // Read all samples of fields()[field_index] into `out` (replacing its
    // contents).  Returns false and sets last_error() if the index is invalid
    // or the data cannot be decoded.
    virtual bool read(size_t field_index, LogSeries& out) const = 0;

    // Samples with t0 <= time <= t1.  The default reads the whole field and
    // filters; readers with an index can do better.
    virtual bool read_range(size_t field_index, double t0, double t1, LogSeries& out) const;

    // Non-fatal notes from open(), e.g. channels skipped because their message
    // encoding is not supported.  Shown in the log browser.
    const std::vector<std::string>& warnings() const { return warnings_; }

    const std::string& path() const { return path_; }
    const std::string& last_error() const { return error_; }

    // Index of the field with this topic and path, if any.
    std::optional<size_t> find_field(std::string_view topic, std::string_view path) const;

   protected:
    bool fail(std::string message) const
    {
        error_ = std::move(message);
        return false;
    }

    std::string              path_;
    std::vector<std::string> warnings_;
    mutable std::string      error_;
};

// ─── Field tree (for browsers) ──────────────────────────────────────────────

// A node of the topic → path-segment tree built from LogReader::fields().
// Path segments are split on "." (array suffixes stay on their segment, so
// "poses[0].position.x" becomes poses[0] → position → x).
struct LogFieldNode
{
    std::string               name;                // segment label (topic name at depth 1)
    int                       field_index  = -1;   // leaf: index into fields(); -1 for groups
    size_t                    sample_count = 0;    // leaf: field's count; group: max of children
    std::vector<LogFieldNode> children;            // in first-seen order
};

// Root node (empty name) whose children are the topics.
LogFieldNode build_field_tree(const std::vector<LogField>& fields);

// Case-insensitive substring match of `filter` against "topic/path".
// An empty filter matches everything.
bool field_matches_filter(const LogField& field, std::string_view filter);

}   // namespace spectra::logs
