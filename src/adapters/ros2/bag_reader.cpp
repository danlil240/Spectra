#include "bag_reader.hpp"

#ifdef SPECTRA_ROS2_BAG

#include <filesystem>
#include <stdexcept>
#include <unordered_map>

#include <rosbag2_cpp/reader.hpp>
#include <rosbag2_storage/storage_options.hpp>
#include <rosbag2_storage/topic_metadata.hpp>

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// BagReader — construction / destruction
// ---------------------------------------------------------------------------

BagReader::BagReader()
    : reader_(std::make_unique<rosbag2_cpp::Reader>())
{
}

BagReader::~BagReader()
{
    close();
}

// ---------------------------------------------------------------------------
// Open / Close
// ---------------------------------------------------------------------------

bool BagReader::open(const std::string& bag_path)
{
    // Close any previously open bag.
    close();
    last_error_.clear();

    if (bag_path.empty()) {
        last_error_ = "BagReader::open: empty path";
        return false;
    }

    // Detect storage format from path.
    const std::string storage_id = detect_storage_id(bag_path);

    rosbag2_storage::StorageOptions opts;
    opts.uri        = bag_path;
    opts.storage_id = storage_id;

    try {
        reader_->open(opts);
    } catch (const std::exception& ex) {
        last_error_ = std::string("BagReader::open failed: ") + ex.what();
        reader_ = std::make_unique<rosbag2_cpp::Reader>(); // reset to clean state
        return false;
    } catch (...) {
        last_error_ = "BagReader::open failed: unknown exception";
        reader_ = std::make_unique<rosbag2_cpp::Reader>();
        return false;
    }

    open_ = true;
    build_metadata();

    // Apply any filter set before open (filter_applied_ resets on close).
    if (!topic_filter_.empty()) {
        apply_filter();
    }

    return true;
}

void BagReader::close()
{
    if (open_) {
        try {
            // rosbag2_cpp::Reader destructor handles actual close; re-create to reset.
            reader_ = std::make_unique<rosbag2_cpp::Reader>();
        } catch (...) {
            // Best-effort close — swallow exceptions.
        }
        open_           = false;
        filter_applied_ = false;
        current_ts_ns_  = 0;
        metadata_       = BagMetadata{};
        topic_type_map_.clear();
        topic_fmt_map_.clear();
    }
}

bool BagReader::is_open() const noexcept
{
    return open_;
}

// ---------------------------------------------------------------------------
// Metadata & topic listing
// ---------------------------------------------------------------------------

const BagMetadata& BagReader::metadata() const noexcept
{
    return metadata_;
}

const std::vector<BagTopicInfo>& BagReader::topics() const noexcept
{
    return metadata_.topics;
}

std::optional<BagTopicInfo> BagReader::topic_info(const std::string& topic_name) const
{
    for (const auto& t : metadata_.topics) {
        if (t.name == topic_name) {
            return t;
        }
    }
    return std::nullopt;
}

bool BagReader::has_topic(const std::string& topic_name) const
{
    return topic_type_map_.count(topic_name) > 0;
}

size_t BagReader::topic_count() const noexcept
{
    return metadata_.topics.size();
}

// ---------------------------------------------------------------------------
// Topic filter
// ---------------------------------------------------------------------------

void BagReader::set_topic_filter(const std::vector<std::string>& topics)
{
    topic_filter_   = topics;
    filter_applied_ = false;

    if (open_) {
        apply_filter();
    }
}

const std::vector<std::string>& BagReader::topic_filter() const noexcept
{
    return topic_filter_;
}

// ---------------------------------------------------------------------------
// Sequential read
// ---------------------------------------------------------------------------

bool BagReader::read_next(BagMessage& msg)
{
    if (!open_) {
        last_error_ = "BagReader::read_next: bag not open";
        return false;
    }

    try {
        if (!reader_->has_next()) {
            return false;
        }

        auto bag_msg = reader_->read_next();

        msg.topic             = bag_msg->topic_name;
        msg.timestamp_ns      = bag_msg->time_stamp;
        msg.serialized_data.assign(
            bag_msg->serialized_data->buffer,
            bag_msg->serialized_data->buffer + bag_msg->serialized_data->buffer_length
        );

        // Resolve type and serialization format from cached map.
        auto it_type = topic_type_map_.find(msg.topic);
        if (it_type != topic_type_map_.end()) {
            msg.type = it_type->second;
        }
        auto it_fmt = topic_fmt_map_.find(msg.topic);
        if (it_fmt != topic_fmt_map_.end()) {
            msg.serialization_fmt = it_fmt->second;
        } else {
            msg.serialization_fmt = "cdr";
        }

        current_ts_ns_ = msg.timestamp_ns;
        return true;

    } catch (const std::exception& ex) {
        last_error_ = std::string("BagReader::read_next failed: ") + ex.what();
        return false;
    } catch (...) {
        last_error_ = "BagReader::read_next failed: unknown exception";
        return false;
    }
}

