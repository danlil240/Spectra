#include "figure_manager.hpp"

#include <cassert>

#include "tab_bar.hpp"

namespace spectra
{

FigureManager::FigureManager(std::vector<std::unique_ptr<Figure>>& figures) : figures_(figures)
{
    ensure_states();
}

void FigureManager::set_tab_bar(TabBar* tab_bar)
{
    tab_bar_ = tab_bar;
    if (tab_bar_)
    {
        sync_tab_bar();
    }
}

size_t FigureManager::create_figure(const FigureConfig& config)
{
    figures_.push_back(std::make_unique<Figure>(config));
    size_t new_index = figures_.size() - 1;

    // Add state for the new figure
    FigureState new_state;
    new_state.custom_title = default_title(next_figure_number() - 1);
    states_.push_back(std::move(new_state));

    // Sync tab bar
    if (tab_bar_)
    {
        // TabBar::add_tab auto-activates, but we control activation ourselves
        tab_bar_->add_tab(get_title(new_index));
    }

    // Switch to the new figure
    switch_to(new_index);

    return new_index;
}

bool FigureManager::close_figure(size_t index)
{
    if (index >= figures_.size())
    {
        return false;
    }

    // Can't close the last figure
    if (figures_.size() <= 1)
    {
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
        tab_bar_->remove_tab(index);
    }

    // Remove figure and state
    figures_.erase(figures_.begin() + static_cast<std::ptrdiff_t>(index));
    if (index < states_.size())
    {
        states_.erase(states_.begin() + static_cast<std::ptrdiff_t>(index));
    }

    // Adjust active index
    if (active_index_ >= figures_.size())
    {
        active_index_ = figures_.size() - 1;
    }
    else if (active_index_ > index)
    {
        --active_index_;
    }

    // Sync tab bar active state
    if (tab_bar_)
    {
        tab_bar_->set_active_tab(active_index_);
    }

    // Notify figure changed
    if (on_figure_changed_)
    {
        on_figure_changed_(active_index_, active_figure());
    }

    return true;
}

bool FigureManager::close_all_except(size_t index)
{
    if (index >= figures_.size())
    {
        return false;
    }

    // Save the figure we want to keep
    save_active_state();

    // Close from the end to avoid index shifting issues
    for (size_t i = figures_.size(); i > 0; --i)
    {
        size_t idx = i - 1;
        if (idx != index)
        {
            if (on_figure_closed_)
            {
                on_figure_closed_(idx);
            }
            figures_.erase(figures_.begin() + static_cast<std::ptrdiff_t>(idx));
            if (idx < states_.size())
            {
                states_.erase(states_.begin() + static_cast<std::ptrdiff_t>(idx));
            }
        }
    }

    active_index_ = 0;

    // Rebuild tab bar
    sync_tab_bar();

    if (on_figure_changed_)
    {
        on_figure_changed_(active_index_, active_figure());
    }

    return true;
}

bool FigureManager::close_to_right(size_t index)
{
    if (index >= figures_.size())
    {
        return false;
    }

    // Nothing to close if this is the last tab
    if (index + 1 >= figures_.size())
    {
        return false;
    }

    save_active_state();

    // Close from the end
    for (size_t i = figures_.size(); i > index + 1; --i)
    {
        size_t idx = i - 1;
        if (on_figure_closed_)
        {
            on_figure_closed_(idx);
        }
        figures_.erase(figures_.begin() + static_cast<std::ptrdiff_t>(idx));
        if (idx < states_.size())
        {
            states_.erase(states_.begin() + static_cast<std::ptrdiff_t>(idx));
        }
    }

    // Adjust active index if it was beyond the closed range
    if (active_index_ > index)
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

size_t FigureManager::duplicate_figure(size_t index)
{
    if (index >= figures_.size())
    {
        return SIZE_MAX;
    }

    // Create a new figure with the same dimensions
    const auto& src = *figures_[index];
    FigureConfig cfg;
    cfg.width = src.width();
    cfg.height = src.height();
    figures_.push_back(std::make_unique<Figure>(cfg));
    size_t new_index = figures_.size() - 1;

    // Copy axis limits from source
    auto& new_fig = *figures_[new_index];
    for (size_t i = 0; i < src.axes().size(); ++i)
    {
        if (src.axes()[i])
        {
            auto& src_ax = *src.axes()[i];
            // Create matching subplot grid
            new_fig.subplot(src.grid_rows(), src.grid_cols(), static_cast<int>(i));
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
    new_fig.style() = src.style();
    new_fig.legend() = src.legend();

    // Create state
    FigureState new_state;
    std::string src_title = get_title(index);
    new_state.custom_title = src_title + " (Copy)";
    states_.push_back(std::move(new_state));

    // Sync tab bar
    if (tab_bar_)
    {
        tab_bar_->add_tab(get_title(new_index));
    }

    switch_to(new_index);
    return new_index;
}

void FigureManager::switch_to(size_t index)
{
    if (index >= figures_.size() || index == active_index_)
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
        tab_bar_->set_active_tab(index);
    }

    // Notify
    if (on_figure_changed_)
    {
        on_figure_changed_(active_index_, active_figure());
    }
}

void FigureManager::switch_to_next()
{
    if (figures_.size() <= 1)
        return;
    size_t next = (active_index_ + 1) % figures_.size();
    switch_to(next);
}

void FigureManager::switch_to_previous()
{
    if (figures_.size() <= 1)
        return;
    size_t prev = (active_index_ == 0) ? figures_.size() - 1 : active_index_ - 1;
    switch_to(prev);
}

void FigureManager::move_tab(size_t from_index, size_t to_index)
{
    if (from_index >= figures_.size() || to_index >= figures_.size() || from_index == to_index)
    {
        return;
    }

    // Move figure
    auto fig = std::move(figures_[from_index]);
    figures_.erase(figures_.begin() + static_cast<std::ptrdiff_t>(from_index));
    figures_.insert(figures_.begin() + static_cast<std::ptrdiff_t>(to_index), std::move(fig));

    // Move state
    if (from_index < states_.size() && to_index < states_.size())
    {
        auto st = std::move(states_[from_index]);
        states_.erase(states_.begin() + static_cast<std::ptrdiff_t>(from_index));
        states_.insert(states_.begin() + static_cast<std::ptrdiff_t>(to_index), std::move(st));
    }

    // Update active index to follow the moved figure
    if (active_index_ == from_index)
    {
        active_index_ = to_index;
    }
    else if (from_index < to_index)
    {
        if (active_index_ > from_index && active_index_ <= to_index)
        {
            --active_index_;
        }
    }
    else
    {
        if (active_index_ >= to_index && active_index_ < from_index)
        {
            ++active_index_;
        }
    }

    sync_tab_bar();
}

Figure* FigureManager::active_figure() const
{
    if (active_index_ < figures_.size())
    {
        return figures_[active_index_].get();
    }
    return nullptr;
}

bool FigureManager::can_close(size_t index) const
{
    (void)index;
    return figures_.size() > 1;
}

FigureState& FigureManager::state(size_t index)
{
    ensure_states();
    if (index >= states_.size())
    {
        static FigureState dummy;
        return dummy;
    }
    return states_[index];
}

const FigureState& FigureManager::state(size_t index) const
{
    if (index >= states_.size())
    {
        static const FigureState dummy;
        return dummy;
    }
    return states_[index];
}

FigureState& FigureManager::active_state()
{
    return state(active_index_);
}

std::string FigureManager::get_title(size_t index) const
{
    if (index >= states_.size())
    {
        return default_title(index);
    }
    const auto& st = states_[index];
    if (!st.custom_title.empty())
    {
        return st.custom_title;
    }
    return default_title(index);
}

void FigureManager::set_title(size_t index, const std::string& title)
{
    ensure_states();
    if (index < states_.size())
    {
        states_[index].custom_title = title;
        if (tab_bar_)
        {
            tab_bar_->set_tab_title(index, title);
        }
    }
}

void FigureManager::mark_modified(size_t index, bool modified)
{
    ensure_states();
    if (index < states_.size())
    {
        states_[index].is_modified = modified;
    }
}

bool FigureManager::is_modified(size_t index) const
{
    if (index >= states_.size())
        return false;
    return states_[index].is_modified;
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

    if (pending_close_ != SIZE_MAX)
    {
        size_t idx = pending_close_;
        pending_close_ = SIZE_MAX;
        close_figure(idx);
        changed = true;
    }

    if (pending_switch_ != SIZE_MAX)
    {
        size_t idx = pending_switch_;
        pending_switch_ = SIZE_MAX;
        if (idx < figures_.size() && idx != active_index_)
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

void FigureManager::queue_close(size_t index)
{
    pending_close_ = index;
}

void FigureManager::queue_switch(size_t index)
{
    pending_switch_ = index;
}

void FigureManager::save_active_state()
{
    ensure_states();
    if (active_index_ >= figures_.size() || active_index_ >= states_.size())
    {
        return;
    }

    auto& fig = *figures_[active_index_];
    auto& st = states_[active_index_];

    // Snapshot axis limits
    st.axes_snapshots.clear();
    for (const auto& ax : fig.axes())
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

void FigureManager::restore_state(size_t index)
{
    if (index >= figures_.size() || index >= states_.size())
    {
        return;
    }

    auto& fig = *figures_[index];
    const auto& st = states_[index];

    // Restore axis limits
    for (size_t i = 0; i < st.axes_snapshots.size() && i < fig.axes().size(); ++i)
    {
        if (fig.axes_mut()[i])
        {
            fig.axes_mut()[i]->xlim(st.axes_snapshots[i].x_limits.min,
                                    st.axes_snapshots[i].x_limits.max);
            fig.axes_mut()[i]->ylim(st.axes_snapshots[i].y_limits.min,
                                    st.axes_snapshots[i].y_limits.max);
        }
    }
}

std::string FigureManager::default_title(size_t index)
{
    return "Figure " + std::to_string(index + 1);
}

void FigureManager::sync_tab_bar()
{
    if (!tab_bar_)
        return;

    // Rebuild tab bar to match figures
    // Remove all tabs except the first (which can't be closed)
    while (tab_bar_->get_tab_count() > 1)
    {
        // Find a closeable tab and remove it
        bool removed = false;
        for (size_t i = tab_bar_->get_tab_count(); i > 0; --i)
        {
            size_t idx = i - 1;
            // Try to remove — remove_tab skips non-closeable tabs
            size_t before = tab_bar_->get_tab_count();
            tab_bar_->remove_tab(idx);
            if (tab_bar_->get_tab_count() < before)
            {
                removed = true;
                break;
            }
        }
        if (!removed)
            break;  // Only non-closeable tabs remain
    }

    // Set first tab title
    if (!figures_.empty())
    {
        tab_bar_->set_tab_title(0, get_title(0));
    }

    // Add remaining tabs
    for (size_t i = 1; i < figures_.size(); ++i)
    {
        tab_bar_->add_tab(get_title(i));
    }

    // Set active
    if (active_index_ < figures_.size())
    {
        tab_bar_->set_active_tab(active_index_);
    }
}

void FigureManager::ensure_states()
{
    while (states_.size() < figures_.size())
    {
        FigureState st;
        st.custom_title = default_title(states_.size());
        states_.push_back(std::move(st));
    }
}

size_t FigureManager::next_figure_number() const
{
    // Find the highest figure number used
    size_t max_num = figures_.size();
    for (const auto& st : states_)
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
