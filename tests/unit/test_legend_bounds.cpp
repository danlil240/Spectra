// Audit S04: legends must stay inside their axes.  Overwide single columns
// are narrowed (labels ellipsized, UTF-8 safe), oversized boxes are clamped,
// and legends that cannot fit even one row in a tiny cell are skipped.

#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <string>
#include <vector>

#include "render/text_fit.hpp"
#include "ui/overlay/legend_layout.hpp"

#if defined(SPECTRA_USE_IMGUI) && __has_include(<imgui.h>)
    #define SPECTRA_TEST_IMGUI_LEGEND 1
    #include <format>
    #include <imgui.h>
    #include <imgui_internal.h>

    #include "ui/overlay/legend_interaction.hpp"
    #include "ui/theme/theme.hpp"
#endif

using namespace spectra;

namespace
{

// True if s is well-formed UTF-8 (no codepoint cut in half).
bool valid_utf8(const std::string& s)
{
    for (size_t i = 0; i < s.size();)
    {
        const auto c   = static_cast<unsigned char>(s[i]);
        size_t     len = c < 0x80           ? 1
                         : (c >> 5) == 0x6  ? 2
                         : (c >> 4) == 0xE  ? 3
                         : (c >> 3) == 0x1E ? 4
                                            : 0;
        if (len == 0 || i + len > s.size())
            return false;
        for (size_t k = 1; k < len; ++k)
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80)
                return false;
        i += len;
    }
    return true;
}

// 7 px per codepoint.
float fake_width(const std::string& s, FontSize)
{
    float w = 0.0f;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80)
            w += 7.0f;
    return w;
}

}   // namespace

TEST(LegendBounds, OverwideColumnIsNarrowedToTheRegion)
{
    // col_w > max_w: a single 900 px column in a 230 px region.
    auto g = legend_layout::compute_grid(3, 900.0f, 22.0f, 10.0f, 8.0f, 14.0f, 230.0f, 200.0f);
    EXPECT_EQ(g.cols, 1);
    EXPECT_LE(g.width, 230.0f);
    EXPECT_FLOAT_EQ(legend_layout::column_width(g, 10.0f, 14.0f), 210.0f);
}

TEST(LegendBounds, ColumnWidthUnchangedWhenItFits)
{
    auto g = legend_layout::compute_grid(30, 80.0f, 22.0f, 10.0f, 8.0f, 14.0f, 800.0f, 200.0f);
    EXPECT_GT(g.cols, 1);
    EXPECT_FLOAT_EQ(legend_layout::column_width(g, 10.0f, 14.0f), 80.0f);
}

TEST(LegendBounds, PlaceKeepsOversizedBoxInside)
{
    const Rect vp{100.0f, 50.0f, 200.0f, 120.0f};
    for (auto pos : {LegendPosition::TopLeft,
                     LegendPosition::TopRight,
                     LegendPosition::BottomLeft,
                     LegendPosition::BottomRight})
    {
        const Rect r = legend_layout::place(vp, pos, 500.0f, 400.0f);
        EXPECT_GE(r.x, vp.x);
        EXPECT_GE(r.y, vp.y);
        EXPECT_LE(r.x + r.w, vp.x + vp.w);
        EXPECT_LE(r.y + r.h, vp.y + vp.h);
    }
    EXPECT_FALSE(legend_layout::fits(vp, 500.0f, 40.0f));
    EXPECT_TRUE(legend_layout::fits(vp, 150.0f, 40.0f));
}

TEST(LegendBounds, TinyCellDoesNotFitOneRow)
{
    // A 60 x 30 subplot: compute_grid keeps one row, but it does not fit.
    const Rect vp{0.0f, 0.0f, 60.0f, 30.0f};
    auto       g = legend_layout::compute_grid(2,
                                         120.0f,
                                         22.0f,
                                         10.0f,
                                         8.0f,
                                         14.0f,
                                         vp.w - 2.0f * legend_layout::MARGIN,
                                         vp.h - 2.0f * legend_layout::MARGIN);
    EXPECT_FALSE(legend_layout::fits(vp, g.width, g.height));
}

