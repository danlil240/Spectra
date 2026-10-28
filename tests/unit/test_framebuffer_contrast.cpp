// Rendered (framebuffer) contrast of plot elements (QA S4-C01).
//
// The source-colour matrices of test_theme / test_palette_visibility prove the
// theme tokens reach their targets; they cannot prove what reaches the screen
// after antialiasing, thin-line coverage, MSDF text and HiDPI scaling.  These
// tests render real headless frames (lavapipe in CI) and measure pixels:
//
//   * title / tick-label text against the figure background    >= 4.5:1
//   * axis spine, 1px series line and markers against the canvas >= 3:1
//   * colourblind-safe palettes: rendered line colours stay distinct under
//     simulated protanopia / deuteranopia (Machado et al. 2009, severity 1).
//
// Each measured element sits in a region known from the figure layout, and the
// peak-ink pixel of the region (the glyph stem / line core) is compared with
// the region's dominant (background) colour.

#include <gtest/gtest.h>

#include <spectra/app.hpp>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "stb_image.h"
#include "ui/theme/theme.hpp"

namespace fs  = std::filesystem;
using UiColor = spectra::ui::Color;

namespace
{

constexpr const char* kThemes[] = {"dark", "night", "light", "high_contrast"};

struct Image
{
    int                  w = 0;
    int                  h = 0;
    std::vector<uint8_t> rgba;