bool BagReader::has_next() const
{
    if (!open_) return false;
    try {
        return reader_->has_next();
    } catch (...) {
        return false;
    }
}

// ---------------------------------------------------------------------------
// Random seek
// ---------------------------------------------------------------------------

bool BagReader::seek(int64_t timestamp_ns)
{
    if (!open_) {
        last_error_ = "BagReader::seek: bag not open";
        return false;
    }

    try {
        reader_->seek(timestamp_ns);
        current_ts_ns_ = timestamp_ns;
        return true;
    } catch (const std::exception& ex) {
        last_error_ = std::string("BagReader::seek failed: ") + ex.what();
        return false;
    } catch (...) {
        last_error_ = "BagReader::seek failed: unknown exception";
        return false;
    }
}

bool BagReader::seek_begin()
{
    return seek(metadata_.start_time_ns);
}

bool BagReader::seek_fraction(double fraction)
{
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;

    const int64_t target = metadata_.start_time_ns +
        static_cast<int64_t>(fraction * static_cast<double>(metadata_.duration_ns));
    return seek(target);
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

int64_t BagReader::current_timestamp_ns() const noexcept
{
    return current_ts_ns_;
}

double BagReader::progress() const noexcept
{
    if (metadata_.duration_ns <= 0) return 0.0;
    const double elapsed = static_cast<double>(current_ts_ns_ - metadata_.start_time_ns);
    const double total   = static_cast<double>(metadata_.duration_ns);
    const double p       = elapsed / total;
    if (p < 0.0) return 0.0;
    if (p > 1.0) return 1.0;
    return p;
}

// ---------------------------------------------------------------------------
// Error handling
// ---------------------------------------------------------------------------

const std::string& BagReader::last_error() const noexcept
{
    return last_error_;
}

void BagReader::clear_error() noexcept
{
    last_error_.clear();
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void BagReader::build_metadata()
{
    try {
        const auto& bag_meta = reader_->get_metadata();

        metadata_.path         = bag_meta.relative_file_paths.empty()
                                     ? std::string{}
                                     : bag_meta.relative_file_paths.front();
        metadata_.storage_id   = bag_meta.storage_identifier;
        metadata_.message_count = bag_meta.message_count;
        metadata_.compressed_size = 0; // rosbag2_storage metadata does not always expose file size

        // Timestamps — rosbag2 uses std::chrono::nanoseconds.
        metadata_.start_time_ns = bag_meta.starting_time
                                       .time_since_epoch()
                                       .count();
        metadata_.duration_ns   = bag_meta.duration.count();
        metadata_.end_time_ns   = metadata_.start_time_ns + metadata_.duration_ns;

        // Per-topic info.
        metadata_.topics.clear();
        metadata_.topics.reserve(bag_meta.topics_with_message_count.size());
        topic_type_map_.clear();
        topic_fmt_map_.clear();

        for (const auto& twmc : bag_meta.topics_with_message_count) {
            BagTopicInfo ti;
            ti.name               = twmc.topic_metadata.name;
            ti.type               = twmc.topic_metadata.type;
            ti.serialization_fmt  = twmc.topic_metadata.serialization_format;
            ti.message_count      = twmc.message_count;
            ti.offered_qos_count  = static_cast<int>(
                twmc.topic_metadata.offered_qos_profiles.size());

            topic_type_map_[ti.name] = ti.type;
            topic_fmt_map_[ti.name]  = ti.serialization_fmt;

            metadata_.topics.push_back(std::move(ti));
        }

    } catch (const std::exception& ex) {
        last_error_ = std::string("BagReader::build_metadata failed: ") + ex.what();
    } catch (...) {
        last_error_ = "BagReader::build_metadata failed: unknown exception";
    }
}

void BagReader::apply_filter()
{
    if (!open_) return;

    try {
        rosbag2_storage::StorageFilter filter;
        filter.topics = topic_filter_;
        reader_->set_filter(filter);
        filter_applied_ = true;
    } catch (const std::exception& ex) {
        last_error_ = std::string("BagReader::apply_filter failed: ") + ex.what();
    } catch (...) {
        last_error_ = "BagReader::apply_filter failed: unknown exception";
    }
}

// static
std::string BagReader::detect_storage_id(const std::string& path)
{
    namespace fs = std::filesystem;
    const fs::path p(path);
    const std::string ext = p.extension().string();

    if (ext == ".db3") return "sqlite3";
    if (ext == ".mcap") return "mcap";

    // If it's a directory, look for metadata.yaml to detect format.
    if (fs::is_directory(p)) {
        // Default to sqlite3 for bag directories (most common Humble default).
        // rosbag2_cpp will detect the actual format from metadata.yaml.
        return "sqlite3";
    }

    // Fallback: let rosbag2 auto-detect.
    return "";
}

} // namespace spectra::adapters::ros2

#endif // SPECTRA_ROS2_BAG