TEST(LegendBounds, LongUtf8LabelIsEllipsizedOnCodepointBoundaries)
{
    std::string label;
    for (int i = 0; i < 40; ++i)
        label += "\xce\xbc\xe6\xb8\xa9 ";   // "μ温 "
    auto        g      = legend_layout::compute_grid(1,
                                         22.0f + 6.0f + fake_width(label, FontSize::Label),
                                         22.0f,
                                         10.0f,
                                         8.0f,
                                         14.0f,
                                         230.0f,
                                         200.0f);
    const float budget = legend_layout::column_width(g, 10.0f, 14.0f) - 22.0f - 6.0f;
    const auto  fit    = fit_text_to_width(label,
                                       budget,
                                       FontSize::Label,
                                       FontSize::Label,
                                       fake_width,
                                       "\xE2\x80\xA6");
    EXPECT_TRUE(fit.truncated);
    EXPECT_LE(fake_width(fit.text, FontSize::Label), budget);
    EXPECT_FALSE(fit.text.empty());
    EXPECT_TRUE(valid_utf8(fit.text));
}

// ─── S4-U01: inside legends protect the plot area ───────────────────────────

namespace
{
// The QA gallery "layout" scene: 50 long labels at 12 px in a ~530 x 390 plot.
legend_layout::GridLayout dense_grid(LegendPosition pos, const Rect& area, bool capped)
{
    constexpr float col_w = 36.0f + 170.0f, row_h = 18.0f, min_row_h = 14.0f, ovf_w = 60.0f;
    constexpr float pad_x = 10.0f, pad_y = 8.0f;
    if (capped)
        return legend_layout::compute_legend_grid(50,
                                                  col_w,
                                                  row_h,
                                                  min_row_h,
                                                  ovf_w,
                                                  pad_x,
                                                  pad_y,
                                                  legend_layout::COL_GAP,
                                                  area,
                                                  pos);
    return legend_layout::compute_static_grid(50,
                                              col_w,
                                              row_h,
                                              min_row_h,
                                              ovf_w,
                                              pad_x,
                                              pad_y,
                                              legend_layout::COL_GAP,
                                              legend_layout::max_box_w(area, pos),
                                              legend_layout::max_box_h(area, pos));
}
}   // namespace

TEST(LegendCoverageCap, DenseExplicitLegendIsCappedToAShareOfThePlot)
{
    const Rect plot{60.0f, 40.0f, 530.0f, 390.0f};
    for (auto pos : {LegendPosition::TopRight,
                     LegendPosition::TopLeft,
                     LegendPosition::BottomLeft,
                     LegendPosition::BottomRight,
                     LegendPosition::Best})
    {
        const auto before = dense_grid(pos, plot, false);
        const auto after  = dense_grid(pos, plot, true);
        // Uncapped, the legend covers most of the plot (the reported defect).
        EXPECT_GT(legend_layout::grid_coverage(before, plot), 0.6f);
        // Capped: at most INSIDE_MAX_COVERAGE, same width, no scrolling, and
        // every entry is either shown or counted by "+N more".
        EXPECT_LE(legend_layout::grid_coverage(after, plot),
                  legend_layout::INSIDE_MAX_COVERAGE + 1e-4f);
        EXPECT_LE(after.height, legend_layout::INSIDE_MAX_HEIGHT * plot.h + 1e-3f);
        EXPECT_FLOAT_EQ(after.width, before.width);
        EXPECT_FALSE(after.scroll);
        EXPECT_GT(after.overflow, before.overflow);
        EXPECT_EQ(after.shown + after.overflow, 50);
        EXPECT_GE(after.visible_rows, legend_layout::INSIDE_MIN_ROWS);
        // Rows keep at least the tight (font + 2) height: labels are not shrunk.
        EXPECT_GE(after.cell_h, 14.0f);
        EXPECT_TRUE(legend_layout::fits(plot, after.width, after.height, pos));
    }
}

