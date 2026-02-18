#include "figure_manager.hpp"

#include <cassert>

#include "tab_bar.hpp"

namespace spectra
{

FigureManager::FigureManager(FigureRegistry& registry) : registry_(registry)
{
    // Import any existing figures from the registry
    for (auto id : registry_.all_ids())
    {
        ordered_ids_.push_back(id);
        FigureState st;
        st.custom_title = default_title(ordered_ids_.size() - 1);
        states_[id] = std::move(st);
    }
    if (!ordered_ids_.empty())
    {
        active_index_ = ordered_ids_[0];
    }
}

size_t FigureManager::id_to_pos(FigureId id) const
{
    for (size_t i = 0; i < ordered_ids_.size(); ++i)
    {
        if (ordered_ids_[i] == id)
            return i;
    }
    return SIZE_MAX;
}

FigureId FigureManager::pos_to_id(size_t pos) const
{
    if (pos < ordered_ids_.size())
        return ordered_ids_[pos];
    return INVALID_FIGURE_ID;
}

void FigureManager::set_tab_bar(TabBar* tab_bar)
{
    tab_bar_ = tab_bar;
    if (tab_bar_)
    {
        sync_tab_bar();
    }
}

FigureId FigureManager::create_figure(const FigureConfig& config)
{
    auto id = registry_.register_figure(std::make_unique<Figure>(config));
    ordered_ids_.push_back(id);

    // Add state for the new figure
    FigureState new_state;
    new_state.custom_title = default_title(next_figure_number() - 1);
    states_[id] = std::move(new_state);

    // Sync tab bar
    if (tab_bar_)
    {
        tab_bar_->add_tab(get_title(id));
    }

    // Switch to the new figure
    switch_to(id);

    return id;
}

bool FigureManager::close_figure(FigureId index)
{
    size_t pos = id_to_pos(index);
    if (pos == SIZE_MAX)
    {
        return false;
    }

    // Last figure: request window close instead of closing the figure
    if (ordered_ids_.size() <= 1)
    {
        if (on_window_close_request_)
            on_window_close_request_();
        return false;
    }

    // Notify before removal
    if (on_figure_closed_)
    {
        on_figure_closed_(index);
    }

    // Remove from tab bar first (before modifying vectors)
    if (tab_bar_)
    {
        tab_bar_->remove_tab(pos);
    }

    // Remove from ordered list and registry
    ordered_ids_.erase(ordered_ids_.begin() + static_cast<std::ptrdiff_t>(pos));
    states_.erase(index);
    registry_.unregister_figure(index);

    // Adjust active index
    if (active_index_ == index)
    {
        // Switch to nearest remaining figure
        size_t new_pos = (pos < ordered_ids_.size()) ? pos : ordered_ids_.size() - 1;
        active_index_ = ordered_ids_[new_pos];
    }

    // Sync tab bar active state
    if (tab_bar_)
    {
        size_t active_pos = id_to_pos(active_index_);
        if (active_pos != SIZE_MAX)
            tab_bar_->set_active_tab(active_pos);
    }

    // Notify figure changed
    if (on_figure_changed_)
    {
        on_figure_changed_(active_index_, active_figure());
    }

    return true;
}

bool FigureManager::close_all_except(FigureId index)
{
    if (id_to_pos(index) == SIZE_MAX)
    {
        return false;
    }

    // Save the figure we want to keep
    save_active_state();

    // Collect IDs to remove
    std::vector<FigureId> to_remove;
    for (auto id : ordered_ids_)
    {
        if (id != index)
            to_remove.push_back(id);
    }

    for (auto id : to_remove)
    {
        if (on_figure_closed_)
        {
            on_figure_closed_(id);
        }
        states_.erase(id);
        registry_.unregister_figure(id);
    }

    ordered_ids_ = {index};
    active_index_ = index;

    // Rebuild tab bar
    sync_tab_bar();

    if (on_figure_changed_)
    {
        on_figure_changed_(active_index_, active_figure());
    }

    return true;
}

bool FigureManager::close_to_right(FigureId index)
{
    size_t pos = id_to_pos(index);
    if (pos == SIZE_MAX)
    {
        return false;
    }

    // Nothing to close if this is the last tab positionally
    if (pos + 1 >= ordered_ids_.size())
    {
        return false;
    }

    save_active_state();

    // Collect IDs to the right
    std::vector<FigureId> to_remove(ordered_ids_.begin() + static_cast<std::ptrdiff_t>(pos + 1),
                                     ordered_ids_.end());

    for (auto id : to_remove)
    {
        if (on_figure_closed_)
        {
            on_figure_closed_(id);
        }
        states_.erase(id);
        registry_.unregister_figure(id);
    }

    ordered_ids_.resize(pos + 1);

    // Adjust active index if it was beyond the closed range
    if (id_to_pos(active_index_) == SIZE_MAX)
    {
        active_index_ = index;
    }

    sync_tab_bar();

    if (on_figure_changed_)
    {
        on_figure_changed_(active_index_, active_figure());
    }

    return true;
}

FigureState FigureManager::remove_figure(FigureId id)
{
    size_t pos = id_to_pos(id);
    if (pos == SIZE_MAX)
        return {};

    // Save current state before removal
    if (id == active_index_)
        save_active_state();

    // Extract state
    FigureState extracted;
    auto st_it = states_.find(id);
    if (st_it != states_.end())
    {
        extracted = std::move(st_it->second);
        states_.erase(st_it);
    }

    // Remove from tab bar before modifying ordered_ids_
    if (tab_bar_)
        tab_bar_->remove_tab(pos);

    // Remove from ordered list (do NOT unregister from registry)
    ordered_ids_.erase(ordered_ids_.begin() + static_cast<std::ptrdiff_t>(pos));

    // Adjust active index
    if (active_index_ == id)
    {
        if (!ordered_ids_.empty())
        {
            size_t new_pos = (pos < ordered_ids_.size()) ? pos : ordered_ids_.size() - 1;
            active_index_ = ordered_ids_[new_pos];
        }
        else
        {
            active_index_ = INVALID_FIGURE_ID;
        }
    }

    // Sync tab bar active state
    if (tab_bar_ && !ordered_ids_.empty())
    {
        size_t active_pos = id_to_pos(active_index_);
        if (active_pos != SIZE_MAX)
            tab_bar_->set_active_tab(active_pos);
    }

    // Notify figure changed
    if (on_figure_changed_)
        on_figure_changed_(active_index_, active_figure());

    return extracted;
}

void FigureManager::add_figure(FigureId id, FigureState fig_state)
{
    // Don't add duplicates
    if (id_to_pos(id) != SIZE_MAX)
        return;

    // Verify figure exists in registry
    if (!registry_.get(id))
        return;

    ordered_ids_.push_back(id);
    states_[id] = std::move(fig_state);

    // Sync tab bar
    if (tab_bar_)
        tab_bar_->add_tab(get_title(id));

    // Switch to the new figure
    switch_to(id);
}

FigureId FigureManager::duplicate_figure(FigureId index)
{
    Figure* src = registry_.get(index);
    if (!src)
    {
        return INVALID_FIGURE_ID;
    }

    // Create a new figure with the same dimensions
    FigureConfig cfg;
    cfg.width = src->width();
    cfg.height = src->height();
    auto new_fig_ptr = std::make_unique<Figure>(cfg);
    auto& new_fig = *new_fig_ptr;

    // Copy axis limits from source
    for (size_t i = 0; i < src->axes().size(); ++i)
    {
        if (src->axes()[i])
        {
            auto& src_ax = *src->axes()[i];
            // Create matching subplot grid
            new_fig.subplot(src->grid_rows(), src->grid_cols(), static_cast<int>(i));
            if (i < new_fig.axes().size() && new_fig.axes()[i])
            {
                new_fig.axes_mut()[i]->xlim(src_ax.x_limits().min, src_ax.x_limits().max);
                new_fig.axes_mut()[i]->ylim(src_ax.y_limits().min, src_ax.y_limits().max);
                if (!src_ax.get_title().empty())
                {
                    new_fig.axes_mut()[i]->title(src_ax.get_title());
                }
            }
        }
    }

    // Copy style
    new_fig.style() = src->style();
    new_fig.legend() = src->legend();

    auto new_id = registry_.register_figure(std::move(new_fig_ptr));
    ordered_ids_.push_back(new_id);

    // Create state
    FigureState new_state;
    std::string src_title = get_title(index);
    new_state.custom_title = src_title + " (Copy)";
    states_[new_id] = std::move(new_state);

    // Sync tab bar
    if (tab_bar_)
    {
        tab_bar_->add_tab(get_title(new_id));
    }

    switch_to(new_id);
    return new_id;
}

void FigureManager::switch_to(FigureId index)
{
    size_t pos = id_to_pos(index);
    if (pos == SIZE_MAX || index == active_index_)
    {
        return;
    }

    // Save current figure state before switching
    save_active_state();

    active_index_ = index;

    // Restore state for the new active figure
    restore_state(index);

    // Sync tab bar
    if (tab_bar_)
    {
        tab_bar_->set_active_tab(pos);
    }

    // Notify
    if (on_figure_changed_)
    {
        on_figure_changed_(active_index_, active_figure());
    }
}

void FigureManager::switch_to_next()
{
    if (ordered_ids_.size() <= 1)
        return;
    size_t pos = id_to_pos(active_index_);
    if (pos == SIZE_MAX)
        return;
    size_t next_pos = (pos + 1) % ordered_ids_.size();
    switch_to(ordered_ids_[next_pos]);
}

void FigureManager::switch_to_previous()
{
    if (ordered_ids_.size() <= 1)
        return;
    size_t pos = id_to_pos(active_index_);
    if (pos == SIZE_MAX)
        return;
    size_t prev_pos = (pos == 0) ? ordered_ids_.size() - 1 : pos - 1;
    switch_to(ordered_ids_[prev_pos]);
}

void FigureManager::move_tab(FigureId from_index, FigureId to_index)
{
    size_t from_pos = id_to_pos(from_index);
    size_t to_pos = id_to_pos(to_index);
    if (from_pos == SIZE_MAX || to_pos == SIZE_MAX || from_pos == to_pos)
    {
        return;
    }

    // Reorder in ordered_ids_
    auto id = ordered_ids_[from_pos];
    ordered_ids_.erase(ordered_ids_.begin() + static_cast<std::ptrdiff_t>(from_pos));
    ordered_ids_.insert(ordered_ids_.begin() + static_cast<std::ptrdiff_t>(to_pos), id);

    sync_tab_bar();
}

Figure* FigureManager::active_figure() const
{
    return registry_.get(active_index_);
}

Figure* FigureManager::get_figure(FigureId id) const
{
    return registry_.get(id);
}

bool FigureManager::can_close(FigureId index) const
{
    (void)index;
    return ordered_ids_.size() > 1;
}

FigureState& FigureManager::state(FigureId index)
{
    ensure_states();
    auto it = states_.find(index);
    if (it == states_.end())
    {
        static FigureState dummy;
        return dummy;
    }
    return it->second;
}

const FigureState& FigureManager::state(FigureId index) const
{
    auto it = states_.find(index);
    if (it == states_.end())
    {
        static const FigureState dummy;
        return dummy;
    }
    return it->second;
}

FigureState& FigureManager::active_state()
{
    return state(active_index_);
}

std::string FigureManager::get_title(FigureId index) const
{
    auto it = states_.find(index);
    if (it == states_.end())
    {
        size_t pos = id_to_pos(index);
        return default_title(pos != SIZE_MAX ? pos : index);
    }
    if (!it->second.custom_title.empty())
    {
        return it->second.custom_title;
    }
    size_t pos = id_to_pos(index);
    return default_title(pos != SIZE_MAX ? pos : index);
}

void FigureManager::set_title(FigureId index, const std::string& title)
{
    ensure_states();
    auto it = states_.find(index);
    if (it != states_.end())
    {
        it->second.custom_title = title;
        if (tab_bar_)
        {
            size_t pos = id_to_pos(index);
            if (pos != SIZE_MAX)
                tab_bar_->set_tab_title(pos, title);
        }
    }
}

void FigureManager::mark_modified(FigureId index, bool modified)
{
    ensure_states();
    auto it = states_.find(index);
    if (it != states_.end())
    {
        it->second.is_modified = modified;
    }
}

bool FigureManager::is_modified(FigureId index) const
{
    auto it = states_.find(index);
    if (it == states_.end())
        return false;
    return it->second.is_modified;
}

bool FigureManager::process_pending()
{
    bool changed = false;

    if (pending_create_)
    {
        Figure* current = active_figure();
        FigureConfig cfg;
        if (current)
        {
            cfg.width = current->width();
            cfg.height = current->height();
        }
        create_figure(cfg);
        pending_create_ = false;
        changed = true;
    }

    if (pending_close_ != INVALID_FIGURE_ID)
    {
        FigureId idx = pending_close_;
        pending_close_ = INVALID_FIGURE_ID;
        close_figure(idx);
        changed = true;
    }

    if (pending_switch_ != INVALID_FIGURE_ID)
    {
        FigureId idx = pending_switch_;
        pending_switch_ = INVALID_FIGURE_ID;
        if (id_to_pos(idx) != SIZE_MAX && idx != active_index_)
        {
            switch_to(idx);
            changed = true;
        }
    }

    return changed;
}

void FigureManager::queue_create()
{
    pending_create_ = true;
}

void FigureManager::queue_close(FigureId index)
{
    pending_close_ = index;
}

void FigureManager::queue_switch(FigureId index)
{
    pending_switch_ = index;
}

void FigureManager::save_active_state()
{
    ensure_states();
    Figure* fig = registry_.get(active_index_);
    if (!fig)
    {
        return;
    }

    auto it = states_.find(active_index_);
    if (it == states_.end())
        return;
    auto& st = it->second;

    // Snapshot axis limits
    st.axes_snapshots.clear();
    for (const auto& ax : fig->axes())
    {
        if (ax)
        {
            FigureState::AxesSnapshot snap;
            snap.x_limits = ax->x_limits();
            snap.y_limits = ax->y_limits();
            st.axes_snapshots.push_back(snap);
        }
    }
}

void FigureManager::restore_state(FigureId index)
{
    Figure* fig = registry_.get(index);
    if (!fig)
        return;

    auto it = states_.find(index);
    if (it == states_.end())
        return;
    const auto& st = it->second;

    // Restore axis limits
    for (size_t i = 0; i < st.axes_snapshots.size() && i < fig->axes().size(); ++i)
    {
        if (fig->axes_mut()[i])
        {
            fig->axes_mut()[i]->xlim(st.axes_snapshots[i].x_limits.min,
                                    st.axes_snapshots[i].x_limits.max);
            fig->axes_mut()[i]->ylim(st.axes_snapshots[i].y_limits.min,
                                    st.axes_snapshots[i].y_limits.max);
        }
    }
}

std::string FigureManager::default_title(FigureId index)
{
    return "Figure " + std::to_string(index + 1);
}

void FigureManager::sync_tab_bar()
{
    if (!tab_bar_)
        return;

    // Rebuild tab bar from scratch (clear without firing callbacks)
    tab_bar_->clear_tabs();

    for (size_t i = 0; i < ordered_ids_.size(); ++i)
    {
        tab_bar_->add_tab(get_title(ordered_ids_[i]));
    }

    // Set active
    size_t active_pos = id_to_pos(active_index_);
    if (active_pos != SIZE_MAX)
    {
        tab_bar_->set_active_tab(active_pos);
    }
}

void FigureManager::ensure_states()
{
    for (auto id : ordered_ids_)
    {
        if (states_.find(id) == states_.end())
        {
            FigureState st;
            st.custom_title = default_title(id_to_pos(id));
            states_[id] = std::move(st);
        }
    }
}

size_t FigureManager::next_figure_number() const
{
    // Find the highest figure number used
    size_t max_num = ordered_ids_.size();
    for (const auto& [id, st] : states_)
    {
        if (!st.custom_title.empty())
        {
            // Try to parse "Figure N" pattern
            const std::string prefix = "Figure ";
            if (st.custom_title.substr(0, prefix.size()) == prefix)
            {
                try
                {
                    size_t num = std::stoul(st.custom_title.substr(prefix.size()));
                    if (num >= max_num)
                        max_num = num + 1;
                }
                catch (...)
                {
                    // Not a number, ignore
                }
            }
        }
    }
    return max_num;
}

}  // namespace spectra
