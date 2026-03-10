#include "ros_log_viewer.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstring>

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// Severity helpers
// ---------------------------------------------------------------------------

LogSeverity severity_from_rcl(uint8_t level)
{
    if (level >= 50)
        return LogSeverity::Fatal;
    if (level >= 40)
        return LogSeverity::Error;
    if (level >= 30)
        return LogSeverity::Warn;
    if (level >= 20)
        return LogSeverity::Info;
    if (level >= 10)
        return LogSeverity::Debug;
    return LogSeverity::Unset;
}

const char* severity_name(LogSeverity s)
{
    switch (s)
    {
        case LogSeverity::Debug:
            return "DEBUG";
        case LogSeverity::Info:
            return "INFO";
        case LogSeverity::Warn:
            return "WARN";
        case LogSeverity::Error:
            return "ERROR";
        case LogSeverity::Fatal:
            return "FATAL";
        default:
            return "UNSET";
    }
}

char severity_char(LogSeverity s)
{
    switch (s)
    {
        case LogSeverity::Debug:
            return 'D';
        case LogSeverity::Info:
            return 'I';
        case LogSeverity::Warn:
            return 'W';
        case LogSeverity::Error:
            return 'E';
        case LogSeverity::Fatal:
            return 'F';
        default:
            return '?';
    }
}

// ---------------------------------------------------------------------------
// LogFilter helpers
// ---------------------------------------------------------------------------

bool ci_contains(const std::string& haystack, const std::string& needle)
{
    if (needle.empty())
        return true;
    auto it = std::search(haystack.begin(),
                          haystack.end(),
                          needle.begin(),
                          needle.end(),
                          [](unsigned char a, unsigned char b)
                          { return std::tolower(a) == std::tolower(b); });
    return it != haystack.end();
}

bool LogFilter::compile_regex(std::regex& out_re) const
{
    if (message_regex_str.empty())
    {
        regex_error.clear();
        return true;
    }
    try
    {
        out_re = std::regex(message_regex_str, std::regex::ECMAScript | std::regex::icase);
        regex_error.clear();
        return true;
    }
    catch (const std::regex_error& ex)
    {
        regex_error = ex.what();
        return false;
    }
}

bool LogFilter::passes(const LogEntry& e) const
{
    if (static_cast<uint8_t>(e.severity) < static_cast<uint8_t>(min_severity))
        return false;
    if (!node_filter.empty() && !ci_contains(e.node, node_filter))
        return false;
    if (!message_regex_str.empty())
    {
        std::regex re;
        if (!compile_regex(re))
            return true;   // invalid regex → pass all
        if (!std::regex_search(e.message, re))
            return false;
    }
    return true;
}