TEST(LegendCoverageCap, SummarisedTallColumnIsCappedToHalfThePlotHeight)
{
    // Wide labels in a tall, narrow plot: one column, summarised, under the
    // area cap but running down nearly the whole plot height.
    const Rect plot{0.0f, 0.0f, 700.0f, 600.0f};
    auto       grid = [&](int n, bool capped)
    {
        return capped ? legend_layout::compute_legend_grid(n,
                                                           300.0f,
                                                           18.0f,
                                                           14.0f,
                                                           60.0f,
                                                           10.0f,
                                                           8.0f,
                                                           legend_layout::COL_GAP,
                                                           plot,
                                                           LegendPosition::TopRight)
                      : legend_layout::compute_static_grid(n,
                                                           300.0f,
                                                           18.0f,
                                                           14.0f,
                                                           60.0f,
                                                           10.0f,
                                                           8.0f,
                                                           legend_layout::COL_GAP,
                                                           300.0f,
                                                           576.0f);
    };
    const auto before = grid(60, false);
    ASSERT_EQ(before.cols, 1);
    ASSERT_GT(before.overflow, 0);
    ASSERT_GT(before.height, 0.8f * plot.h);
    // Narrow max width forces one column for the uncapped reference; the
    // capped grid in the 700 px plot may use two.
    const auto after = grid(60, true);
    EXPECT_LE(after.height, legend_layout::INSIDE_MAX_HEIGHT * plot.h + 0.02f);
    EXPECT_LE(legend_layout::grid_coverage(after, plot),
              legend_layout::INSIDE_MAX_COVERAGE + 1e-4f);
    EXPECT_EQ(after.shown + after.overflow, 60);

    // A complete (not summarised) 12-entry legend is left alone even though
    // it is taller than half of a short plot: it covers little of the area.
    const Rect short_plot{0.0f, 0.0f, 900.0f, 300.0f};
    const auto full = legend_layout::compute_legend_grid(12,
                                                         120.0f,
                                                         18.0f,
                                                         14.0f,
                                                         60.0f,
                                                         10.0f,
                                                         8.0f,
                                                         legend_layout::COL_GAP,
                                                         short_plot,
                                                         LegendPosition::TopRight);
    EXPECT_GT(full.height, legend_layout::INSIDE_MAX_HEIGHT * short_plot.h);
    EXPECT_EQ(full.overflow, 0);
    EXPECT_EQ(full.cols, 1);
}

TEST(LegendCoverageCap, SmallLegendsAndOutsideStripsAreUnchanged)
{
    const Rect plot{0.0f, 0.0f, 530.0f, 390.0f};
    auto       same = [](const legend_layout::GridLayout& a, const legend_layout::GridLayout& b)
    {
        return a.rows == b.rows && a.cols == b.cols && a.shown == b.shown
               && a.overflow == b.overflow && a.width == b.width && a.height == b.height;
    };
    // A handful of entries covers little of the plot: no cap.
    for (int n : {1, 3, 8})
    {
        const auto un  = legend_layout::compute_static_grid(n,
                                                           150.0f,
                                                           18.0f,
                                                           14.0f,
                                                           60.0f,
                                                           10.0f,
                                                           8.0f,
                                                           legend_layout::COL_GAP,
                                                           506.0f,
                                                           366.0f);
        const auto cap = legend_layout::compute_legend_grid(n,
                                                            150.0f,
                                                            18.0f,
                                                            14.0f,
                                                            60.0f,
                                                            10.0f,
                                                            8.0f,
                                                            legend_layout::COL_GAP,
                                                            plot,
                                                            LegendPosition::TopRight);
        EXPECT_TRUE(same(un, cap)) << n;
        EXPECT_EQ(cap.overflow, 0) << n;
    }
    // The OutsideRight strip is not plot area: never capped.
    const Rect strip{540.0f, 0.0f, 260.0f, 390.0f};
    EXPECT_TRUE(same(dense_grid(LegendPosition::OutsideRight, strip, false),
                     dense_grid(LegendPosition::OutsideRight, strip, true)));
}

TEST(LegendCoverageCap, ShortLegendInTinySubplotKeepsMinimumRows)
{
    // Four entries would cover > 35% of this small cell, but the cap never
    // cuts below INSIDE_MIN_ROWS rows, so they all stay listed.
    const Rect cell{0.0f, 0.0f, 200.0f, 140.0f};
    const auto g = legend_layout::compute_legend_grid(4,
                                                      150.0f,
                                                      18.0f,
                                                      14.0f,
                                                      60.0f,
                                                      10.0f,
                                                      8.0f,
                                                      legend_layout::COL_GAP,
                                                      cell,
                                                      LegendPosition::TopLeft);
    EXPECT_GT(legend_layout::grid_coverage(g, cell), legend_layout::INSIDE_MAX_COVERAGE);
    EXPECT_EQ(g.shown, 4);
    EXPECT_EQ(g.overflow, 0);

    // Twelve entries: capped to four rows (three entries + "+9 more").
    const auto g12 = legend_layout::compute_legend_grid(12,
                                                        150.0f,
                                                        18.0f,
                                                        14.0f,
                                                        60.0f,
                                                        10.0f,
                                                        8.0f,
                                                        legend_layout::COL_GAP,
                                                        cell,
                                                        LegendPosition::TopLeft);
    EXPECT_EQ(g12.visible_rows, legend_layout::INSIDE_MIN_ROWS);
    EXPECT_EQ(g12.shown + g12.overflow, 12);
    EXPECT_EQ(g12.overflow, 9);
}

