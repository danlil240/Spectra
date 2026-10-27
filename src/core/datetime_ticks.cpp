#include "core/datetime_ticks.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <vector>

#include "io/text_metrics.hpp"

namespace spectra
{

namespace
{

constexpr double SECONDS_PER_DAY = 86400.0;
// Beyond about +-30 million years calendar fields stop being meaningful (and
// overflow int arithmetic): such axes fall back to plain numeric ticks.
constexpr double MAX_ABS_EPOCH = 1e15;
constexpr size_t MAX_TICKS     = 200;
// Stepping down to reach two ticks never goes below this fraction of the target.
constexpr double MIN_STEP_FRACTION = 0.35;

constexpr std::array<const char*, 12> MONTHS =
    {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

enum class Unit
{
    Fraction,   // sub-second steps (count is the step in seconds)
    Second,
    Minute,
    Hour,
    Day,
    Month,
    Year,
};

struct Candidate
{
    Unit   unit    = Unit::Second;
    double count   = 1.0;   // step in units of `unit` (seconds for Fraction)
    double nominal = 1.0;   // approximate step in seconds
};

std::vector<Candidate> candidates()
{
    std::vector<Candidate> out;
    for (int e = -6; e <= -1; ++e)
    {
        for (double m : {1.0, 2.0, 5.0})
        {
            const double s = m * std::pow(10.0, e);
            out.push_back({Unit::Fraction, s, s});
        }
    }
    for (double c : {1.0, 2.0, 5.0, 10.0, 15.0, 30.0})
        out.push_back({Unit::Second, c, c});
    for (double c : {1.0, 2.0, 5.0, 10.0, 15.0, 30.0})
        out.push_back({Unit::Minute, c, c * 60.0});
    for (double c : {1.0, 2.0, 3.0, 6.0, 12.0})
        out.push_back({Unit::Hour, c, c * 3600.0});
    for (double c : {1.0, 2.0, 7.0, 14.0})
        out.push_back({Unit::Day, c, c * SECONDS_PER_DAY});
    for (double c : {1.0, 2.0, 3.0, 6.0})
        out.push_back({Unit::Month, c, c * 30.436875 * SECONDS_PER_DAY});
    for (double k = 1.0; k <= 1e7; k *= 10.0)
    {
        for (double m : {1.0, 2.0, 5.0})
            out.push_back({Unit::Year, m * k, m * k * 365.2425 * SECONDS_PER_DAY});
    }
    return out;
}

int64_t floor_div(int64_t a, int64_t b)
{
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        --q;
    return q;
}

// Inverse of days_from_civil (H. Hinnant's civil_from_days).
void civil_from_days(int64_t z, int64_t& y, int& m, int& d)
{
    z += 719468;
    const int64_t era = floor_div(z, 146097);
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y                 = yoe + era * 400;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp  = (5 * doy + 2) / 153;
    d                 = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m                 = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    if (m <= 2)
        ++y;
}

double epoch_of(int64_t year, int month, int day)
{
    return static_cast<double>(days_from_civil(year, month, day)) * SECONDS_PER_DAY;
}

std::string date_iso(const CivilTime& c)
{
    return std::format("{:04}-{:02}-{:02}", c.year, c.month, c.day);
}

std::string month_day(const CivilTime& c)
{
    return std::format("{} {:02}", MONTHS[static_cast<size_t>(c.month - 1)], c.day);
}

// Ticks of one candidate step inside [lo, hi].
std::vector<double> tick_positions(double lo, double hi, const Candidate& c)
{
    std::vector<double> out;
    switch (c.unit)
    {
        case Unit::Fraction:
        case Unit::Second:
        case Unit::Minute:
        case Unit::Hour:
        {
            const double unit_s = c.unit == Unit::Minute ? 60.0
                                  : c.unit == Unit::Hour ? 3600.0
                                                         : 1.0;
            const double step   = c.count * unit_s;
            const double first  = std::ceil(lo / step - 1e-9);
            const double last   = std::floor(hi / step + 1e-9);
            for (double i = first; i <= last && out.size() < MAX_TICKS; i += 1.0)
                out.push_back(i * step);
            break;
        }
        case Unit::Day:
        {
            const auto k  = static_cast<int>(c.count);
            auto       d  = static_cast<int64_t>(std::ceil(lo / SECONDS_PER_DAY - 1e-9));
            const auto dl = static_cast<int64_t>(std::floor(hi / SECONDS_PER_DAY + 1e-9));
            for (; d <= dl && out.size() < MAX_TICKS; ++d)
            {
                int64_t y   = 0;
                int     m   = 0;
                int     dom = 0;
                civil_from_days(d, y, m, dom);
                // Day-of-month aligned (1, 1+k, ...), dropping a last tick that
                // would sit closer than k days to the next month's 1st.
                if ((dom - 1) % k == 0 && (k == 1 || dom + k <= 31))
                    out.push_back(static_cast<double>(d) * SECONDS_PER_DAY);
            }
            break;
        }
        case Unit::Month:
        {
            const auto      k  = static_cast<int64_t>(c.count);
            const CivilTime a  = civil_from_epoch(lo);
            const CivilTime b  = civil_from_epoch(hi);
            const int64_t   m0 = a.year * 12 + (a.month - 1);
            const int64_t   m1 = b.year * 12 + (b.month - 1);
            for (int64_t mi = m0; mi <= m1 && out.size() < MAX_TICKS; ++mi)
            {
                const int64_t y   = floor_div(mi, 12);
                const int     mon = static_cast<int>(mi - y * 12) + 1;
                if ((mon - 1) % k != 0)
                    continue;
                const double t = epoch_of(y, mon, 1);
                if (t >= lo - 1e-6 && t <= hi + 1e-6)
                    out.push_back(t);
            }
            break;
        }
        case Unit::Year:
        {
            const auto      k = static_cast<int64_t>(c.count);
            const CivilTime a = civil_from_epoch(lo);
            const CivilTime b = civil_from_epoch(hi);
            int64_t         y = a.year;
            const int64_t   r = ((y % k) + k) % k;
            if (r != 0)
                y += k - r;
            for (; y <= b.year && out.size() < MAX_TICKS; y += k)
            {
                const double t = epoch_of(y, 1, 1);
                if (t >= lo - 1e-6 && t <= hi + 1e-6)
                    out.push_back(t);
            }
            break;
        }
    }
    return out;
}

std::string year_span(const std::vector<double>& pos)
{
    if (pos.empty())
        return {};
    const int64_t y0 = civil_from_epoch(pos.front()).year;
    const int64_t y1 = civil_from_epoch(pos.back()).year;
    return y0 == y1 ? std::format("{}", y0) : std::format("{}\xE2\x80\x93{}", y0, y1);
}

TickResult label_ticks(std::vector<double> pos, const Candidate& c)
{
    TickResult r;
    r.positions = std::move(pos);
    r.labels.reserve(r.positions.size());
    bool any_january = false;
    for (double t : r.positions)
    {
        const CivilTime ct = civil_from_epoch(t);
        switch (c.unit)
        {
            case Unit::Fraction:
            {
                const int decimals =
                    std::clamp(static_cast<int>(std::ceil(-std::log10(c.count) - 1e-9)), 1, 6);
                double sec = ct.second;
                // Avoid "60.0" from rounding just below a whole minute.
                const double scale = std::pow(10.0, decimals);
                sec                = std::round(sec * scale) / scale;
                if (sec >= 60.0)
                    sec -= 60.0;
                r.labels.push_back(std::format("{:0{}.{}f}", sec, decimals + 3, decimals));
                break;
            }
            case Unit::Second:
                r.labels.push_back(std::format("{:02}:{:02}:{:02}",
                                               ct.hour,
                                               ct.minute,
                                               static_cast<int>(std::floor(ct.second + 1e-6))));
                break;
            case Unit::Minute:
            case Unit::Hour:
                // Midnight ticks name the new day instead of "00:00".
                if (ct.hour == 0 && ct.minute == 0)
                    r.labels.push_back(month_day(ct));
                else
                    r.labels.push_back(std::format("{:02}:{:02}", ct.hour, ct.minute));
                break;
            case Unit::Day:
                r.labels.push_back(month_day(ct));
                break;
            case Unit::Month:
                // January ticks name the year, the others the month.
                if (ct.month == 1)
                {
                    any_january = true;
                    r.labels.push_back(std::format("{}", ct.year));
                }
                else
                {
                    r.labels.push_back(MONTHS[static_cast<size_t>(ct.month - 1)]);
                }
                break;
            case Unit::Year:
                r.labels.push_back(std::format("{}", ct.year));
                break;
        }
    }
    if (r.positions.empty())
        return r;

    // Coarser context in the axis corner.
    const CivilTime first = civil_from_epoch(r.positions.front());
    switch (c.unit)
    {
        case Unit::Fraction:
            r.offset_label =
                std::format("{} {:02}:{:02}", date_iso(first), first.hour, first.minute);
            break;
        case Unit::Second:
        case Unit::Minute:
        case Unit::Hour:
            r.offset_label = date_iso(first);
            break;
        case Unit::Day:
            r.offset_label = year_span(r.positions);
            break;
        case Unit::Month:
            if (!any_january)
                r.offset_label = year_span(r.positions);
            break;
        case Unit::Year:
            break;
    }
    return r;
}

double max_label_px(const std::vector<std::string>& labels)
{
    float w = 0.0f;
    for (const auto& l : labels)
        w = std::max(w, inter_text_width(l, TICK_LABEL_FONT_PX));
    return static_cast<double>(w);
}

double min_gap(const std::vector<double>& pos)
{
    double g = std::numeric_limits<double>::max();
    for (size_t i = 1; i < pos.size(); ++i)
        g = std::min(g, pos[i] - pos[i - 1]);
    return g;
}

}   // namespace

int64_t days_from_civil(int64_t y, int m, int d)
{
    y -= m <= 2 ? 1 : 0;
    const int64_t era = floor_div(y, 400);
    const int64_t yoe = y - era * 400;
    const int64_t mp  = m > 2 ? m - 3 : m + 9;
    const int64_t doy = (153 * mp + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

CivilTime civil_from_epoch(double t)
{
    CivilTime c;
    if (!std::isfinite(t))
        return c;
    t                 = std::clamp(t, -MAX_ABS_EPOCH, MAX_ABS_EPOCH);
    const double days = std::floor(t / SECONDS_PER_DAY);
    double       sod  = t - days * SECONDS_PER_DAY;   // seconds of day, [0, 86400)
    // Values a hair below a whole second print as that second.
    if (const double r = std::round(sod); std::abs(sod - r) < 1e-6 * std::max(1.0, sod))
        sod = r;
    auto z = static_cast<int64_t>(days);
    if (sod >= SECONDS_PER_DAY)
    {
        sod -= SECONDS_PER_DAY;
        ++z;
    }
    civil_from_days(z, c.year, c.month, c.day);
    c.hour   = static_cast<int>(sod / 3600.0);
    c.minute = static_cast<int>((sod - c.hour * 3600.0) / 60.0);
    c.second = sod - c.hour * 3600.0 - c.minute * 60.0;
    return c;
}

TickResult generate_datetime_ticks(double lo, double hi, const LinearTickOptions& opt)
{
    if (!std::isfinite(lo) || !std::isfinite(hi))
        return {};
    if (hi < lo)
        std::swap(lo, hi);
    const double                        range = hi - lo;
    static const std::vector<Candidate> CANDS = candidates();
    if (std::max(std::abs(lo), std::abs(hi)) > MAX_ABS_EPOCH || !(range > 0.0)
        || range < CANDS.front().nominal)
        return generate_linear_ticks(lo, hi, opt);

    const double axis_px   = opt.axis_px > 1.0 ? opt.axis_px : DEFAULT_TICK_AXIS_PX;
    const double target_px = opt.target_px > 1.0 ? opt.target_px : Y_TICK_TARGET_PX;
    const double px_per_s  = axis_px / range;
    const double raw       = range * target_px / axis_px;

    size_t idx = 0;
    while (idx + 1 < CANDS.size() && CANDS[idx].nominal < raw * 0.75)
        ++idx;

    // Short axes: step down until at least two ticks are visible, as long as
    // they stay reasonably far apart.
    std::vector<double> pos = tick_positions(lo, hi, CANDS[idx]);
    while (pos.size() < 2 && idx > 0
           && CANDS[idx - 1].nominal * px_per_s >= MIN_STEP_FRACTION * target_px)
    {
        --idx;
        pos = tick_positions(lo, hi, CANDS[idx]);
    }

    TickResult result = label_ticks(std::move(pos), CANDS[idx]);
    // Wider steps until side-by-side labels no longer collide.
    while (idx + 1 < CANDS.size())
    {
        const bool fits = !opt.labels_side_by_side || result.positions.size() < 2
                          || min_gap(result.positions) * px_per_s
                                 >= max_label_px(result.labels) + TICK_LABEL_GAP_PX;
        if (fits && result.positions.size() < MAX_TICKS)
            break;
        ++idx;
        result = label_ticks(tick_positions(lo, hi, CANDS[idx]), CANDS[idx]);
    }
    if (result.positions.empty())
        return generate_linear_ticks(lo, hi, opt);
    return result;
}

}   // namespace spectra
