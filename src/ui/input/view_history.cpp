#include "view_history.hpp"

#include <algorithm>
#include <spectra/figure.hpp>
#include <utility>

namespace spectra
{

const AxesView* find_axes_view(const ViewState& state, const Axes* ax)
{
    for (const auto& v : state)
        if (v.axes == ax)
            return &v;
    return nullptr;
}

void set_axes_view(ViewState& state, Axes* ax, AxisLimits x, AxisLimits y)
{
    for (auto& v : state)
    {
        if (v.axes == ax)
        {
            v.x = x;
            v.y = y;
            return;
        }
    }
    state.push_back({ax, x, y});
}

namespace
{
bool same_limits(const AxisLimits& a, const AxisLimits& b)
{
    return a.min == b.min && a.max == b.max;
}
}   // namespace

bool same_view(const ViewState& a, const ViewState& b)
{
    if (a.size() != b.size())
        return false;
    for (const auto& v : a)
    {
        const AxesView* o = find_axes_view(b, v.axes);
        if (!o || !same_limits(v.x, o->x) || !same_limits(v.y, o->y))
            return false;
    }
    return true;
}

size_t apply_view_state(Figure& fig, const ViewState& state)
{
    size_t applied = 0;
    for (const auto& v : state)
    {
        if (!v.axes)
            continue;
        const auto& all = fig.axes();
        const bool  live =
            std::any_of(all.begin(), all.end(), [&](const auto& p) { return p.get() == v.axes; });
        if (!live)
            continue;
        v.axes->xlim(v.x.min, v.x.max);
        v.axes->ylim(v.y.min, v.y.max);
        ++applied;
    }
    return applied;
}

// ─── ViewHistory ────────────────────────────────────────────────────────────

ViewHistory::PushResult ViewHistory::push(std::string       label,
                                          ViewState         before,
                                          ViewState         after,
                                          std::string       coalesce_key,
                                          Clock::duration   coalesce_window,
                                          Clock::time_point now)
{
    if (!coalesce_key.empty() && cursor_ == entries_.size() && !entries_.empty())
    {
        auto&      top     = entries_.back();
        const bool in_time = coalesce_window <= Clock::duration::zero()
                             || (now >= top->updated && now - top->updated <= coalesce_window);
        if (top->coalesce_key == coalesce_key && in_time)
        {
            // Axes new to this burst (linked meanwhile) keep their first state.
            for (const auto& v : before)
                if (!find_axes_view(top->before, v.axes))
                    top->before.push_back(v);
            for (const auto& v : after)
                set_axes_view(top->after, v.axes, v.x, v.y);
            top->updated = now;
            return {top, true};
        }
    }

    if (same_view(before, after))
        return {};

    entries_.resize(cursor_);   // a new change drops the forward entries
    auto entry          = std::make_shared<ViewHistoryEntry>();
    entry->label        = std::move(label);
    entry->before       = std::move(before);
    entry->after        = std::move(after);
    entry->coalesce_key = std::move(coalesce_key);
    entry->updated      = now;
    entries_.push_back(entry);
    if (entries_.size() > MAX_ENTRIES)
        entries_.erase(entries_.begin());
    cursor_ = entries_.size();
    return {entry, false};
}

const ViewState* ViewHistory::back()
{
    if (cursor_ == 0)
        return nullptr;
    --cursor_;
    return &entries_[cursor_]->before;
}

const ViewState* ViewHistory::forward()
{
    if (cursor_ >= entries_.size())
        return nullptr;
    return &entries_[cursor_++]->after;
}

const ViewState& ViewHistory::undo_entry(const std::shared_ptr<ViewHistoryEntry>& entry)
{
    if (cursor_ > 0 && entries_[cursor_ - 1] == entry)
        --cursor_;
    return entry->before;
}

const ViewState& ViewHistory::redo_entry(const std::shared_ptr<ViewHistoryEntry>& entry)
{
    if (cursor_ < entries_.size() && entries_[cursor_] == entry)
        ++cursor_;
    return entry->after;
}

std::string ViewHistory::back_label() const
{
    return cursor_ > 0 ? entries_[cursor_ - 1]->label : std::string{};
}

std::string ViewHistory::forward_label() const
{
    return cursor_ < entries_.size() ? entries_[cursor_]->label : std::string{};
}

void ViewHistory::clear()
{
    entries_.clear();
    cursor_ = 0;
}

// ─── ViewHistoryStore ───────────────────────────────────────────────────────

std::shared_ptr<ViewHistory> ViewHistoryStore::history(const Figure* fig)
{
    auto& slot = histories_[fig];
    if (!slot)
        slot = std::make_shared<ViewHistory>();
    return slot;
}

ViewHistory* ViewHistoryStore::find(const Figure* fig) const
{
    auto it = histories_.find(fig);
    return it == histories_.end() ? nullptr : it->second.get();
}

void ViewHistoryStore::forget(const Figure* fig)
{
    histories_.erase(fig);
}

}   // namespace spectra