TEST(LegendScroll, WheelOffsetIsClampedToTheEntryRange)
{
    // 50 entries, 13 visible: offsets 0..37.
    EXPECT_EQ(legend_layout::max_scroll(50, 13), 37);
    EXPECT_EQ(legend_layout::max_scroll(5, 13), 0);
    float off = 0.0f;
    off       = legend_layout::scroll_offset(off, 1.0f, 50, 13);   // wheel up at the top
    EXPECT_FLOAT_EQ(off, 0.0f);
    off = legend_layout::scroll_offset(off, -1.0f, 50, 13);   // one notch down
    EXPECT_FLOAT_EQ(off, legend_layout::SCROLL_ENTRIES_PER_NOTCH);
    EXPECT_EQ(legend_layout::scroll_first(off, 50, 13), 3);
    off = legend_layout::scroll_offset(off, -0.5f, 50, 13);   // trackpad half notch
    EXPECT_EQ(legend_layout::scroll_first(off, 50, 13), 5);   // 4.5 rounds away
    off = legend_layout::scroll_offset(off, -100.0f, 50, 13);
    EXPECT_FLOAT_EQ(off, 37.0f);
    EXPECT_EQ(legend_layout::scroll_first(off, 50, 13), 37);
    off = legend_layout::scroll_offset(off, 2.0f, 50, 13);   // back up
    EXPECT_EQ(legend_layout::scroll_first(off, 50, 13), 31);
    // Entries removed: a stale offset is clamped, a non-finite one resets.
    EXPECT_EQ(legend_layout::scroll_first(37.0f, 20, 13), 7);
    EXPECT_FLOAT_EQ(legend_layout::scroll_offset(37.0f, 0.0f, 20, 13), 7.0f);
    EXPECT_FLOAT_EQ(legend_layout::scroll_offset(NAN, 0.0f, 50, 13), 0.0f);
    EXPECT_EQ(legend_layout::scroll_first(NAN, 50, 13), 0);
}

TEST(LegendScroll, HiddenEntriesAreThoseBeforeAndAfterTheVisibleRun)
{
    auto h = legend_layout::hidden_entry_indices(10, 0, 4);
    EXPECT_EQ(h, (std::vector<int>{4, 5, 6, 7, 8, 9}));
    h = legend_layout::hidden_entry_indices(10, 3, 4);
    EXPECT_EQ(h, (std::vector<int>{0, 1, 2, 7, 8, 9}));
    h = legend_layout::hidden_entry_indices(10, 6, 4);   // scrolled to the end
    EXPECT_EQ(h, (std::vector<int>{0, 1, 2, 3, 4, 5}));
    h = legend_layout::hidden_entry_indices(10, 99, 4);   // clamped
    EXPECT_EQ(h, (std::vector<int>{0, 1, 2, 3, 4, 5}));
    EXPECT_TRUE(legend_layout::hidden_entry_indices(3, 0, 4).empty());
}

#ifdef SPECTRA_TEST_IMGUI_LEGEND

