#include "series_transform.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>

#include "math/data_transform.hpp"
#include "series_clipboard.hpp"
#include "ui/input/selection_context.hpp"
#include "undo_manager.hpp"

namespace spectra
{

namespace
{

bool is_xy_series(const Series* s)
{
    return dynamic_cast<const LineSeries*>(s) != nullptr
           || dynamic_cast<const ScatterSeries*>(s) != nullptr;
}

std::span<const float> series_x(const Series& s)
{
    if (const auto* ls = dynamic_cast<const LineSeries*>(&s))
        return ls->x_data();
    if (const auto* sc = dynamic_cast<const ScatterSeries*>(&s))
        return sc->x_data();
    return {};
}

std::span<const float> series_y(const Series& s)
{
    if (const auto* ls = dynamic_cast<const LineSeries*>(&s))
        return ls->y_data();
    if (const auto* sc = dynamic_cast<const ScatterSeries*>(&s))
        return sc->y_data();
    return {};
}

void set_series_data(Series&                s,
                     std::span<const float> x,
                     std::span<const float> y,
                     double                 x_off,
                     double                 y_off)
{
    if (auto* ls = dynamic_cast<LineSeries*>(&s))
        ls->set_x(x).set_y(y);
    else if (auto* sc = dynamic_cast<ScatterSeries*>(&s))
        sc->set_x(x).set_y(y);
    s.x_offset(x_off);
    s.y_offset(y_off);
}

std::optional<size_t> index_of(const AxesBase& ax, const Series* s)
{
    const auto& sv = ax.series();
    for (size_t i = 0; i < sv.size(); ++i)
    {
        if (sv[i].get() == s)
            return i;
    }
    return std::nullopt;
}

bool figure_owns(const Figure& fig, const Axes* ax)
{
    if (!ax)
        return false;
    return std::any_of(fig.axes().begin(),
                       fig.axes().end(),
                       [ax](const std::unique_ptr<Axes>& a) { return a.get() == ax; });
}

// Copies keep the source colour and differ by line (or marker) style.
void make_distinguishable(SeriesSnapshot& snap)
{
    if (snap.type == SeriesSnapshot::Type::Line && snap.style.line_style != LineStyle::None)
    {
        snap.style.line_style =
            snap.style.line_style == LineStyle::Dashed ? LineStyle::Dotted : LineStyle::Dashed;
        return;
    }
    snap.style.marker_style = snap.style.marker_style == MarkerStyle::TriangleUp
                                  ? MarkerStyle::Square
                                  : MarkerStyle::TriangleUp;
}

Series* create_copy(Axes& ax, const Series* source, const SeriesSnapshot& snap, double x_off)
{
    Series* s = SeriesClipboard::paste_to(ax, snap);
    if (!s)
        return nullptr;
    s->x_offset(x_off);
    s->y_offset(0.0);
    const auto src_idx = source ? index_of(ax, source) : std::nullopt;
    const auto new_idx = index_of(ax, s);
    if (src_idx && new_idx && *new_idx != *src_idx + 1)
        ax.move_series(*new_idx, *src_idx + 1);
    return s;
}

void remove_now(AxesBase* ax, Series* s)
{
    if (!ax || !s)
        return;
    if (auto idx = index_of(*ax, s))
        ax->remove_series(*idx);
}

struct Extent
{
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();

