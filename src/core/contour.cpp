#include "contour.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>

namespace spectra::contour
{

namespace
{

// Cell edges: 0 = bottom (v0-v1), 1 = right (v1-v2), 2 = top (v3-v2), 3 = left (v0-v3),
// with corners v0 = (c, r), v1 = (c+1, r), v2 = (c+1, r+1), v3 = (c, r+1).
// Segments per case (pairs of edges, -1 = none).  Saddles (5, 10) are
// resolved separately.
constexpr std::array<std::array<int8_t, 4>, 16> CASES = {{
    {-1, -1, -1, -1},   // 0
    {3, 0, -1, -1},     // 1: v0
    {0, 1, -1, -1},     // 2: v1
    {3, 1, -1, -1},     // 3: v0 v1
    {1, 2, -1, -1},     // 4: v2
    {-1, -1, -1, -1},   // 5: v0 v2 (saddle)
    {0, 2, -1, -1},     // 6: v1 v2
    {2, 3, -1, -1},     // 7: v0 v1 v2
    {2, 3, -1, -1},     // 8: v3
    {0, 2, -1, -1},     // 9: v0 v3
    {-1, -1, -1, -1},   // 10: v1 v3 (saddle)
    {1, 2, -1, -1},     // 11: v0 v1 v3
    {3, 1, -1, -1},     // 12: v2 v3
    {0, 1, -1, -1},     // 13: v0 v2 v3
    {3, 0, -1, -1},     // 14: v1 v2 v3
    {-1, -1, -1, -1},   // 15
}};

struct Grid
{
    std::span<const float> z;
    size_t                 cols;
    size_t                 rows;
    float                  level;

    float at(size_t c, size_t r) const { return z[r * cols + c]; }

    // Global edge ids: horizontal edges (c, r)-(c+1, r) first, then vertical
    // edges (c, r)-(c, r+1).
    size_t h_count() const { return (cols - 1) * rows; }
    size_t h_edge(size_t c, size_t r) const { return r * (cols - 1) + c; }
    size_t v_edge(size_t c, size_t r) const { return h_count() + r * cols + c; }

    size_t cell_edge(size_t c, size_t r, int e) const
    {
        switch (e)
        {
            case 0:
                return h_edge(c, r);
            case 1:
                return v_edge(c + 1, r);
            case 2:
                return h_edge(c, r + 1);
            default:
                return v_edge(c, r);
        }
    }

