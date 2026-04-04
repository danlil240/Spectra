// BagPlayer — implementation.
//
// See bag_player.hpp for design notes.

#include "bag_player.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include "message_introspector.hpp"
#include "ros_plot_manager.hpp"
#include "subplot_manager.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
#endif

// TimelineEditor lives in the core spectra library.
#include <ui/animation/timeline_editor.hpp>

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

BagPlayer::BagPlayer(RosPlotManager& plot_mgr, MessageIntrospector& intr, BagPlayerConfig config)
    : plot_mgr_(plot_mgr), intr_(intr), config_(config),
      rate_(std::clamp(config.rate, MIN_RATE, MAX_RATE)), loop_(config.loop)
{
}

BagPlayer::~BagPlayer()
{
    close();
}

// ---------------------------------------------------------------------------
// Bag lifecycle
// ---------------------------------------------------------------------------

bool BagPlayer::open(const std::string& bag_path, const std::vector<std::string>& topics)
{
    std::lock_guard<std::mutex> lk(mutex_);

    // Close any existing bag first.
    if (open_)
    {
        // (Internal close without locking again.)
        reader_.close();
        lookahead_msg_.reset();
        open_           = false;
        state_          = PlayerState::Stopped;
        playhead_sec_   = 0.0;
        bag_time_ns_    = 0;
        total_injected_ = 0;
        activity_bands_.clear();
        band_index_.clear();
        topic_fields_.clear();
    }

    if (!reader_.open(bag_path))
    {
        last_error_ = reader_.last_error();
        return false;
    }

    bag_path_    = bag_path;
    metadata_    = reader_.metadata();
    bag_time_ns_ = metadata_.start_time_ns;
    open_        = true;
    last_error_.clear();
    playhead_sec_   = 0.0;
    total_injected_ = 0;
    topic_filter_   = topics;

    // Apply topic filter to reader.
    if (!topics.empty())
        reader_.set_topic_filter(topics);

    // Build activity bands.
    if (config_.activity_buckets > 0)
        scan_activity();

    // Wire TimelineEditor if already attached.
    if (timeline_editor_)
        register_timeline_tracks();

    return true;
}

void BagPlayer::close()
{
    std::lock_guard<std::mutex> lk(mutex_);

    if (!open_)
        return;

    reader_.close();
    lookahead_msg_.reset();
    open_           = false;
    state_          = PlayerState::Stopped;
    playhead_sec_   = 0.0;
    bag_time_ns_    = 0;
    total_injected_ = 0;
    activity_bands_.clear();
    band_index_.clear();
    topic_fields_.clear();
}

bool BagPlayer::is_open() const noexcept
{
    std::lock_guard<std::mutex> lk(mutex_);
    return open_;
}

std::string BagPlayer::bag_path() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return bag_path_;
}

std::string BagPlayer::last_error() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return last_error_;
}

const BagMetadata& BagPlayer::metadata() const noexcept
{
    // metadata_ is set once on open and never mutated — safe to read without lock.
    return metadata_;
}

double BagPlayer::duration_sec() const noexcept
{
    return metadata_.duration_sec();
}

double BagPlayer::start_time_sec() const noexcept
{
    return metadata_.start_time_sec();
}

// ---------------------------------------------------------------------------
// Transport controls
// ---------------------------------------------------------------------------

void BagPlayer::play()
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (!open_)
        return;

    if (state_ == PlayerState::Stopped)
    {
        // Seek to beginning if at the end.
        if (playhead_sec_ >= duration_sec())
        {
            playhead_sec_ = 0.0;
            bag_time_ns_  = metadata_.start_time_ns;
            lookahead_msg_.reset();
            reader_.seek_begin();
        }
    }

    if (state_ != PlayerState::Playing)
    {
        set_state(PlayerState::Playing);
    }
}

void BagPlayer::pause()
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (state_ == PlayerState::Playing)
        set_state(PlayerState::Paused);
}

void BagPlayer::stop()
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (state_ != PlayerState::Stopped)
    {
        set_state(PlayerState::Stopped);
        // Reset to beginning.
        if (open_)
        {
            playhead_sec_ = 0.0;
            bag_time_ns_  = metadata_.start_time_ns;
            lookahead_msg_.reset();
            reader_.seek_begin();
        }
        sync_timeline_playhead();
    }
}

void BagPlayer::toggle_play()
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (!open_)
        return;

    if (state_ == PlayerState::Playing)
        set_state(PlayerState::Paused);
    else
        set_state(PlayerState::Playing);   // also covers Stopped → Playing
}

