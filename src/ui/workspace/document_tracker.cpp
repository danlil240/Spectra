#include "document_tracker.hpp"

#include <cstring>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <string_view>

namespace spectra
{

namespace
{

// FNV-1a, 64 bit.
class Hasher
{
   public:
    void bytes(const void* data, size_t n)
    {
        const auto* p = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < n; ++i)
        {
            h_ ^= p[i];
            h_ *= 1099511628211ULL;
        }
    }
    template <typename T>
    void value(const T& v)
    {
        bytes(&v, sizeof(T));
    }
    void str(std::string_view s)
    {
        value(s.size());
        bytes(s.data(), s.size());
    }
    uint64_t get() const { return h_; }

   private:
    uint64_t h_ = 1469598103934665603ULL;
};

void hash_series(Hasher& h, const Series& s)
{
    h.value(reinterpret_cast<uintptr_t>(&s));
    h.value(s.change_generation());
    h.str(s.label());
    h.value(s.visible());
    const PlotStyle& st = s.plot_style();
    h.value(static_cast<int>(st.line_style));
    h.value(static_cast<int>(st.marker_style));
    h.value(st.line_width);
    h.value(st.marker_size);
    h.value(st.opacity);
    h.value(s.auto_color_index());
    h.value(s.color_letter());
    // Theme-resolved colours change with the theme, explicit ones are the user's.
    if (!s.has_auto_color() && s.color_letter() == '\0')
    {
        const Color& c = s.color();
        h.value(c.r);
        h.value(c.g);
        h.value(c.b);
        h.value(c.a);
    }
}

void hash_axes(Hasher& h, const AxesBase& ax)
{
    h.value(reinterpret_cast<uintptr_t>(&ax));
    h.str(ax.get_title());
    h.value(ax.grid_enabled());
    if (const auto* a2 = dynamic_cast<const Axes*>(&ax))
    {
        h.str(a2->get_xlabel());
        h.str(a2->get_ylabel());
        h.value(a2->annotations().size());
    }
    else if (const auto* a3 = dynamic_cast<const Axes3D*>(&ax))
    {
        h.str(a3->get_xlabel());
        h.str(a3->get_ylabel());
        h.str(a3->get_zlabel());
    }
    h.value(ax.series().size());
    for (const auto& s : ax.series())
    {
        if (s)
            hash_series(h, *s);
    }
}

}   // namespace

uint64_t figure_fingerprint(const Figure& figure)
{
    Hasher h;
    h.value(figure.grid_rows());
    h.value(figure.grid_cols());
    h.value(figure.legend().visible);
    h.value(static_cast<int>(figure.legend().position));
    h.str(figure.tab_title());
    h.value(figure.axes().size());
    h.value(figure.all_axes().size());
    figure.for_each_axes([&h](const AxesBase* ax) { hash_axes(h, *ax); });
    return h.get();
}

bool DocumentTracker::detect(const Snapshot& now)
{
    if (!initialized_)
    {
        mark_saved(now);
        return false;
    }
    bool changed = false;
    for (const auto& [id, fp] : now.figures)
    {
        auto it = figures_.find(id);
        if (it == figures_.end())
        {
            // A figure that appeared since the last check (structure tracks it).
            figures_[id] = Entry{fp, false};
            continue;
        }
        if (!it->second.modified && it->second.baseline != fp)
        {
            it->second.modified = true;
            changed             = true;
        }
        else if (it->second.modified && it->second.baseline != fp)
        {
            // Keeps changing: report so autosave restarts its debounce.
            changed             = true;
            it->second.baseline = fp;
            it->second.modified = true;
        }
    }
    if (structure_.baseline != now.structure)
    {
        structure_.baseline = now.structure;
        structure_.modified = true;
        changed             = true;
    }
    return changed;
}

void DocumentTracker::absorb(const Snapshot& now)
{
    if (!initialized_)
    {
        mark_saved(now);
        return;
    }
    std::unordered_map<FigureId, Entry> next;
    next.reserve(now.figures.size());
    for (const auto& [id, fp] : now.figures)
    {
        auto it = figures_.find(id);
        if (it == figures_.end())
        {
            next[id] = Entry{fp, false};
            continue;
        }
        Entry e = it->second;
        if (!e.modified)
            e.baseline = fp;
        next[id] = e;
    }
    figures_ = std::move(next);
    if (!structure_.modified)
        structure_.baseline = now.structure;
}

void DocumentTracker::mark_saved(const Snapshot& now)
{
    figures_.clear();
    for (const auto& [id, fp] : now.figures)
        figures_[id] = Entry{fp, false};
    structure_   = Entry{now.structure, false};
    initialized_ = true;
    forced_      = false;
}

bool DocumentTracker::is_modified() const
{
    if (forced_ || structure_.modified)
        return true;
    for (const auto& [id, e] : figures_)
    {
        if (e.modified)
            return true;
    }
    return false;
}

bool DocumentTracker::is_figure_modified(FigureId id) const
{
    auto it = figures_.find(id);
    if (it == figures_.end())
        return false;
    // A document-wide mark (restored autosave, merged open) cannot say which
    // figure changed: treat every figure as modified so closing one prompts.
    return forced_ || it->second.modified;
}

size_t DocumentTracker::modified_figure_count() const
{
    size_t n = 0;
    for (const auto& [id, e] : figures_)
        n += e.modified ? 1 : 0;
    return n;
}

}   // namespace spectra