    UiColor at(int x, int y) const
    {
        const uint8_t* p = &rgba[(static_cast<size_t>(y) * w + x) * 4];
        return UiColor(p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, 1.0f);
    }
    uint32_t key(int x, int y) const
    {
        const uint8_t* p = &rgba[(static_cast<size_t>(y) * w + x) * 4];
        return (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
    }
};

bool load(const fs::path& p, Image& img)
{
    int      ch   = 0;
    stbi_uc* data = stbi_load(p.string().c_str(), &img.w, &img.h, &ch, 4);
    if (!data)
        return false;
    img.rgba.assign(data, data + static_cast<size_t>(img.w) * img.h * 4);
    stbi_image_free(data);
    return true;
}

struct Region
{
    int x0, y0, x1, y1;   // half-open pixel rectangle
};

Region clip(const Image& img, Region r)
{
    return {std::clamp(r.x0, 0, img.w),
            std::clamp(r.y0, 0, img.h),
            std::clamp(r.x1, 0, img.w),
            std::clamp(r.y1, 0, img.h)};
}

// Most frequent colour of the region: the surface the element is drawn on.
UiColor dominant(const Image& img, Region r)
{
    r = clip(img, r);
    std::map<uint32_t, int> hist;
    for (int y = r.y0; y < r.y1; ++y)
        for (int x = r.x0; x < r.x1; ++x)
            ++hist[img.key(x, y)];
    uint32_t best = 0;
    int      n    = -1;
    for (const auto& [k, c] : hist)
        if (c > n)
        {
            n    = c;
            best = k;
        }
    return UiColor(((best >> 16) & 0xFF) / 255.0f,
                   ((best >> 8) & 0xFF) / 255.0f,
                   (best & 0xFF) / 255.0f,
                   1.0f);
}

struct Ink
{
    float   ratio  = 1.0f;   // contrast of the peak-ink pixel against the background
    UiColor color  = {};     // that pixel
    int     pixels = 0;      // pixels within 10% (in ratio) of the peak
};

// Peak-ink pixel of the region against `bg`, using the theme helper's WCAG ratio.
Ink peak_ink(const Image& img, Region r, const UiColor& bg)
{
    r = clip(img, r);
    Ink out;
    for (int y = r.y0; y < r.y1; ++y)
        for (int x = r.x0; x < r.x1; ++x)
        {
            const UiColor c  = img.at(x, y);
            const float   cr = c.contrast_ratio(bg);
            if (cr > out.ratio)
            {
                out.ratio = cr;
                out.color = c;
            }
        }
    for (int y = r.y0; y < r.y1; ++y)
        for (int x = r.x0; x < r.x1; ++x)
            if (img.at(x, y).contrast_ratio(bg) >= 1.0f + (out.ratio - 1.0f) * 0.9f)
                ++out.pixels;
    return out;
}

// Median over the given 1px-wide strips of each strip's peak-ink contrast:
// how strongly a thin element (spine, series line) shows along its length,
// insensitive to the few strips where a grid line or another series crosses.
float median_strip_peak(const Image& img, const std::vector<Region>& strips, const UiColor& bg)
{
    std::vector<float> peaks;
    for (const Region& r : strips)
        peaks.push_back(peak_ink(img, r, bg).ratio);
    if (peaks.empty())
        return 1.0f;
    std::nth_element(peaks.begin(), peaks.begin() + peaks.size() / 2, peaks.end());
    return peaks[peaks.size() / 2];
}

fs::path out_dir()
{
    const char* env = std::getenv("SPECTRA_FBCONTRAST_DIR");
    fs::path dir = env && *env ? fs::path(env) : fs::temp_directory_path() / "spectra_fbcontrast";
    fs::create_directories(dir);
    return dir;
}

bool keep_images()
{
    const char* env = std::getenv("SPECTRA_FBCONTRAST_DIR");
    return env && *env;
}

constexpr uint32_t kW = 640;
constexpr uint32_t kH = 400;

constexpr float kTextTarget    = 4.5f;   // WCAG 1.4.3
constexpr float kGraphicTarget = 3.0f;   // WCAG 1.4.11

float probe_curve(float x)
{
    return 7.5f + 1.0f * std::sin(x * 1.3f);
}

// 1px line in the upper half, markers in the lower half, title and tick labels.
void fill_probe_figure(spectra::Figure& fig)
{
    auto& ax = fig.subplot(1, 1, 1);
    ax.xlim(0.0, 10.0);
    ax.ylim(0.0, 10.0);
    ax.title("Contrast Probe Title");
    std::vector<float> x(400), y(400);
    for (size_t i = 0; i < x.size(); ++i)
    {
        x[i] = 10.0f * static_cast<float>(i) / static_cast<float>(x.size() - 1);
        y[i] = probe_curve(x[i]);
    }
    ax.line(x, y).width(1.0f);
    std::vector<float> mx, my;
    for (int i = 1; i <= 9; ++i)
    {
        mx.push_back(static_cast<float>(i));
        my.push_back(2.5f);
    }
    ax.scatter(mx, my).marker_size(8.0f);
}

std::string png_name(const std::string& stem, int scale)
{
    return stem + "_" + std::to_string(scale) + "x.png";
}

}   // namespace

// Title, tick labels, axis spines, a 1px series line and markers, measured on
// the rendered PNG of every theme at 1x and 2x export scale.
TEST(FramebufferContrast, PlotElementsReachTargetsAtOneAndTwoX)
{
    const fs::path dir = out_dir();
    spectra::Rect  vp{};
    {
        spectra::App app({.headless = true, .socket_path = ""});
        // One figure per export: a figure holds a single pending export.
        std::vector<spectra::Figure*> figs;
        for (const char* theme : kThemes)
            for (uint32_t s : {1u, 2u})
            {
                auto& fig = app.figure({.width = kW, .height = kH});
                fill_probe_figure(fig);
                fig.save_png((dir / png_name(theme, static_cast<int>(s))).string(),
                             {.width = kW * s, .height = kH * s, .theme = theme});
                figs.push_back(&fig);
            }
        app.run();
        figs.front()->compute_layout();
        vp = figs.front()->axes()[0]->viewport();
    }
    ASSERT_GT(vp.y, 10.0f);
    ASSERT_GT(vp.x, 10.0f);
    ASSERT_GT(vp.w, 100.0f);

    for (const char* theme : kThemes)
        for (int s : {1, 2})
        {
            const std::string where = std::string(theme) + " @" + std::to_string(s) + "x";
            Image             img;
            ASSERT_TRUE(load(dir / png_name(theme, s), img)) << where;
            ASSERT_EQ(img.w, static_cast<int>(kW) * s) << where;
            ASSERT_EQ(img.h, static_cast<int>(kH) * s) << where;

            const auto L = static_cast<int>(vp.x * s);
            const auto T = static_cast<int>(vp.y * s);
            const auto R = static_cast<int>((vp.x + vp.w) * s);
            const auto B = static_cast<int>((vp.y + vp.h) * s);
            auto px      = [&](float v) { return static_cast<int>((vp.x + v / 10.0f * vp.w) * s); };
            auto py      = [&](float v)
            { return static_cast<int>((vp.y + (1.0f - v / 10.0f) * vp.h) * s); };

            // Surfaces: figure background around the text, plot canvas inside.
            const UiColor fig_bg = dominant(img, {0, 0, img.w, T});
            const UiColor canvas = dominant(img, {L + 2 * s, T + 2 * s, R - 2 * s, B - 2 * s});

            // Text: title above the plot, tick labels below / left of it.
            const Ink title  = peak_ink(img, {L, 0, R, T - 2 * s}, fig_bg);
            const Ink xticks = peak_ink(img, {L - 8 * s, B + 3 * s, R + 8 * s, B + 22 * s}, fig_bg);
            const Ink yticks = peak_ink(img, {0, T - 8 * s, L - 3 * s, B + 8 * s}, fig_bg);

            // Spines: per-column (bottom) / per-row (left) peaks along the edge.
            std::vector<Region> bottom, left;
            for (int x = L + 10 * s; x < R - 10 * s; ++x)
                bottom.push_back({x, B - 2 * s, x + 1, B + 2 * s});
            for (int y = T + 10 * s; y < B - 10 * s; ++y)
                left.push_back({L - 2 * s, y, L + 2 * s, y + 1});
            const float spine_b = median_strip_peak(img, bottom, canvas);
            const float spine_l = median_strip_peak(img, left, canvas);

            // 1px series line: per-column peak in a band around the known curve.
            std::vector<Region> curve;
            for (int x = L + 6 * s; x < R - 6 * s; ++x)
            {
                const float dx = (static_cast<float>(x) / s - vp.x) / vp.w * 10.0f;
                const int   yc = py(probe_curve(dx));
                curve.push_back({x, yc - 3 * s, x + 1, yc + 3 * s + 1});
            }
            const float line = median_strip_peak(img, curve, canvas);

            // Markers: the weakest of the nine.
            float marker = 1e9f;
            for (int i = 1; i <= 9; ++i)
            {
                const Ink m = peak_ink(
                    img,
                    {px(i) - 6 * s, py(2.5f) - 6 * s, px(i) + 6 * s + 1, py(2.5f) + 6 * s + 1},
                    canvas);
                marker = std::min(marker, m.ratio);
            }

            std::printf(
                "[fbcontrast] %-18s title %5.2f  xticks %5.2f  yticks %5.2f  spine %5.2f/%5.2f"
                "  line %5.2f  marker %5.2f\n",
                where.c_str(),
                title.ratio,
                xticks.ratio,
                yticks.ratio,
                spine_b,
                spine_l,
                line,
                marker);

            // Stems must actually reach the peak, not a single stray pixel.
            const int min_text_px = 12 * s * s;
            EXPECT_GE(title.ratio, kTextTarget) << where << " title";
            EXPECT_GE(title.pixels, min_text_px) << where << " title ink";
            EXPECT_GE(xticks.ratio, kTextTarget) << where << " x tick labels";
            EXPECT_GE(xticks.pixels, min_text_px) << where << " x tick label ink";
            EXPECT_GE(yticks.ratio, kTextTarget) << where << " y tick labels";
            EXPECT_GE(yticks.pixels, min_text_px) << where << " y tick label ink";
            EXPECT_GE(spine_b, kGraphicTarget) << where << " bottom spine";
            EXPECT_GE(spine_l, kGraphicTarget) << where << " left spine";
            EXPECT_GE(line, kGraphicTarget) << where << " 1px series line";
            EXPECT_GE(marker, kGraphicTarget) << where << " markers";
        }
    if (!keep_images())
        fs::remove_all(dir);
}

namespace
{

// Machado, Oliveira & Fernandes 2009, severity 1.0, applied to linear RGB.
// (The same matrices back spectra::ui::simulate_cvd(); written out here so the
// test checks the rendered pixels against the published model directly.)
using Mat3                   = std::array<std::array<double, 3>, 3>;
constexpr Mat3 kProtanopia   = {{{0.152286, 1.052583, -0.204868},
                                 {0.114503, 0.786281, 0.099216},
                                 {-0.003882, -0.048116, 1.051998}}};
constexpr Mat3 kDeuteranopia = {{{0.367322, 0.860646, -0.227968},
                                 {0.280085, 0.672501, 0.047413},
                                 {-0.011820, 0.042940, 0.968881}}};

struct Lab
{
    double L, a, b;
};

double lin(double c)
{
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

// OKLab of a linear-RGB triple.
Lab oklab_linear(double r, double g, double b)
{
    const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    return {0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
            1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}

Lab simulate(const UiColor& c, const Mat3* m)
{
    double v[3] = {lin(c.r), lin(c.g), lin(c.b)};
    if (m)
    {
        double o[3];
        for (int i = 0; i < 3; ++i)
            o[i] = std::clamp((*m)[i][0] * v[0] + (*m)[i][1] * v[1] + (*m)[i][2] * v[2], 0.0, 1.0);
        std::copy(o, o + 3, v);
    }
    return oklab_linear(v[0], v[1], v[2]);
}

double delta_e(const Lab& x, const Lab& y)
{
    return std::hypot(x.L - y.L, x.a - y.a, x.b - y.b);
}

struct PairMin
{
    double dist = 1e9;
    size_t i = 0, j = 0;
};

PairMin min_pair(const std::vector<UiColor>& cs, const Mat3* m)
{
    PairMin out;
    for (size_t i = 0; i < cs.size(); ++i)
        for (size_t j = i + 1; j < cs.size(); ++j)
        {
            const double d = delta_e(simulate(cs[i], m), simulate(cs[j], m));
            if (d < out.dist)
                out = {d, i, j};
        }
    return out;
}

size_t palette_size(const char* name)
{
    spectra::ui::ThemeManager tm;
    spectra::ui::ThemeManager::set_current(&tm);
    tm.ensure_initialized();
    tm.set_data_palette(name);
    const size_t n = tm.current_data_palette().colors.size();
    spectra::ui::ThemeManager::set_current(nullptr);
    return n;
}

}   // namespace

// The colourblind-safe palettes stay distinguishable as drawn: the colours of
// the rendered lines (after theme ink adaptation, visibility adjustment and
// 8-bit output) keep a minimum pairwise OKLab distance under simulated
// protanopia and deuteranopia on every theme.
TEST(FramebufferContrast, ColorblindPalettesStayDistinctUnderSimulatedCvd)
{
    // OKLab ΔE.  0.04 is the floor test_palette_visibility holds the source
    // palettes to (worst of normal / P / D / T); see DECISIONS-fbcontrast.md.
    constexpr double kMinDelta = 0.04;
    const fs::path   dir       = out_dir();

    for (const char* palette : {"colorblind", "tol_bright", "tol_muted"})
    {
        const size_t n = palette_size(palette);
        ASSERT_GE(n, 5u) << palette;
        spectra::Rect vp{};
        {
            spectra::App app({.headless = true, .socket_path = "", .data_palette = palette});
            for (const char* theme : kThemes)
            {
                auto& fig = app.figure({.width = kW, .height = kH});
                auto& ax  = fig.subplot(1, 1, 1);
                ax.xlim(0.0, 1.0);
                ax.ylim(0.0, static_cast<double>(n + 1));
                ax.grid(false);
                const std::vector<float> x = {0.0f, 1.0f};
                for (size_t i = 0; i < n; ++i)
                {
                    const float yv = static_cast<float>(n - i);   // slot 0 on top
                    ax.line(std::vector<float>{x}, std::vector<float>{yv, yv}).width(3.0f);
                }
                fig.save_png((dir / (std::string(palette) + "_" + theme + ".png")).string(),
                             {.theme = theme});
            }
            app.run();
            auto& first = *app.figure_registry().get(app.figure_registry().all_ids().front());
            first.compute_layout();
            vp = first.axes()[0]->viewport();
        }
        ASSERT_GT(vp.h, 100.0f) << palette;
        for (const char* theme : kThemes)
        {
            const std::string where = std::string(palette) + " on " + theme;
            Image             img;
            ASSERT_TRUE(load(dir / (std::string(palette) + "_" + theme + ".png"), img)) << where;

            const UiColor canvas = dominant(img,
                                            {static_cast<int>(vp.x) + 2,
                                             static_cast<int>(vp.y) + 2,
                                             static_cast<int>(vp.x + vp.w) - 2,
                                             static_cast<int>(vp.y + vp.h) - 2});
            const float   step   = vp.h / static_cast<float>(n + 1);
            const int     x0     = static_cast<int>(vp.x + 0.25f * vp.w);
            const int     x1     = static_cast<int>(vp.x + 0.75f * vp.w);

            std::vector<UiColor> drawn;
            for (size_t i = 0; i < n; ++i)
            {
                // Slot i sits at y = n - i, i.e. (i + 1) steps below the top.
                const int yc = static_cast<int>(std::lround(vp.y + step * (i + 1)));
                // Mode of the per-column peak pixel = the line's solid core colour.
                std::map<uint32_t, int> hist;
                for (int x = x0; x < x1; ++x)
                {
                    int   by = yc;
                    float bc = 0.0f;
                    for (int y = yc - 3; y <= yc + 3; ++y)
                    {
                        const float c = img.at(x, y).contrast_ratio(canvas);
                        if (c > bc)
                        {
                            bc = c;
                            by = y;
                        }
                    }
                    ++hist[img.key(x, by)];
                }
                const auto     best = std::max_element(hist.begin(),
                                                   hist.end(),
                                                   [](const auto& a, const auto& b)
                                                   { return a.second < b.second; });
                const uint32_t k    = best->first;
                drawn.emplace_back(((k >> 16) & 0xFF) / 255.0f,
                                   ((k >> 8) & 0xFF) / 255.0f,
                                   (k & 0xFF) / 255.0f,
                                   1.0f);
                EXPECT_GE(drawn.back().contrast_ratio(canvas), kGraphicTarget)
                    << where << " slot " << i;
            }

            const PairMin normal = min_pair(drawn, nullptr);
            const PairMin protan = min_pair(drawn, &kProtanopia);
            const PairMin deutan = min_pair(drawn, &kDeuteranopia);
            std::printf("[fbcontrast] %-26s normal %.3f (%zu,%zu)  protan %.3f (%zu,%zu)  "
                        "deutan %.3f (%zu,%zu)\n",
                        where.c_str(),
                        normal.dist,
                        normal.i,
                        normal.j,
                        protan.dist,
                        protan.i,
                        protan.j,
                        deutan.dist,
                        deutan.i,
                        deutan.j);
            EXPECT_GE(normal.dist, kMinDelta) << where << " slots " << normal.i << "," << normal.j;
            EXPECT_GE(protan.dist, kMinDelta)
                << where << " protanopia slots " << protan.i << "," << protan.j;
            EXPECT_GE(deutan.dist, kMinDelta)
                << where << " deuteranopia slots " << deutan.i << "," << deutan.j;
        }
    }
    if (!keep_images())
        fs::remove_all(dir);
}
