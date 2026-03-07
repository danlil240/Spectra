#include "tf/tf_buffer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace spectra::adapters::ros2
{

namespace
{
uint64_t steady_now_ns()
{
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
}

spectra::quat make_quat(double x, double y, double z, double w)
{
    return spectra::quat{
        static_cast<float>(x),
        static_cast<float>(y),
        static_cast<float>(z),
        static_cast<float>(w),
    };
}
}   // namespace

void TfFrameStats::push(uint64_t now_ns, uint64_t stale_threshold_ms)
{
    recv_timestamps_ns.push_back(now_ns);
    ever_received = true;
    stale         = false;
    (void)stale_threshold_ms;
}

void TfFrameStats::compute(uint64_t now_ns,
                           uint64_t stale_threshold_ms,
                           uint64_t hz_window_ns)
{
    const uint64_t window_start = now_ns > hz_window_ns ? now_ns - hz_window_ns : 0;
    while (!recv_timestamps_ns.empty() && recv_timestamps_ns.front() < window_start)
        recv_timestamps_ns.pop_front();

    const double window_s = static_cast<double>(hz_window_ns) / 1e9;
    hz = window_s > 0.0 ? static_cast<double>(recv_timestamps_ns.size()) / window_s : 0.0;

    if (ever_received)
    {
        const uint64_t last = last_transform.recv_ns;
        age_ms = now_ns >= last ? (now_ns - last) / 1'000'000ULL : 0ULL;
    }
    else
    {
        age_ms = 0;
    }

    stale = !is_static && ever_received && age_ms > stale_threshold_ms;
}

void TfBuffer::inject_transform(const TransformStamp& ts)
{
    const std::string parent_frame = normalize_frame_id(ts.parent_frame);
    const std::string child_frame  = normalize_frame_id(ts.child_frame);
    if (parent_frame.empty() || child_frame.empty() || parent_frame == child_frame)
        return;

    const uint64_t stamp_ns = ts.recv_ns != 0 ? ts.recv_ns : steady_now_ns();

    std::lock_guard<std::mutex> lock(mutex_);

    TimedTransform timed;
    timed.stamp_ns  = stamp_ns;
    timed.transform = stamp_to_transform(ts);
    timed.is_static = ts.is_static;

    auto& history = history_by_child_[child_frame];
    if (timed.is_static)
    {
        history.clear();
        history.push_back(timed);
    }
    else
    {
        const auto insert_it = std::upper_bound(
            history.begin(),
            history.end(),
            timed.stamp_ns,
            [](uint64_t stamp, const TimedTransform& item) { return stamp < item.stamp_ns; });
        history.insert(insert_it, timed);
        prune_history_unlocked(child_frame);
    }

    TransformStamp raw_stamp = ts;
    raw_stamp.parent_frame   = parent_frame;
    raw_stamp.child_frame    = child_frame;
    raw_stamp.recv_ns        = stamp_ns;

    auto& stats = frames_[child_frame];
    stats.frame_id         = child_frame;
    stats.parent_frame_id  = parent_frame;
    stats.is_static        = timed.is_static;
    stats.last_transform   = raw_stamp;
    stats.push(stamp_ns, stale_threshold_ms_);

    parent_of_[child_frame] = parent_frame;

    auto& parent_stats = frames_[parent_frame];
    parent_stats.frame_id = parent_frame;

    rebuild_tree_unlocked();
}

void TfBuffer::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    frames_.clear();
    parent_of_.clear();
    children_of_.clear();
    history_by_child_.clear();
}

void TfBuffer::set_stale_threshold_ms(uint64_t ms)
{
    std::lock_guard<std::mutex> lock(mutex_);
    stale_threshold_ms_ = ms;
}

uint64_t TfBuffer::stale_threshold_ms() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return stale_threshold_ms_;
}

void TfBuffer::set_hz_window_ms(uint64_t ms)
{
    std::lock_guard<std::mutex> lock(mutex_);
    hz_window_ms_ = ms;
}

uint64_t TfBuffer::hz_window_ms() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return hz_window_ms_;
}

void TfBuffer::set_cache_duration_s(double seconds)
{
    std::lock_guard<std::mutex> lock(mutex_);
    cache_duration_ns_ = seconds > 0.0
                             ? static_cast<uint64_t>(seconds * 1e9)
                             : 0ULL;
    for (const auto& [child_frame, _] : history_by_child_)
        prune_history_unlocked(child_frame);
}

double TfBuffer::cache_duration_s() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<double>(cache_duration_ns_) / 1e9;
}

size_t TfBuffer::frame_count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return frames_.size();
}

bool TfBuffer::has_frame(const std::string& frame_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return frames_.find(normalize_frame_id(frame_id)) != frames_.end();
}

