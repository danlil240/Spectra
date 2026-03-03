// SubplotManager — implementation.
//
// See subplot_manager.hpp for design notes.

#include "subplot_manager.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <spectra/color.hpp>

// AxisLinkManager lives under src/ui/data/ — included here (not in the public
// header) so that adapter users don't need src/ui on their include path.
#include "ui/data/axis_link.hpp"

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

SubplotManager::SubplotManager(Ros2Bridge&          bridge,
                               MessageIntrospector& intr,
                               int                  rows,
                               int                  cols,
                               spectra::Figure*     external_figure)
    : bridge_(bridge)
    , intr_(intr)
    , rows_(rows < 1 ? 1 : rows)
    , cols_(cols < 1 ? 1 : cols)
{
    if (external_figure)
    {
        figure_ = external_figure;
    }
    else
    {
        // Create the shared Figure with default size.
        spectra::FigureConfig cfg;
        cfg.width  = 1280;
        cfg.height = static_cast<uint32_t>(720 * rows_ / std::max(1, cols_));
        if (cfg.height < 400)
            cfg.height = 400;

        owned_figure_ = std::make_unique<spectra::Figure>(cfg);
        figure_ = owned_figure_.get();
    }

    // Pre-create all subplot Axes so they exist in the figure's axes_ list.
    // This also sets the figure's grid_rows_ / grid_cols_ to the final values.
    const int n = rows_ * cols_;
    for (int i = 1; i <= n; ++i)
        figure_->subplot(rows_, cols_, i);

    // Create the AxisLinkManager.
    link_manager_ = std::make_unique<spectra::AxisLinkManager>();

    // Initialise the slots vector — one SlotEntry per grid cell.
    // SlotEntry is non-copyable (unique_ptr member), so emplace_back each entry.
    slots_.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        slots_.emplace_back();
        slots_.back().slot = i + 1;
        // Axes pointer — pre-created above, index i corresponds to subplot(rows,cols,i+1).
        slots_.back().axes = figure_->axes()[static_cast<size_t>(i)].get();
    }
}

SubplotManager::~SubplotManager()
{
    // When bound to an external Figure (spectra-ros main canvas), that figure
    // can be destroyed by WindowManager before RosAppShell shutdown runs.
    // In that case, any Axes* / LineSeries* cached in slots_ are dangling and
    // must never be dereferenced here.
    if (!owned_figure_)
    {
        for (auto& se : slots_)
        {
            if (se.subscriber)
            {
                se.subscriber->stop();
                se.subscriber.reset();
            }
            se.series = nullptr;
            se.axes   = nullptr;
            se.topic.clear();
            se.field_path.clear();
            se.type_name.clear();
            se.extractor_id     = -1;
            se.samples_received = 0;
            se.auto_fitted      = false;
            se.drain_buf.clear();
        }
        return;
    }

    clear();
}

// ---------------------------------------------------------------------------
// Grid helpers
// ---------------------------------------------------------------------------

int SubplotManager::index_of(int row, int col) const
{
    if (row < 1 || row > rows_ || col < 1 || col > cols_)
        return -1;
    return (row - 1) * cols_ + col;
}

// ---------------------------------------------------------------------------
// Plot management
// ---------------------------------------------------------------------------

SubplotHandle SubplotManager::add_plot(int                slot,
                                       const std::string& topic,
                                       const std::string& field_path,
                                       const std::string& type_name,
                                       size_t             buffer_depth)
{
    SubplotHandle bad;
    bad.slot = -1;

    if (slot < 1 || slot > capacity())
        return bad;
    if (topic.empty() || field_path.empty())
        return bad;

    // Resolve message type.
    std::string resolved_type = type_name;
    if (resolved_type.empty())
    {
        resolved_type = detect_type(topic);
        if (resolved_type.empty())
            return bad;
    }

    SlotEntry& se = slots_[static_cast<size_t>(slot - 1)];

    // If there was a previous subscription, stop it.
    if (se.subscriber)
    {
        se.subscriber->stop();
        se.subscriber.reset();
    }

    se.topic      = topic;
    se.field_path = field_path;
    se.type_name  = resolved_type;

    // Clear any stale series data.
    if (se.series)
    {
        // Remove old series from axes and add a fresh one.
        se.axes->clear_series();
        se.series = nullptr;
    }

    // Build the label.
    std::string lbl = topic + "/" + field_path;

    // Add a LineSeries to the subplot axes.
    spectra::Color color = next_color();
    se.color_index       = color_cursor_ - 1;
    spectra::LineSeries& ls = se.axes->line();
    ls.label(lbl);
    ls.color(color);
    se.series = &ls;

    // Label axes.
    se.axes->xlabel("time (s)");
    se.axes->ylabel(lbl);

    // Reset auto-fit state.
    se.samples_received = 0;
    se.auto_fitted      = false;

    // Configure scroll controller.
    se.scroll.set_window_s(scroll_window_s_);

    // Subscribe if the bridge is running.
    if (bridge_.is_ok())
    {
        auto sub = std::make_unique<GenericSubscriber>(
            bridge_.node(), topic, resolved_type, intr_, buffer_depth);

        int eid = sub->add_field(field_path);
        if (eid < 0)
            return bad;

        se.extractor_id = eid;

        if (!sub->start())
            return bad;

        // Pre-allocate scratch buffer.
        se.drain_buf.reserve(std::min(buffer_depth, MAX_DRAIN_PER_POLL));
        se.drain_buf.clear();

        se.subscriber = std::move(sub);
    }

    // Re-link X axes across all active slots.
    rebuild_x_links();

    SubplotHandle h;
    h.slot       = slot;
    h.topic      = topic;
    h.field_path = field_path;
    h.axes       = se.axes;
    h.series     = se.series;
    return h;
}

