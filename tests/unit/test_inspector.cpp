#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>

#include <spectra/axes3d.hpp>
#include <string>

#include "ui/imgui/field_layout.hpp"
// SelectionContext is header-only, no ImGui dependency
#include "ui/input/axes_selection.hpp"
#include "ui/input/selection_context.hpp"

#if defined(SPECTRA_USE_IMGUI) && __has_include(<imgui.h>)
    #define SPECTRA_TEST_IMGUI_INSPECTOR 1
    #include <imgui.h>
    #include <imgui_internal.h>

    #include "ui/imgui/widgets.hpp"
    #include "ui/overlay/inspector.hpp"
    #include "ui/theme/theme.hpp"
#endif

using namespace spectra;
using namespace spectra::ui;

// ─── SelectionContext Tests ─────────────────────────────────────────────────

TEST(SelectionContext, DefaultIsNone)
{
    SelectionContext ctx;
    EXPECT_EQ(ctx.type, SelectionType::None);
    EXPECT_EQ(ctx.figure, nullptr);
    EXPECT_EQ(ctx.axes, nullptr);
    EXPECT_EQ(ctx.series, nullptr);
    EXPECT_EQ(ctx.axes_index, -1);
    EXPECT_EQ(ctx.series_index, -1);
}

TEST(SelectionContext, SelectFigure)
{
    Figure           fig;
    SelectionContext ctx;
    ctx.select_figure(&fig);

    EXPECT_EQ(ctx.type, SelectionType::Figure);
    EXPECT_EQ(ctx.figure, &fig);
    EXPECT_EQ(ctx.axes, nullptr);
    EXPECT_EQ(ctx.series, nullptr);
}

TEST(SelectionContext, SelectAxes)
{
    Figure           fig;
    auto&            ax = fig.subplot(1, 1, 1);
    SelectionContext ctx;
    ctx.select_axes(&fig, &ax, 0);

    EXPECT_EQ(ctx.type, SelectionType::Axes);
    EXPECT_EQ(ctx.figure, &fig);
    EXPECT_EQ(ctx.axes, &ax);
    EXPECT_EQ(ctx.axes_index, 0);
    EXPECT_EQ(ctx.series, nullptr);
}

TEST(SelectionContext, SelectSeries)
{
    Figure fig;
    auto&  ax  = fig.subplot(1, 1, 1);
    float  x[] = {1, 2, 3};
    float  y[] = {4, 5, 6};
    auto&  s   = ax.line(x, y).label("test");

    SelectionContext ctx;
    ctx.select_series(&fig, &ax, 0, &s, 0);

    EXPECT_EQ(ctx.type, SelectionType::Series);
    EXPECT_EQ(ctx.figure, &fig);
    EXPECT_EQ(ctx.axes, &ax);
    EXPECT_EQ(ctx.series, &s);
    EXPECT_EQ(ctx.axes_index, 0);
    EXPECT_EQ(ctx.series_index, 0);
}

TEST(SelectionContext, ClearResetsAll)
{
    Figure fig;
    auto&  ax  = fig.subplot(1, 1, 1);
    float  x[] = {1, 2, 3};
    float  y[] = {4, 5, 6};
    auto&  s   = ax.line(x, y);

    SelectionContext ctx;
    ctx.select_series(&fig, &ax, 0, &s, 0);
    EXPECT_EQ(ctx.type, SelectionType::Series);

    ctx.clear();
    EXPECT_EQ(ctx.type, SelectionType::None);
    EXPECT_EQ(ctx.figure, nullptr);
    EXPECT_EQ(ctx.axes, nullptr);
    EXPECT_EQ(ctx.series, nullptr);
    EXPECT_EQ(ctx.axes_index, -1);
    EXPECT_EQ(ctx.series_index, -1);
}

