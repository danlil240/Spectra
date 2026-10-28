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

    ImGuiWindow* win = ImGui::FindWindowByName(std::format("##legend_{:x}_{}", 1ul, 0).c_str());
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

#endif
