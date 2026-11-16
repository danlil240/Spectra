// Sequential data palettes (viridis, monochrome) used as a series cycle are
// sampled evenly over the ramp so a few lines stay distinguishable; categorical
// palettes keep palette[slot % n].  Also covers the "C<n>" palette-slot colour
// sent from Python over IPC.

#include <gtest/gtest.h>
#include <set>
#include <spectra/axes.hpp>
#include <spectra/color.hpp>
#include <spectra/figure.hpp>
#include <vector>

#include "daemon/figure_model.hpp"
#include "ipc/message.hpp"
#include "ui/theme/theme.hpp"

using namespace spectra;
using spectra::ui::DataPalette;

namespace
{

bool same_rgb(const Color& a, const ui::Color& b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

std::vector<LineSeries*> add_lines(Axes& ax, int n)
{
    static const float       x[] = {0.0f, 1.0f};
    std::vector<LineSeries*> out;
    for (int i = 0; i < n; ++i)
        out.push_back(&ax.line(x, x));
    return out;
}

}   // namespace

TEST(SequentialPalette, SlotSampling)
{
    // 4 lines on a 10-slot ramp: 10%..90% -> slots 1, 3, 6, 8.
    EXPECT_EQ(ui::sequential_palette_slot(0, 4, 10), 1u);
    EXPECT_EQ(ui::sequential_palette_slot(1, 4, 10), 3u);
    EXPECT_EQ(ui::sequential_palette_slot(2, 4, 10), 6u);
    EXPECT_EQ(ui::sequential_palette_slot(3, 4, 10), 8u);
    // One line takes the middle; two take the 10% / 90% ends.
    EXPECT_EQ(ui::sequential_palette_slot(0, 1, 10), 5u);
    EXPECT_EQ(ui::sequential_palette_slot(0, 2, 10), 1u);
    EXPECT_EQ(ui::sequential_palette_slot(1, 2, 10), 8u);
    // As many lines as slots: every slot once, in order.
    for (size_t i = 0; i < 10; ++i)
        EXPECT_EQ(ui::sequential_palette_slot(i, 10, 10), i);
    EXPECT_EQ(ui::sequential_palette_slot(11, 12, 10), 1u);
    EXPECT_EQ(ui::sequential_palette_slot(0, 3, 0), 0u);
}

TEST(SequentialPalette, FourViridisLinesAreDistinct)
{
    ui::ThemeManager tm;
    tm.ensure_initialized();
    const DataPalette& viridis = tm.get_data_palette("viridis");
    ASSERT_TRUE(viridis.sequential);

    Figure fig;
    auto&  ax    = fig.subplot(1, 1, 1);
    auto   lines = add_lines(ax, 4);
    ui::resolve_auto_series_colors(fig, viridis, false);

    const size_t want[] = {1, 3, 6, 8};
    for (size_t i = 0; i < 4; ++i)
        EXPECT_TRUE(same_rgb(lines[i]->color(), viridis.colors[want[i]])) << i;
}

TEST(SequentialPalette, SamplingIsPerAxesAndIgnoresExplicitColours)
{
    ui::ThemeManager tm;
    tm.ensure_initialized();
    const DataPalette& mono = tm.get_data_palette("monochrome");

    Figure      fig;
    auto&       a1  = fig.subplot(1, 2, 1);
    auto&       a2  = fig.subplot(1, 2, 2);
    auto        l1  = add_lines(a1, 2);
    const float x[] = {0.0f, 1.0f};
    a1.line(x, x).color(colors::red);   // explicit: not counted
    a1.plot(x, x, "r--");               // letter: not counted
    auto l2 = add_lines(a2, 1);
    ui::resolve_auto_series_colors(fig, mono, false);

    // Two auto lines in a1 -> 10% / 90% of 5 slots -> slots 0 and 4.
    EXPECT_TRUE(same_rgb(l1[0]->color(), mono.colors[0]));
    EXPECT_TRUE(same_rgb(l1[1]->color(), mono.colors[4]));
    // One auto line in a2 -> middle slot.
    EXPECT_TRUE(same_rgb(l2[0]->color(), mono.colors[2]));
}

TEST(SequentialPalette, CategoricalPalettesKeepSlotOrder)
{
    ui::ThemeManager tm;
    tm.ensure_initialized();
    for (const char* name : {"default", "colorblind", "tol_bright", "wong"})
    {
        const DataPalette& pal = tm.get_data_palette(name);
        ASSERT_FALSE(pal.sequential) << name;
        Figure fig;
        auto&  ax    = fig.subplot(1, 1, 1);
        auto   lines = add_lines(ax, 4);
        ui::resolve_auto_series_colors(fig, pal, false);
        for (size_t i = 0; i < 4; ++i)
            EXPECT_TRUE(same_rgb(lines[i]->color(), pal.colors[i])) << name << " " << i;
    }
}

TEST(SequentialPalette, RanksFollowPaletteSlotNotPosition)
{
    ui::ThemeManager tm;
    tm.ensure_initialized();
    const DataPalette& viridis = tm.get_data_palette("viridis");
    Figure             fig;
    auto&              ax    = fig.subplot(1, 1, 1);
    auto               lines = add_lines(ax, 2);
    lines[0]->set_auto_color_index(5);   // e.g. "C5"
    lines[1]->set_auto_color_index(1);
    ui::resolve_auto_series_colors(fig, viridis, false);
    EXPECT_TRUE(same_rgb(lines[1]->color(), viridis.colors[1]));   // lower slot first
    EXPECT_TRUE(same_rgb(lines[0]->color(), viridis.colors[8]));
}

TEST(SequentialPalette, SeriesSharingASlotShareTheColour)
{
    // errorbar(color="C2") gives the data and the error series slot 2.
    ui::ThemeManager tm;
    tm.ensure_initialized();
    const DataPalette& viridis = tm.get_data_palette("viridis");
    Figure             fig;
    auto&              ax    = fig.subplot(1, 1, 1);
    auto               lines = add_lines(ax, 3);
    lines[0]->set_auto_color_index(2);
    lines[1]->set_auto_color_index(2);
    lines[2]->set_auto_color_index(0);
    ui::resolve_auto_series_colors(fig, viridis, false);
    // Two distinct slots -> 10% / 90% of the ramp; slot 2 ranks second.
    EXPECT_TRUE(same_rgb(lines[2]->color(), viridis.colors[1]));
    EXPECT_TRUE(same_rgb(lines[0]->color(), viridis.colors[8]));
    EXPECT_TRUE(same_rgb(lines[1]->color(), viridis.colors[8]));
}

// ─── "C<n>" palette slot over IPC ───────────────────────────────────────────

TEST(PaletteSlotTag, RoundTrip)
{
    EXPECT_EQ(ipc::palette_slot_tag(3), "C3");
    EXPECT_EQ(ipc::parse_palette_slot_tag("C3"), 3);
    EXPECT_EQ(ipc::parse_palette_slot_tag("C12"), 12);
    EXPECT_EQ(ipc::parse_palette_slot_tag("C"), -1);
    EXPECT_EQ(ipc::parse_palette_slot_tag("c3"), -1);
    EXPECT_EQ(ipc::parse_palette_slot_tag("Cx"), -1);
    EXPECT_EQ(ipc::parse_palette_slot_tag("r"), -1);
    EXPECT_EQ(ipc::parse_palette_slot_tag(""), -1);
}

TEST(PaletteSlotTag, FigureModelRecordsSlotAndAppliesDiff)
{
    daemon::FigureModel model;
    const uint64_t      fid = model.create_figure("t", 640, 480);
    model.add_axes(fid);
    const uint32_t si = model.add_series(fid, "s", "line");
    model.set_series_color(fid, si, 1.0f, 0.0f, 0.0f, 1.0f);   // explicit first

    const ipc::DiffOp op = model.set_series_palette_index(fid, si, 4);
    EXPECT_EQ(op.type, ipc::DiffOp::Type::SET_SERIES_COLOR);
    EXPECT_EQ(op.str_val, "C4");
    const auto snap = model.snapshot();
    ASSERT_EQ(snap.figures.size(), 1u);
    EXPECT_EQ(snap.figures[0].series[si].color_index, 4);
    EXPECT_EQ(snap.figures[0].series[si].color_letter, 0);

    // Applying the diff (as a mirrored model does) restores the slot.
    model.set_series_color(fid, si, 0.0f, 1.0f, 0.0f, 1.0f);
    EXPECT_EQ(model.snapshot().figures[0].series[si].color_index, -1);
    ASSERT_TRUE(model.apply_diff_op(op));
    EXPECT_EQ(model.snapshot().figures[0].series[si].color_index, 4);
}
