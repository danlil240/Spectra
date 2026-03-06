// RosPlotManager — implementation.
//
// See ros_plot_manager.hpp for design notes.

#include "ros_plot_manager.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <spectra/color.hpp>

#include "topic_discovery.hpp"

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

RosPlotManager::RosPlotManager(Ros2Bridge& bridge, MessageIntrospector& intr)
    : bridge_(bridge), intr_(intr)
{
}

RosPlotManager::~RosPlotManager()
{
    clear();
}

// ---------------------------------------------------------------------------
// Plot management
// ---------------------------------------------------------------------------

PlotHandle RosPlotManager::add_plot(const std::string& topic,
                                    const std::string& field_path,
                                    const std::string& type_name,
                                    size_t             buffer_depth)
{
    PlotHandle bad;
    bad.id = -1;

    if (topic.empty() || field_path.empty())
        return bad;

    // Resolve the message type.
    std::string resolved_type = type_name;
    if (resolved_type.empty())
    {
        resolved_type = detect_type(topic);
        if (resolved_type.empty())
            return bad;
    }

    // Build the entry under the mutex so handles() / find_entry() stay consistent.
    std::lock_guard<std::mutex> lk(mutex_);

    auto entry        = std::make_unique<PlotEntry>();
    entry->id         = next_id_++;
    entry->topic      = topic;
    entry->field_path = field_path;
    entry->type_name  = resolved_type;
    entry->color_index = color_cursor_;

    // ---------- Spectra Figure + Axes + LineSeries ----------------------
    spectra::FigureConfig cfg;
    cfg.width  = figure_width_;
    cfg.height = figure_height_;
    entry->figure = std::make_unique<spectra::Figure>(cfg);

    // Single subplot (1×1).
    entry->axes = &entry->figure->subplot(1, 1, 1);

    // Build the label: "topic/field_path"
    std::string lbl = topic + "/" + field_path;

    // Create an empty LineSeries (data arrives via poll()).
    spectra::LineSeries& ls = entry->axes->line();
    ls.label(lbl);
    ls.color(next_color());
    entry->series = &ls;

    // Label axes with sensible defaults.
    entry->axes->xlabel("time (s)");
    entry->axes->ylabel(lbl);

    // ---------- GenericSubscriber ---------------------------------------
    // Only create the subscription if the bridge is running.
    if (bridge_.is_ok())
    {
        auto sub = std::make_unique<GenericSubscriber>(
            bridge_.node(), topic, resolved_type, intr_, buffer_depth);

        int eid = sub->add_field(field_path);
        if (eid < 0)
        {
            // Field path not found in schema — bail out.
            return bad;
        }
        entry->extractor_id = eid;

        if (!sub->start())
        {
            // Subscription failed (schema introspection error).
            return bad;
        }

        entry->subscriber = std::move(sub);
    }

    // Configure presented_buffer on the axes for auto-scroll.
    entry->axes->presented_buffer(static_cast<float>(scroll_window_s_));

    // Reserve scratch buffer up front — avoids the first-poll allocation.
    entry->drain_buf.resize(std::min(buffer_depth, MAX_DRAIN_PER_POLL));
    entry->drain_buf.clear();

    PlotHandle h;
    h.id         = entry->id;
    h.topic      = entry->topic;
    h.field_path = entry->field_path;
    h.figure     = entry->figure.get();
    h.axes       = entry->axes;
    h.series     = entry->series;

    entries_.push_back(std::move(entry));
    return h;
}

bool RosPlotManager::remove_plot(int id)
{
    std::lock_guard<std::mutex> lk(mutex_);

    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [id](const std::unique_ptr<PlotEntry>& e) { return e->id == id; });
    if (it == entries_.end())
        return false;

    // Stop subscription before destroying.
    if ((*it)->subscriber)
        (*it)->subscriber->stop();

    entries_.erase(it);
    return true;
}

void RosPlotManager::clear()
{
    std::lock_guard<std::mutex> lk(mutex_);
    for (auto& e : entries_)
    {
        if (e->subscriber)
            e->subscriber->stop();
    }
    entries_.clear();
}