bool LogFilter::passes_compiled(const LogEntry& e, const std::regex* compiled_re) const
{
    if (static_cast<uint8_t>(e.severity) < static_cast<uint8_t>(min_severity))
        return false;
    if (!node_filter.empty() && !ci_contains(e.node, node_filter))
        return false;
    if (compiled_re && !message_regex_str.empty())
    {
        if (!std::regex_search(e.message, *compiled_re))
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

std::string RosLogViewer::format_stamp(int64_t stamp_ns)
{
    const int64_t sec  = stamp_ns / 1'000'000'000LL;
    const int64_t nsec = std::abs(stamp_ns % 1'000'000'000LL);
    char          buf[32];
    std::snprintf(buf, sizeof(buf), "%" PRId64 ".%09" PRId64, sec, nsec);
    return buf;
}

std::string RosLogViewer::format_wall_time(double wall_time_s)
{
    const auto total_s = static_cast<int64_t>(wall_time_s);
    const int  ms      = static_cast<int>((wall_time_s - total_s) * 1000.0);
    const int  hh      = static_cast<int>(total_s / 3600) % 24;
    const int  mm      = static_cast<int>(total_s / 60) % 60;
    const int  ss      = static_cast<int>(total_s) % 60;
    char       buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", hh, mm, ss, ms);
    return buf;
}

// ---------------------------------------------------------------------------
// RosLogViewer — construction / destruction
// ---------------------------------------------------------------------------

RosLogViewer::RosLogViewer(rclcpp::Node::SharedPtr node) : node_(std::move(node))
{
    ring_.resize(capacity_);
}

RosLogViewer::~RosLogViewer()
{
    unsubscribe();
}

// ---------------------------------------------------------------------------
// Subscription lifecycle
// ---------------------------------------------------------------------------

void RosLogViewer::subscribe(const std::string& topic)
{
    std::lock_guard<std::mutex> lock(sub_mutex_);
    topic_ = topic;
    if (!node_)
        return;

    // The /rosout topic uses rcl_interfaces/msg/Log.
    // We use GenericSubscription so we don't hard-code the type support.
    subscription_ = node_->create_generic_subscription(
        topic_,
        "rcl_interfaces/msg/Log",
        rclcpp::QoS(rclcpp::KeepLast(1000)).reliable().durability_volatile(),
        [this](std::shared_ptr<rclcpp::SerializedMessage> msg) { on_message(std::move(msg)); });
}

void RosLogViewer::unsubscribe()
{
    std::lock_guard<std::mutex> lock(sub_mutex_);
    subscription_.reset();
}

bool RosLogViewer::is_subscribed() const
{
    std::lock_guard<std::mutex> lock(sub_mutex_);
    return static_cast<bool>(subscription_);
}

// ---------------------------------------------------------------------------
// Buffer control
// ---------------------------------------------------------------------------

void RosLogViewer::set_capacity(size_t n)
{
    n = std::clamp(n, MIN_CAPACITY, MAX_CAPACITY);
    std::lock_guard<std::mutex> lock(ring_mutex_);
    if (n == capacity_)
        return;

    // Collect existing entries in order, then resize.
    std::vector<LogEntry> tmp;
    tmp.reserve(ring_size_);
    for (size_t i = 0; i < ring_size_; ++i)
        tmp.push_back(ring_[(ring_head_ - ring_size_ + i + capacity_) % capacity_]);

    capacity_ = n;
    ring_.resize(capacity_);
    ring_head_ = 0;
    ring_size_ = 0;

    // Re-insert keeping only the most recent n entries.
    const size_t keep_from = (tmp.size() > n) ? (tmp.size() - n) : 0;
    for (size_t i = keep_from; i < tmp.size(); ++i)
    {
        ring_[ring_head_] = std::move(tmp[i]);
        ring_head_        = (ring_head_ + 1) % capacity_;
        if (ring_size_ < capacity_)
            ++ring_size_;
    }
}

void RosLogViewer::clear()
{
    std::lock_guard<std::mutex> lock(ring_mutex_);
    ring_head_ = 0;
    ring_size_ = 0;
}

// ---------------------------------------------------------------------------
// Filter API
// ---------------------------------------------------------------------------

void RosLogViewer::set_filter(const LogFilter& f)
{
    filter_      = f;
    regex_dirty_ = true;
}

void RosLogViewer::maybe_recompile_regex()
{
    if (!regex_dirty_)
        return;
    regex_dirty_ = false;
    if (filter_.message_regex_str.empty())
    {
        regex_valid_ = false;
        return;
    }
    regex_valid_ = filter_.compile_regex(compiled_re_);
}

// ---------------------------------------------------------------------------
// Read API
// ---------------------------------------------------------------------------

std::vector<LogEntry> RosLogViewer::snapshot() const
{
    std::lock_guard<std::mutex> lock(ring_mutex_);
    std::vector<LogEntry>       out;
    out.reserve(ring_size_);
    for (size_t i = 0; i < ring_size_; ++i)
        out.push_back(ring_[(ring_head_ - ring_size_ + i + capacity_) % capacity_]);
    return out;
}

std::vector<LogEntry> RosLogViewer::filtered_snapshot()
{
    maybe_recompile_regex();
    const std::regex* re =
        (regex_valid_ && !filter_.message_regex_str.empty()) ? &compiled_re_ : nullptr;

    std::lock_guard<std::mutex> lock(ring_mutex_);
    std::vector<LogEntry>       out;
    out.reserve(ring_size_);
    for (size_t i = 0; i < ring_size_; ++i)
    {
        const LogEntry& e = ring_[(ring_head_ - ring_size_ + i + capacity_) % capacity_];
        if (filter_.passes_compiled(e, re))
            out.push_back(e);
    }
    return out;
}

void RosLogViewer::for_each_filtered(std::function<void(const LogEntry&)> cb)
{
    maybe_recompile_regex();
    const std::regex* re =
        (regex_valid_ && !filter_.message_regex_str.empty()) ? &compiled_re_ : nullptr;

    std::lock_guard<std::mutex> lock(ring_mutex_);
    for (size_t i = 0; i < ring_size_; ++i)
    {
        const LogEntry& e = ring_[(ring_head_ - ring_size_ + i + capacity_) % capacity_];
        if (filter_.passes_compiled(e, re))
            cb(e);
    }
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------

size_t RosLogViewer::entry_count() const
{
    std::lock_guard<std::mutex> lock(ring_mutex_);
    return ring_size_;
}

std::array<uint32_t, 6> RosLogViewer::severity_counts() const
{
    std::array<uint32_t, 6>     counts{};
    std::lock_guard<std::mutex> lock(ring_mutex_);
    for (size_t i = 0; i < ring_size_; ++i)
    {
        const LogEntry& e   = ring_[(ring_head_ - ring_size_ + i + capacity_) % capacity_];
        const size_t    idx = static_cast<uint8_t>(e.severity) / 10;
        if (idx < counts.size())
            ++counts[idx];
    }
    return counts;
}

// ---------------------------------------------------------------------------
// Inject
// ---------------------------------------------------------------------------

void RosLogViewer::inject(LogEntry e)
{
    if (paused_.load(std::memory_order_relaxed))
        return;
    std::lock_guard<std::mutex> lock(ring_mutex_);
    if (e.seq == 0)
        e.seq = next_seq_;
    ++next_seq_;
    ring_[ring_head_] = std::move(e);
    ring_head_        = (ring_head_ + 1) % capacity_;
    if (ring_size_ < capacity_)
        ++ring_size_;
    total_received_.fetch_add(1, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// make_entry
// ---------------------------------------------------------------------------

LogEntry RosLogViewer::make_entry(uint64_t    seq,
                                  uint8_t     level,
                                  int32_t     stamp_sec,
                                  uint32_t    stamp_nanosec,
                                  double      wall_time_s,
                                  std::string node,
                                  std::string message,
                                  std::string file,
                                  std::string function,
                                  uint32_t    line)
{
    LogEntry e;
    e.seq = seq;
    e.stamp_ns =
        static_cast<int64_t>(stamp_sec) * 1'000'000'000LL + static_cast<int64_t>(stamp_nanosec);
    e.wall_time_s = wall_time_s;
    e.severity    = severity_from_rcl(level);
    e.node        = std::move(node);
    e.message     = std::move(message);
    e.file        = std::move(file);
    e.function    = std::move(function);
    e.line        = line;
    return e;
}

// ---------------------------------------------------------------------------
// on_message — executor thread
//
// rcl_interfaces/msg/Log CDR layout (all fields in order):
//   byte    stamp.sec       (4 bytes, int32)  — but first 4 bytes are CDR header
//   byte    stamp.nanosec   (4 bytes, uint32)
//   byte    level           (1 byte,  uint8)  + 3 pad
//   string  name            (uint32 len + chars + null + pad)
//   string  msg
//   string  file
//   string  function
//   uint32  line
//
// CDR byte layout (OMG CDR, little-endian, header = 0x00 0x01 0x00 0x00):
//   Offset 0: header (4 bytes): 00 01 00 00
//   Offset 4: stamp.sec   (int32_t LE, 4 bytes)
//   Offset 8: stamp.nanosec (uint32_t LE, 4 bytes)
//   Offset 12: level (uint8_t, 1 byte) + 3 alignment pad
//   Offset 16: name string length (uint32_t LE) + chars + null + align pad
//   ... msg, file, function (same string encoding)
//   ... line (uint32_t LE)
// ---------------------------------------------------------------------------

static double wall_time_now()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// Read a CDR string: [uint32_t len][chars incl null][alignment pad]
// Returns true on success; advances offset past the field (aligned to 4 bytes).
static bool read_cdr_string(const uint8_t* buf, size_t buf_size, size_t& offset, std::string& out)
{
    // Align to 4-byte boundary for length field.
    const size_t align = (4 - (offset % 4)) % 4;
    offset += align;

    if (offset + 4 > buf_size)
        return false;
    uint32_t len;
    std::memcpy(&len, buf + offset, 4);
    offset += 4;

    if (len == 0)
    {
        out.clear();
        return true;
    }
    if (offset + len > buf_size)
        return false;

    // len includes the null terminator.
    out.assign(reinterpret_cast<const char*>(buf + offset), len > 0 ? len - 1 : 0);
    offset += len;
    return true;
}

void RosLogViewer::on_message(std::shared_ptr<rclcpp::SerializedMessage> raw_msg)
{
    if (paused_.load(std::memory_order_relaxed))
        return;
    if (!raw_msg)
        return;

    const uint8_t* buf      = raw_msg->get_rcl_serialized_message().buffer;
    const size_t   buf_size = raw_msg->get_rcl_serialized_message().buffer_length;

    // Minimum: 4 (CDR header) + 4 (sec) + 4 (nanosec) + 4 (level+pad) = 16
    if (!buf || buf_size < 16)
        return;

    size_t offset = 4;   // skip CDR header

    int32_t  stamp_sec;
    uint32_t stamp_nanosec;
    std::memcpy(&stamp_sec, buf + offset, 4);
    offset += 4;
    std::memcpy(&stamp_nanosec, buf + offset, 4);
    offset += 4;

    if (offset >= buf_size)
        return;
    const uint8_t level = buf[offset];
    offset += 4;   // level (1 byte) + 3 alignment pad

    std::string node_name, msg_str, file_str, func_str;
    uint32_t    line_num = 0;

    if (!read_cdr_string(buf, buf_size, offset, node_name))
        return;
    if (!read_cdr_string(buf, buf_size, offset, msg_str))
        return;
    if (!read_cdr_string(buf, buf_size, offset, file_str))
    { /* optional */
    }
    if (!read_cdr_string(buf, buf_size, offset, func_str))
    { /* optional */
    }

    // Align to 4 for line uint32.
    const size_t align = (4 - (offset % 4)) % 4;
    offset += align;
    if (offset + 4 <= buf_size)
        std::memcpy(&line_num, buf + offset, 4);

    uint64_t seq;
    {
        std::lock_guard<std::mutex> lock(ring_mutex_);
        seq = next_seq_++;
    }

    LogEntry e = make_entry(seq,
                            level,
                            stamp_sec,
                            stamp_nanosec,
                            wall_time_now(),
                            std::move(node_name),
                            std::move(msg_str),
                            std::move(file_str),
                            std::move(func_str),
                            line_num);
    {
        std::lock_guard<std::mutex> lock(ring_mutex_);
        ring_[ring_head_] = std::move(e);
        ring_head_        = (ring_head_ + 1) % capacity_;
        if (ring_size_ < capacity_)
            ++ring_size_;
    }
    total_received_.fetch_add(1, std::memory_order_relaxed);
}

}   // namespace spectra::adapters::ros2
