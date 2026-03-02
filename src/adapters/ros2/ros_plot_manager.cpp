// RosPlotManager — implementation.
//
// See ros_plot_manager.hpp for design notes.

#include "ros_plot_manager.hpp"

#include <algorithm>
#include <cmath>

#include <spectra/color.hpp>

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

    for (auto& entry : entries_)
    {
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

        if (n == 0)
            continue;

        // Append to the LineSeries.
        for (size_t i = 0; i < n; ++i)
        {
            const FieldSample& s = entry->drain_buf[i];
            const double t_sec   = static_cast<double>(s.timestamp_ns) * 1e-9;
            entry->series->append(static_cast<float>(t_sec),
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
    if (!bridge_.is_ok())
        return {};

    auto node = bridge_.node();
    if (!node)
        return {};

    // Query the topic graph for type names.
    auto names_and_types = node->get_topic_names_and_types();
    auto it = names_and_types.find(topic);
    if (it == names_and_types.end() || it->second.empty())
        return {};

    // Return the first advertised type.
    return it->second.front();
}

spectra::Color RosPlotManager::next_color()
{
    const spectra::Color c =
        spectra::palette::default_cycle[color_cursor_ % spectra::palette::default_cycle_size];
    ++color_cursor_;
    return c;
}

}   // namespace spectra::adapters::ros2