TEST(SelectionContext, SelectFigureClearsPrevious)
{
    Figure fig;
    auto&  ax  = fig.subplot(1, 1, 1);
    float  x[] = {1, 2, 3};
    float  y[] = {4, 5, 6};
    auto&  s   = ax.line(x, y);

    SelectionContext ctx;
    ctx.select_series(&fig, &ax, 0, &s, 0);
    EXPECT_EQ(ctx.type, SelectionType::Series);
    EXPECT_NE(ctx.series, nullptr);

    // Selecting figure should clear series/axes
    ctx.select_figure(&fig);
    EXPECT_EQ(ctx.type, SelectionType::Figure);
    EXPECT_EQ(ctx.series, nullptr);
    EXPECT_EQ(ctx.axes, nullptr);
    EXPECT_EQ(ctx.axes_index, -1);
}

TEST(SelectionContext, MultipleAxesSelection)
{
    Figure fig;
    auto&  ax0 = fig.subplot(2, 1, 1);
    auto&  ax1 = fig.subplot(2, 1, 2);

    SelectionContext ctx;
    ctx.select_axes(&fig, &ax0, 0);
    EXPECT_EQ(ctx.axes, &ax0);
    EXPECT_EQ(ctx.axes_index, 0);

    ctx.select_axes(&fig, &ax1, 1);
    EXPECT_EQ(ctx.axes, &ax1);
    EXPECT_EQ(ctx.axes_index, 1);
}

// ─── Axes Selection Preservation Tests ───────────────────────────────────────

TEST(SelectionContext, PreserveAxesIndexWhenSwitchingFigures)
{
    // Create two figures with different numbers of axes
    Figure fig1;
    Figure fig2;

    auto& fig1_ax0 = fig1.subplot(2, 1, 1);   // index 0
    auto& fig1_ax1 = fig1.subplot(2, 1, 2);   // index 1

    auto& fig2_ax0 = fig2.subplot(1, 1, 1);   // index 0 (only axes)

    SelectionContext ctx;

    // Select axes index 1 in fig1
    ctx.select_axes(&fig1, &fig1_ax1, 1);
    EXPECT_EQ(ctx.type, SelectionType::Axes);
    EXPECT_EQ(ctx.figure, &fig1);
    EXPECT_EQ(ctx.axes, &fig1_ax1);
    EXPECT_EQ(ctx.axes_index, 1);

    // Simulate switching to fig2 with same axes index (invalid, should fallback to 0)
    // This mimics the logic in ImGuiIntegration::build_ui
    if (ctx.type == SelectionType::Axes && ctx.figure != &fig2)
    {
        int target_idx = ctx.axes_index;
        if (target_idx >= 0 && target_idx < static_cast<int>(fig2.axes().size()))
        {
            ctx.select_axes(&fig2, fig2.axes_mut()[target_idx].get(), target_idx);
        }
        else
        {
            // Index out of range, fall back to first axes
            ctx.select_axes(&fig2, fig2.axes_mut()[0].get(), 0);
        }
    }

    // Should have switched to fig2's axes at index 0 (fallback)
    EXPECT_EQ(ctx.type, SelectionType::Axes);
    EXPECT_EQ(ctx.figure, &fig2);
    EXPECT_EQ(ctx.axes, &fig2_ax0);
    EXPECT_EQ(ctx.axes_index, 0);

    // Now try switching back to fig1 with valid index 0
    if (ctx.type == SelectionType::Axes && ctx.figure != &fig1)
    {
        int target_idx = 0;   // Explicitly test index 0
        if (target_idx >= 0 && target_idx < static_cast<int>(fig1.axes().size()))
        {
            ctx.select_axes(&fig1, fig1.axes_mut()[target_idx].get(), target_idx);
        }
        else
        {
            // Fallback (shouldn't happen in this case)
            ctx.select_axes(&fig1, fig1.axes_mut()[0].get(), 0);
        }
    }

    // Should have switched to fig1's axes at index 0
    EXPECT_EQ(ctx.type, SelectionType::Axes);
    EXPECT_EQ(ctx.figure, &fig1);
    EXPECT_EQ(ctx.axes, &fig1_ax0);
    EXPECT_EQ(ctx.axes_index, 0);
}

