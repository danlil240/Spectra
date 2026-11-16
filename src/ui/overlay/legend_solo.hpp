#pragma once

// Legend "solo": double-clicking a legend entry shows only that series in its
// axes; double-clicking it again restores the visibility every series had
// before.  Non-destructive: the saved visibilities are only compared by
// pointer, and series added while soloed are left alone.  Header-only and
// toolkit-free so it can be unit tested.

#include <spectra/axes.hpp>
#include <spectra/series.hpp>

#include <unordered_map>
#include <utility>
#include <vector>

namespace spectra
{

class LegendSolo
{
   public:
    // Solo @p target in @p axes, or restore when @p target is already the
    // soloed series.  Soloing another entry while one is soloed switches the
    // solo and keeps the original visibilities for the eventual restore.
    // Returns true when @p target is soloed afterwards.
    bool toggle(AxesBase& axes, Series& target)
    {
        auto it = states_.find(&axes);
        // State left by an axes that was destroyed (same address reused):
        // its soloed series is gone, so start afresh.
        if (it != states_.end() && !contains(axes, it->second.soloed))
        {
            states_.erase(it);
            it = states_.end();
        }
        if (it != states_.end() && it->second.soloed == &target)
        {
            restore(axes, it->second);
            states_.erase(it);
            return false;
        }
        if (it == states_.end())
        {
            State st;
            for (const auto& s : axes.series())
                if (s)
                    st.saved.emplace_back(s.get(), s->visible());
            it = states_.emplace(&axes, std::move(st)).first;
        }
        it->second.soloed = &target;
        for (const auto& s : axes.series())
            if (s)
                s->visible(s.get() == &target);
        return true;
    }

    // Soloed series of @p axes, or nullptr.
    const Series* soloed(const AxesBase& axes) const
    {
        auto it = states_.find(&axes);
        return it != states_.end() ? it->second.soloed : nullptr;
    }

    // Drop the state of an axes that is going away (nothing is restored).
    void forget(const AxesBase* axes) { states_.erase(axes); }
    void clear() { states_.clear(); }

   private:
    struct State
    {
        const Series*                               soloed = nullptr;
        std::vector<std::pair<const Series*, bool>> saved;
    };

    static bool contains(const AxesBase& axes, const Series* series)
    {
        for (const auto& s : axes.series())
            if (s.get() == series)
                return true;
        return false;
    }

    static void restore(AxesBase& axes, const State& st)
    {
        for (const auto& s : axes.series())
        {
            if (!s)
                continue;
            for (const auto& [ptr, vis] : st.saved)
            {
                if (ptr == s.get())
                {
                    s->visible(vis);
                    break;
                }
            }
        }
    }

    std::unordered_map<const AxesBase*, State> states_;
};

}   // namespace spectra
