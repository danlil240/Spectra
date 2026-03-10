#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string_view>
#include <string>
#include <unordered_map>
#include <vector>

#include <spectra/math3d.hpp>

namespace spectra::adapters::ros2
{

struct TransformStamp
{
    std::string parent_frame;
    std::string child_frame;

    double tx{0.0};
    double ty{0.0};
    double tz{0.0};

    double qx{0.0};
    double qy{0.0};
    double qz{0.0};
    double qw{1.0};

    uint64_t recv_ns{0};
    bool     is_static{false};
};

struct TfFrameStats
{
    std::string          frame_id;
    std::string          parent_frame_id;
    bool                 is_static{false};
    TransformStamp       last_transform;
    std::deque<uint64_t> recv_timestamps_ns;
    double               hz{0.0};
    uint64_t             age_ms{0};
    bool                 stale{false};
    bool                 ever_received{false};

    void push(uint64_t now_ns, uint64_t stale_threshold_ms);
    void compute(uint64_t now_ns,
                 uint64_t stale_threshold_ms,
                 uint64_t hz_window_ns = 1'000'000'000ULL);
};

struct TfTreeSnapshot
{
    std::vector<TfFrameStats>                                 frames;
    std::vector<std::string>                                  roots;
    std::unordered_map<std::string, std::vector<std::string>> children;
    uint64_t                                                  snapshot_ns{0};
    uint32_t                                                  total_frames{0};
    uint32_t                                                  static_frames{0};
    uint32_t                                                  dynamic_frames{0};
    uint32_t                                                  stale_frames{0};
};

struct TransformResult
{
    bool        ok{false};
    double      tx{0.0};
    double      ty{0.0};
    double      tz{0.0};
    double      qx{0.0};
    double      qy{0.0};
    double      qz{0.0};
    double      qw{1.0};
    std::string error;
    double      roll_deg{0.0};
    double      pitch_deg{0.0};
    double      yaw_deg{0.0};
};

class TfBuffer
{
   public:
    TfBuffer() = default;

    void inject_transform(const TransformStamp& ts);
    void clear();

    void     set_stale_threshold_ms(uint64_t ms);
    uint64_t stale_threshold_ms() const;

    void     set_hz_window_ms(uint64_t ms);
    uint64_t hz_window_ms() const;

    void   set_cache_duration_s(double seconds);
    double cache_duration_s() const;

    size_t frame_count() const;
    bool   has_frame(const std::string& frame_id) const;

    TfTreeSnapshot           snapshot() const;
    TransformResult          lookup_transform(const std::string& source_frame,
                                              const std::string& target_frame,
                                              uint64_t           lookup_time_ns = 0) const;
    bool                     can_transform(const std::string& source_frame,
                                           const std::string& target_frame,
                                           uint64_t           lookup_time_ns = 0) const;
    std::vector<std::string> all_frames() const;

   private:
    struct TimedTransform
    {
        uint64_t           stamp_ns{0};
        spectra::Transform transform;
        bool               is_static{false};
    };

    void                          rebuild_tree_unlocked();
    void                          prune_history_unlocked(const std::string& child_frame);
    std::vector<std::string>      chain_to_root_unlocked(const std::string& frame) const;
    std::optional<TimedTransform> sample_edge_unlocked(const std::string& child_frame,
                                                       uint64_t           lookup_time_ns) const;

    static TransformStamp     timed_to_stamp(const std::string&    parent_frame,
                                             const std::string&    child_frame,
                                             const TimedTransform& timed);
    static TransformResult    to_result(const spectra::Transform& transform);
    static spectra::Transform stamp_to_transform(const TransformStamp& stamp);
    static std::string        normalize_frame_id(std::string_view frame_id);
    static void               quat_to_euler_deg(double  qx,
                                                double  qy,
                                                double  qz,
                                                double  qw,
                                                double& roll,
                                                double& pitch,
                                                double& yaw);

    mutable std::mutex                                          mutex_;
    std::unordered_map<std::string, TfFrameStats>               frames_;
    std::unordered_map<std::string, std::string>                parent_of_;
    std::unordered_map<std::string, std::vector<std::string>>   children_of_;
    std::unordered_map<std::string, std::deque<TimedTransform>> history_by_child_;
    uint64_t                                                    stale_threshold_ms_{500};
    uint64_t                                                    hz_window_ms_{1000};
    uint64_t cache_duration_ns_{10'000'000'000ULL};
};

}   // namespace spectra::adapters::ros2