SubplotHandle SubplotManager::add_plot(int                row,
                                       int                col,
                                       const std::string& topic,
                                       const std::string& field_path,
                                       const std::string& type_name,
                                       size_t             buffer_depth)
{
    return add_plot(index_of(row, col), topic, field_path, type_name, buffer_depth);
}

bool SubplotManager::remove_plot(int slot)
{
    if (slot < 1 || slot > capacity())
        return false;

    SlotEntry& se = slots_[static_cast<size_t>(slot - 1)];
    if (!se.active() || se.topic.empty())
        return false;

    if (se.subscriber)
    {
        se.subscriber->stop();
        se.subscriber.reset();
    }

    if (se.series)
    {
        se.axes->clear_series();
        se.series = nullptr;
    }

    se.topic.clear();
    se.field_path.clear();
    se.type_name.clear();
    se.extractor_id     = -1;
    se.samples_received = 0;
    se.auto_fitted      = false;
    se.drain_buf.clear();

    // Re-link (may have fewer active slots now).
    rebuild_x_links();

    return true;
}

void SubplotManager::clear()
{
    for (int i = 1; i <= capacity(); ++i)
        remove_plot(i);
}

bool SubplotManager::has_plot(int slot) const
{
    if (slot < 1 || slot > capacity())
        return false;
    const SlotEntry& se = slots_[static_cast<size_t>(slot - 1)];
    return !se.topic.empty() && se.series != nullptr;
}

int SubplotManager::active_count() const
{
    int count = 0;
    for (const auto& se : slots_)
    {
        if (!se.topic.empty() && se.series != nullptr)
            ++count;
    }
    return count;
}

SubplotHandle SubplotManager::handle(int slot) const
{
    SubplotHandle bad;
    bad.slot = -1;

    if (slot < 1 || slot > capacity())
        return bad;

    const SlotEntry& se = slots_[static_cast<size_t>(slot - 1)];
    if (!se.active() || se.topic.empty())
        return bad;

    SubplotHandle h;
    h.slot       = slot;
    h.topic      = se.topic;
    h.field_path = se.field_path;
    h.axes       = se.axes;
    h.series     = se.series;
    return h;
}

