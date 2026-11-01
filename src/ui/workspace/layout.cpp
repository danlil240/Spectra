#include "layout.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/series.hpp>
#include <system_error>

#include "core/config_paths.hpp"

namespace spectra
{

size_t LayoutOpenResult::count(BindingStatus status) const
{
    return static_cast<size_t>(std::count_if(slots.begin(),
                                             slots.end(),
                                             [status](const LayoutBindingSlot& s)
                                             { return s.result.status == status; }));
}

std::string unresolved_label(const std::string& label, const std::string& uri)
{
    const std::string what = uri.empty() ? std::string("no data source") : uri;
    return label.empty() ? "[unresolved: " + what + "]" : label + " [unresolved: " + what + "]";
}

Series* find_series(const FigureRegistry& registry, FigureId figure_id, const Series* series)
{
    if (!series)
        return nullptr;
    Figure* fig = registry.get(figure_id);
    if (!fig)
        return nullptr;
    Series* found = nullptr;
    fig->for_each_axes(
        [&](AxesBase* ax)
        {
            if (found)
                return;
            for (auto& s : ax->series_mut())
            {
                if (s.get() == series)
                {
                    found = s.get();
                    return;
                }
            }
        });
    return found;
}

namespace
{

void resolve_slot(LayoutBindingSlot& slot, Series& series, const BindingResolver& resolver)
{
    series.set_data_binding(slot.uri);
    if (slot.uri.empty())
    {
        slot.result = {BindingStatus::Unresolved, "no data binding saved for this series"};
    }
    else
    {
        slot.result =
            resolver.resolve(slot.uri, series, slot.figure_id, slot.axes_index, slot.steps);
    }
    series.label(slot.result.status == BindingStatus::Unresolved
                     ? unresolved_label(slot.label, slot.uri)
                     : slot.label);
}

}   // namespace

LayoutOpenResult instantiate_layout(const WorkspaceData&   data,
                                    FigureRegistry&        registry,
                                    const BindingResolver& resolver)
{
    LayoutOpenResult     out;
    std::vector<Figure*> figs;
    for (const auto& fs : data.figures)
    {
        // A plain workspace has no bindings: its figures open without series.
        auto fig =
            data.layout_only ? Workspace::build_layout_figure(fs) : Workspace::build_figure(fs);
        FigureId id = registry.register_figure(std::move(fig));
        out.figure_ids.push_back(id);
        figs.push_back(registry.get(id));
    }
    Workspace::apply(data, figs);

    for (size_t fi = 0; fi < figs.size(); ++fi)
    {
        Figure* fig = figs[fi];
        if (!fig)
            continue;
        for (size_t ai = 0; ai < fig->axes().size(); ++ai)
        {
            Axes* ax = fig->axes()[ai].get();
            if (!ax)
                continue;
            std::vector<LayoutTransformStep> steps;
            for (const auto& t : data.transforms)
            {
                if (t.figure_index == fi && t.axes_index == ai)
                    steps.insert(steps.end(), t.steps.begin(), t.steps.end());
            }
            for (auto& s : ax->series_mut())
            {
                if (!s)
                    continue;
                LayoutBindingSlot slot;
                slot.figure_id  = out.figure_ids[fi];
                slot.axes_index = static_cast<uint32_t>(ai);
                slot.series     = s.get();
                slot.uri        = s->data_binding();
                slot.label      = s->label();
                slot.steps      = steps;
                resolve_slot(slot, *s, resolver);
                out.slots.push_back(std::move(slot));
            }
        }
    }
    return out;
}

bool rebind_slot(LayoutBindingSlot&     slot,
                 const std::string&     uri,
                 FigureRegistry&        registry,
                 const BindingResolver& resolver)
{
    Series* series = find_series(registry, slot.figure_id, slot.series);
    if (!series)
        return false;
    slot.uri = uri;
    resolve_slot(slot, *series, resolver);
    return true;
}

std::string remap_dock_figures(const std::string&                                      dock_state,
                               const std::function<std::optional<FigureId>(FigureId)>& map)
{
    std::string out;
    out.reserve(dock_state.size());
    size_t pos = 0;
    while (pos < dock_state.size())
    {
        size_t next = std::string::npos;
        size_t klen = 0;
        for (const char* key : {"\"figure\":", "\"active\":"})
        {
            const size_t p = dock_state.find(key, pos);
            if (p < next)
            {
                next = p;
                klen = std::char_traits<char>::length(key);
            }
        }
        if (next == std::string::npos)
        {
            out.append(dock_state, pos, std::string::npos);
            break;
        }
        const size_t num_start = next + klen;
        size_t       num_end   = num_start;
        while (num_end < dock_state.size()
               && std::isdigit(static_cast<unsigned char>(dock_state[num_end])))
            ++num_end;
        out.append(dock_state, pos, num_start - pos);
        if (num_end > num_start)
        {
            const std::string digits = dock_state.substr(num_start, num_end - num_start);
            FigureId          id     = 0;
            const auto [ptr, ec] =
                std::from_chars(digits.data(), digits.data() + digits.size(), id);
            const auto mapped = (ec == std::errc{} && ptr == digits.data() + digits.size())
                                    ? map(id)
                                    : std::nullopt;
            out += mapped ? std::to_string(*mapped) : digits;
        }
        pos = num_end;
    }
    return out;
}

// ─── RecentLayouts ──────────────────────────────────────────────────────────

RecentLayouts::RecentLayouts(std::string path, size_t max_entries)
    : path_(std::move(path)), max_entries_(std::max<size_t>(1, max_entries))
{
}

std::string RecentLayouts::default_path()
{
    const std::filesystem::path dir = user_config_dir();
    if (dir.empty())
        return {};
    return (dir / "recent_layouts.txt").string();
}

bool RecentLayouts::load()
{
    entries_.clear();
    if (path_.empty())
        return false;
    std::ifstream f(path_);
    if (!f.is_open())
        return false;
    std::string line;
    while (std::getline(f, line) && entries_.size() < max_entries_)
    {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (!line.empty() && std::find(entries_.begin(), entries_.end(), line) == entries_.end())
            entries_.push_back(line);
    }
    return true;
}

bool RecentLayouts::save() const
{
    if (path_.empty())
        return true;
    const std::filesystem::path p(path_);
    std::error_code             ec;
    if (p.has_parent_path())
        std::filesystem::create_directories(p.parent_path(), ec);
    const std::filesystem::path tmp = p.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f.is_open())
            return false;
        for (const auto& e : entries_)
            f << e << '\n';
        if (!f.good())
            return false;
    }
    std::filesystem::rename(tmp, p, ec);
    if (ec)
    {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

void RecentLayouts::push(const std::string& file)
{
    if (file.empty())
        return;
    std::error_code ec;
    auto            abs  = std::filesystem::absolute(file, ec);
    std::string     norm = ec ? file : abs.lexically_normal().string();
    entries_.erase(std::remove(entries_.begin(), entries_.end(), norm), entries_.end());
    entries_.insert(entries_.begin(), norm);
    if (entries_.size() > max_entries_)
        entries_.resize(max_entries_);
    save();
}

void RecentLayouts::remove(const std::string& file)
{
    entries_.erase(std::remove(entries_.begin(), entries_.end(), file), entries_.end());
    save();
}

void RecentLayouts::clear()
{
    entries_.clear();
    save();
}

}   // namespace spectra
