#include "px4_plot_manager.hpp"

#include <algorithm>

namespace spectra::adapters::px4
{

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Px4PlotManager::Px4PlotManager()  = default;
Px4PlotManager::~Px4PlotManager() = default;

// ---------------------------------------------------------------------------
// load_ulog
// ---------------------------------------------------------------------------

void Px4PlotManager::load_ulog(const ULogReader& reader)
{
    ulog_ = &reader;
    // Refresh all existing fields with new data.
    for (auto& f : fields_)
        refresh_ulog_field(f);
    bump_revision();
}

// ---------------------------------------------------------------------------
// clear
// ---------------------------------------------------------------------------

void Px4PlotManager::clear()
{
    fields_.clear();
    ulog_   = nullptr;
    bridge_ = nullptr;
    last_seen_ts_.clear();
    bump_revision();
}

// ---------------------------------------------------------------------------
// poll (real-time mode)
// ---------------------------------------------------------------------------

void Px4PlotManager::poll()
{
    if (!bridge_ || !bridge_->is_receiving())
        return;

    bool changed = false;
    for (auto& f : fields_)
        changed |= refresh_live_field(f);

    if (changed)
        bump_revision();
}

// ---------------------------------------------------------------------------
// add_field
// ---------------------------------------------------------------------------

size_t Px4PlotManager::add_field(const std::string& topic,
                                 const std::string& field,
                                 int                array_idx,
                                 uint8_t            multi_id)
{
    PlotField pf;
    pf.topic     = topic;
    pf.field     = field;
    pf.array_idx = array_idx;
    pf.multi_id  = multi_id;

    // Generate label.
    pf.label = topic + "." + field;
    if (array_idx >= 0)
        pf.label += "[" + std::to_string(array_idx) + "]";
    if (multi_id > 0)
        pf.label += " (" + std::to_string(multi_id) + ")";

    // Fill data if ULog is loaded.
    if (ulog_)
        refresh_ulog_field(pf);

    fields_.push_back(std::move(pf));
    bump_revision();
    return fields_.size() - 1;
}

// ---------------------------------------------------------------------------
// add_live_field
// ---------------------------------------------------------------------------

size_t Px4PlotManager::add_live_field(const std::string& channel, const std::string& field)
{
    PlotField pf;
    pf.topic = channel;
    pf.field = field;
    pf.label = channel + "." + field;

    fields_.push_back(std::move(pf));
    bump_revision();
    return fields_.size() - 1;
}

// ---------------------------------------------------------------------------
// remove_field
// ---------------------------------------------------------------------------

void Px4PlotManager::remove_field(size_t index)
{
    if (index < fields_.size())
    {
        fields_.erase(fields_.begin() + static_cast<ptrdiff_t>(index));
        bump_revision();
    }
}

// ---------------------------------------------------------------------------
// remove_topic
// ---------------------------------------------------------------------------

void Px4PlotManager::remove_topic(const std::string& topic)
{
    const size_t old_size = fields_.size();
    fields_.erase(std::remove_if(fields_.begin(),
                                 fields_.end(),
                                 [&](const PlotField& f) { return f.topic == topic; }),
                  fields_.end());
    if (fields_.size() != old_size)
        bump_revision();
}

// ---------------------------------------------------------------------------
// available_topics
// ---------------------------------------------------------------------------

std::vector<std::string> Px4PlotManager::available_topics() const
{
    std::vector<std::string> result;

    if (ulog_ && ulog_->is_open())
    {
        result = ulog_->topic_names();
    }
    else if (bridge_ && bridge_->is_receiving())
    {
        result = bridge_->channel_names();
    }

    std::sort(result.begin(), result.end());
    return result;
}

// ---------------------------------------------------------------------------
// topic_fields
// ---------------------------------------------------------------------------

std::vector<std::string> Px4PlotManager::topic_fields(const std::string& topic) const
{
    std::vector<std::string> result;

    if (ulog_ && ulog_->is_open())
    {
        auto* fmt = ulog_->format(topic);
        if (fmt)
        {
            for (auto& f : fmt->fields)
            {
                if (f.array_size > 1)
                {
                    for (int i = 0; i < f.array_size; ++i)
                        result.push_back(f.name + "[" + std::to_string(i) + "]");
                }
                else
                {
                    result.push_back(f.name);
                }
            }
        }
    }
    else if (bridge_ && bridge_->is_receiving())
    {
        auto latest = bridge_->channel_latest(topic);
        if (latest)
        {
            for (auto& tf : latest->fields)
                result.push_back(tf.name);
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// refresh_ulog_field
// ---------------------------------------------------------------------------

void Px4PlotManager::refresh_ulog_field(PlotField& f)
{
    if (!ulog_ || !ulog_->is_open())
        return;

    auto* ts = ulog_->data_for(f.topic, f.multi_id);
    if (!ts)
        return;

    if (f.array_idx >= 0)
    {
        auto [times, values] = ts->extract_array_element(f.field, f.array_idx);
        f.times              = std::move(times);
        f.values             = std::move(values);
    }
    else
    {
        auto [times, values] = ts->extract_field(f.field);
        f.times              = std::move(times);
        f.values             = std::move(values);
    }
}

// ---------------------------------------------------------------------------
// refresh_live_field
// ---------------------------------------------------------------------------

bool Px4PlotManager::refresh_live_field(PlotField& f)
{
    if (!bridge_)
        return false;

    auto msgs = bridge_->channel_snapshot(f.topic);
    if (msgs.empty())
        return false;

    // Rebuild time series from channel buffer.
    f.times.clear();
    f.values.clear();
    f.times.reserve(msgs.size());
    f.values.reserve(msgs.size());

    // Reference time: first message timestamp.
    uint64_t t0 = msgs.front().timestamp_us;

    for (auto& msg : msgs)
    {
        float t = static_cast<float>(msg.timestamp_us - t0) * 1e-6f;

        // Trim to time window.
        float t_end = static_cast<float>(msgs.back().timestamp_us - t0) * 1e-6f;
        if (t < t_end - static_cast<float>(time_window_s_))
            continue;

        // Find the matching field.
        for (auto& tf : msg.fields)
        {
            if (tf.name == f.field)
            {
                f.times.push_back(t);
                f.values.push_back(static_cast<float>(tf.value));
                break;
            }
        }
    }

    return true;
}

}   // namespace spectra::adapters::px4