std::vector<SubplotHandle> SubplotManager::handles() const
{
    std::vector<SubplotHandle> result;
    for (const auto& se : slots_)
    {
        if (!se.topic.empty() && se.series != nullptr)
        {
            SubplotHandle h;
            h.slot       = se.slot;
            h.topic      = se.topic;
            h.field_path = se.field_path;
            h.axes       = se.axes;
            h.series     = se.series;
            result.push_back(h);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// poll() — hot path (render thread, called every frame)
// ---------------------------------------------------------------------------

void SubplotManager::poll()
{
    const double wall_now =
        std::chrono::duration<double>(
            std::chrono::system_clock::now().time_since_epoch()).count();

    for (auto& se : slots_)
    {
        if (!se.active())
            continue;

        // Advance scroll clock.
        se.scroll.set_now(wall_now);

        if (!se.subscriber || !se.subscriber->is_running())
        {
            se.scroll.tick(se.series, se.axes);
            continue;
        }

        const int eid = se.extractor_id;
        if (eid < 0)
        {
            se.scroll.tick(se.series, se.axes);
            continue;
        }

        // Ensure scratch buffer capacity (grows only).
        if (se.drain_buf.capacity() < MAX_DRAIN_PER_POLL)
            se.drain_buf.reserve(MAX_DRAIN_PER_POLL);

        const size_t n =
            se.subscriber->pop_bulk(eid, se.drain_buf.data(), MAX_DRAIN_PER_POLL);

        if (n > 0)
        {
            for (size_t i = 0; i < n; ++i)
            {
                const FieldSample& s = se.drain_buf[i];
                const double t_sec   = static_cast<double>(s.timestamp_ns) * 1e-9;
                se.series->append(static_cast<float>(t_sec),
                                  static_cast<float>(s.value));

                if (on_data_cb_)
                    on_data_cb_(se.slot, t_sec, s.value);
            }

            se.samples_received += n;

            if (!se.auto_fitted && se.samples_received >= auto_fit_samples_)
            {
                se.axes->auto_fit();
                se.auto_fitted = true;
            }
        }

        se.scroll.tick(se.series, se.axes);
    }
}

// ---------------------------------------------------------------------------
// Shared cursor
// ---------------------------------------------------------------------------

void SubplotManager::notify_cursor(spectra::Axes* source_axes,
                                   float          data_x,
                                   float          data_y,
                                   double         screen_x,
                                   double         screen_y)
{
    if (!source_axes)
    {
        clear_cursor();
        return;
    }

    spectra::SharedCursor cur;
    cur.valid       = true;
    cur.data_x      = data_x;
    cur.data_y      = data_y;
    cur.screen_x    = screen_x;
    cur.screen_y    = screen_y;
    cur.source_axes = source_axes;
    link_manager_->update_shared_cursor(cur);
}

void SubplotManager::clear_cursor()
{
    link_manager_->clear_shared_cursor();
}

// ---------------------------------------------------------------------------
// Auto-scroll
// ---------------------------------------------------------------------------

void SubplotManager::set_time_window(double seconds)
{
    scroll_window_s_ = seconds;
    for (auto& se : slots_)
        se.scroll.set_window_s(seconds);
}

void SubplotManager::set_now(double wall_time_s)
{
    for (auto& se : slots_)
        se.scroll.set_now(wall_time_s);
}

void SubplotManager::pause_scroll(int slot)
{
    if (slot < 1 || slot > capacity())
        return;
    slots_[static_cast<size_t>(slot - 1)].scroll.pause();
}

void SubplotManager::resume_scroll(int slot)
{
    if (slot < 1 || slot > capacity())
        return;
    slots_[static_cast<size_t>(slot - 1)].scroll.resume();
}

bool SubplotManager::is_scroll_paused(int slot) const
{
    if (slot < 1 || slot > capacity())
        return false;
    return slots_[static_cast<size_t>(slot - 1)].scroll.is_paused();
}

void SubplotManager::pause_all_scroll()
{
    for (auto& se : slots_)
        se.scroll.pause();
}

void SubplotManager::resume_all_scroll()
{
    for (auto& se : slots_)
        se.scroll.resume();
}

size_t SubplotManager::total_memory_bytes() const
{
    size_t total = 0;
    for (const auto& se : slots_)
        total += ScrollController::memory_bytes(se.series);
    return total;
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void SubplotManager::set_figure_size(uint32_t w, uint32_t h)
{
    if (figure_)
        figure_->set_size(w, h);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

SubplotManager::SlotEntry* SubplotManager::slot_entry(int slot)
{
    if (slot < 1 || slot > capacity())
        return nullptr;
    return &slots_[static_cast<size_t>(slot - 1)];
}

const SubplotManager::SlotEntry* SubplotManager::slot_entry(int slot) const
{
    if (slot < 1 || slot > capacity())
        return nullptr;
    return &slots_[static_cast<size_t>(slot - 1)];
}

std::string SubplotManager::detect_type(const std::string& topic) const
{
    if (!bridge_.is_ok())
        return {};

    auto node = bridge_.node();
    if (!node)
        return {};

    auto names_and_types = node->get_topic_names_and_types();
    auto it = names_and_types.find(topic);
    if (it == names_and_types.end() || it->second.empty())
        return {};

    return it->second.front();
}

spectra::Color SubplotManager::next_color()
{
    const spectra::Color c =
        spectra::palette::default_cycle[color_cursor_ % spectra::palette::default_cycle_size];
    ++color_cursor_;
    return c;
}

void SubplotManager::rebuild_x_links()
{
    // Collect all active axes pointers.
    std::vector<spectra::Axes*> active_axes;
    for (const auto& se : slots_)
    {
        if (se.axes && !se.topic.empty())
            active_axes.push_back(se.axes);
    }

    if (active_axes.size() < 2)
        return;

    // Remove existing links and rebuild.
    // We rebuild from scratch on every change to keep it simple and correct.
    // First, unlink each axes from all groups.
    for (auto* ax : active_axes)
        link_manager_->unlink(ax);

    // Link all active axes together on X (chain: link each pair with the first).
    spectra::Axes* leader = active_axes[0];
    for (size_t i = 1; i < active_axes.size(); ++i)
        link_manager_->link(leader, active_axes[i], spectra::LinkAxis::X);
}

}   // namespace spectra::adapters::ros2
