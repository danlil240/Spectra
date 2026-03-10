// bag_recorder.cpp — BagRecorder implementation
//
// Gated behind SPECTRA_ROS2_BAG.  The header provides empty stubs when
// the define is absent, so this translation unit is only compiled when
// rosbag2 is available (see CMakeLists.txt).

#ifdef SPECTRA_ROS2_BAG

    #include "bag_recorder.hpp"

    #include <algorithm>
    #include <cassert>
    #include <cstdio>
    #include <filesystem>
    #include <sstream>
    #include <stdexcept>

    #include <rosbag2_cpp/writer.hpp>
    #include <rosbag2_storage/storage_options.hpp>

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

BagRecorder::BagRecorder(rclcpp::Node::SharedPtr node) : node_(std::move(node)) {}

BagRecorder::~BagRecorder()
{
    // Graceful stop on destruction — lock not held here.
    stop();
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void BagRecorder::set_max_size_bytes(uint64_t bytes) noexcept
{
    std::lock_guard<std::mutex> lk(mutex_);
    max_size_bytes_ = bytes;
}

uint64_t BagRecorder::max_size_bytes() const noexcept
{
    std::lock_guard<std::mutex> lk(mutex_);
    return max_size_bytes_;
}

void BagRecorder::set_max_duration_seconds(double seconds) noexcept
{
    std::lock_guard<std::mutex> lk(mutex_);
    max_duration_seconds_ = seconds;
}

double BagRecorder::max_duration_seconds() const noexcept
{
    std::lock_guard<std::mutex> lk(mutex_);
    return max_duration_seconds_;
}

void BagRecorder::set_storage_id(const std::string& id)
{
    std::lock_guard<std::mutex> lk(mutex_);
    storage_id_override_ = id;
}

const std::string& BagRecorder::storage_id() const noexcept
{
    std::lock_guard<std::mutex> lk(mutex_);
    return storage_id_override_;
}

void BagRecorder::set_reliable_qos(bool reliable) noexcept
{
    std::lock_guard<std::mutex> lk(mutex_);
    reliable_qos_ = reliable;
}

bool BagRecorder::reliable_qos() const noexcept
{
    std::lock_guard<std::mutex> lk(mutex_);
    return reliable_qos_;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool BagRecorder::start(const std::string& bag_path, const std::vector<std::string>& topics)
{
    std::lock_guard<std::mutex> lk(mutex_);

    if (state_ == RecordingState::Recording)
    {
        last_error_ = "BagRecorder::start() called while already recording";
        return false;
    }
    if (!node_)
    {
        last_error_ = "BagRecorder::start() called with null node";
        return false;
    }
    if (bag_path.empty())
    {
        last_error_ = "BagRecorder::start() called with empty path";
        return false;
    }

    // Reset counters
    message_count_     = 0;
    bytes_total_       = 0;
    bytes_since_split_ = 0;
    split_index_       = 0;
    last_error_.clear();
    topic_type_map_.clear();
    subscriptions_.clear();

    base_path_ = bag_path;
    topics_    = topics;

    // Determine actual storage ID
    const std::string sid =
        storage_id_override_.empty() ? detect_storage_id(bag_path) : storage_id_override_;

    // Open writer at base path (split 0)
    current_path_ = bag_path;
    if (!open_writer(current_path_, sid))
    {
        return false;
    }

    // Subscribe to requested topics
    if (!subscribe_topics())
    {
        close_writer();
        return false;
    }

    start_time_       = std::chrono::steady_clock::now();
    split_start_time_ = start_time_;
    state_            = RecordingState::Recording;

    return true;
}

void BagRecorder::stop()
{
    std::lock_guard<std::mutex> lk(mutex_);

    if (state_ == RecordingState::Idle)
    {
        return;
    }

    state_ = RecordingState::Stopping;

    // Cancel all subscriptions
    subscriptions_.clear();

    // Finalize the current bag file
    close_writer();

    state_ = RecordingState::Idle;
    topic_type_map_.clear();
}

// ---------------------------------------------------------------------------
// State / Statistics
// ---------------------------------------------------------------------------

RecordingState BagRecorder::state() const noexcept
{
    std::lock_guard<std::mutex> lk(mutex_);
    return state_;
}

bool BagRecorder::is_recording() const noexcept
{
    std::lock_guard<std::mutex> lk(mutex_);
    return state_ == RecordingState::Recording;
}

std::string BagRecorder::recording_path() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return base_path_;
}

std::string BagRecorder::current_path() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return current_path_;
}

uint64_t BagRecorder::recorded_message_count() const noexcept
{
    std::lock_guard<std::mutex> lk(mutex_);
    return message_count_;
}

uint64_t BagRecorder::recorded_bytes() const noexcept
{
    std::lock_guard<std::mutex> lk(mutex_);
    return bytes_total_;
}

double BagRecorder::elapsed_seconds() const noexcept
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (state_ == RecordingState::Idle)
    {
        return 0.0;
    }
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - start_time_).count();
}