TEST(SelectionContext, HandleEmptyFigureWhenSwitching)
{
    Figure fig_with_axes;
    Figure empty_fig;

    auto& ax = fig_with_axes.subplot(1, 1, 1);

    SelectionContext ctx;
    ctx.select_axes(&fig_with_axes, &ax, 0);

    EXPECT_EQ(ctx.type, SelectionType::Axes);
    EXPECT_EQ(ctx.figure, &fig_with_axes);

    // Simulate switching to empty figure (should clear selection)
    if (ctx.figure != &empty_fig && empty_fig.axes().empty())
    {
        ctx.clear();
    }

    // Selection should be cleared
    EXPECT_EQ(ctx.type, SelectionType::None);
    EXPECT_EQ(ctx.figure, nullptr);
    EXPECT_EQ(ctx.axes, nullptr);
    EXPECT_EQ(ctx.axes_index, -1);
}

// ─── SelectionType enum coverage ────────────────────────────────────────────

TEST(SelectionType, AllValuesDistinct)
{
    EXPECT_NE(SelectionType::None, SelectionType::Figure);
    EXPECT_NE(SelectionType::Figure, SelectionType::Axes);
    EXPECT_NE(SelectionType::Axes, SelectionType::Series);
    EXPECT_NE(SelectionType::None, SelectionType::Series);
}

// ─── Axes section selection: 2D and 3D axes (issue #105) ───────────────────

TEST(AxesSelection, SlotsListBoth2DAnd3DAxesInSlotOrder)
{
    Figure fig;
    auto&  a2    = fig.subplot(1, 2, 1);
    auto&  a3    = fig.subplot3d(1, 2, 2);
    auto   slots = figure_axes_slots(fig);
    ASSERT_EQ(slots.size(), 2u);
    EXPECT_EQ(slots[0].axes2d, &a2);
    EXPECT_EQ(slots[0].axes3d, nullptr);
    EXPECT_EQ(slots[0].index, 0);
    EXPECT_EQ(slots[1].axes3d, &a3);
    EXPECT_EQ(slots[1].axes2d, nullptr);
    EXPECT_EQ(slots[1].base, static_cast<AxesBase*>(&a3));
    EXPECT_EQ(slots[1].index, 1);
}

TEST(AxesSelection, Only3DFigureSelectsThe3DAxes)
{
    // Before #105 the Axes section cleared the selection when a figure had
    // no 2D axes, so a 3D-only figure showed an empty inspector.
    Figure           fig;
    auto&            a3 = fig.subplot3d(1, 1, 1);
    SelectionContext ctx;
    sync_axes_section_selection(ctx, fig);
    EXPECT_EQ(ctx.type, SelectionType::Axes);
    EXPECT_EQ(ctx.axes, nullptr);
    EXPECT_EQ(ctx.axes_base, static_cast<AxesBase*>(&a3));
    EXPECT_EQ(ctx.axes_index, 0);
}

TEST(AxesSelection, Keeps3DSelectionAndSlotAcrossFigures)
{
    Figure f1;
    f1.subplot(1, 2, 1);
    auto&  f1_3d = f1.subplot3d(1, 2, 2);
    Figure f2;
    f2.subplot(1, 2, 1);
    auto& f2_3d = f2.subplot3d(1, 2, 2);

    SelectionContext ctx;
    select_axes_slot(ctx, f1, figure_axes_slots(f1)[1]);
    sync_axes_section_selection(ctx, f1);
    EXPECT_EQ(ctx.axes_base, static_cast<AxesBase*>(&f1_3d));

    sync_axes_section_selection(ctx, f2);   // switched tab: same slot
    EXPECT_EQ(ctx.figure, &f2);
    EXPECT_EQ(ctx.axes_base, static_cast<AxesBase*>(&f2_3d));
    EXPECT_EQ(ctx.axes_index, 1);
}