PlayerState BagPlayer::state() const noexcept
{
    std::lock_guard<std::mutex> lk(mutex_);
    return state_;
}

bool BagPlayer::is_playing() const noexcept
{
    std::lock_guard<std::mutex> lk(mutex_);
    return state_ == PlayerState::Playing;
}

bool BagPlayer::is_paused() const noexcept
{
    std::lock_guard<std::mutex> lk(mutex_);
    return state_ == PlayerState::Paused;
}

bool BagPlayer::is_stopped() const noexcept
{
    std::lock_guard<std::mutex> lk(mutex_);
    return state_ == PlayerState::Stopped;
}

// ---------------------------------------------------------------------------
// Seek
// ---------------------------------------------------------------------------

bool BagPlayer::seek(double offset_sec)
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (!open_)
        return false;

    const double clamped = clamp_offset(offset_sec);
    playhead_sec_        = clamped;
    bag_time_ns_         = offset_to_ns(clamped);

    lookahead_msg_.reset();
    const bool ok = reader_.seek(bag_time_ns_);
    if (!ok)
        last_error_ = reader_.last_error();

    sync_timeline_playhead();

    if (on_playhead_)
        on_playhead_(playhead_sec_);

    return ok;
}

bool BagPlayer::seek_begin()
{
    return seek(0.0);
}

bool BagPlayer::seek_fraction(double fraction)
{
    const double frac = std::clamp(fraction, 0.0, 1.0);
    return seek(frac * duration_sec());
}

void BagPlayer::step_forward()
{
    seek(playhead_sec_ + step_size_sec_);
}

void BagPlayer::step_backward()
{
    seek(playhead_sec_ - step_size_sec_);
}

void BagPlayer::set_step_size(double sec)
{
    step_size_sec_ = std::max(0.001, sec);
}

// ---------------------------------------------------------------------------
// Rate control
// ---------------------------------------------------------------------------

double BagPlayer::rate() const noexcept
{
    std::lock_guard<std::mutex> lk(mutex_);
    return rate_;
}

void BagPlayer::set_rate(double r)
{
    std::lock_guard<std::mutex> lk(mutex_);
    rate_ = std::clamp(r, MIN_RATE, MAX_RATE);
}

// ---------------------------------------------------------------------------
// Loop mode
// ---------------------------------------------------------------------------

bool BagPlayer::loop() const noexcept
{
    std::lock_guard<std::mutex> lk(mutex_);
    return loop_;
}

void BagPlayer::set_loop(bool enabled)
{
    std::lock_guard<std::mutex> lk(mutex_);
    loop_ = enabled;
}

// ---------------------------------------------------------------------------
// Playhead
// ---------------------------------------------------------------------------

double BagPlayer::playhead_sec() const noexcept
{
    std::lock_guard<std::mutex> lk(mutex_);
    return playhead_sec_;
}

double BagPlayer::progress() const noexcept
{
    std::lock_guard<std::mutex> lk(mutex_);
    const double                dur = duration_sec();
    if (dur <= 0.0)
        return 0.0;
    return std::clamp(playhead_sec_ / dur, 0.0, 1.0);
}

// ---------------------------------------------------------------------------
// Topic activity bands
// ---------------------------------------------------------------------------

std::vector<TopicActivityBand> BagPlayer::topic_activity_bands() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return activity_bands_;
}

const TopicActivityBand* BagPlayer::activity_band(const std::string& topic) const
{
    std::lock_guard<std::mutex> lk(mutex_);
    auto                        it = band_index_.find(topic);
    if (it == band_index_.end())
        return nullptr;
    return &activity_bands_[it->second];
}

// ---------------------------------------------------------------------------
// TimelineEditor integration
// ---------------------------------------------------------------------------

void BagPlayer::set_timeline_editor(spectra::TimelineEditor* editor)
{
    std::lock_guard<std::mutex> lk(mutex_);
    timeline_editor_ = editor;

    if (editor && open_)
    {
        register_timeline_tracks();
        sync_timeline_playhead();
    }
}

spectra::TimelineEditor* BagPlayer::timeline_editor() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return timeline_editor_;
}

// ---------------------------------------------------------------------------
// advance() — render thread hot path
// ---------------------------------------------------------------------------