// The interactive legend's ImGui window stays inside a narrow viewport even
// with a very long label, and the label text does not run past its column.
TEST(LegendBoundsImGui, InteractiveLegendStaysInsideNarrowAxes)
{
    ImGui::CreateContext();
    ImGuiIO& io    = ImGui::GetIO();
    io.DisplaySize = ImVec2(800.0f, 600.0f);
    io.DeltaTime   = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    // Untextured AA lines, so only glyph vertices sample the font texture.
    ImGui::GetStyle().AntiAliasedLinesUseTex = false;
    unsigned char* px                        = nullptr;
    int            tw = 0, th = 0;
    io.Fonts->GetTexDataAsRGBA32(&px, &tw, &th);

    ui::ThemeManager  theme;
    LegendInteraction legend;
    legend.set_theme_manager(&theme);

    Axes               ax;
    std::vector<float> x = {0.0f, 1.0f};
    ax.line(x, x).label(
        "A deliberately long legend label \xce\xbc\xe6\xb8\xa9 that would never fit in this narrow "
        "subplot cell at all");
    ax.line(x, x).label("short");
    const Rect vp{20.0f, 30.0f, 220.0f, 160.0f};

    LegendConfig cfg;
    cfg.position = LegendPosition::TopLeft;
    cfg.bg_color = Color{0.2f, 0.2f, 0.2f, 1.0f};   // plain window, no glass card

    ImGui::NewFrame();
    legend.draw(ax, vp, 0, cfg, 1);
    ImGui::Render();

    ImGuiWindow* win = ImGui::FindWindowByName(legend.window_name(1, 0).c_str());
    ASSERT_NE(win, nullptr);
    EXPECT_GE(win->Pos.x, vp.x);
    EXPECT_GE(win->Pos.y, vp.y);
    EXPECT_LE(win->Pos.x + win->Size.x, vp.x + vp.w);
    EXPECT_LE(win->Pos.y + win->Size.y, vp.y + vp.h);

    // Every vertex the legend emits lies inside the axes, and label glyphs
    // (textured vertices) end before the visibility-icon slot at the right
    // end of the column (window padding pad_x + 4 = 14, eye slot 16).
    const ImVec2 white   = io.Fonts->TexUvWhitePixel;
    const float  label_r = win->Pos.x + win->Size.x - 14.0f - 16.0f;
    int          glyphs  = 0;
    for (const ImDrawVert& v : win->DrawList->VtxBuffer)
    {
        EXPECT_GE(v.pos.x, vp.x - 0.5f);
        EXPECT_LE(v.pos.x, vp.x + vp.w + 0.5f);
        if (v.uv.x != white.x || v.uv.y != white.y)
        {
            ++glyphs;
            EXPECT_LE(v.pos.x, label_r + 0.5f);
        }
    }
    EXPECT_GT(glyphs, 0);
    ImGui::DestroyContext();
}

// A summarised interactive legend: the "+N more" cell lists the hidden
// entries on hover, and clicking it opens the full (scrollable) legend whose
// rows toggle series visibility like the legend's own rows.
TEST(LegendBoundsImGui, OverflowCellIsDiscoverable)
{
    ImGui::CreateContext();
    ImGuiIO& io    = ImGui::GetIO();
    io.DisplaySize = ImVec2(1000.0f, 700.0f);
    io.DeltaTime   = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    unsigned char* px = nullptr;
    int            tw = 0, th = 0;
    io.Fonts->GetTexDataAsRGBA32(&px, &tw, &th);

    ui::ThemeManager  theme;
    LegendInteraction legend;
    legend.set_theme_manager(&theme);

    Axes               ax;
    std::vector<float> x = {0.0f, 1.0f};
    for (int i = 0; i < 50; ++i)
        ax.line(x, x).label("Series " + std::to_string(i + 1));
    const Rect vp{20.0f, 30.0f, 380.0f, 200.0f};
    ax.set_viewport(vp);

    LegendConfig cfg;
    cfg.position = LegendPosition::TopLeft;

    auto frame = [&](float mx, float my, bool down)
    {
        io.AddMousePosEvent(mx, my);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
        ImGui::NewFrame();
        legend.draw(ax, vp, 0, cfg, 1);
        ImGui::Render();
    };

    frame(-100.0f, -100.0f, false);
    const auto* oc = legend.overflow_cell(1, 0);
    ASSERT_NE(oc, nullptr);
    EXPECT_GT(oc->hidden, 0);
    EXPECT_EQ(oc->shown + oc->hidden, 50);
    EXPECT_FALSE(oc->hovered);
    EXPECT_FALSE(oc->popup_open);
    // The cell is inside the plot area.
    EXPECT_GE(oc->cell.x, vp.x);
    EXPECT_LE(oc->cell.y + oc->cell.h, vp.y + vp.h);

    // Hover: tooltip with the hidden entries.
    const float cx = oc->cell.x + 4.0f;
    const float cy = oc->cell.y + oc->cell.h * 0.5f;
    frame(cx, cy, false);
    frame(cx, cy, false);
    oc = legend.overflow_cell(1, 0);
    ASSERT_NE(oc, nullptr);
    EXPECT_TRUE(oc->hovered);

    // Click: the full legend popup opens and lists entries.
    frame(cx, cy, true);
    frame(cx, cy, false);
    frame(cx, cy, false);
    oc = legend.overflow_cell(1, 0);
    ASSERT_NE(oc, nullptr);
    ASSERT_TRUE(oc->popup_open);
    EXPECT_GT(oc->popup_rows, 0);
    EXPECT_GT(oc->popup_list.h, 0.0f);

    // Clicking the first popup row toggles the first series, and the popup stays open.
    ASSERT_TRUE(ax.series()[0]->visible());
    const float rx = oc->popup_list.x + 12.0f;
    const float ry = oc->popup_list.y + 4.0f;
    frame(rx, ry, false);
    frame(rx, ry, true);
    frame(rx, ry, false);
    frame(rx, ry, false);
    EXPECT_FALSE(ax.series()[0]->visible());
    oc = legend.overflow_cell(1, 0);
    ASSERT_NE(oc, nullptr);
    EXPECT_TRUE(oc->popup_open);

    ImGui::DestroyContext();
}