TEST(AxesSelection, EmptyFigureClears)
{
    Figure           fig;
    SelectionContext ctx;
    ctx.type = SelectionType::Axes;
    sync_axes_section_selection(ctx, fig);
    EXPECT_EQ(ctx.type, SelectionType::None);
}

// ─── Compact inspector rows (narrow panels) ─────────────────────────────────

TEST(InspectorRowLayout, FieldRowsStackWhenTheValueColumnIsTooNarrow)
{
    using namespace spectra::ui::widgets;
    namespace tk = spectra::ui::tokens;
    // Default 320 px inspector: side by side, value after the label column.
    RowLayout r = field_row_layout(288.0f, 40.0f);
    EXPECT_FALSE(r.stacked);
    EXPECT_FLOAT_EQ(r.value_x, tk::INSPECTOR_LABEL_WIDTH);
    // At the 240 px minimum (~208 px content) rows still fit side by side.
    EXPECT_FALSE(field_row_layout(208.0f, 40.0f).stacked);
    // Squeezed below 240 px (down to the 160 px fitted minimum): stacked.
    EXPECT_TRUE(field_row_layout(170.0f, 40.0f).stacked);
    EXPECT_TRUE(field_row_layout(128.0f, 40.0f).stacked);
    // A long label pushes the value column right; too far and it stacks.
    EXPECT_FALSE(field_row_layout(288.0f, 150.0f).stacked);
    EXPECT_FLOAT_EQ(field_row_layout(288.0f, 150.0f).value_x, 150.0f);
    EXPECT_TRUE(field_row_layout(208.0f, 150.0f).stacked);
}

TEST(InspectorRowLayout, TextAndToggleRowsNeverOverlapTheirLabel)
{
    using namespace spectra::ui::widgets;
    RowLayout r = text_row_layout(288.0f, 50.0f, 60.0f, 0.45f);
    EXPECT_FALSE(r.stacked);
    EXPECT_FLOAT_EQ(r.value_x, 288.0f * 0.45f);
    // A label longer than 45 % moves the value after it rather than under it.
    r = text_row_layout(288.0f, 180.0f, 60.0f, 0.45f);
    EXPECT_FALSE(r.stacked);
    EXPECT_FLOAT_EQ(r.value_x, 180.0f);
    // Narrow row: the value would run past the edge, so it goes below.
    EXPECT_TRUE(text_row_layout(140.0f, 90.0f, 70.0f, 0.45f).stacked);

    EXPECT_FALSE(toggle_row_layout(288.0f, 100.0f, 36.0f).stacked);
    EXPECT_FLOAT_EQ(toggle_row_layout(288.0f, 100.0f, 36.0f).value_x, 252.0f);
    EXPECT_TRUE(toggle_row_layout(140.0f, 120.0f, 36.0f).stacked);
}

#ifdef SPECTRA_TEST_IMGUI_INSPECTOR

class InspectorImGui : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        ctx_           = ImGui::CreateContext();
        ImGuiIO& io    = ImGui::GetIO();
        io.DeltaTime   = 1.0f / 60.0f;
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(400.0f, 2000.0f);
        io.Fonts->AddFontDefault();
        unsigned char* px = nullptr;
        int            tw = 0, th = 0;
        io.Fonts->GetTexDataAsRGBA32(&px, &tw, &th);
        tm_.ensure_initialized();
        ThemeManager::set_current(&tm_);
    }

    void TearDown() override
    {
        ThemeManager::set_current(nullptr);
        ImGui::DestroyContext(ctx_);
    }

    // Draw the inspector for a few frames (sections animate open) and return
    // the text it emitted on the last one.
    std::string draw(Inspector& insp, Figure& fig, int frames = 40)
    {
        std::string text;
        for (int i = 0; i < frames; ++i)
        {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(400, 2000));
            ImGui::Begin("inspector_test");
            const bool last = (i == frames - 1);
            if (last)
                ImGui::LogToBuffer();
            insp.draw(fig);
            if (last)
            {
                text = ImGui::GetCurrentContext()->LogBuffer.c_str();
                ImGui::LogFinish();
            }
            ImGui::End();
            ImGui::Render();
        }
        return text;
    }

    ImGuiContext* ctx_ = nullptr;
    ThemeManager  tm_;
};

