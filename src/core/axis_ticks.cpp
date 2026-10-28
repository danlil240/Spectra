#include "core/axis_ticks.hpp"

#include "core/axis_scale.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <limits>

namespace spectra
{

namespace
{

// An offset is split off when the labels would otherwise repeat this many
// leading digits: |value| >= OFFSET_MIN_RATIO * span (1e7 ± 3, epoch seconds).
constexpr double OFFSET_MIN_RATIO = 1e4;
// Labels whose magnitude reaches 10^SCI_MAX_EXP or stays below
// 10^(SCI_MIN_EXP + 1) get a shared ×10^n multiplier instead.
constexpr int SCI_MAX_EXP = 6;
constexpr int SCI_MIN_EXP = -4;
// Hard cap so absurd inputs (huge viewport, tiny label estimate) stay cheap.
constexpr long long MAX_TICKS          = 200;
constexpr size_t    MAX_CATEGORY_TICKS = 100;
// Category ticks / integer steps closer than this (pixels) are thinned out.
constexpr double MIN_CATEGORY_PX = 30.0;
// Stepping down to reach two ticks never goes below this fraction of the target.
constexpr double MIN_STEP_FRACTION = 0.35;
// Sqrt axes: neighbouring ticks stay at least this fraction of the target apart
// (the square root compresses large |v|, so the coarse step is set there).
constexpr double SQRT_MIN_GAP_FRACTION = 0.5;
// Sqrt axes: gaps wider than this many target spacings are subdivided.
constexpr double SQRT_SPLIT_GAP_FACTOR = 2.0;

double pow10i(int e)
{
    return std::pow(10.0, static_cast<double>(e));
}

// Strip binary representation noise (0.30000000000000004 -> 0.3).
double clean(double v)
{
    if (v == 0.0 || !std::isfinite(v))
        return v;
    // snprintf/strtod rather than <charconv>: Apple libc++ has no floating-point from_chars.
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.14g", v);
    return std::strtod(buf, nullptr);
}

std::string superscript_int(int e)
{
    static constexpr const char* DIGITS[] = {"⁰", "¹", "²", "³", "⁴", "⁵", "⁶", "⁷", "⁸", "⁹"};
    std::string out;
    if (e < 0)
        out += "⁻";
    for (char c : std::to_string(std::abs(e)))
        out += DIGITS[c - '0'];
    return out;
}

// Compact positive number for the corner label: "100", "7.9", "1.7×10⁹", "10⁷".
std::string compact_number(double a)
{
    if (a >= 1e-3 && a < 1e5)
        return std::format("{:.12g}", a);
    int         e  = static_cast<int>(std::floor(std::log10(a)));
    std::string ms = std::format("{:.12g}", a / pow10i(e));
    if (ms == "10")
    {
        ms = "1";
        ++e;
    }
    return ms == "1" ? "10" + superscript_int(e) : ms + "×10" + superscript_int(e);
}

size_t utf8_length(const std::string& s)
{
    return static_cast<size_t>(
        std::count_if(s.begin(), s.end(), [](char c) { return (c & 0xC0) != 0x80; }));
}

double max_label_px(const std::vector<std::string>& labels)
{
    size_t n = 0;
    for (const auto& l : labels)
        n = std::max(n, utf8_length(l));
    return static_cast<double>(n) * TICK_LABEL_CHAR_PX;
}

// Index range [first, last] of multiples of step inside [lo, hi].
bool tick_index_range(double lo, double hi, double step, long long& first, long long& last)
{
    const double a = std::ceil(lo / step - 1e-9);
    const double b = std::floor(hi / step + 1e-9);
    if (!std::isfinite(a) || !std::isfinite(b) || std::abs(a) > 9e15 || std::abs(b) > 9e15)
        return false;
    first = static_cast<long long>(a);
    last  = static_cast<long long>(b);
    return true;
}

long long tick_count(double lo, double hi, double step)
{
    long long first = 0;
    long long last  = 0;
    if (!tick_index_range(lo, hi, step, first, last))
        return 0;
    return std::max(0LL, last - first + 1);
}

// 1-2-2.5-5 steps (1-2-5 for integer-only axes) around raw, ascending.
std::vector<double> step_candidates(double raw, bool integer_only)
{
    static constexpr double MANT_ALL[] = {1.0, 2.0, 2.5, 5.0};
    static constexpr double MANT_INT[] = {1.0, 2.0, 5.0};
    int                     k0         = static_cast<int>(std::floor(std::log10(raw))) - 1;
    if (integer_only)
        k0 = std::max(k0, 0);
    std::vector<double> out;
    for (int k = k0; k <= k0 + 6; ++k)
    {
        if (integer_only)
        {
            for (double m : MANT_INT)
                out.push_back(clean(m * pow10i(k)));
        }
        else
        {
            for (double m : MANT_ALL)
                out.push_back(clean(m * pow10i(k)));
        }
    }
    return out;
}

TickResult build_ticks(double lo, double hi, double step)
{
    TickResult result;
    long long  first = 0;
    long long  last  = 0;
    if (!tick_index_range(lo, hi, step, first, last))
        return result;
    for (long long i = first; i <= last && i - first < MAX_TICKS; ++i)
    {
        double v = static_cast<double>(i) * step;
        if (std::abs(v) < step * 1e-6)
            v = 0.0;
        result.positions.push_back(v);
    }
    const TickFormat fmt = choose_tick_format(result.positions, step, lo, hi);
    result.labels.reserve(result.positions.size());
    for (double v : result.positions)
        result.labels.push_back(format_tick_label(v, fmt));
    result.offset_label = format_tick_offset(fmt);
    return result;
}

// True when @p s is a 1-2-5 step (1, 2 or 5 times a power of ten).
bool is_125_step(double s)
{
    if (!(s > 0.0) || !std::isfinite(s))
        return false;
    const double m = clean(s / pow10i(static_cast<int>(std::floor(std::log10(s) + 1e-9))));
    return m == 1.0 || m == 2.0 || m == 5.0;
}

// Multiples of @p step inside [lo, hi] (at most MAX_TICKS + 1 of them).
std::vector<double> multiples_in(double lo, double hi, double step)
{
    std::vector<double> out;
    long long           first = 0;
    long long           last  = 0;
    if (!tick_index_range(lo, hi, step, first, last))
        return out;
    for (long long i = first; i <= last && i - first <= MAX_TICKS; ++i)
    {
        double v = static_cast<double>(i) * step;
        if (std::abs(v) < step * 1e-6)
            v = 0.0;
        out.push_back(v);
    }
    return out;
}

// Tick values for a sqrt axis whose neighbours are at least @p min_gap_px
// apart on screen (see generate_sqrt_ticks).  @p finest receives the smallest
// step used.  Returns false when no step yields two such ticks.
template <typename ToPx>
bool sqrt_tick_values(double               lo,
                      double               hi,
                      const ToPx&          px,
                      double               raw_step,
                      double               min_gap_px,
                      double               split_gap_px,
                      bool                 integer_only,
                      std::vector<double>& ticks,
                      double&              finest)
{
    auto min_gap = [&](const std::vector<double>& t)
    {
        double g = std::numeric_limits<double>::infinity();
        for (size_t i = 1; i < t.size(); ++i)
            g = std::min(g, px(t[i]) - px(t[i - 1]));
        return g;
    };

    // Coarse step: the finest 1-2-5 step, no finer than the one a linear axis
    // of the same length would use, whose ticks keep min_gap_px apart
    // everywhere.  The square root compresses large |v|, so this is decided
    // at the end of the axis farthest from zero.
    ticks.clear();
    // 1-2-5 steps only: a 2.5 step cannot be halved into nice values, which
    // would leave the stretched region near zero without ticks.
    std::vector<double> cands = step_candidates(raw_step, integer_only);
    std::erase_if(cands, [](double c) { return !is_125_step(c); });
    size_t first = 0;
    for (size_t i = 1; i < cands.size(); ++i)
    {
        if (std::abs(std::log(cands[i] / raw_step)) < std::abs(std::log(cands[first] / raw_step)))
            first = i;
    }
    for (size_t ci = first; ci < cands.size(); ++ci)
    {
        const double c = cands[ci];
        if (tick_count(lo, hi, c) > MAX_TICKS)
            continue;
        std::vector<double> t = multiples_in(lo, hi, c);
        if (t.size() < 2)
            break;   // steps only grow from here
        if (min_gap(t) >= min_gap_px)
        {
            ticks  = std::move(t);
            finest = c;
            break;
        }
    }
    if (ticks.size() < 2)
        return false;

    // Subdivide wide gaps (the stretched region around zero and the margins
    // before the first / after the last tick) with finer 1-2-5 steps.
    struct Gap
    {
        double a, b, step;
        bool   a_tick, b_tick;
    };
    std::vector<Gap> work;
    if (lo < ticks.front())
        work.push_back({lo, ticks.front(), finest, false, true});
    for (size_t i = 1; i < ticks.size(); ++i)
        work.push_back({ticks[i - 1], ticks[i], finest, true, true});
    if (ticks.back() < hi)
        work.push_back({ticks.back(), hi, finest, true, false});

    static constexpr double DIVISORS[] = {2.0, 2.5, 5.0, 10.0};
    while (!work.empty() && static_cast<long long>(ticks.size()) < MAX_TICKS)
    {
        const Gap g = work.back();
        work.pop_back();
        if (px(g.b) - px(g.a) < split_gap_px)
            continue;
        for (double d : DIVISORS)
        {
            const double s = clean(g.step / d);
            if (!is_125_step(s) || (integer_only && s < 1.0))
                continue;
            // Interior multiples; an axis limit that is not a tick yet may
            // receive one itself.
            std::vector<double> cand;
            for (double v : multiples_in(g.a, g.b, s))
            {
                const double tol = s * 1e-6;
                if ((v > g.a + tol || !g.a_tick) && (v < g.b - tol || !g.b_tick))
                    cand.push_back(v);
            }
            if (cand.empty() || cand.size() > 64)
                continue;
            // Greedy, nearest to zero first (the square root stretches small
            // |v| the most): keep every candidate that stays min_gap_px away
            // from the ticks around it.
            std::sort(cand.begin(),
                      cand.end(),
                      [](double x, double y) { return std::abs(x) < std::abs(y); });
            std::vector<double> inner;
            for (double v : cand)
            {
                bool fits = (!g.a_tick || px(v) - px(g.a) >= min_gap_px)
                            && (!g.b_tick || px(g.b) - px(v) >= min_gap_px);
                for (size_t i = 0; fits && i < inner.size(); ++i)
                    fits = std::abs(px(v) - px(inner[i])) >= min_gap_px;
                if (fits)
                    inner.insert(std::upper_bound(inner.begin(), inner.end(), v), v);
            }
            if (inner.empty())
                continue;
            finest = std::min(finest, s);
            work.push_back({g.a, inner.front(), s, g.a_tick, true});
            for (size_t i = 1; i < inner.size(); ++i)
                work.push_back({inner[i - 1], inner[i], s, true, true});
            work.push_back({inner.back(), g.b, s, true, g.b_tick});
            ticks.insert(ticks.end(), inner.begin(), inner.end());
            break;
        }
    }
    std::sort(ticks.begin(), ticks.end());
    return true;
}

}   // namespace

TickFormat choose_tick_format(const std::vector<double>& positions,
                              double                     step,
                              double                     lo,
                              double                     hi)
{
    TickFormat fmt;
    if (positions.empty() || !(step > 0.0) || !std::isfinite(step))
        return fmt;

    const double span     = std::max(hi - lo, step);
    const double abs_max  = std::max(std::abs(lo), std::abs(hi));
    const bool   one_side = (lo > 0.0 && hi > 0.0) || (lo < 0.0 && hi < 0.0);
    if (one_side && abs_max >= OFFSET_MIN_RATIO * span)
    {
        // Round the centre to the decade above the span so the labels count
        // from a short, round offset (1e7 ± 3 -> -3..3 with "+10⁷").
        const double p = pow10i(static_cast<int>(std::floor(std::log10(span))) + 1);
        fmt.offset     = clean(std::round(0.5 * (lo + hi) / p) * p);
    }

    double mag = 0.0;
    for (double v : positions)
        mag = std::max(mag, std::abs(v - fmt.offset));
    if (mag > 0.0 && std::isfinite(mag))
    {
        const int e = static_cast<int>(std::floor(std::log10(mag) + 1e-9));
        if (e >= SCI_MAX_EXP || e <= SCI_MIN_EXP)
            fmt.exponent = e;
    }

    // One precision for the whole axis: the fewest decimals that represent
    // the (scaled) step exactly, so 0 / 0.5 / 1 reads 0.0 / 0.5 / 1.0.
    const double s = step / pow10i(fmt.exponent);
    for (fmt.decimals = 0; fmt.decimals < 15; ++fmt.decimals)
    {
        const double x = s * pow10i(fmt.decimals);
        if (std::abs(x - std::round(x)) <= 1e-6 * std::max(1.0, x))
            break;
    }
    return fmt;
}

std::string format_tick_label(double value, const TickFormat& fmt)
{
    double x = (value - fmt.offset) / pow10i(fmt.exponent);
    // Values that round to zero print as "0", never "-0" / "-0.0".
    if (std::abs(x) < 0.5 * pow10i(-fmt.decimals))
        x = 0.0;
    return std::format("{:.{}f}", x, fmt.decimals);
}

std::string format_tick_offset(const TickFormat& fmt)
{
    std::string out;
    if (fmt.exponent != 0)
        out = "×10" + superscript_int(fmt.exponent);
    if (fmt.offset != 0.0)
    {
        if (!out.empty())
            out += ' ';
        out += fmt.offset > 0.0 ? '+' : '-';
        out += compact_number(std::abs(fmt.offset));
    }
    return out;
}

TickResult generate_linear_ticks(double lo, double hi, const LinearTickOptions& opt)
{
    TickResult result;
    if (!std::isfinite(lo) || !std::isfinite(hi))
        return result;
    if (hi < lo)
        std::swap(lo, hi);

    const double range = hi - lo;
    if (range == 0.0)
    {
        const double half = lo != 0.0 ? std::abs(lo) * 0.1 : 0.5;
        return generate_linear_ticks(lo - half, lo + half, opt);
    }

    // Below double precision of the values themselves ticks would coincide:
    // show a single centered tick.
    const double abs_max = std::max(std::abs(lo), std::abs(hi));
    const double min_range =
        std::max(abs_max * std::numeric_limits<double>::epsilon() * 16.0, 1e-300);
    if (range < min_range)
    {
        const double mid = (lo + hi) * 0.5;
        result.positions.push_back(mid);
        result.labels.push_back(std::format("{:.15g}", mid));
        return result;
    }

    const double axis_px     = opt.axis_px > 1.0 ? opt.axis_px : DEFAULT_TICK_AXIS_PX;
    const double target_px   = opt.target_px > 1.0 ? opt.target_px : Y_TICK_TARGET_PX;
    const double px_per_unit = axis_px / range;

    // Category axis: one tick per category when they all fit.
    if (opt.integer_steps && !opt.categories.empty())
    {
        const double        tol = range * 1e-9;
        std::vector<double> visible;
        for (double c : opt.categories)
        {
            if (c >= lo - tol && c <= hi + tol)
                visible.push_back(c);
        }
        if (!visible.empty() && visible.size() <= MAX_CATEGORY_TICKS)
        {
            double gap = 1.0;
            for (size_t i = 1; i < visible.size(); ++i)
                gap = i == 1 ? visible[1] - visible[0] : std::min(gap, visible[i] - visible[i - 1]);
            const TickFormat fmt = choose_tick_format(visible, gap, lo, hi);
            TickResult       cat;
            cat.positions = visible;
            for (double v : visible)
                cat.labels.push_back(format_tick_label(v, fmt));
            cat.offset_label = format_tick_offset(fmt);
            double need      = MIN_CATEGORY_PX;
            if (opt.labels_side_by_side)
                need = std::max(need, max_label_px(cat.labels) + TICK_LABEL_GAP_PX);
            if (visible.size() == 1 || gap * px_per_unit >= need)
                return cat;
        }
    }

    const bool   integer_only = opt.integer_steps && range >= 2.0;
    const double raw          = range * target_px / axis_px;
    const auto   cands        = step_candidates(raw, integer_only);

    size_t idx = 0;
    if (integer_only)
    {
        // Category axes label as many categories as fit (1, 2, 5, ... apart).
        while (idx + 1 < cands.size() && cands[idx] * px_per_unit < MIN_CATEGORY_PX)
            ++idx;
    }
    else
    {
        for (size_t i = 1; i < cands.size(); ++i)
        {
            if (std::abs(std::log(cands[i] / raw)) < std::abs(std::log(cands[idx] / raw)))
                idx = i;
        }
    }

    // Short axes: step down until at least two ticks are visible, as long as
    // they stay reasonably far apart.
    while (tick_count(lo, hi, cands[idx]) < 2 && idx > 0
           && cands[idx - 1] * px_per_unit >= MIN_STEP_FRACTION * target_px)
        --idx;

    result = build_ticks(lo, hi, cands[idx]);
    // Wider steps until x labels no longer collide (and never absurd counts).
    while (idx + 1 < cands.size())
    {
        const long long n = static_cast<long long>(result.positions.size());
        const bool      fits =
            !opt.labels_side_by_side
            || cands[idx] * px_per_unit >= max_label_px(result.labels) + TICK_LABEL_GAP_PX;
        if (fits && n < MAX_TICKS)
            break;
        ++idx;
        result = build_ticks(lo, hi, cands[idx]);
    }
    return result;
}

TickResult generate_sqrt_ticks(double lo, double hi, const LinearTickOptions& opt)
{
    if (!std::isfinite(lo) || !std::isfinite(hi))
        return {};
    if (hi < lo)
        std::swap(lo, hi);
    const double a_lo   = signed_sqrt(lo);
    const double a_span = signed_sqrt(hi) - a_lo;
    if (!(a_span > 0.0) || !std::isfinite(a_span) || (opt.integer_steps && !opt.categories.empty()))
        return generate_linear_ticks(lo, hi, opt);

    const double axis_px   = opt.axis_px > 1.0 ? opt.axis_px : DEFAULT_TICK_AXIS_PX;
    const double target_px = opt.target_px > 1.0 ? opt.target_px : Y_TICK_TARGET_PX;
    auto         px        = [&](double v) { return (signed_sqrt(v) - a_lo) / a_span * axis_px; };
    const bool   integer_only = opt.integer_steps && hi - lo >= 2.0;
    const double raw          = (hi - lo) * target_px / axis_px;

    double     min_gap_px = SQRT_MIN_GAP_FRACTION * target_px;
    TickResult result;
    // x labels sit side by side: widen the minimum gap until they fit.
    for (int pass = 0; pass < 4; ++pass)
    {
        std::vector<double> ticks;
        double              finest = 0.0;
        if (!sqrt_tick_values(lo,
                              hi,
                              px,
                              raw,
                              min_gap_px,
                              std::max(SQRT_SPLIT_GAP_FACTOR * target_px, 2.0 * min_gap_px),
                              integer_only,
                              ticks,
                              finest))
            return generate_linear_ticks(lo, hi, opt);
        const TickFormat fmt = choose_tick_format(ticks, finest, lo, hi);
        result               = {};
        result.positions     = std::move(ticks);
        for (double v : result.positions)
            result.labels.push_back(format_tick_label(v, fmt));
        result.offset_label = format_tick_offset(fmt);
        if (!opt.labels_side_by_side)
            break;
        const double need = max_label_px(result.labels) + TICK_LABEL_GAP_PX;
        if (need <= min_gap_px)
            break;
        min_gap_px = need;
    }
    return result;
}

}   // namespace spectra