    void add(double v)
    {
        if (!std::isfinite(v))
            return;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    bool valid() const { return lo <= hi; }
};

// Rule documented in the header: fit when the visible data leaves the view or
// fills less than a quarter of it on either axis.
bool view_needs_fit(const Axes& ax, const AxisLimits& vx, const AxisLimits& vy)
{
    Extent ex;
    Extent ey;
    for (const auto& sp : ax.series())
    {
        if (!sp || !sp->visible() || sp->excluded_from_autoscale() || !is_xy_series(sp.get()))
            continue;
        const auto   xs = series_x(*sp);
        const auto   ys = series_y(*sp);
        const size_t n  = std::min(xs.size(), ys.size());
        for (size_t i = 0; i < n; ++i)
        {
            ex.add(sp->x_offset() + xs[i]);
            ey.add(sp->y_offset() + ys[i]);
        }
    }
    if (!ex.valid() || !ey.valid())
        return false;

    auto misfit = [](const Extent& e, const AxisLimits& v)
    {
        const double span = v.max - v.min;
        if (!(span > 0.0) || !std::isfinite(span))
            return true;
        const double tol = span * 1e-6;
        if (e.lo < v.min - tol || e.hi > v.max + tol)
            return true;
        return (e.hi - e.lo) < 0.25 * span;
    };
    return misfit(ex, vx) || misfit(ey, vy);
}

struct OverwriteRecord
{
    Axes*              axes   = nullptr;
    Series*            series = nullptr;
    std::vector<float> old_x, old_y, new_x, new_y;
    double             old_x_off = 0.0, old_y_off = 0.0, new_x_off = 0.0;
    std::string        old_label, new_label;
};

struct CopyRecord
{
    Axes*          axes    = nullptr;
    Series*        source  = nullptr;
    Series*        current = nullptr;   // the live copy, null while undone
    SeriesSnapshot snap;
    double         x_off = 0.0;
};

struct AxesRecord
{
    Axes*       axes      = nullptr;
    bool        rescaled  = false;
    bool        relabeled = false;
    AxisLimits  old_x, old_y;
    std::string old_ylabel, new_ylabel;
};

struct TransformState
{
    std::vector<OverwriteRecord>            overwrites;
    std::vector<CopyRecord>                 copies;
    std::vector<AxesRecord>                 axes;
    std::function<void(AxesBase*, Series*)> remove_series;
};

void undo_state(TransformState& st)
{
    for (auto& c : st.copies)
    {
        if (!c.current || !c.axes)
            continue;
        if (index_of(*c.axes, c.current))
        {
            if (st.remove_series)
                st.remove_series(c.axes, c.current);
            else
                remove_now(c.axes, c.current);
        }
        c.current = nullptr;
    }
    for (auto& o : st.overwrites)
    {
        if (!o.axes || !index_of(*o.axes, o.series))
            continue;
        set_series_data(*o.series, o.old_x, o.old_y, o.old_x_off, o.old_y_off);
        o.series->label(o.old_label);
    }
    for (auto& a : st.axes)
    {
        if (a.relabeled)
            a.axes->ylabel(a.old_ylabel);
        if (a.rescaled)
        {
            a.axes->xlim(a.old_x.min, a.old_x.max);
            a.axes->ylim(a.old_y.min, a.old_y.max);
        }
    }
}

void redo_state(TransformState& st)
{
    for (auto& o : st.overwrites)
    {
        if (!o.axes || !index_of(*o.axes, o.series))
            continue;
        set_series_data(*o.series, o.new_x, o.new_y, o.new_x_off, 0.0);
        o.series->label(o.new_label);
    }
    for (auto& c : st.copies)
    {
        if (c.current || !c.axes)
            continue;
        c.current = create_copy(*c.axes, c.source, c.snap, c.x_off);
    }
    for (auto& a : st.axes)
    {
        if (a.relabeled)
            a.axes->ylabel(a.new_ylabel);
        if (a.rescaled)
            a.axes->auto_fit();
    }
}

}   // namespace

// ─── Targets ────────────────────────────────────────────────────────────────

SeriesTransformTargets resolve_transform_targets(const ui::SelectionContext& sel,
                                                 Figure*                     figure,
                                                 Axes*                       active)
{
    SeriesTransformTargets out;
    if (!figure)
        return out;

    if (sel.type == ui::SelectionType::Series && sel.figure == figure)
    {
        for (const auto& e : sel.selected_series)
        {
            Axes* ax = e.axes ? e.axes : dynamic_cast<Axes*>(e.axes_base);
            if (!ax || !e.series || !figure_owns(*figure, ax) || !index_of(*ax, e.series))
                continue;
            if (is_xy_series(e.series))
                out.targets.push_back({ax, e.series});
        }
        if (!out.targets.empty())
        {
            out.from_selection = true;
            return out;
        }
    }

    Axes* plot = nullptr;
    if (sel.type == ui::SelectionType::Axes && sel.figure == figure
        && figure_owns(*figure, sel.axes))
        plot = sel.axes;
    else if (figure_owns(*figure, active))
        plot = active;
    else
    {
        for (const auto& ax : figure->axes())
        {
            if (ax)
            {
                plot = ax.get();
                break;
            }
        }
    }
    if (!plot)
        return out;

    out.plot = plot;
    for (const auto& sp : plot->series())
    {
        if (sp && sp->visible() && is_xy_series(sp.get()))
            out.targets.push_back({plot, sp.get()});
    }
    return out;
}

std::string describe_transform_targets(const SeriesTransformTargets& targets)
{
    if (targets.from_selection)
    {
        return "Applies to: " + std::to_string(targets.targets.size()) + " selected series";
    }
    const std::string title =
        targets.plot && !targets.plot->title().empty() ? " (" + targets.plot->title() + ")" : "";
    if (targets.targets.empty())
        return "Applies to: no line or scatter series in current plot" + title;
    return "Applies to: all series in current plot" + title;
}

// ─── Specs and names ────────────────────────────────────────────────────────

std::string transform_suffix(const std::string& name)
{
    static const std::pair<const char*, const char*> k_suffixes[] = {
        {"Identity", "identity"},
        {"Log10", "log10"},
        {"Ln", "ln"},
        {"Abs", "abs"},
        {"Negate", "neg"},
        {"Normalize", "norm"},
        {"Standardize", "z-score"},
        {"Derivative", "d/dt"},
        {"CumulativeSum", "cumsum"},
        {"Diff", "diff"},
        {"Scale", "scale"},
        {"Offset", "offset"},
        {"Clamp", "clamp"},
        {"FFT", "FFT"},
    };
    for (const auto& [n, s] : k_suffixes)
    {
        if (name == n)
            return s;
    }
    return name;
}

std::string transformed_series_name(const std::string& name,
                                    const std::string& suffix,
                                    size_t             series_index)
{
    const std::string base = name.empty() ? "series " + std::to_string(series_index + 1) : name;
    return base + " \xC2\xB7 " + suffix;   // U+00B7 middle dot
}

bool make_registry_transform_spec(const std::string& name, SeriesTransformSpec& out)
{
    DataTransform xform;
    if (!TransformRegistry::instance().get_transform(name, xform))
        return false;

    out.fn            = [xform](std::span<const float> x,
                     std::span<const float> y,
                     std::vector<float>&    rx,
                     std::vector<float>&    ry) { xform.apply_y(x, y, rx, ry); };
    out.suffix        = transform_suffix(name);
    out.description   = "Transform: " + name;
    const auto t      = xform.type();
    out.changes_units = t == TransformType::Derivative || t == TransformType::FFT
                        || t == TransformType::Log10 || t == TransformType::Ln;
    out.x_absolute = t == TransformType::FFT;
    return true;
}

// ─── Apply ──────────────────────────────────────────────────────────────────

SeriesTransformResult apply_series_transform(const std::vector<SeriesTransformTarget>& targets,
                                             const SeriesTransformSpec&                spec,
                                             SeriesTransformMode                       mode,
                                             UndoManager*                              undo,
                                             const SeriesTransformHooks&               hooks)
{
    SeriesTransformResult result;
    if (!spec.fn)
        return result;

    auto state           = std::make_shared<TransformState>();
    state->remove_series = hooks.remove_series;

    // Affected axes in first-seen order, with the view before any change.
    auto axes_record = [&](Axes* ax) -> AxesRecord&
    {
        for (auto& a : state->axes)
        {
            if (a.axes == ax)
                return a;
        }
        AxesRecord rec;
        rec.axes  = ax;
        rec.old_x = ax->x_limits();
        rec.old_y = ax->y_limits();
        state->axes.push_back(rec);
        return state->axes.back();
    };

    // Compute every output before changing anything, so that targets which
    // read each other's data (custom formulas) all see the original values.
    struct Pending
    {
        SeriesTransformTarget target;
        size_t                index = 0;
        std::vector<float>    x, y;
    };
    std::vector<Pending> pending;
    pending.reserve(targets.size());
    for (const auto& t : targets)
    {
        if (!t.axes || !t.series || !is_xy_series(t.series))
            continue;
        const auto idx = index_of(*t.axes, t.series);
        if (!idx)
            continue;
        axes_record(t.axes);

        const auto             xs    = series_x(*t.series);
        const auto             ys    = series_y(*t.series);
        const double           y_off = t.series->y_offset();
        std::vector<float>     y_logical;
        std::span<const float> y_in = ys;
        if (y_off != 0.0)
        {
            y_logical.resize(ys.size());
            for (size_t i = 0; i < ys.size(); ++i)
                y_logical[i] = static_cast<float>(y_off + static_cast<double>(ys[i]));
            y_in = y_logical;
        }

        Pending p;
        p.target = t;
        p.index  = *idx;
        spec.fn(xs, y_in, p.x, p.y);
        const size_t n = std::min(p.x.size(), p.y.size());
        p.x.resize(n);
        p.y.resize(n);
        pending.push_back(std::move(p));
    }
    if (pending.empty())
        return result;

    for (auto& p : pending)
    {
        Series&      src   = *p.target.series;
        const double x_off = spec.x_absolute ? 0.0 : src.x_offset();
        const auto   name  = transformed_series_name(src.label(), spec.suffix, p.index);

        if (mode == SeriesTransformMode::Overwrite)
        {
            OverwriteRecord o;
            o.axes        = p.target.axes;
            o.series      = &src;
            const auto xs = series_x(src);
            const auto ys = series_y(src);
            o.old_x.assign(xs.begin(), xs.end());
            o.old_y.assign(ys.begin(), ys.end());
            o.old_x_off = src.x_offset();
            o.old_y_off = src.y_offset();
            o.old_label = src.label();
            o.new_x     = std::move(p.x);
            o.new_y     = std::move(p.y);
            o.new_x_off = x_off;
            o.new_label = name;
            set_series_data(src, o.new_x, o.new_y, o.new_x_off, 0.0);
            src.label(o.new_label);
            state->overwrites.push_back(std::move(o));
        }
        else
        {
            CopyRecord c;
            c.axes         = p.target.axes;
            c.source       = &src;
            c.snap         = SeriesClipboard::snapshot(src);
            c.snap.x_data  = std::move(p.x);
            c.snap.y_data  = std::move(p.y);
            c.snap.label   = name;
            c.snap.visible = true;
            c.x_off        = x_off;
            make_distinguishable(c.snap);
            c.current = create_copy(*c.axes, c.source, c.snap, c.x_off);
            if (c.current)
                result.created.push_back(c.current);
            state->copies.push_back(std::move(c));
        }
        ++result.applied;
    }

    for (auto& a : state->axes)
    {
        // Y label: overwrite mode only, when every series of the axes changed.
        if (mode == SeriesTransformMode::Overwrite && spec.changes_units
            && !a.axes->ylabel().empty())
        {
            const auto& sv  = a.axes->series();
            const bool  all = std::all_of(sv.begin(),
                                         sv.end(),
                                         [&](const std::unique_ptr<Series>& s)
                                         {
                                             return std::any_of(state->overwrites.begin(),
                                                                state->overwrites.end(),
                                                                [&](const OverwriteRecord& o)
                                                                { return o.series == s.get(); });
                                         });
            if (all)
            {
                a.relabeled  = true;
                a.old_ylabel = a.axes->ylabel();
                a.new_ylabel = a.old_ylabel + " \xC2\xB7 " + spec.suffix;
                a.axes->ylabel(a.new_ylabel);
                result.relabeled.push_back(a.axes);
            }
        }
        if (view_needs_fit(*a.axes, a.old_x, a.old_y))
        {
            a.rescaled = true;
            a.axes->auto_fit();
            result.rescaled.push_back(a.axes);
        }
    }

    if (undo)
    {
        UndoAction action;
        action.description = spec.description.empty() ? "Apply transform" : spec.description;
        action.undo_fn     = [state]() { undo_state(*state); };
        action.redo_fn     = [state]() { redo_state(*state); };
        undo->push(std::move(action));
    }
    return result;
}

}   // namespace spectra