TfTreeSnapshot TfBuffer::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    TfTreeSnapshot snapshot;
    snapshot.snapshot_ns = steady_now_ns();
    const uint64_t hz_window_ns = hz_window_ms_ * 1'000'000ULL;

    for (const auto& [_, stats] : frames_)
    {
        TfFrameStats copy = stats;
        copy.compute(snapshot.snapshot_ns, stale_threshold_ms_, hz_window_ns);
        snapshot.frames.push_back(std::move(copy));
    }

    std::sort(snapshot.frames.begin(),
              snapshot.frames.end(),
              [](const TfFrameStats& a, const TfFrameStats& b)
              {
                  return a.frame_id < b.frame_id;
              });

    for (const auto& frame : snapshot.frames)
    {
        if (!frame.parent_frame_id.empty())
            snapshot.children[frame.parent_frame_id].push_back(frame.frame_id);
    }
    for (auto& [_, children] : snapshot.children)
        std::sort(children.begin(), children.end());

    for (const auto& frame : snapshot.frames)
    {
        const bool has_parent = !frame.parent_frame_id.empty()
                             && frames_.find(frame.parent_frame_id) != frames_.end();
        if (!has_parent)
            snapshot.roots.push_back(frame.frame_id);

        if (frame.is_static)
            ++snapshot.static_frames;
        else
            ++snapshot.dynamic_frames;
        if (frame.stale)
            ++snapshot.stale_frames;
    }

    std::sort(snapshot.roots.begin(), snapshot.roots.end());
    snapshot.total_frames = static_cast<uint32_t>(snapshot.frames.size());
    return snapshot;
}

TransformResult TfBuffer::lookup_transform(const std::string& source_frame,
                                           const std::string& target_frame,
                                           uint64_t lookup_time_ns) const
{
    TransformResult result;

    const std::string source = normalize_frame_id(source_frame);
    const std::string target = normalize_frame_id(target_frame);
    if (source.empty() || target.empty())
    {
        result.error = "source and target frames must be non-empty";
        return result;
    }

    if (source == target)
    {
        result.ok = true;
        result.qw = 1.0;
        return result;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (frames_.find(source) == frames_.end())
    {
        result.error = "Unknown source frame: " + source;
        return result;
    }
    if (frames_.find(target) == frames_.end())
    {
        result.error = "Unknown target frame: " + target;
        return result;
    }

    const auto source_chain = chain_to_root_unlocked(source);
    const auto target_chain = chain_to_root_unlocked(target);

    std::unordered_map<std::string, size_t> source_index;
    for (size_t i = 0; i < source_chain.size(); ++i)
        source_index[source_chain[i]] = i;

    std::string lca;
    size_t target_lca_index = 0;
    for (size_t i = 0; i < target_chain.size(); ++i)
    {
        if (source_index.find(target_chain[i]) != source_index.end())
        {
            lca              = target_chain[i];
            target_lca_index = i;
            break;
        }
    }

    if (lca.empty())
    {
        result.error = "No common ancestor between '" + source + "' and '" + target + "'";
        return result;
    }

    const size_t source_lca_index = source_index[lca];
    spectra::Transform source_to_lca;
    for (size_t i = 0; i < source_lca_index; ++i)
    {
        const std::string& child_frame = source_chain[i];
        const auto sampled = sample_edge_unlocked(child_frame, lookup_time_ns);
        if (!sampled.has_value())
        {
            result.error = "No transform sample available for '" + child_frame + "'";
            return result;
        }
        source_to_lca = source_to_lca.compose(sampled->transform.inverse());
    }

    spectra::Transform lca_to_target;
    for (size_t i = target_lca_index; i > 0; --i)
    {
        const std::string& child_frame = target_chain[i - 1];
        const auto sampled = sample_edge_unlocked(child_frame, lookup_time_ns);
        if (!sampled.has_value())
        {
            result.error = "No transform sample available for '" + child_frame + "'";
            return result;
        }
        lca_to_target = lca_to_target.compose(sampled->transform);
    }

    return to_result(source_to_lca.compose(lca_to_target));
}

bool TfBuffer::can_transform(const std::string& source_frame,
                             const std::string& target_frame,
                             uint64_t lookup_time_ns) const
{
    return lookup_transform(source_frame, target_frame, lookup_time_ns).ok;
}

std::vector<std::string> TfBuffer::all_frames() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    names.reserve(frames_.size());
    for (const auto& [name, _] : frames_)
        names.push_back(name);
    std::sort(names.begin(), names.end());
    return names;
}

void TfBuffer::rebuild_tree_unlocked()
{
    children_of_.clear();
    for (const auto& [child_frame, parent_frame] : parent_of_)
        children_of_[parent_frame].push_back(child_frame);

    for (auto& [_, children] : children_of_)
        std::sort(children.begin(), children.end());
}

void TfBuffer::prune_history_unlocked(const std::string& child_frame)
{
    if (cache_duration_ns_ == 0)
        return;

    auto it = history_by_child_.find(child_frame);
    if (it == history_by_child_.end() || it->second.empty() || it->second.back().is_static)
        return;

    const uint64_t newest  = it->second.back().stamp_ns;
    const uint64_t cutoff  = newest > cache_duration_ns_ ? newest - cache_duration_ns_ : 0;
    while (it->second.size() > 1 && it->second.front().stamp_ns < cutoff)
        it->second.pop_front();
}