    // Crossing point of the level on a global edge (grid-index space).
    std::pair<float, float> point(size_t edge) const
    {
        size_t c0, r0, c1, r1;
        if (edge < h_count())
        {
            r0 = edge / (cols - 1);
            c0 = edge % (cols - 1);
            c1 = c0 + 1;
            r1 = r0;
        }
        else
        {
            const size_t v = edge - h_count();
            r0             = v / cols;
            c0             = v % cols;
            c1             = c0;
            r1             = r0 + 1;
        }
        const double a = at(c0, r0);
        const double b = at(c1, r1);
        double       t = (b != a) ? (static_cast<double>(level) - a) / (b - a) : 0.5;
        t              = std::clamp(t, 0.0, 1.0);
        return {static_cast<float>(static_cast<double>(c0) + t * static_cast<double>(c1 - c0)),
                static_cast<float>(static_cast<double>(r0) + t * static_cast<double>(r1 - r0))};
    }
};

}   // namespace

std::vector<Polyline> trace(std::span<const float> z, size_t cols, size_t rows, float level)
{
    std::vector<Polyline> out;
    if (cols < 2 || rows < 2 || z.size() < cols * rows || !std::isfinite(level))
        return out;

    const Grid g{z, cols, rows, level};

    // 1. Segments between global edge ids.
    std::vector<std::pair<size_t, size_t>> segs;
    for (size_t r = 0; r + 1 < rows; ++r)
    {
        for (size_t c = 0; c + 1 < cols; ++c)
        {
            const float v0 = g.at(c, r);
            const float v1 = g.at(c + 1, r);
            const float v2 = g.at(c + 1, r + 1);
            const float v3 = g.at(c, r + 1);
            if (!std::isfinite(v0) || !std::isfinite(v1) || !std::isfinite(v2)
                || !std::isfinite(v3))
                continue;
            const int idx = (v0 >= level ? 1 : 0) | (v1 >= level ? 2 : 0) | (v2 >= level ? 4 : 0)
                            | (v3 >= level ? 8 : 0);
            if (idx == 0 || idx == 15)
                continue;

            std::array<int8_t, 4> e = CASES[static_cast<size_t>(idx)];
            if (idx == 5 || idx == 10)
            {
                const bool center_above = (static_cast<double>(v0) + v1 + v2 + v3) * 0.25 >= level;
                // Case 5 (v0, v2 above): an above centre joins v0 and v2, so
                // the below corners v1 and v3 are cut off; otherwise v0 and v2
                // are.  Case 10 is the mirror image.
                const bool cut_v1_v3 = (idx == 5) == center_above;
                e                    = cut_v1_v3 ? std::array<int8_t, 4>{0, 1, 2, 3}
                                                 : std::array<int8_t, 4>{3, 0, 1, 2};
            }
            for (int k = 0; k < 4 && e[static_cast<size_t>(k)] >= 0; k += 2)
                segs.emplace_back(g.cell_edge(c, r, e[static_cast<size_t>(k)]),
                                  g.cell_edge(c, r, e[static_cast<size_t>(k + 1)]));
        }
    }
    if (segs.empty())
        return out;

    // 2. Edge -> segments using it (at most two: the cells on either side).
    std::unordered_map<size_t, std::array<int64_t, 2>> uses;
    uses.reserve(segs.size() * 2);
    auto add_use = [&](size_t edge, int64_t s)
    {
        auto [it, inserted] = uses.try_emplace(edge, std::array<int64_t, 2>{-1, -1});
        auto& slots         = it->second;
        if (slots[0] < 0)
            slots[0] = s;
        else
            slots[1] = s;
    };
    for (size_t i = 0; i < segs.size(); ++i)
    {
        add_use(segs[i].first, static_cast<int64_t>(i));
        add_use(segs[i].second, static_cast<int64_t>(i));
    }
    auto degree = [&](size_t edge)
    {
        const auto& s = uses.at(edge);
        return (s[0] >= 0 ? 1 : 0) + (s[1] >= 0 ? 1 : 0);
    };

    // 3. Walk chains: open curves from their free ends first, then loops.
    std::vector<bool> used(segs.size(), false);
    auto              walk = [&](size_t first_seg, size_t start_edge)
    {
        Polyline line;
        auto     push = [&](size_t edge)
        {
            const auto [px, py] = g.point(edge);
            line.x.push_back(px);
            line.y.push_back(py);
        };
        push(start_edge);
        size_t seg  = first_seg;
        size_t edge = start_edge;
        while (true)
        {
            used[seg]       = true;
            const auto&  sg = segs[seg];
            const size_t nx = (sg.first == edge) ? sg.second : sg.first;
            push(nx);
            edge = nx;
            if (edge == start_edge)
            {
                line.closed = true;
                break;
            }
            const auto& s    = uses.at(edge);
            int64_t     next = -1;
            for (int64_t cand : s)
            {
                if (cand >= 0 && !used[static_cast<size_t>(cand)])
                {
                    next = cand;
                    break;
                }
            }
            if (next < 0)
                break;
            seg = static_cast<size_t>(next);
        }
        out.push_back(std::move(line));
    };

    for (size_t i = 0; i < segs.size(); ++i)
    {
        if (used[i])
            continue;
        if (degree(segs[i].first) == 1)
            walk(i, segs[i].first);
        else if (degree(segs[i].second) == 1)
            walk(i, segs[i].second);
    }
    for (size_t i = 0; i < segs.size(); ++i)
    {
        if (!used[i])
            walk(i, segs[i].first);
    }
    return out;
}

std::vector<float> auto_levels(float zmin, float zmax, int n, bool cover_range)
{
    std::vector<float> out;
    if (!std::isfinite(zmin) || !std::isfinite(zmax) || n < 1)
        return out;
    if (zmax < zmin)
        std::swap(zmin, zmax);
    const double lo    = zmin;
    const double hi    = zmax;
    const double range = hi - lo;
    if (!(range > std::max(std::abs(lo), std::abs(hi)) * 1e-12))
    {
        if (cover_range)
        {
            const double pad = std::max(std::abs(lo) * 0.05, 0.5);
            out              = {static_cast<float>(lo - pad), static_cast<float>(lo + pad)};
        }
        return out;
    }

    // Smallest 1/2/2.5/5 x 10^k step that gives at most n intervals.
    const double raw  = range / static_cast<double>(n);
    const double mag  = std::pow(10.0, std::floor(std::log10(raw)));
    double       step = 10.0 * mag;
    for (double m : {1.0, 2.0, 2.5, 5.0, 10.0})
    {
        if (m * mag >= raw * (1.0 - 1e-9))
        {
            step = m * mag;
            break;
        }
    }

    const double eps = step * 1e-9;
    double       k0  = cover_range ? std::floor(lo / step + 1e-9) : std::ceil(lo / step - 1e-9);
    double       k1  = cover_range ? std::ceil(hi / step - 1e-9) : std::floor(hi / step + 1e-9);
    for (double k = k0; k <= k1 + 0.5; k += 1.0)
    {
        double v = k * step;
        if (std::abs(v) < eps)
            v = 0.0;
        if (!cover_range && (v <= lo + eps || v >= hi - eps))
            continue;
        out.push_back(static_cast<float>(v));
        if (out.size() > 1000)
            break;
    }
    return out;
}

}   // namespace spectra::contour