// S4-U01: the summarised interactive legend scrolls its entries with the
// mouse wheel while hovered; the "+N more" cell then summarises the entries
// before and after the visible run (same total), and wheel-up returns.
TEST(LegendBoundsImGui, WheelOverSummarisedLegendScrollsEntries)
{
    ImGui::CreateContext();
    ImGuiIO& io    = ImGui::GetIO();
    io.DisplaySize = ImVec2(1000.0f, 700.0f);
    io.DeltaTime   = 1.0f / 60.0f;
    // One frame per event: a wheel event lands in the frame it is sent in.
    io.ConfigInputTrickleEventQueue = false;
    io.Fonts->AddFontDefault();
    unsigned char* px = nullptr;
    int            tw = 0, th = 0;
    io.Fonts->GetTexDataAsRGBA32(&px, &tw, &th);

    ui::ThemeManager  theme;
    LegendInteraction legend;
    legend.set_theme_manager(&theme);

    Axes               ax;
    std::vector<float> x = {0.0f, 1.0f};
    for (int i = 0; i < 50; ++i)
        ax.line(x, x).label("Series " + std::to_string(i + 1));
    const Rect vp{20.0f, 30.0f, 600.0f, 400.0f};
    ax.set_viewport(vp);

    LegendConfig cfg;
    cfg.position = LegendPosition::TopRight;   // explicit inside position

    auto frame = [&](float mx, float my, float wheel)
    {
        io.AddMousePosEvent(mx, my);
        if (wheel != 0.0f)
            io.AddMouseWheelEvent(0.0f, wheel);
        ImGui::NewFrame();
        const bool consumed = legend.draw(ax, vp, 0, cfg, 1);
        ImGui::Render();
        return consumed;
    };

    frame(-100.0f, -100.0f, 0.0f);
    frame(-100.0f, -100.0f, 0.0f);
    ImGuiWindow* win = ImGui::FindWindowByName(legend.window_name(1, 0).c_str());
    ASSERT_NE(win, nullptr);
    // The capped box covers at most INSIDE_MAX_COVERAGE of the plot.
    EXPECT_LE(win->Size.x * win->Size.y, legend_layout::INSIDE_MAX_COVERAGE * vp.w * vp.h + 1.0f);
    const auto* oc = legend.overflow_cell(1, 0);
    ASSERT_NE(oc, nullptr);
    const int shown  = oc->shown;
    const int hidden = oc->hidden;
    EXPECT_EQ(shown + hidden, 50);
    EXPECT_EQ(oc->first, 0);

    // Wheel over the plot but outside the legend: no scroll.
    frame(vp.x + 10.0f, vp.y + vp.h - 10.0f, -1.0f);
    oc = legend.overflow_cell(1, 0);
    ASSERT_NE(oc, nullptr);
    EXPECT_EQ(oc->first, 0);

    // Wheel down over the legend box: the run moves, the click/wheel is consumed.
    const float lx = win->Pos.x + 30.0f;
    const float ly = win->Pos.y + win->Size.y * 0.3f;
    frame(lx, ly, 0.0f);
    EXPECT_TRUE(frame(lx, ly, -1.0f));
    oc = legend.overflow_cell(1, 0);
    ASSERT_NE(oc, nullptr);
    EXPECT_EQ(oc->first, static_cast<int>(legend_layout::SCROLL_ENTRIES_PER_NOTCH));
    EXPECT_EQ(oc->shown, shown);
    EXPECT_EQ(oc->hidden, hidden);

    // Far down: clamped to the last full page.
    for (int i = 0; i < 30; ++i)
        frame(lx, ly, -1.0f);
    oc = legend.overflow_cell(1, 0);
    ASSERT_NE(oc, nullptr);
    EXPECT_EQ(oc->first, 50 - shown);

    // Hovering "+N more" now lists the entries before the run first.
    const float cx = oc->cell.x + 4.0f;
    const float cy = oc->cell.y + oc->cell.h * 0.5f;
    frame(cx, cy, 0.0f);
    frame(cx, cy, 0.0f);
    oc = legend.overflow_cell(1, 0);
    ASSERT_NE(oc, nullptr);
    EXPECT_TRUE(oc->hovered);
    EXPECT_EQ(oc->tooltip_first, "Series 1");

    // Wheel up returns to the top.
    for (int i = 0; i < 30; ++i)
        frame(lx, ly, 1.0f);
    oc = legend.overflow_cell(1, 0);
    ASSERT_NE(oc, nullptr);
    EXPECT_EQ(oc->first, 0);

    ImGui::DestroyContext();
}