size_t RosPlotManager::plot_count() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return entries_.size();
}

PlotHandle RosPlotManager::handle(int id) const
{
    std::lock_guard<std::mutex> lk(mutex_);
    const PlotEntry* e = find_entry(id);
    if (!e)
    {
        PlotHandle bad;
        bad.id = -1;
        return bad;
    }

    PlotHandle h;
    h.id         = e->id;
    h.topic      = e->topic;
    h.field_path = e->field_path;
    h.figure     = e->figure.get();
    h.axes       = e->axes;
    h.series     = e->series;
    return h;
}

std::vector<PlotHandle> RosPlotManager::handles() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<PlotHandle> result;
    result.reserve(entries_.size());
    for (const auto& e : entries_)
    {
        PlotHandle h;
        h.id         = e->id;
        h.topic      = e->topic;
        h.field_path = e->field_path;
        h.figure     = e->figure.get();
        h.axes       = e->axes;
        h.series     = e->series;
        result.push_back(h);
    }
    return result;
}

// ---------------------------------------------------------------------------
// poll() — hot path (render thread, called every frame)
// ---------------------------------------------------------------------------

void RosPlotManager::poll()
{
    // We do NOT hold the mutex during the ring-buffer drain to keep the
    // hot path lock-free.  entries_ must not be mutated concurrently with
    // poll(); this contract is documented in the header (render thread only).

    // Wall-clock "now" for all plots this frame (seconds since epoch).
    const double wall_now =
        std::chrono::duration<double>(
            std::chrono::system_clock::now().time_since_epoch()).count();

    for (auto& entry : entries_)
    {
        // Set the time origin on first frame so that all series x-values
        // use small relative seconds instead of epoch seconds (~1.7e9),
        // which exceed float's ~7-digit precision.
        if (!entry->has_time_origin)
        {
            entry->time_origin     = wall_now;
            entry->has_time_origin = true;
        }

        const double now_rel = wall_now - entry->time_origin;
        entry->axes->set_presented_buffer_right_edge(now_rel);

        // Prune old data regardless of subscription state.
        if (entry->series)
        {
            const float prune_before =
                static_cast<float>(now_rel - PRUNE_FACTOR * entry->axes->presented_buffer_seconds());
            entry->series->erase_before(prune_before);
        }

        if (!entry->subscriber || !entry->subscriber->is_running())
            continue;

        const int eid = entry->extractor_id;
        if (eid < 0)
            continue;

        // Ensure scratch buffer is large enough (grows only).
        if (entry->drain_buf.capacity() < MAX_DRAIN_PER_POLL)
            entry->drain_buf.reserve(MAX_DRAIN_PER_POLL);

        // Drain up to MAX_DRAIN_PER_POLL samples.
        const size_t n =
            entry->subscriber->pop_bulk(eid,
                                        entry->drain_buf.data(),
                                        MAX_DRAIN_PER_POLL);

        if (n > 0)
        {
            // Append to the LineSeries using relative time (seconds since
            // origin) so that float precision is not lost at epoch-scale
            // timestamps (~1.7e9 exceeds float's ~7-digit mantissa).
            const double origin = entry->time_origin;
            for (size_t i = 0; i < n; ++i)
            {
                const FieldSample& s = entry->drain_buf[i];
                const double t_sec   = static_cast<double>(s.timestamp_ns) * 1e-9;
                const double t_rel   = t_sec - origin;
                entry->series->append(static_cast<float>(t_rel),
                                      static_cast<float>(s.value));

                if (on_data_cb_)
                    on_data_cb_(entry->id, t_sec, s.value);
            }

            entry->samples_received += n;

            // Auto-fit Y once we have enough samples.
            if (!entry->auto_fitted && entry->samples_received >= auto_fit_samples_)
            {
                entry->axes->auto_fit();
                entry->auto_fitted = true;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void RosPlotManager::set_figure_size(uint32_t w, uint32_t h)
{
    figure_width_  = w;
    figure_height_ = h;
}

void RosPlotManager::set_default_buffer_depth(size_t depth)
{
    default_buffer_depth_ = depth;
}

void RosPlotManager::set_auto_fit_samples(size_t n)
{
    auto_fit_samples_ = n;
}


void RosPlotManager::set_time_window(double seconds)
{
    if (seconds < MIN_WINDOW_S) seconds = MIN_WINDOW_S;
    if (seconds > MAX_WINDOW_S) seconds = MAX_WINDOW_S;
    scroll_window_s_ = seconds;
    // Apply to all existing entries immediately.
    for (auto& e : entries_)
        e->axes->presented_buffer(static_cast<float>(seconds));
}

double RosPlotManager::time_window() const
{
    return scroll_window_s_;
}

void RosPlotManager::pause_scroll(int id)
{
    PlotEntry* e = find_entry(id);
    if (e && e->axes)
    {
        // Freeze current view by setting explicit xlim (pauses follow).
        auto lim = e->axes->x_limits();
        e->axes->xlim(lim.min, lim.max);
    }
}

void RosPlotManager::resume_scroll(int id)
{
    PlotEntry* e = find_entry(id);
    if (e && e->axes)
        e->axes->resume_follow();
}

void RosPlotManager::toggle_scroll_paused(int id)
{
    PlotEntry* e = find_entry(id);
    if (e && e->axes)
    {
        if (e->axes->is_presented_buffer_following())
        {
            auto lim = e->axes->x_limits();
            e->axes->xlim(lim.min, lim.max);
        }
        else
        {
            e->axes->resume_follow();
        }
    }
}

bool RosPlotManager::is_scroll_paused(int id) const
{
    const PlotEntry* e = find_entry(id);
    return e && e->axes ? !e->axes->is_presented_buffer_following() : false;
}

void RosPlotManager::pause_all_scroll()
{
    for (auto& e : entries_)
    {
        if (e->axes)
        {
            auto lim = e->axes->x_limits();
            e->axes->xlim(lim.min, lim.max);
        }
    }
}

void RosPlotManager::resume_all_scroll()
{
    for (auto& e : entries_)
    {
        if (e->axes)
            e->axes->resume_follow();
    }
}

size_t RosPlotManager::memory_bytes(int id) const
{
    const PlotEntry* e = find_entry(id);
    return (e && e->series) ? e->series->memory_bytes() : 0;
}

size_t RosPlotManager::total_memory_bytes() const
{
    size_t total = 0;
    for (const auto& e : entries_)
    {
        if (e->series)
            total += e->series->memory_bytes();
    }
    return total;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

RosPlotManager::PlotEntry* RosPlotManager::find_entry(int id)
{
    for (auto& e : entries_)
    {
        if (e->id == id)
            return e.get();
    }
    return nullptr;
}

const RosPlotManager::PlotEntry* RosPlotManager::find_entry(int id) const
{
    for (const auto& e : entries_)
    {
        if (e->id == id)
            return e.get();
    }
    return nullptr;
}

std::string RosPlotManager::detect_type(const std::string& topic) const
{
    // Prefer the TopicDiscovery cache — queries DDS on the executor thread,
    // never deadlocks with rmw_fastrtps.
    if (discovery_)
    {
        if (discovery_->has_topic(topic))
        {
            TopicInfo ti = discovery_->topic(topic);
            if (!ti.types.empty())
                return ti.types.front();
        }
        return {};
    }

    // Do NOT fall back to node_->get_topic_names_and_types() — that DDS
    // graph call can deadlock with rmw_fastrtps's discovery thread when
    // namespaced participants are present.  Return empty and let the caller
    // retry after TopicDiscovery has refreshed.
    return {};
}

spectra::Color RosPlotManager::next_color()
{
    const spectra::Color c =
        spectra::palette::default_cycle[color_cursor_ % spectra::palette::default_cycle_size];
    ++color_cursor_;
    return c;
}

}   // namespace spectra::adapters::ros2
