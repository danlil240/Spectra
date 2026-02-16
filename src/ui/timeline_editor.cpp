#include "ui/timeline_editor.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "ui/camera_animator.hpp"
#include "ui/keyframe_interpolator.hpp"

#ifdef PLOTIX_USE_IMGUI
    #include "imgui.h"
#endif

namespace plotix
{

TimelineEditor::TimelineEditor()
{
    view_end_ = duration_;
}

// ─── Playback ────────────────────────────────────────────────────────────────

void TimelineEditor::play()
{
    std::lock_guard lock(mutex_);
    if (state_ == PlaybackState::Stopped)
    {
        playhead_ = 0.0f;
        ping_pong_dir_ = 1;
    }
    state_ = PlaybackState::Playing;
    fire_playback_change();
}

void TimelineEditor::pause()
{
    std::lock_guard lock(mutex_);
    if (state_ == PlaybackState::Playing || state_ == PlaybackState::Recording)
    {
        state_ = PlaybackState::Paused;
        fire_playback_change();
    }
}

void TimelineEditor::stop()
{
    std::lock_guard lock(mutex_);
    state_ = PlaybackState::Stopped;
    playhead_ = 0.0f;
    ping_pong_dir_ = 1;
    fire_playback_change();
}

void TimelineEditor::toggle_play()
{
    std::lock_guard lock(mutex_);
    if (state_ == PlaybackState::Playing)
    {
        state_ = PlaybackState::Paused;
    }
    else
    {
        if (state_ == PlaybackState::Stopped)
        {
            playhead_ = 0.0f;
            ping_pong_dir_ = 1;
        }
        state_ = PlaybackState::Playing;
    }
    fire_playback_change();
}

PlaybackState TimelineEditor::playback_state() const
{
    std::lock_guard lock(mutex_);
    return state_;
}

bool TimelineEditor::is_playing() const
{
    std::lock_guard lock(mutex_);
    return state_ == PlaybackState::Playing;
}

bool TimelineEditor::is_recording() const
{
    std::lock_guard lock(mutex_);
    return state_ == PlaybackState::Recording;
}

// ─── Playhead ────────────────────────────────────────────────────────────────

float TimelineEditor::playhead() const
{
    std::lock_guard lock(mutex_);
    return playhead_;
}

void TimelineEditor::set_playhead(float time)
{
    std::lock_guard lock(mutex_);
    playhead_ = time;
    clamp_playhead();
}

bool TimelineEditor::advance(float dt)
{
    std::lock_guard lock(mutex_);
    if (state_ != PlaybackState::Playing && state_ != PlaybackState::Recording)
    {
        return false;
    }

    float loop_end = effective_loop_out();
    float loop_start = has_loop_region_ ? loop_in_ : 0.0f;

    if (loop_mode_ == LoopMode::PingPong)
    {
        playhead_ += dt * static_cast<float>(ping_pong_dir_);

        if (playhead_ >= loop_end)
        {
            playhead_ = loop_end - (playhead_ - loop_end);
            ping_pong_dir_ = -1;
        }
        else if (playhead_ <= loop_start)
        {
            playhead_ = loop_start + (loop_start - playhead_);
            ping_pong_dir_ = 1;
        }
        clamp_playhead();
        if (interpolator_)
            interpolator_->evaluate(playhead_);
        return true;
    }

    playhead_ += dt;

    if (playhead_ >= loop_end)
    {
        if (loop_mode_ == LoopMode::Loop)
        {
            float overshoot = playhead_ - loop_end;
            playhead_ = loop_start + std::fmod(overshoot, loop_end - loop_start);
            clamp_playhead();
            if (interpolator_)
                interpolator_->evaluate(playhead_);
            return true;
        }
        // LoopMode::None — stop at end
        playhead_ = loop_end;
        if (interpolator_)
            interpolator_->evaluate(playhead_);
        state_ = PlaybackState::Stopped;
        fire_playback_change();
        return false;
    }

    if (interpolator_)
        interpolator_->evaluate(playhead_);
    return true;
}

void TimelineEditor::scrub_to(float time)
{
    std::lock_guard lock(mutex_);
    playhead_ = time;
    clamp_playhead();
    if (on_scrub_)
    {
        on_scrub_(playhead_);
    }
}

void TimelineEditor::step_forward()
{
    std::lock_guard lock(mutex_);
    if (fps_ <= 0.0f)
        return;
    float frame_dur = 1.0f / fps_;
    playhead_ += frame_dur;
    clamp_playhead();
}

void TimelineEditor::step_backward()
{
    std::lock_guard lock(mutex_);
    if (fps_ <= 0.0f)
        return;
    float frame_dur = 1.0f / fps_;
    playhead_ -= frame_dur;
    clamp_playhead();
}

// ─── Duration & FPS ──────────────────────────────────────────────────────────

float TimelineEditor::duration() const
{
    std::lock_guard lock(mutex_);
    return duration_;
}

void TimelineEditor::set_duration(float seconds)
{
    std::lock_guard lock(mutex_);
    duration_ = std::max(0.0f, seconds);
    if (!has_loop_region_)
    {
        view_end_ = duration_;
    }
    clamp_playhead();
}

float TimelineEditor::fps() const
{
    std::lock_guard lock(mutex_);
    return fps_;
}

void TimelineEditor::set_fps(float target_fps)
{
    std::lock_guard lock(mutex_);
    fps_ = std::max(1.0f, target_fps);
}

uint32_t TimelineEditor::frame_count() const
{
    std::lock_guard lock(mutex_);
    return static_cast<uint32_t>(std::ceil(duration_ * fps_));
}

uint32_t TimelineEditor::current_frame() const
{
    std::lock_guard lock(mutex_);
    return static_cast<uint32_t>(std::floor(playhead_ * fps_));
}

float TimelineEditor::frame_to_time(uint32_t frame) const
{
    std::lock_guard lock(mutex_);
    if (fps_ <= 0.0f)
        return 0.0f;
    return static_cast<float>(frame) / fps_;
}

uint32_t TimelineEditor::time_to_frame(float time) const
{
    std::lock_guard lock(mutex_);
    return static_cast<uint32_t>(std::floor(time * fps_));
}

// ─── Loop ────────────────────────────────────────────────────────────────────

LoopMode TimelineEditor::loop_mode() const
{
    std::lock_guard lock(mutex_);
    return loop_mode_;
}

void TimelineEditor::set_loop_mode(LoopMode mode)
{
    std::lock_guard lock(mutex_);
    loop_mode_ = mode;
    if (mode != LoopMode::PingPong)
    {
        ping_pong_dir_ = 1;
    }
}

float TimelineEditor::loop_in() const
{
    std::lock_guard lock(mutex_);
    return has_loop_region_ ? loop_in_ : 0.0f;
}

float TimelineEditor::loop_out() const
{
    std::lock_guard lock(mutex_);
    return effective_loop_out();
}

void TimelineEditor::set_loop_region(float in, float out)
{
    std::lock_guard lock(mutex_);
    loop_in_ = std::max(0.0f, in);
    loop_out_ = std::min(out, duration_);
    if (loop_out_ <= loop_in_)
    {
        loop_out_ = loop_in_ + 0.001f;
    }
    has_loop_region_ = true;
}

void TimelineEditor::clear_loop_region()
{
    std::lock_guard lock(mutex_);
    has_loop_region_ = false;
    loop_in_ = 0.0f;
    loop_out_ = 0.0f;
}

// ─── Snap ────────────────────────────────────────────────────────────────────

SnapMode TimelineEditor::snap_mode() const
{
    std::lock_guard lock(mutex_);
    return snap_mode_;
}

void TimelineEditor::set_snap_mode(SnapMode mode)
{
    std::lock_guard lock(mutex_);
    snap_mode_ = mode;
}

float TimelineEditor::snap_interval() const
{
    std::lock_guard lock(mutex_);
    return snap_interval_;
}

void TimelineEditor::set_snap_interval(float interval)
{
    std::lock_guard lock(mutex_);
    snap_interval_ = std::max(0.001f, interval);
}

float TimelineEditor::snap_time(float time) const
{
    std::lock_guard lock(mutex_);
    switch (snap_mode_)
    {
        case SnapMode::Frame:
        {
            if (fps_ <= 0.0f)
                return time;
            float frame_dur = 1.0f / fps_;
            return std::round(time / frame_dur) * frame_dur;
        }
        case SnapMode::Beat:
        {
            if (snap_interval_ <= 0.0f)
                return time;
            return std::round(time / snap_interval_) * snap_interval_;
        }
        case SnapMode::None:
        default:
            return time;
    }
}

// ─── Tracks ──────────────────────────────────────────────────────────────────

uint32_t TimelineEditor::add_track(const std::string& name, Color color)
{
    std::lock_guard lock(mutex_);
    uint32_t id = next_track_id_++;
    TimelineTrack track;
    track.id = id;
    track.name = name;
    track.color = color;
    tracks_.push_back(std::move(track));
    return id;
}

void TimelineEditor::remove_track(uint32_t track_id)
{
    std::lock_guard lock(mutex_);
    std::erase_if(tracks_, [track_id](const TimelineTrack& t) { return t.id == track_id; });
}

void TimelineEditor::rename_track(uint32_t track_id, const std::string& name)
{
    std::lock_guard lock(mutex_);
    for (auto& t : tracks_)
    {
        if (t.id == track_id)
        {
            t.name = name;
            return;
        }
    }
}

TimelineTrack* TimelineEditor::get_track(uint32_t track_id)
{
    std::lock_guard lock(mutex_);
    for (auto& t : tracks_)
    {
        if (t.id == track_id)
            return &t;
    }
    return nullptr;
}

const TimelineTrack* TimelineEditor::get_track(uint32_t track_id) const
{
    std::lock_guard lock(mutex_);
    for (auto& t : tracks_)
    {
        if (t.id == track_id)
            return &t;
    }
    return nullptr;
}

const std::vector<TimelineTrack>& TimelineEditor::tracks() const
{
    std::lock_guard lock(mutex_);
    return tracks_;
}

size_t TimelineEditor::track_count() const
{
    std::lock_guard lock(mutex_);
    return tracks_.size();
}

void TimelineEditor::set_track_visible(uint32_t track_id, bool visible)
{
    std::lock_guard lock(mutex_);
    for (auto& t : tracks_)
    {
        if (t.id == track_id)
        {
            t.visible = visible;
            return;
        }
    }
}

void TimelineEditor::set_track_locked(uint32_t track_id, bool locked)
{
    std::lock_guard lock(mutex_);
    for (auto& t : tracks_)
    {
        if (t.id == track_id)
        {
            t.locked = locked;
            return;
        }
    }
}

// ─── Keyframes ───────────────────────────────────────────────────────────────

void TimelineEditor::add_keyframe(uint32_t track_id, float time)
{
    std::lock_guard lock(mutex_);
    for (auto& t : tracks_)
    {
        if (t.id == track_id)
        {
            if (t.locked)
                return;

            // Check for duplicate (within tolerance)
            for (const auto& kf : t.keyframes)
            {
                if (std::abs(kf.time - time) < 0.001f)
                    return;
            }

            KeyframeMarker kf;
            kf.time = time;
            kf.track_id = track_id;
            t.keyframes.push_back(kf);

            // Keep sorted
            std::sort(t.keyframes.begin(),
                      t.keyframes.end(),
                      [](const KeyframeMarker& a, const KeyframeMarker& b)
                      { return a.time < b.time; });

            if (on_keyframe_added_)
            {
                on_keyframe_added_(track_id, time);
            }
            return;
        }
    }
}

void TimelineEditor::remove_keyframe(uint32_t track_id, float time)
{
    std::lock_guard lock(mutex_);
    for (auto& t : tracks_)
    {
        if (t.id == track_id)
        {
            if (t.locked)
                return;

            auto it = std::find_if(t.keyframes.begin(),
                                   t.keyframes.end(),
                                   [time](const KeyframeMarker& kf)
                                   { return std::abs(kf.time - time) < 0.001f; });
            if (it != t.keyframes.end())
            {
                t.keyframes.erase(it);
                if (on_keyframe_removed_)
                {
                    on_keyframe_removed_(track_id, time);
                }
            }
            return;
        }
    }
}

void TimelineEditor::move_keyframe(uint32_t track_id, float old_time, float new_time)
{
    std::lock_guard lock(mutex_);
    for (auto& t : tracks_)
    {
        if (t.id == track_id)
        {
            if (t.locked)
                return;

            auto it = std::find_if(t.keyframes.begin(),
                                   t.keyframes.end(),
                                   [old_time](const KeyframeMarker& kf)
                                   { return std::abs(kf.time - old_time) < 0.001f; });
            if (it != t.keyframes.end())
            {
                it->time = std::clamp(new_time, 0.0f, duration_);
                std::sort(t.keyframes.begin(),
                          t.keyframes.end(),
                          [](const KeyframeMarker& a, const KeyframeMarker& b)
                          { return a.time < b.time; });
            }
            return;
        }
    }
}

void TimelineEditor::clear_keyframes(uint32_t track_id)
{
    std::lock_guard lock(mutex_);
    for (auto& t : tracks_)
    {
        if (t.id == track_id)
        {
            t.keyframes.clear();
            return;
        }
    }
}

size_t TimelineEditor::total_keyframe_count() const
{
    std::lock_guard lock(mutex_);
    size_t count = 0;
    for (const auto& t : tracks_)
    {
        count += t.keyframes.size();
    }
    return count;
}

// ─── Selection ───────────────────────────────────────────────────────────────

void TimelineEditor::select_keyframe(uint32_t track_id, float time)
{
    std::lock_guard lock(mutex_);
    auto* kf = find_keyframe(track_id, time);
    if (kf)
    {
        kf->selected = true;
        fire_selection_change();
    }
}

void TimelineEditor::deselect_keyframe(uint32_t track_id, float time)
{
    std::lock_guard lock(mutex_);
    auto* kf = find_keyframe(track_id, time);
    if (kf)
    {
        kf->selected = false;
        fire_selection_change();
    }
}

void TimelineEditor::select_all_keyframes()
{
    std::lock_guard lock(mutex_);
    for (auto& t : tracks_)
    {
        for (auto& kf : t.keyframes)
        {
            kf.selected = true;
        }
    }
    fire_selection_change();
}

void TimelineEditor::deselect_all()
{
    std::lock_guard lock(mutex_);
    for (auto& t : tracks_)
    {
        for (auto& kf : t.keyframes)
        {
            kf.selected = false;
        }
    }
    fire_selection_change();
}

void TimelineEditor::select_keyframes_in_range(float t_min, float t_max)
{
    std::lock_guard lock(mutex_);
    for (auto& t : tracks_)
    {
        for (auto& kf : t.keyframes)
        {
            if (kf.time >= t_min && kf.time <= t_max)
            {
                kf.selected = true;
            }
        }
    }
    fire_selection_change();
}

std::vector<KeyframeMarker*> TimelineEditor::selected_keyframes()
{
    std::lock_guard lock(mutex_);
    std::vector<KeyframeMarker*> result;
    for (auto& t : tracks_)
    {
        for (auto& kf : t.keyframes)
        {
            if (kf.selected)
            {
                result.push_back(&kf);
            }
        }
    }
    return result;
}

size_t TimelineEditor::selected_count() const
{
    std::lock_guard lock(mutex_);
    size_t count = 0;
    for (const auto& t : tracks_)
    {
        for (const auto& kf : t.keyframes)
        {
            if (kf.selected)
                ++count;
        }
    }
    return count;
}

void TimelineEditor::delete_selected()
{
    std::lock_guard lock(mutex_);
    for (auto& t : tracks_)
    {
        if (t.locked)
            continue;
        std::erase_if(t.keyframes, [](const KeyframeMarker& kf) { return kf.selected; });
    }
    fire_selection_change();
}

// ─── Zoom & Scroll ───────────────────────────────────────────────────────────

float TimelineEditor::view_start() const
{
    std::lock_guard lock(mutex_);
    return view_start_;
}

float TimelineEditor::view_end() const
{
    std::lock_guard lock(mutex_);
    return view_end_;
}

void TimelineEditor::set_view_range(float start, float end)
{
    std::lock_guard lock(mutex_);
    view_start_ = std::max(0.0f, start);
    view_end_ = std::max(view_start_ + 0.01f, end);
}

float TimelineEditor::zoom() const
{
    std::lock_guard lock(mutex_);
    return zoom_;
}

void TimelineEditor::set_zoom(float pixels_per_second)
{
    std::lock_guard lock(mutex_);
    zoom_ = std::clamp(pixels_per_second, 10.0f, 10000.0f);
}

void TimelineEditor::zoom_in()
{
    std::lock_guard lock(mutex_);
    zoom_ = std::min(zoom_ * 1.25f, 10000.0f);
    // Narrow view range around playhead
    float center = playhead_;
    float half_range = (view_end_ - view_start_) * 0.5f / 1.25f;
    view_start_ = std::max(0.0f, center - half_range);
    view_end_ = center + half_range;
}

void TimelineEditor::zoom_out()
{
    std::lock_guard lock(mutex_);
    zoom_ = std::max(zoom_ / 1.25f, 10.0f);
    float center = (view_start_ + view_end_) * 0.5f;
    float half_range = (view_end_ - view_start_) * 0.5f * 1.25f;
    view_start_ = std::max(0.0f, center - half_range);
    view_end_ = center + half_range;
}

void TimelineEditor::scroll_to_playhead()
{
    std::lock_guard lock(mutex_);
    float range = view_end_ - view_start_;
    view_start_ = std::max(0.0f, playhead_ - range * 0.5f);
    view_end_ = view_start_ + range;
}

// ─── Callbacks ───────────────────────────────────────────────────────────────

void TimelineEditor::set_on_playback_change(PlaybackCallback cb)
{
    std::lock_guard lock(mutex_);
    on_playback_change_ = std::move(cb);
}

void TimelineEditor::set_on_scrub(ScrubCallback cb)
{
    std::lock_guard lock(mutex_);
    on_scrub_ = std::move(cb);
}

void TimelineEditor::set_on_keyframe_added(KeyframeCallback cb)
{
    std::lock_guard lock(mutex_);
    on_keyframe_added_ = std::move(cb);
}

void TimelineEditor::set_on_keyframe_removed(KeyframeCallback cb)
{
    std::lock_guard lock(mutex_);
    on_keyframe_removed_ = std::move(cb);
}

void TimelineEditor::set_on_selection_change(SelectionCallback cb)
{
    std::lock_guard lock(mutex_);
    on_selection_change_ = std::move(cb);
}

// ─── Internal helpers ────────────────────────────────────────────────────────

float TimelineEditor::effective_loop_out() const
{
    // Caller must hold mutex_
    if (has_loop_region_ && loop_out_ > loop_in_)
    {
        return loop_out_;
    }
    return duration_;
}

void TimelineEditor::clamp_playhead()
{
    // Caller must hold mutex_
    playhead_ = std::clamp(playhead_, 0.0f, duration_);
}

void TimelineEditor::fire_playback_change()
{
    // Caller must hold mutex_
    if (on_playback_change_)
    {
        on_playback_change_(state_);
    }
}

void TimelineEditor::fire_selection_change()
{
    // Caller must hold mutex_
    if (on_selection_change_)
    {
        std::vector<KeyframeMarker*> sel;
        for (auto& t : tracks_)
        {
            for (auto& kf : t.keyframes)
            {
                if (kf.selected)
                    sel.push_back(&kf);
            }
        }
        on_selection_change_(sel);
    }
}

KeyframeMarker* TimelineEditor::find_keyframe(uint32_t track_id, float time, float tolerance)
{
    // Caller must hold mutex_
    for (auto& t : tracks_)
    {
        if (t.id == track_id)
        {
            for (auto& kf : t.keyframes)
            {
                if (std::abs(kf.time - time) < tolerance)
                {
                    return &kf;
                }
            }
            return nullptr;
        }
    }
    return nullptr;
}

// ─── KeyframeInterpolator integration (Week 11) ─────────────────────────────

void TimelineEditor::set_interpolator(KeyframeInterpolator* interp)
{
    std::lock_guard lock(mutex_);
    interpolator_ = interp;
}

KeyframeInterpolator* TimelineEditor::interpolator() const
{
    std::lock_guard lock(mutex_);
    return interpolator_;
}

void TimelineEditor::set_camera_animator(CameraAnimator* anim)
{
    std::lock_guard lock(mutex_);
    camera_animator_ = anim;
}

CameraAnimator* TimelineEditor::camera_animator() const
{
    std::lock_guard lock(mutex_);
    return camera_animator_;
}

void TimelineEditor::evaluate_at_playhead()
{
    std::lock_guard lock(mutex_);
    if (interpolator_)
    {
        interpolator_->evaluate(playhead_);
    }
    if (camera_animator_)
    {
        camera_animator_->evaluate_at(playhead_);
    }
}

uint32_t TimelineEditor::add_animated_track(const std::string& name,
                                            float default_value,
                                            Color color)
{
    std::lock_guard lock(mutex_);

    // Add the visual track
    uint32_t id = next_track_id_++;
    TimelineTrack track;
    track.id = id;
    track.name = name;
    track.color = color;
    tracks_.push_back(std::move(track));

    // Add a matching interpolator channel with the same ID
    if (interpolator_)
    {
        // We need to ensure the channel ID matches the track ID.
        // The interpolator assigns IDs sequentially, so we add and track the mapping.
        // For simplicity, we use add_channel which returns its own ID.
        // The track_id and channel_id may differ — we store the mapping in the track name.
        interpolator_->add_channel(name, default_value);
    }

    return id;
}

void TimelineEditor::add_animated_keyframe(uint32_t track_id,
                                           float time,
                                           float value,
                                           int interp_mode)
{
    std::lock_guard lock(mutex_);

    // Add visual keyframe marker to the track
    for (auto& t : tracks_)
    {
        if (t.id == track_id)
        {
            if (t.locked)
                return;

            // Check for duplicate
            for (const auto& kf : t.keyframes)
            {
                if (std::abs(kf.time - time) < 0.001f)
                {
                    // Update existing — just update the interpolator channel
                    if (interpolator_)
                    {
                        TypedKeyframe tkf(time, value, static_cast<InterpMode>(interp_mode));
                        interpolator_->add_keyframe(track_id, tkf);
                    }
                    return;
                }
            }

            KeyframeMarker kf;
            kf.time = time;
            kf.track_id = track_id;
            t.keyframes.push_back(kf);

            std::sort(t.keyframes.begin(),
                      t.keyframes.end(),
                      [](const KeyframeMarker& a, const KeyframeMarker& b)
                      { return a.time < b.time; });

            if (on_keyframe_added_)
            {
                on_keyframe_added_(track_id, time);
            }
            break;
        }
    }

    // Add typed keyframe to the interpolator channel
    if (interpolator_)
    {
        TypedKeyframe tkf(time, value, static_cast<InterpMode>(interp_mode));
        interpolator_->add_keyframe(track_id, tkf);
    }
}

std::string TimelineEditor::serialize() const
{
    std::lock_guard lock(mutex_);
    std::ostringstream ss;
    ss << "{\"duration\":" << duration_ << ",\"fps\":" << fps_
       << ",\"loop_mode\":" << static_cast<int>(loop_mode_)
       << ",\"snap_mode\":" << static_cast<int>(snap_mode_)
       << ",\"snap_interval\":" << snap_interval_;

    if (has_loop_region_)
    {
        ss << ",\"loop_in\":" << loop_in_ << ",\"loop_out\":" << loop_out_;
    }

    // Serialize tracks
    ss << ",\"tracks\":[";
    for (size_t i = 0; i < tracks_.size(); ++i)
    {
        if (i > 0)
            ss << ",";
        const auto& t = tracks_[i];
        ss << "{\"id\":" << t.id << ",\"name\":\"" << t.name << "\""
           << ",\"color\":[" << t.color.r << "," << t.color.g << "," << t.color.b << ","
           << t.color.a << "]"
           << ",\"visible\":" << (t.visible ? "true" : "false")
           << ",\"locked\":" << (t.locked ? "true" : "false") << ",\"keyframes\":[";
        for (size_t k = 0; k < t.keyframes.size(); ++k)
        {
            if (k > 0)
                ss << ",";
            ss << "{\"t\":" << t.keyframes[k].time << "}";
        }
        ss << "]}";
    }
    ss << "]";

    // Include interpolator data if present
    if (interpolator_)
    {
        ss << ",\"interpolator\":" << interpolator_->serialize();
    }

    ss << "}";
    return ss.str();
}

bool TimelineEditor::deserialize(const std::string& json)
{
    std::lock_guard lock(mutex_);

    // Minimal parsing — extract key fields
    auto extract_float = [&](const std::string& key, float def) -> float
    {
        std::string search = "\"" + key + "\":";
        auto pos = json.find(search);
        if (pos == std::string::npos)
            return def;
        pos += search.size();
        try
        {
            return std::stof(json.substr(pos));
        }
        catch (...)
        {
            return def;
        }
    };

    auto extract_int = [&](const std::string& key, int def) -> int
    {
        std::string search = "\"" + key + "\":";
        auto pos = json.find(search);
        if (pos == std::string::npos)
            return def;
        pos += search.size();
        try
        {
            return std::stoi(json.substr(pos));
        }
        catch (...)
        {
            return def;
        }
    };

    duration_ = extract_float("duration", 10.0f);
    fps_ = extract_float("fps", 60.0f);
    loop_mode_ = static_cast<LoopMode>(extract_int("loop_mode", 0));
    snap_mode_ = static_cast<SnapMode>(extract_int("snap_mode", 1));
    snap_interval_ = extract_float("snap_interval", 0.1f);

    float li = extract_float("loop_in", -1.0f);
    float lo = extract_float("loop_out", -1.0f);
    if (li >= 0.0f && lo > li)
    {
        loop_in_ = li;
        loop_out_ = lo;
        has_loop_region_ = true;
    }

    view_end_ = duration_;

    // Deserialize interpolator data if present
    if (interpolator_)
    {
        auto interp_pos = json.find("\"interpolator\":");
        if (interp_pos != std::string::npos)
        {
            interp_pos += 16;  // skip "interpolator":
            // Find matching closing brace
            int depth = 0;
            size_t start = interp_pos;
            for (size_t i = interp_pos; i < json.size(); ++i)
            {
                if (json[i] == '{')
                    depth++;
                else if (json[i] == '}')
                {
                    depth--;
                    if (depth == 0)
                    {
                        interpolator_->deserialize(json.substr(start, i - start + 1));
                        break;
                    }
                }
            }
        }
    }

    return true;
}

// ─── ImGui Drawing ───────────────────────────────────────────────────────────

#ifdef PLOTIX_USE_IMGUI

void TimelineEditor::draw(float width, float height)
{
    std::lock_guard lock(mutex_);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##timeline_editor", ImVec2(width, height), true);

    const float track_height = 28.0f;
    const float header_height = 32.0f;
    const float ruler_height = 24.0f;
    const float track_label_width = 140.0f;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();

    float timeline_width = width - track_label_width;
    float time_range = view_end_ - view_start_;
    if (time_range <= 0.0f)
        time_range = 1.0f;
    float px_per_sec = timeline_width / time_range;

    auto time_to_px = [&](float t) -> float
    { return track_label_width + (t - view_start_) * px_per_sec; };

    // ─── Transport controls bar ──────────────────────────────────────
    ImGui::SetCursorScreenPos(origin);
    ImGui::BeginGroup();
    ImGui::Indent(4.0f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);

    if (ImGui::SmallButton(state_ == PlaybackState::Playing ? "||" : ">"))
    {
        if (state_ == PlaybackState::Playing)
        {
            state_ = PlaybackState::Paused;
        }
        else
        {
            if (state_ == PlaybackState::Stopped)
            {
                playhead_ = 0.0f;
                ping_pong_dir_ = 1;
            }
            state_ = PlaybackState::Playing;
        }
        fire_playback_change();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("[]"))
    {
        state_ = PlaybackState::Stopped;
        playhead_ = 0.0f;
        ping_pong_dir_ = 1;
        fire_playback_change();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("|<"))
    {
        if (fps_ > 0.0f)
            playhead_ = std::max(0.0f, playhead_ - 1.0f / fps_);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(">|"))
    {
        if (fps_ > 0.0f)
            playhead_ = std::min(duration_, playhead_ + 1.0f / fps_);
    }
    ImGui::SameLine();
    ImGui::Text("%.2fs  F:%u/%u",
                playhead_,
                static_cast<uint32_t>(std::floor(playhead_ * fps_)),
                static_cast<uint32_t>(std::ceil(duration_ * fps_)));

    ImGui::EndGroup();

    // ─── Time ruler ──────────────────────────────────────────────────
    float ruler_y = origin.y + header_height;
    draw_list->AddRectFilled(ImVec2(origin.x + track_label_width, ruler_y),
                             ImVec2(origin.x + width, ruler_y + ruler_height),
                             IM_COL32(40, 40, 40, 255));

    // Tick marks
    float tick_spacing = 1.0f;  // 1 second
    if (px_per_sec < 30.0f)
        tick_spacing = 5.0f;
    else if (px_per_sec < 60.0f)
        tick_spacing = 2.0f;
    else if (px_per_sec > 300.0f)
        tick_spacing = 0.5f;
    else if (px_per_sec > 600.0f)
        tick_spacing = 0.1f;

    float t = std::floor(view_start_ / tick_spacing) * tick_spacing;
    while (t <= view_end_)
    {
        float px = origin.x + time_to_px(t);
        bool major = std::abs(std::fmod(t, tick_spacing * 5.0f)) < 0.001f;
        float tick_h = major ? ruler_height : ruler_height * 0.5f;
        draw_list->AddLine(ImVec2(px, ruler_y + ruler_height - tick_h),
                           ImVec2(px, ruler_y + ruler_height),
                           IM_COL32(120, 120, 120, 255));
        if (major)
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "%.1fs", t);
            draw_list->AddText(ImVec2(px + 2, ruler_y + 2), IM_COL32(180, 180, 180, 255), buf);
        }
        t += tick_spacing;
    }

    // ─── Tracks ──────────────────────────────────────────────────────
    float track_y = ruler_y + ruler_height;
    for (size_t i = 0; i < tracks_.size(); ++i)
    {
        auto& track = tracks_[i];
        float y = track_y + static_cast<float>(i) * track_height;

        // Track label background
        ImU32 bg = (i % 2 == 0) ? IM_COL32(30, 30, 30, 255) : IM_COL32(35, 35, 35, 255);
        draw_list->AddRectFilled(
            ImVec2(origin.x, y), ImVec2(origin.x + width, y + track_height), bg);

        // Track label
        ImU32 label_col =
            track.visible ? IM_COL32(200, 200, 200, 255) : IM_COL32(100, 100, 100, 128);
        draw_list->AddText(ImVec2(origin.x + 8, y + 6), label_col, track.name.c_str());

        if (track.locked)
        {
            draw_list->AddText(ImVec2(origin.x + track_label_width - 20, y + 6),
                               IM_COL32(200, 100, 100, 200),
                               "L");
        }

        // Keyframe diamonds
        ImU32 kf_color = IM_COL32(static_cast<int>(track.color.r * 255),
                                  static_cast<int>(track.color.g * 255),
                                  static_cast<int>(track.color.b * 255),
                                  static_cast<int>(track.color.a * 255));

        for (auto& kf : track.keyframes)
        {
            float kf_px = origin.x + time_to_px(kf.time);
            float kf_cy = y + track_height * 0.5f;
            float sz = kf.selected ? 6.0f : 4.5f;

            // Diamond shape
            draw_list->AddQuadFilled(ImVec2(kf_px, kf_cy - sz),
                                     ImVec2(kf_px + sz, kf_cy),
                                     ImVec2(kf_px, kf_cy + sz),
                                     ImVec2(kf_px - sz, kf_cy),
                                     kf.selected ? IM_COL32(255, 255, 100, 255) : kf_color);

            if (kf.selected)
            {
                draw_list->AddQuad(ImVec2(kf_px, kf_cy - sz - 1),
                                   ImVec2(kf_px + sz + 1, kf_cy),
                                   ImVec2(kf_px, kf_cy + sz + 1),
                                   ImVec2(kf_px - sz - 1, kf_cy),
                                   IM_COL32(255, 255, 255, 200));
            }
        }
    }

    // ─── Playhead line ───────────────────────────────────────────────
    float ph_px = origin.x + time_to_px(playhead_);
    draw_list->AddLine(ImVec2(ph_px, ruler_y),
                       ImVec2(ph_px, track_y + static_cast<float>(tracks_.size()) * track_height),
                       IM_COL32(255, 80, 80, 220),
                       2.0f);

    // Playhead triangle on ruler
    draw_list->AddTriangleFilled(ImVec2(ph_px - 5, ruler_y),
                                 ImVec2(ph_px + 5, ruler_y),
                                 ImVec2(ph_px, ruler_y + 8),
                                 IM_COL32(255, 80, 80, 255));

    // ─── Loop region overlay ─────────────────────────────────────────
    if (has_loop_region_)
    {
        float li_px = origin.x + time_to_px(loop_in_);
        float lo_px = origin.x + time_to_px(loop_out_);
        draw_list->AddRectFilled(
            ImVec2(li_px, ruler_y),
            ImVec2(lo_px, track_y + static_cast<float>(tracks_.size()) * track_height),
            IM_COL32(80, 140, 255, 30));
    }

    // ─── Ruler click-to-scrub ────────────────────────────────────────
    ImGui::SetCursorScreenPos(ImVec2(origin.x + track_label_width, ruler_y));
    ImGui::InvisibleButton("##ruler_scrub", ImVec2(timeline_width, ruler_height));
    if (ImGui::IsItemActive())
    {
        float mx = ImGui::GetIO().MousePos.x - origin.x;
        float t_click = view_start_ + (mx - track_label_width) / px_per_sec;
        playhead_ = std::clamp(t_click, 0.0f, duration_);
        if (on_scrub_)
            on_scrub_(playhead_);
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
}

#endif  // PLOTIX_USE_IMGUI

}  // namespace plotix