// Legend ImGui windows are pooled: ImGui never frees a window, so a name per
// figure address leaked one window per figure ever shown (soak: +5 windows per
// open/close cycle).  Forgotten figures return their name for reuse.
TEST(LegendBoundsImGui, LegendWindowsAreReusedAfterFigureIsForgotten)
{
    ImGui::CreateContext();
    ImGuiIO& io    = ImGui::GetIO();
    io.DisplaySize = ImVec2(800.0f, 600.0f);
    io.DeltaTime   = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    unsigned char* px = nullptr;
    int            tw = 0, th = 0;
    io.Fonts->GetTexDataAsRGBA32(&px, &tw, &th);

    ui::ThemeManager  theme;
    LegendInteraction legend;
    legend.set_theme_manager(&theme);

    Axes               ax;
    std::vector<float> x = {0.0f, 1.0f};
    ax.line(x, x).label("a");
    const Rect   vp{20.0f, 30.0f, 400.0f, 300.0f};
    LegendConfig cfg;
    cfg.position = LegendPosition::TopLeft;

    auto frame = [&](std::initializer_list<uintptr_t> figures)
    {
        ImGui::NewFrame();
        for (uintptr_t f : figures)
            legend.draw(ax, vp, 0, cfg, f);
        ImGui::Render();
    };

    // Two figures side by side: two distinct windows.
    frame({1, 2});
    const std::string w1 = legend.window_name(1, 0);
    const std::string w2 = legend.window_name(2, 0);
    ASSERT_FALSE(w1.empty());
    ASSERT_FALSE(w2.empty());
    EXPECT_NE(w1, w2);
    EXPECT_NE(ImGui::FindWindowByName(w1.c_str()), nullptr);
    const int windows = ImGui::GetCurrentContext()->Windows.Size;

    // Churn: close figure 1, open a new one, many times.  The pool and the
    // ImGui window list stay bounded; the live figure keeps its own window.
    uintptr_t prev = 1;
    for (uintptr_t f = 3; f < 40; ++f)
    {
        legend.forget_figure(prev);
        EXPECT_TRUE(legend.window_name(prev, 0).empty());
        frame({f, 2});
        EXPECT_EQ(legend.window_name(2, 0), w2);
        EXPECT_NE(legend.window_name(f, 0), w2);
        prev = f;
    }
    EXPECT_EQ(legend.legend_window_pool_size(), 2u);
    EXPECT_EQ(ImGui::GetCurrentContext()->Windows.Size, windows);
    ImGui::DestroyContext();
}

#endif