uint32_t BagRecorder::split_index() const noexcept
{
    std::lock_guard<std::mutex> lk(mutex_);
    return split_index_;
}

std::vector<std::string> BagRecorder::recorded_topics() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (state_ == RecordingState::Idle)
    {
        return {};
    }
    return topics_;
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

void BagRecorder::set_split_callback(SplitCallback cb)
{
    std::lock_guard<std::mutex> lk(mutex_);
    split_cb_ = std::move(cb);
}

void BagRecorder::set_error_callback(ErrorCallback cb)
{
    std::lock_guard<std::mutex> lk(mutex_);
    error_cb_ = std::move(cb);
}

// ---------------------------------------------------------------------------
// Error handling
// ---------------------------------------------------------------------------

const std::string& BagRecorder::last_error() const noexcept
{
    std::lock_guard<std::mutex> lk(mutex_);
    return last_error_;
}

void BagRecorder::clear_error() noexcept
{
    std::lock_guard<std::mutex> lk(mutex_);
    last_error_.clear();
}

// ---------------------------------------------------------------------------
// Private — subscribe_topics
// ---------------------------------------------------------------------------

bool BagRecorder::subscribe_topics()
{
    // Called with mutex_ held.
    if (!node_)
    {
        last_error_ = "null node";
        return false;
    }
    if (topics_.empty())
    {
        last_error_ = "no topics to record";
        return false;
    }

    // Discover topic type info for the requested topics.
    // NOTE: get_topic_names_and_types() is a DDS graph API call.  When called
    // from a non-executor thread it can deadlock with rmw_fastrtps's discovery
    // thread when namespaced participants are present.  Bag recording is
    // user-initiated and infrequent, so the risk is low.  A future improvement
    // would be to resolve types from a TopicDiscovery cache instead.
    const auto graph_topics = node_->get_topic_names_and_types();
    for (const auto& [tname, ttypes] : graph_topics)
    {
        if (!ttypes.empty())
        {
            topic_type_map_[tname] = ttypes.front();
        }
    }

    // Build QoS
    auto qos = reliable_qos_ ? rclcpp::QoS(rclcpp::KeepLast(1000)).reliable()
                             : rclcpp::QoS(rclcpp::KeepLast(1000)).best_effort();

    // Subscribe to each requested topic
    for (const auto& topic : topics_)
    {
        // Resolve type
        auto it = topic_type_map_.find(topic);
        if (it == topic_type_map_.end())
        {
            last_error_ = "topic not found in ROS2 graph: " + topic;
            subscriptions_.clear();
            return false;
        }
        const std::string& msg_type = it->second;

        // Register topic with the writer (must happen before first write)
        rosbag2_storage::TopicMetadata tm;
        tm.name                 = topic;
        tm.type                 = msg_type;
        tm.serialization_format = "cdr";
        try
        {
            writer_->create_topic(tm);
        }
        catch (const std::exception& e)
        {
            last_error_ = std::string("create_topic failed for ") + topic + ": " + e.what();
            subscriptions_.clear();
            return false;
        }

        // Create generic subscription
        try
        {
            auto sub = node_->create_generic_subscription(
                topic,
                msg_type,
                qos,
                [this, topic, msg_type](std::shared_ptr<const rclcpp::SerializedMessage> msg)
                { on_message(topic, msg_type, msg); });
            subscriptions_.push_back(std::move(sub));
        }
        catch (const std::exception& e)
        {
            last_error_ = std::string("subscription failed for ") + topic + ": " + e.what();
            subscriptions_.clear();
            return false;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Private — on_message (executor thread)
// ---------------------------------------------------------------------------

void BagRecorder::on_message(const std::string& topic_name,
                             const std::string& /*message_type*/,
                             std::shared_ptr<const rclcpp::SerializedMessage> msg)
{
    if (!msg)
    {
        return;
    }

    std::lock_guard<std::mutex> lk(mutex_);

    if (state_ != RecordingState::Recording || !writer_)
    {
        return;
    }

    // Build rosbag2 message
    auto bag_msg        = std::make_shared<rosbag2_storage::SerializedBagMessage>();
    bag_msg->topic_name = topic_name;
    bag_msg->time_stamp = node_->now().nanoseconds();

    // Copy serialized buffer
    const auto& rcl_buf       = msg->get_rcl_serialized_message();
    bag_msg->serialized_data  = std::make_shared<rcutils_uint8_array_t>();
    *bag_msg->serialized_data = rcl_buf;

    const uint64_t msg_bytes = rcl_buf.buffer_length;

    try
    {
        writer_->write(bag_msg);
    }
    catch (const std::exception& e)
    {
        last_error_ = std::string("write error: ") + e.what();
        if (error_cb_)
        {
            error_cb_(last_error_);
        }
        return;
    }

    ++message_count_;
    bytes_total_ += msg_bytes;
    bytes_since_split_ += msg_bytes;

    // Check auto-split after updating counters
    check_and_split();
}

// ---------------------------------------------------------------------------
// Private — check_and_split (called with mutex_ held)
// ---------------------------------------------------------------------------

void BagRecorder::check_and_split()
{
    bool need_split = false;

    // Size-based split
    if (max_size_bytes_ > 0 && bytes_since_split_ >= max_size_bytes_)
    {
        need_split = true;
    }

    // Duration-based split
    if (!need_split && max_duration_seconds_ > 0.0)
    {
        const auto   now     = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - split_start_time_).count();
        if (elapsed >= max_duration_seconds_)
        {
            need_split = true;
        }
    }

    if (need_split)
    {
        do_split();
    }
}

// ---------------------------------------------------------------------------
// Private — do_split (called with mutex_ held)
// ---------------------------------------------------------------------------

void BagRecorder::do_split()
{
    // Snapshot info for the callback
    const std::string closed_path  = current_path_;
    const uint64_t    msgs_closed  = message_count_;
    const uint64_t    bytes_closed = bytes_since_split_;

    // Determine new split path
    const uint32_t    new_split_idx = split_index_ + 1;
    const std::string new_path      = make_split_path(base_path_, new_split_idx);

    // Determine storage ID from the existing or override setting
    const std::string sid =
        storage_id_override_.empty() ? detect_storage_id(base_path_) : storage_id_override_;

    // Close current writer
    close_writer();

    // Open new writer
    current_path_ = new_path;
    if (!open_writer(current_path_, sid))
    {
        // Error already set by open_writer; stop recording
        state_ = RecordingState::Idle;
        subscriptions_.clear();
        return;
    }

    // Re-register all topics with the new writer
    for (const auto& [tname, ttype] : topic_type_map_)
    {
        rosbag2_storage::TopicMetadata tm;
        tm.name                 = tname;
        tm.type                 = ttype;
        tm.serialization_format = "cdr";
        try
        {
            writer_->create_topic(tm);
        }
        catch (const std::exception& e)
        {
            last_error_ = std::string("re-register topic on split: ") + e.what();
            if (error_cb_)
            {
                error_cb_(last_error_);
            }
        }
    }

    // Update state
    split_index_       = new_split_idx;
    bytes_since_split_ = 0;
    split_start_time_  = std::chrono::steady_clock::now();

    // Fire split callback
    if (split_cb_)
    {
        RecordingSplitInfo info;
        info.closed_path        = closed_path;
        info.new_path           = new_path;
        info.split_index        = split_index_;
        info.messages_in_closed = msgs_closed;
        info.bytes_in_closed    = bytes_closed;
        split_cb_(info);
    }
}

// ---------------------------------------------------------------------------
// Private — open_writer (called with mutex_ held)
// ---------------------------------------------------------------------------

bool BagRecorder::open_writer(const std::string& path, const std::string& sid)
{
    try
    {
        writer_ = std::make_unique<rosbag2_cpp::Writer>();

        rosbag2_storage::StorageOptions opts;
        opts.uri        = path;
        opts.storage_id = sid;

        writer_->open(opts);
        return true;
    }
    catch (const std::exception& e)
    {
        last_error_ = std::string("failed to open bag '") + path + "': " + e.what();
        writer_.reset();
        return false;
    }
}

// ---------------------------------------------------------------------------
// Private — close_writer (called with mutex_ held)
// ---------------------------------------------------------------------------

void BagRecorder::close_writer()
{
    if (writer_)
    {
        try
        {
            writer_.reset();   // rosbag2_cpp::Writer finalizes on destruction
        }
        catch (...)
        {
            // Suppress exceptions on close
        }
    }
}

// ---------------------------------------------------------------------------
// Private — make_split_path
// ---------------------------------------------------------------------------

std::string BagRecorder::make_split_path(const std::string& base_path, uint32_t idx) const
{
    // Split the base path into stem + extension.
    // /path/to/output.db3 → /path/to/output_split001.db3
    // /path/to/output.mcap → /path/to/output_split001.mcap
    namespace fs = std::filesystem;

    const fs::path    p(base_path);
    const std::string stem   = p.stem().string();
    const std::string ext    = p.extension().string();
    const fs::path    parent = p.parent_path();

    char suffix[16];
    std::snprintf(suffix, sizeof(suffix), "_split%03u", idx);

    const fs::path new_name = fs::path(stem + suffix + ext);
    return (parent / new_name).string();
}

// ---------------------------------------------------------------------------
// Private — detect_storage_id
// ---------------------------------------------------------------------------

/*static*/ std::string BagRecorder::detect_storage_id(const std::string& path)
{
    const auto dot = path.rfind('.');
    if (dot != std::string::npos)
    {
        const std::string ext = path.substr(dot);
        if (ext == ".mcap")
        {
            return "mcap";
        }
        if (ext == ".db3")
        {
            return "sqlite3";
        }
    }
    return "sqlite3";   // default
}

}   // namespace spectra::adapters::ros2

#endif   // SPECTRA_ROS2_BAG
