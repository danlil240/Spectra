#include "core/axis_ticks.hpp"

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

}   // namespace spectra