std::vector<std::string> TfBuffer::chain_to_root_unlocked(const std::string& frame) const
{
    std::vector<std::string> chain;
    std::string current = frame;
    const size_t max_depth = frames_.size() + 1;
    while (!current.empty() && chain.size() < max_depth)
    {
        chain.push_back(current);
        const auto it = parent_of_.find(current);
        if (it == parent_of_.end())
            break;
        current = it->second;
    }
    return chain;
}

std::optional<TfBuffer::TimedTransform> TfBuffer::sample_edge_unlocked(const std::string& child_frame,
                                                                       uint64_t lookup_time_ns) const
{
    const auto it = history_by_child_.find(child_frame);
    if (it == history_by_child_.end() || it->second.empty())
        return std::nullopt;

    const auto& history = it->second;
    if (lookup_time_ns == 0 || history.size() == 1 || history.back().is_static)
        return history.back();

    if (lookup_time_ns < history.front().stamp_ns || lookup_time_ns > history.back().stamp_ns)
        return std::nullopt;

    const auto upper = std::lower_bound(
        history.begin(),
        history.end(),
        lookup_time_ns,
        [](const TimedTransform& item, uint64_t stamp) { return item.stamp_ns < stamp; });

    if (upper == history.begin())
        return *upper;
    if (upper == history.end())
        return history.back();
    if (upper->stamp_ns == lookup_time_ns)
        return *upper;

    const auto& before = *(upper - 1);
    const auto& after  = *upper;
    const uint64_t span_ns = after.stamp_ns - before.stamp_ns;
    if (span_ns == 0)
        return after;

    const float t = static_cast<float>(
        static_cast<double>(lookup_time_ns - before.stamp_ns)
        / static_cast<double>(span_ns));

    TimedTransform interpolated;
    interpolated.stamp_ns  = lookup_time_ns;
    interpolated.is_static = false;
    interpolated.transform = spectra::Transform::lerp(before.transform, after.transform, t);
    return interpolated;
}

TransformStamp TfBuffer::timed_to_stamp(const std::string& parent_frame,
                                        const std::string& child_frame,
                                        const TimedTransform& timed)
{
    TransformStamp stamp;
    stamp.parent_frame = parent_frame;
    stamp.child_frame  = child_frame;
    stamp.tx           = timed.transform.translation.x;
    stamp.ty           = timed.transform.translation.y;
    stamp.tz           = timed.transform.translation.z;
    stamp.qx           = timed.transform.rotation.x;
    stamp.qy           = timed.transform.rotation.y;
    stamp.qz           = timed.transform.rotation.z;
    stamp.qw           = timed.transform.rotation.w;
    stamp.recv_ns      = timed.stamp_ns;
    stamp.is_static    = timed.is_static;
    return stamp;
}

TransformResult TfBuffer::to_result(const spectra::Transform& transform)
{
    TransformResult result;
    result.ok = true;
    result.tx = transform.translation.x;
    result.ty = transform.translation.y;
    result.tz = transform.translation.z;
    result.qx = transform.rotation.x;
    result.qy = transform.rotation.y;
    result.qz = transform.rotation.z;
    result.qw = transform.rotation.w;
    quat_to_euler_deg(result.qx,
                      result.qy,
                      result.qz,
                      result.qw,
                      result.roll_deg,
                      result.pitch_deg,
                      result.yaw_deg);
    return result;
}

spectra::Transform TfBuffer::stamp_to_transform(const TransformStamp& stamp)
{
    return {
        spectra::vec3{stamp.tx, stamp.ty, stamp.tz},
        spectra::quat_normalize(make_quat(stamp.qx, stamp.qy, stamp.qz, stamp.qw)),
    };
}

std::string TfBuffer::normalize_frame_id(std::string_view frame_id)
{
    if (!frame_id.empty() && frame_id.front() == '/')
        frame_id.remove_prefix(1);
    return std::string(frame_id);
}

void TfBuffer::quat_to_euler_deg(double qx,
                                 double qy,
                                 double qz,
                                 double qw,
                                 double& roll,
                                 double& pitch,
                                 double& yaw)
{
    constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;

    const double sinr_cosp = 2.0 * (qw * qx + qy * qz);
    const double cosr_cosp = 1.0 - 2.0 * (qx * qx + qy * qy);
    roll = std::atan2(sinr_cosp, cosr_cosp) * kRadToDeg;

    const double sinp = 2.0 * (qw * qy - qz * qx);
    pitch = std::fabs(sinp) >= 1.0 ? std::copysign(90.0, sinp) : std::asin(sinp) * kRadToDeg;

    const double siny_cosp = 2.0 * (qw * qz + qx * qy);
    const double cosy_cosp = 1.0 - 2.0 * (qy * qy + qz * qz);
    yaw = std::atan2(siny_cosp, cosy_cosp) * kRadToDeg;
}

}   // namespace spectra::adapters::ros2