bool BagPlayer::advance(double dt)
{
    std::lock_guard<std::mutex> lk(mutex_);

    if (!open_ || state_ != PlayerState::Playing)
        return state_ != PlayerState::Stopped;

    const double bag_dt = dt * rate_;
    const double new_ph = playhead_sec_ + bag_dt;
    const double dur    = duration_sec();

    // Check if we've reached (or passed) the end.
    if (new_ph >= dur)
    {
        if (loop_)
        {
            // Inject remaining messages up to end.
            const int64_t end_ns = metadata_.end_time_ns;
            inject_until(end_ns);

            // Loop: reset to beginning.
            playhead_sec_ = 0.0;
            bag_time_ns_  = metadata_.start_time_ns;
            lookahead_msg_.reset();
            reader_.seek_begin();
            total_injected_ = 0;
        }
        else
        {
            // Play remaining messages up to the end.
            inject_until(metadata_.end_time_ns);

            playhead_sec_ = dur;
            bag_time_ns_  = metadata_.end_time_ns;
            set_state(PlayerState::Stopped);
        }
    }
    else
    {
        // Normal advance: inject messages in [current, new_ph].
        const int64_t end_ns = offset_to_ns(new_ph);
        inject_until(end_ns);
        playhead_sec_ = new_ph;
        bag_time_ns_  = end_ns;
    }

    sync_timeline_playhead();

    if (on_playhead_)
        on_playhead_(playhead_sec_);

    return state_ != PlayerState::Stopped;
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

void BagPlayer::set_on_state_change(StateCallback cb)
{
    std::lock_guard<std::mutex> lk(mutex_);
    on_state_change_ = std::move(cb);
}

void BagPlayer::set_on_playhead(PlayheadCallback cb)
{
    std::lock_guard<std::mutex> lk(mutex_);
    on_playhead_ = std::move(cb);
}

void BagPlayer::set_on_message(MessageCallback cb)
{
    std::lock_guard<std::mutex> lk(mutex_);
    on_message_ = std::move(cb);
}

void BagPlayer::set_max_inject_per_frame(uint32_t n)
{
    std::lock_guard<std::mutex> lk(mutex_);
    config_.max_inject_per_frame = n;
}

uint64_t BagPlayer::total_injected() const noexcept
{
    std::lock_guard<std::mutex> lk(mutex_);
    return total_injected_;
}

void BagPlayer::set_subplot_manager(SubplotManager* mgr)
{
    std::lock_guard<std::mutex> lk(mutex_);
    subplot_mgr_ = mgr;
}

// ---------------------------------------------------------------------------
// Private: inject_until
// ---------------------------------------------------------------------------

uint32_t BagPlayer::inject_until(int64_t end_ns)
{
    // Called with mutex_ already held.
    uint32_t       injected   = 0;
    const uint32_t max_inject = config_.max_inject_per_frame;

    // Consume the cached lookahead message first (if any).
    if (lookahead_msg_.has_value())
    {
        auto& la = *lookahead_msg_;
        if (la.timestamp_ns > end_ns)
            return 0;   // still ahead of current window

        inject_message(la);
        ++injected;
        ++total_injected_;
        lookahead_msg_.reset();
    }

    BagMessage msg;
    while (reader_.has_next() && injected < max_inject)
    {
        if (!reader_.read_next(msg))
            break;

        if (msg.timestamp_ns > end_ns)
        {
            // Cache the overshot message for the next advance() call
            // instead of seeking back (rosbag2 reopens the DB on seek).
            lookahead_msg_ = std::move(msg);
            break;
        }

        inject_message(msg);
        ++injected;
        ++total_injected_;
    }

    return injected;
}

// ---------------------------------------------------------------------------
// Private: inject_message
// ---------------------------------------------------------------------------

void BagPlayer::inject_message(const BagMessage& msg)
{
    // Called with mutex_ already held.

    if (!msg.valid())
        return;

    // Lazy-build cached accessors for this topic's numeric fields.
    auto& tf = topic_fields_[msg.topic];
    if (!tf.scanned)
    {
        tf.scanned = true;

        auto schema = intr_.introspect(msg.type);
        if (!schema)
            return;

        const auto paths = schema->numeric_paths();
        for (const auto& field_path : paths)
        {
            FieldAccessor acc = intr_.make_accessor(*schema, field_path);
            if (!acc.valid())
                continue;
            // Skip fields that require pointer indirection (dynamic arrays,
            // strings, etc.) — the accessor would dereference random CDR bytes
            // as memory addresses, causing a crash.
            if (acc.requires_deserialized_struct())
                continue;

            TopicFields::FieldEntry fe;
            fe.accessor                  = std::move(acc);
            tf.field_entries[field_path] = std::move(fe);
        }
    }

    if (tf.field_entries.empty())
        return;

#ifdef SPECTRA_ROS2_BAG
    // CDR format: 4-byte header followed by the serialized struct.
    if (msg.serialized_data.size() < 4)
        return;

    const uint8_t* cdr_body      = msg.serialized_data.data() + 4;
    const size_t   cdr_body_size = msg.serialized_data.size() - 4;

    const double bag_time_sec = static_cast<double>(msg.timestamp_ns) * 1e-9
                                - static_cast<double>(metadata_.start_time_ns) * 1e-9;

    for (auto& [field_path, fe] : tf.field_entries)
    {
        const auto& acc = fe.accessor;

        // Bounds-check: ensure the CDR body is large enough for the leaf.
        const size_t total_offset = acc.flat_byte_offset();
        const size_t leaf_sz      = acc.leaf_size();
        if (leaf_sz == 0 || total_offset + leaf_sz > cdr_body_size)
            continue;

        const double value = acc.extract_double(static_cast<const void*>(cdr_body));
        if (std::isnan(value))
            continue;

        // Inject into SubplotManager series that match this topic + field.
        if (subplot_mgr_)
        {
            const int cap = subplot_mgr_->capacity();
            for (int s = 1; s <= cap; ++s)
            {
                auto* se = subplot_mgr_->slot_entry_pub(s);
                if (!se || !se->active())
                    continue;

                // Check primary series.
                if (se->topic == msg.topic && se->field_path == field_path && se->series)
                {
                    se->series->append(static_cast<float>(bag_time_sec), static_cast<float>(value));
                }

                // Check extra series in this slot.
                for (auto& es : se->extra_series)
                {
                    if (es->topic == msg.topic && es->field_path == field_path && es->series)
                    {
                        es->series->append(static_cast<float>(bag_time_sec),
                                           static_cast<float>(value));
                    }
                }
            }
        }

        if (on_message_)
            on_message_(msg.topic, bag_time_sec, value);
    }
#endif   // SPECTRA_ROS2_BAG
}

// ---------------------------------------------------------------------------
// Private: scan_activity
// ---------------------------------------------------------------------------

void BagPlayer::scan_activity()
{
    // Called with mutex_ held.

    activity_bands_.clear();
    band_index_.clear();

    if (!open_ || metadata_.duration_ns <= 0)
        return;

    const double   dur_sec    = metadata_.duration_sec();
    const uint32_t n_buckets  = config_.activity_buckets;
    const double   bucket_sec = dur_sec / static_cast<double>(n_buckets);

    // Build per-topic bucket bitsets using per-topic message counts from metadata.
    // For an approximate activity map we use the per-topic metadata if available,
    // otherwise we do a full sequential scan (expensive but correct).

    // Try metadata-only scan first: rosbag2 metadata contains per-topic message
    // counts but NOT per-message timestamps, so we can't derive intervals from
    // metadata alone.  We therefore do a quick full-scan pass to detect
    // presence in each time bucket.  The scan is sequential and limited to
    // topic + timestamp (no field extraction), so it's fast.

    // Temporary bucket structure: topic → vector<bool> of n_buckets
    std::unordered_map<std::string, std::vector<bool>> buckets;
    for (const auto& ti : metadata_.topics)
        buckets[ti.name].assign(n_buckets, false);

    // Sequential pass over the bag (timestamps only, no deserialization).
    // We create a temporary BagReader to avoid disrupting the playback reader.
    BagReader scan_reader;
    if (scan_reader.open(bag_path_))
    {
        if (!topic_filter_.empty())
            scan_reader.set_topic_filter(topic_filter_);

        BagMessage scan_msg;
        while (scan_reader.has_next())
        {
            if (!scan_reader.read_next(scan_msg))
                break;

            // We only need the timestamp and topic.
            const double offset_sec =
                static_cast<double>(scan_msg.timestamp_ns - metadata_.start_time_ns) * 1e-9;

            const uint32_t bucket_idx = static_cast<uint32_t>(
                std::min(static_cast<double>(n_buckets - 1), offset_sec / bucket_sec));

            auto bit = buckets.find(scan_msg.topic);
            if (bit != buckets.end())
                bit->second[bucket_idx] = true;
        }
        scan_reader.close();
    }

    // Convert bucket bitsets to interval lists (run-length encoding).
    for (auto& [topic, bvec] : buckets)
    {
        TopicActivityBand band;
        band.topic = topic;

        bool   in_run    = false;
        double run_start = 0.0;

        for (uint32_t i = 0; i < n_buckets; ++i)
        {
            const double t = static_cast<double>(i) * bucket_sec;
            if (bvec[i] && !in_run)
            {
                in_run    = true;
                run_start = t;
            }
            else if (!bvec[i] && in_run)
            {
                in_run = false;
                band.intervals.push_back({run_start, t});
            }
        }
        if (in_run)
            band.intervals.push_back({run_start, dur_sec});

        band_index_[topic] = activity_bands_.size();
        activity_bands_.push_back(std::move(band));
    }
}

// ---------------------------------------------------------------------------
// Private: register_timeline_tracks
// ---------------------------------------------------------------------------

void BagPlayer::register_timeline_tracks()
{
    // Called with mutex_ held.
    if (!timeline_editor_)
        return;

    const double dur = duration_sec();
    if (dur <= 0.0)
        return;

    // Set the timeline duration to match the bag.
    timeline_editor_->set_duration(static_cast<float>(dur));

    // Register one track per topic.  Colour cycles: use a simple palette.
    static const spectra::Color kPalette[] = {
        {0.27f, 0.52f, 1.00f, 1.0f},   // blue
        {0.18f, 0.80f, 0.44f, 1.0f},   // green
        {1.00f, 0.42f, 0.21f, 1.0f},   // orange
        {0.84f, 0.29f, 0.96f, 1.0f},   // purple
        {0.97f, 0.87f, 0.20f, 1.0f},   // yellow
        {0.20f, 0.90f, 0.95f, 1.0f},   // cyan
        {1.00f, 0.30f, 0.47f, 1.0f},   // red
        {0.55f, 0.90f, 0.20f, 1.0f},   // lime
    };
    static constexpr size_t kPaletteSize = sizeof(kPalette) / sizeof(kPalette[0]);

    size_t color_idx = 0;
    for (auto& band : activity_bands_)
    {
        if (band.timeline_track_id != 0)
            continue;   // already registered

        const spectra::Color c = kPalette[color_idx % kPaletteSize];
        ++color_idx;

        const uint32_t tid     = timeline_editor_->add_track(band.topic, c);
        band.timeline_track_id = tid;

        // Add a keyframe marker at the start of each activity interval.
        for (const auto& interval : band.intervals)
        {
            timeline_editor_->add_keyframe(tid, static_cast<float>(interval.start_sec));
            timeline_editor_->add_keyframe(tid, static_cast<float>(interval.end_sec));
        }
    }

    // Wire scrub callback so seeking in the editor seeks the player.
    timeline_editor_->set_on_scrub(
        [this](float t)
        {
            // NOTE: This callback is called from the render thread while the mutex
            // is NOT held (TimelineEditor holds its own mutex during scrub).
            // We set the scrub flag and handle it in the next advance() call.
            // However, for responsiveness we update playhead_sec_ directly.
            // The mutex is re-acquired here safely because TimelineEditor's scrub
            // fires outside its lock scope.
            std::lock_guard<std::mutex> lk2(mutex_);
            if (!open_)
                return;
            const double clamped = clamp_offset(static_cast<double>(t));
            playhead_sec_        = clamped;
            bag_time_ns_         = offset_to_ns(clamped);
            lookahead_msg_.reset();
            reader_.seek(bag_time_ns_);
        });
}

// ---------------------------------------------------------------------------
// Private: sync_timeline_playhead
// ---------------------------------------------------------------------------

void BagPlayer::sync_timeline_playhead()
{
    // Called with mutex_ held.
    if (!timeline_editor_)
        return;
    timeline_editor_->set_playhead(static_cast<float>(playhead_sec_));
}

// ---------------------------------------------------------------------------
// Private: set_state
// ---------------------------------------------------------------------------

void BagPlayer::set_state(PlayerState s)
{
    // Called with mutex_ held.
    if (state_ == s)
        return;
    state_ = s;
    if (on_state_change_)
        on_state_change_(s);
}

// ---------------------------------------------------------------------------
// Private: helpers
// ---------------------------------------------------------------------------

double BagPlayer::clamp_offset(double sec) const noexcept
{
    return std::clamp(sec, 0.0, duration_sec());
}

int64_t BagPlayer::offset_to_ns(double offset_sec) const noexcept
{
    return metadata_.start_time_ns + static_cast<int64_t>(offset_sec * 1e9);
}

double BagPlayer::ns_to_offset(int64_t ns) const noexcept
{
    return static_cast<double>(ns - metadata_.start_time_ns) * 1e-9;
}

}   // namespace spectra::adapters::ros2