TEST_F(InspectorImGui, Shows3DAxesPage)
{
    Figure fig;
    fig.subplot(1, 2, 1);
    auto& a3 = fig.subplot3d(1, 2, 2);
    a3.title("Helix");

    SelectionContext ctx;
    select_axes_slot(ctx, fig, figure_axes_slots(fig)[1]);
    Inspector insp;
    insp.set_context(ctx);
    const std::string out = draw(insp, fig);

    EXPECT_NE(out.find("Axes 2 (3D)"), std::string::npos) << out;
    EXPECT_NE(out.find("Enable Lighting"), std::string::npos) << out;
    EXPECT_NE(out.find("Bounding Box"), std::string::npos) << out;
    EXPECT_NE(out.find("XY Plane"), std::string::npos) << out;
    EXPECT_NE(out.find("Reset Camera"), std::string::npos) << out;
    EXPECT_NE(out.find("Projection"), std::string::npos) << out;
    // The selection survives the draw (3D page does not drop it).
    EXPECT_EQ(insp.context().axes_base, static_cast<AxesBase*>(&a3));
}

TEST_F(InspectorImGui, 2DAxesPageHasNo3DControls)
{
    Figure fig;
    auto&  a2 = fig.subplot(1, 1, 1);

    SelectionContext ctx;
    select_axes_slot(ctx, fig, figure_axes_slots(fig)[0]);
    Inspector insp;
    insp.set_context(ctx);
    const std::string out = draw(insp, fig);

    EXPECT_NE(out.find("Axes 1"), std::string::npos) << out;
    EXPECT_EQ(out.find("(3D)"), std::string::npos) << out;
    EXPECT_EQ(out.find("Enable Lighting"), std::string::npos) << out;
    EXPECT_EQ(insp.context().axes, &a2);
}

TEST_F(InspectorImGui, NarrowPanelStacksFieldRows)
{
    // Lay out one slider row in a window of the given width; return the
    // slider frame's left edge relative to the content start, and whether it
    // sits below the label line.
    auto slider_row = [](float width)
    {
        struct Out
        {
            float dx    = 0.0f;
            bool  below = false;
        } out;
        float v = 0.5f;
        for (int i = 0; i < 3; ++i)
        {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(width, 400));
            ImGui::Begin("row_test", nullptr, ImGuiWindowFlags_NoDecoration);
            const ImVec2 start = ImGui::GetCursorScreenPos();
            widgets::slider_field("Line Width", v, 0.0f, 10.0f);
            out.dx    = ImGui::GetItemRectMin().x - start.x;
            out.below = ImGui::GetItemRectMin().y >= start.y + ImGui::GetTextLineHeight();
            ImGui::End();
            ImGui::Render();
        }
        return out;
    };

    const auto wide = slider_row(320.0f);
    EXPECT_FALSE(wide.below);
    EXPECT_NEAR(wide.dx, spectra::ui::tokens::INSPECTOR_LABEL_WIDTH, 1.0f);

    const auto narrow = slider_row(180.0f);
    EXPECT_TRUE(narrow.below) << "label and slider must not share a squeezed row";
    EXPECT_NEAR(narrow.dx, 0.0f, 1.0f) << "stacked slider takes the full width";
}

#endif   // SPECTRA_TEST_IMGUI_INSPECTOR
