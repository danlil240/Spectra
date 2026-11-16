// View history (Back / Forward) and single-axis navigation: the history
// model, wheel modifiers, axis gutters, thin box zoom, horizontal wheel pan,
// and how view changes interact with the undo stack and linked axes.

#include <cmath>

#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>

#include "ui/commands/undo_manager.hpp"
#include "ui/data/axis_link.hpp"
#include "ui/input/input.hpp"
#include "ui/input/view_history.hpp"
#include "ui/input/view_nav.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include <spectra/figure_registry.hpp>

    #include "render/backend.hpp"
    #include "render/renderer.hpp"
    #include "ui/app/session_runtime.hpp"
    #include "ui/app/window_ui_context.hpp"
    #include "ui/app/window_ui_context_builder.hpp"
    #include "ui/commands/command_registry.hpp"
    #include "ui/commands/shortcut_manager.hpp"
    #include "ui/imgui/imgui_integration.hpp"
    #include "ui/theme/theme.hpp"
#endif

using namespace spectra;
using namespace std::chrono_literals;

namespace
{
constexpr int kPress   = 1;
constexpr int kRelease = 0;
constexpr int kLeft    = 0;
constexpr int kMiddle  = 2;
constexpr int kBack    = 3;   // mouse button 4
constexpr int kForward = 4;   // mouse button 5
constexpr int kShift   = 0x0001;
constexpr int kCtrl    = 0x0002;
constexpr int kAlt     = 0x0004;
constexpr int kKeyEsc  = 256;

ViewState one(Axes* ax, double x0, double x1, double y0 = 0.0, double y1 = 1.0)
{
    return {{ax, {x0, x1}, {y0, y1}}};
}
}   // namespace

// ─── ViewHistory model ──────────────────────────────────────────────────────

TEST(ViewHistory, PushBackForwardWalkTheStates)
{
    Axes*       ax = reinterpret_cast<Axes*>(0x10);
    ViewHistory h;
    EXPECT_FALSE(h.can_back());
    EXPECT_FALSE(h.can_forward());
    EXPECT_EQ(h.back(), nullptr);

    ASSERT_TRUE(h.push("a", one(ax, 0, 10), one(ax, 2, 8)).entry);
    ASSERT_TRUE(h.push("b", one(ax, 2, 8), one(ax, 3, 5)).entry);
    EXPECT_EQ(h.size(), 2u);
    EXPECT_EQ(h.back_label(), "b");

    const ViewState* s = h.back();
    ASSERT_NE(s, nullptr);
    EXPECT_DOUBLE_EQ(s->front().x.min, 2.0);
    s = h.back();
    ASSERT_NE(s, nullptr);
    EXPECT_DOUBLE_EQ(s->front().x.min, 0.0);
    EXPECT_EQ(h.back(), nullptr);
    EXPECT_EQ(h.forward_label(), "a");

    s = h.forward();
    ASSERT_NE(s, nullptr);
    EXPECT_DOUBLE_EQ(s->front().x.max, 8.0);
    s = h.forward();
    ASSERT_NE(s, nullptr);
    EXPECT_DOUBLE_EQ(s->front().x.max, 5.0);
    EXPECT_EQ(h.forward(), nullptr);
}

TEST(ViewHistory, NewChangeDropsForwardEntries)
{
    Axes*       ax = reinterpret_cast<Axes*>(0x10);
    ViewHistory h;
    h.push("a", one(ax, 0, 10), one(ax, 2, 8));
    h.push("b", one(ax, 2, 8), one(ax, 3, 5));
    h.back();
    h.push("c", one(ax, 2, 8), one(ax, 1, 9));
    EXPECT_EQ(h.size(), 2u);
    EXPECT_FALSE(h.can_forward());
    EXPECT_EQ(h.back_label(), "c");
}

TEST(ViewHistory, UnchangedViewIsNotRecorded)
{
    Axes*       ax = reinterpret_cast<Axes*>(0x10);
    ViewHistory h;
    EXPECT_FALSE(h.push("noop", one(ax, 0, 10), one(ax, 0, 10)).entry);
    EXPECT_EQ(h.size(), 0u);
}

TEST(ViewHistory, CoalescesABurstWithinTheWindow)
{
    Axes*                                ax = reinterpret_cast<Axes*>(0x10);
    ViewHistory                          h;
    const ViewHistory::Clock::time_point t0{};
    auto r1 = h.push("Zoom", one(ax, 0, 10), one(ax, 1, 9), "wheel", 600ms, t0 + 1s);
    auto r2 = h.push("Zoom", one(ax, 1, 9), one(ax, 2, 8), "wheel", 600ms, t0 + 1s + 100ms);
    auto r3 = h.push("Zoom", one(ax, 2, 8), one(ax, 3, 7), "wheel", 600ms, t0 + 1s + 600ms);
    EXPECT_FALSE(r1.coalesced);
    EXPECT_TRUE(r2.coalesced);
    EXPECT_TRUE(r3.coalesced);   // 500 ms after the previous step
    EXPECT_EQ(r1.entry, r3.entry);
    ASSERT_EQ(h.size(), 1u);
    EXPECT_DOUBLE_EQ(r1.entry->before.front().x.min, 0.0);
    EXPECT_DOUBLE_EQ(r1.entry->after.front().x.min, 3.0);

    // A pause longer than the window starts a new entry, as does a new key.
    auto r4 = h.push("Zoom", one(ax, 3, 7), one(ax, 4, 6), "wheel", 600ms, t0 + 3s);
    EXPECT_FALSE(r4.coalesced);
    auto r5 = h.push("Zoom", one(ax, 4, 6), one(ax, 4, 5), "other", 600ms, t0 + 3s);
    EXPECT_FALSE(r5.coalesced);
    EXPECT_EQ(h.size(), 3u);

    // No coalescing after stepping back: the burst is over.
    h.back();
    auto r6 = h.push("Zoom", one(ax, 4, 6), one(ax, 5, 6), "other", 600ms, t0 + 3s);
    EXPECT_FALSE(r6.coalesced);
}

TEST(ViewHistory, ZeroWindowCoalescesWhileTheKeyMatches)
{
    Axes*                                ax = reinterpret_cast<Axes*>(0x10);
    ViewHistory                          h;
    const ViewHistory::Clock::time_point t0{};
    h.push("Edit", one(ax, 0, 10), one(ax, 0, 11), "edit:1", {}, t0);
    auto r = h.push("Edit", one(ax, 0, 11), one(ax, 0, 12), "edit:1", {}, t0 + 1h);
    EXPECT_TRUE(r.coalesced);
    EXPECT_EQ(h.size(), 1u);
    EXPECT_DOUBLE_EQ(r.entry->after.front().x.max, 12.0);
}

TEST(ViewHistory, CapsTheNumberOfEntries)
{
    Axes*       ax = reinterpret_cast<Axes*>(0x10);
    ViewHistory h;
    for (int i = 0; i < 150; ++i)
        h.push("p", one(ax, i, i + 10.0), one(ax, i + 1.0, i + 11.0));
    EXPECT_EQ(h.size(), ViewHistory::MAX_ENTRIES);
    EXPECT_EQ(h.cursor(), ViewHistory::MAX_ENTRIES);
}

TEST(ViewHistory, UndoAndRedoKeepTheCursorInStep)
{
    Axes*       ax = reinterpret_cast<Axes*>(0x10);
    ViewHistory h;
    auto        a = h.push("a", one(ax, 0, 10), one(ax, 2, 8)).entry;
    auto        b = h.push("b", one(ax, 2, 8), one(ax, 3, 5)).entry;
    EXPECT_DOUBLE_EQ(h.undo_entry(b).front().x.min, 2.0);
    EXPECT_EQ(h.cursor(), 1u);
    EXPECT_DOUBLE_EQ(h.undo_entry(a).front().x.min, 0.0);
    EXPECT_EQ(h.cursor(), 0u);
    EXPECT_DOUBLE_EQ(h.redo_entry(a).front().x.min, 2.0);
    EXPECT_EQ(h.cursor(), 1u);
    // Not adjacent: state returned, cursor untouched.
    EXPECT_DOUBLE_EQ(h.undo_entry(b).front().x.min, 2.0);
    EXPECT_EQ(h.cursor(), 1u);
}

TEST(ViewHistory, ApplySkipsAxesNoLongerInTheFigure)
{
    Figure fig(FigureConfig{400, 300});
    auto&  ax = fig.subplot(1, 1, 1);
    ax.xlim(0.0, 1.0);
    Axes*     stale = reinterpret_cast<Axes*>(0x10);
    ViewState s     = one(&ax, 3, 4);
    s.push_back({stale, {0, 1}, {0, 1}});
    EXPECT_EQ(apply_view_state(fig, s), 1u);
    EXPECT_DOUBLE_EQ(ax.x_limits().min, 3.0);
}

TEST(ViewHistoryStore, OneHistoryPerFigureForgottenOnClose)
{
    ViewHistoryStore store;
    Figure           f1(FigureConfig{100, 100});
    Figure           f2(FigureConfig{100, 100});
    auto             h1 = store.history(&f1);
    EXPECT_EQ(store.history(&f1), h1);
    EXPECT_NE(store.history(&f2), h1);
    EXPECT_EQ(store.find(&f1), h1.get());
    std::weak_ptr<ViewHistory> weak = h1;
    h1.reset();
    store.forget(&f1);
    EXPECT_TRUE(weak.expired());
    EXPECT_EQ(store.find(&f1), nullptr);
}

// ─── Pure navigation helpers ────────────────────────────────────────────────

TEST(ViewNav, WheelModifiersPickTheAxes)
{
    EXPECT_EQ(wheel_zoom_axes(0), ViewAxes::Both);
    EXPECT_EQ(wheel_zoom_axes(kShift), ViewAxes::X);
    EXPECT_EQ(wheel_zoom_axes(kCtrl), ViewAxes::Y);
    EXPECT_EQ(wheel_zoom_axes(kAlt), ViewAxes::Y);
    EXPECT_EQ(wheel_zoom_axes(kShift | kCtrl), ViewAxes::Both);
}

TEST(ViewNav, GutterHitTesting)
{
    const Rect vp{100.0f, 50.0f, 400.0f, 300.0f};   // data area 100..500 × 50..350
    EXPECT_EQ(hit_test_axis_gutter(vp, 300, 200), AxisGutter::None);   // inside
    EXPECT_EQ(hit_test_axis_gutter(vp, 300, 360), AxisGutter::X);      // below
    EXPECT_EQ(hit_test_axis_gutter(vp, 300, 350 + AXIS_GUTTER_X_BAND), AxisGutter::X);
    EXPECT_EQ(hit_test_axis_gutter(vp, 300, 351 + AXIS_GUTTER_X_BAND), AxisGutter::None);
    EXPECT_EQ(hit_test_axis_gutter(vp, 60, 200), AxisGutter::Y);   // left
    EXPECT_EQ(hit_test_axis_gutter(vp, 100 - AXIS_GUTTER_Y_BAND - 1, 200), AxisGutter::None);
    EXPECT_EQ(hit_test_axis_gutter(vp, 60, 360), AxisGutter::None);    // corner
    EXPECT_EQ(hit_test_axis_gutter(vp, 300, 40), AxisGutter::None);    // title above
    EXPECT_EQ(hit_test_axis_gutter(vp, 510, 200), AxisGutter::None);   // right
    EXPECT_EQ(hit_test_axis_gutter(Rect{0, 0, 0, 0}, 0, 1), AxisGutter::None);
}

TEST(ViewNav, ThinBoxZoomKeepsOneAxis)
{
    EXPECT_EQ(classify_box_zoom(3, 2), BoxZoomKind::None);   // a click
    EXPECT_EQ(classify_box_zoom(5, -5), BoxZoomKind::None);
    EXPECT_EQ(classify_box_zoom(200, 150), BoxZoomKind::XY);
    EXPECT_EQ(classify_box_zoom(200, 3), BoxZoomKind::X);     // used to be dropped
    EXPECT_EQ(classify_box_zoom(-200, 10), BoxZoomKind::X);   // nearly horizontal
    EXPECT_EQ(classify_box_zoom(4, -120), BoxZoomKind::Y);
    EXPECT_EQ(classify_box_zoom(11, 120), BoxZoomKind::Y);
    EXPECT_EQ(classify_box_zoom(30, 11), BoxZoomKind::XY);   // short, not thin
    EXPECT_EQ(classify_box_zoom(30, 20), BoxZoomKind::XY);
}

TEST(ViewNav, ClampMinSpanKeepsARepresentableRange)
{
    const AxisLimits l = clamp_min_span({1e9, 1e9});
    EXPECT_GT(l.max, l.min);
    const AxisLimits ok = clamp_min_span({0.0, 1.0});
    EXPECT_DOUBLE_EQ(ok.min, 0.0);
    EXPECT_DOUBLE_EQ(ok.max, 1.0);
}

// ─── InputHandler ───────────────────────────────────────────────────────────

namespace
{
class ViewInputFixture : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        fig_     = std::make_unique<Figure>(FigureConfig{800, 600});
        auto& ax = fig_->subplot(1, 1, 1);
        ax.xlim(0.0, 10.0);
        ax.ylim(0.0, 10.0);
        fig_->compute_layout();
        handler_.set_figure(fig_.get());
        handler_.set_active_axes(&ax);
        handler_.set_undo_manager(&undo_);
        handler_.set_axis_link_manager(&links_);
        const auto& vp = ax.viewport();
        handler_.set_viewport(vp.x, vp.y, vp.w, vp.h);
    }

    Axes&        axes() { return *fig_->axes()[0]; }
    const Rect&  vp() { return axes().viewport(); }
    double       px(double fx) { return vp().x + vp().w * fx; }
    double       py(double fy) { return vp().y + vp().h * fy; }
    ViewHistory* history() { return handler_.view_history_store().find(fig_.get()); }

    void drag(int button, double x0, double y0, double x1, double y1, int mods = 0)
    {
        handler_.on_mouse_button(button, kPress, mods, x0, y0);
        handler_.on_mouse_move((x0 + x1) * 0.5, (y0 + y1) * 0.5);
        handler_.on_mouse_move(x1, y1);
        handler_.on_mouse_button(button, kRelease, mods, x1, y1);
    }

    std::unique_ptr<Figure> fig_;
    UndoManager             undo_;
    AxisLinkManager         links_;
    InputHandler            handler_;
};
}   // namespace

TEST_F(ViewInputFixture, ShiftWheelZoomsXOnly)
{
    handler_.on_scroll(0.0, 1.0, px(0.5), py(0.5), kShift);
    EXPECT_LT(axes().x_limits().max - axes().x_limits().min, 10.0);
    EXPECT_DOUBLE_EQ(axes().y_limits().min, 0.0);
    EXPECT_DOUBLE_EQ(axes().y_limits().max, 10.0);
}

TEST_F(ViewInputFixture, CtrlOrAltWheelZoomsYOnly)
{
    handler_.on_scroll(0.0, 1.0, px(0.5), py(0.5), kCtrl);
    EXPECT_DOUBLE_EQ(axes().x_limits().min, 0.0);
    EXPECT_DOUBLE_EQ(axes().x_limits().max, 10.0);
    const double span = axes().y_limits().max - axes().y_limits().min;
    EXPECT_LT(span, 10.0);
    handler_.on_scroll(0.0, 1.0, px(0.5), py(0.5), kAlt);
    EXPECT_LT(axes().y_limits().max - axes().y_limits().min, span);
    EXPECT_DOUBLE_EQ(axes().x_limits().max, 10.0);
}

TEST_F(ViewInputFixture, PlainWheelStillZoomsBothAboutTheCursor)
{
    handler_.on_scroll(0.0, 1.0, px(0.25), py(0.5));
    const auto x = axes().x_limits();
    const auto y = axes().y_limits();
    EXPECT_NEAR(x.max - x.min, 8.0, 1e-4);   // 1 / 1.25
    EXPECT_NEAR(y.max - y.min, 8.0, 1e-4);
    // Cursor at x=2.5 stays put: 2.5 - 0.25 * 8 = 0.5
    EXPECT_NEAR(x.min, 0.5, 1e-4);
}

TEST_F(ViewInputFixture, SingleAxisWheelZoomRespectsLogScale)
{
    axes().xscale(ScaleType::Log10);
    axes().xlim(1.0, 1000.0);
    // Cursor at the middle of the axis: 10^1.5.
    handler_.on_scroll(0.0, 1.0, px(0.5), py(0.5), kShift);
    const auto x = axes().x_limits();
    EXPECT_GT(x.min, 0.0);
    // Three decades zoomed by 1/1.25 in axis space, centred on 10^1.5.
    EXPECT_NEAR(std::log10(x.max) - std::log10(x.min), 2.4, 1e-4);
    EXPECT_NEAR((std::log10(x.max) + std::log10(x.min)) * 0.5, 1.5, 1e-4);
    EXPECT_DOUBLE_EQ(axes().y_limits().max, 10.0);
}

TEST_F(ViewInputFixture, HorizontalWheelPansX)
{
    // Positive x offset scrolls left: the view moves to smaller x.
    handler_.on_scroll(1.0, 0.0, px(0.5), py(0.5));
    EXPECT_NEAR(axes().x_limits().min, -1.0, 1e-9);
    EXPECT_NEAR(axes().x_limits().max, 9.0, 1e-9);
    EXPECT_DOUBLE_EQ(axes().y_limits().min, 0.0);

    // On a log axis it pans by ratios (never to zero or below).
    axes().xscale(ScaleType::Log10);
    axes().xlim(1.0, 100.0);
    handler_.on_scroll(5.0, 0.0, px(0.5), py(0.5));
    EXPECT_NEAR(std::log10(axes().x_limits().min), -1.0, 1e-9);
    EXPECT_NEAR(std::log10(axes().x_limits().max), 1.0, 1e-9);
}

TEST_F(ViewInputFixture, WheelOverGuttersZoomsThatAxisOnly)
{
    // X gutter: below the data area.
    handler_.on_scroll(0.0, 1.0, px(0.5), vp().y + vp().h + 15.0);
    EXPECT_LT(axes().x_limits().max - axes().x_limits().min, 10.0);
    EXPECT_DOUBLE_EQ(axes().y_limits().max, 10.0);

    // Y gutter: left of the data area.
    const double xspan = axes().x_limits().max - axes().x_limits().min;
    handler_.on_scroll(0.0, 1.0, vp().x - 20.0, py(0.5));
    EXPECT_LT(axes().y_limits().max - axes().y_limits().min, 10.0);
    EXPECT_DOUBLE_EQ(axes().x_limits().max - axes().x_limits().min, xspan);
}

TEST_F(ViewInputFixture, DragInGutterPansThatAxisOnly)
{
    const double gy = vp().y + vp().h + 15.0;
    drag(kLeft, px(0.5), gy, px(0.6), gy + 30.0);   // vertical motion ignored
    EXPECT_NEAR(axes().x_limits().min, -1.0, 1e-6);
    EXPECT_NEAR(axes().x_limits().max, 9.0, 1e-6);
    EXPECT_DOUBLE_EQ(axes().y_limits().min, 0.0);
    EXPECT_EQ(handler_.gutter_drag_axis(), AxisGutter::None);
    ASSERT_NE(history(), nullptr);
    EXPECT_EQ(history()->back_label(), "Pan X");

    const double gx = vp().x - 20.0;
    // Dragging up by 10% pulls the content up (like a plot drag).
    drag(kMiddle, gx, py(0.5), gx + 40.0, py(0.4));
    EXPECT_NEAR(axes().y_limits().min, -1.0, 1e-6);
    EXPECT_NEAR(axes().x_limits().min, -1.0, 1e-6);
    EXPECT_EQ(history()->back_label(), "Pan Y");
}

TEST_F(ViewInputFixture, EscCancelsAGutterDrag)
{
    const double gy = vp().y + vp().h + 15.0;
    handler_.on_mouse_button(kLeft, kPress, 0, px(0.5), gy);
    handler_.on_mouse_move(px(0.8), gy);
    EXPECT_TRUE(handler_.has_active_gesture());
    handler_.on_key(kKeyEsc, kPress, 0);
    EXPECT_EQ(handler_.gutter_drag_axis(), AxisGutter::None);
    EXPECT_DOUBLE_EQ(axes().x_limits().min, 0.0);
    handler_.on_mouse_button(kLeft, kRelease, 0, px(0.8), gy);
    EXPECT_EQ(history() ? history()->size() : 0u, 0u);
}

TEST_F(ViewInputFixture, ThinBoxZoomZoomsOneAxis)
{
    handler_.set_tool_mode(ToolMode::BoxZoom);
    drag(kLeft, px(0.2), py(0.5), px(0.6), py(0.5) + 2.0);
    EXPECT_NEAR(axes().x_limits().min, 2.0, 1e-3);
    EXPECT_NEAR(axes().x_limits().max, 6.0, 1e-3);
    EXPECT_DOUBLE_EQ(axes().y_limits().min, 0.0);
    EXPECT_DOUBLE_EQ(axes().y_limits().max, 10.0);
    EXPECT_EQ(history()->back_label(), "Zoom X");

    drag(kLeft, px(0.5), py(0.1), px(0.5) + 3.0, py(0.6));   // vertical band
    EXPECT_NEAR(axes().y_limits().min, 4.0, 1e-3);
    EXPECT_NEAR(axes().y_limits().max, 9.0, 1e-3);
    EXPECT_NEAR(axes().x_limits().min, 2.0, 1e-3);
    EXPECT_EQ(history()->back_label(), "Zoom Y");
}

TEST_F(ViewInputFixture, ThinBoxZoomOnLogAxisStaysPositive)
{
    axes().yscale(ScaleType::Log10);
    axes().ylim(1.0, 1e4);
    handler_.set_tool_mode(ToolMode::BoxZoom);
    drag(kLeft, px(0.5), py(0.25), px(0.5) + 2.0, py(0.75));
    const auto y = axes().y_limits();
    EXPECT_NEAR(std::log10(y.min), 1.0, 1e-3);
    EXPECT_NEAR(std::log10(y.max), 3.0, 1e-3);
}

TEST_F(ViewInputFixture, WheelBurstIsOneEntryAndBackForwardRestore)
{
    for (int i = 0; i < 5; ++i)
        handler_.on_scroll(0.0, 1.0, px(0.5), py(0.5));
    const auto zoomed = axes().x_limits();
    ASSERT_NE(history(), nullptr);
    EXPECT_EQ(history()->size(), 1u);
    EXPECT_EQ(undo_.undo_count(), 1u);

    EXPECT_TRUE(handler_.can_view_back());
    EXPECT_TRUE(handler_.view_back());
    EXPECT_DOUBLE_EQ(axes().x_limits().min, 0.0);
    EXPECT_DOUBLE_EQ(axes().x_limits().max, 10.0);
    EXPECT_FALSE(handler_.view_back());

    EXPECT_TRUE(handler_.view_forward());
    EXPECT_DOUBLE_EQ(axes().x_limits().min, zoomed.min);
    EXPECT_DOUBLE_EQ(axes().x_limits().max, zoomed.max);
    EXPECT_FALSE(handler_.view_forward());
    // Navigation is not an edit.
    EXPECT_EQ(undo_.undo_count(), 1u);
}

TEST_F(ViewInputFixture, ShiftAndCtrlBurstsAreSeparateEntries)
{
    handler_.on_scroll(0.0, 1.0, px(0.5), py(0.5), kShift);
    handler_.on_scroll(0.0, 1.0, px(0.5), py(0.5), kShift);
    handler_.on_scroll(0.0, 1.0, px(0.5), py(0.5), kCtrl);
    EXPECT_EQ(history()->size(), 2u);
    EXPECT_EQ(history()->back_label(), "Zoom Y");
}

TEST_F(ViewInputFixture, PanDragIsOneEntryUndoneByCtrlZ)
{
    handler_.set_tool_mode(ToolMode::Pan);
    drag(kLeft, px(0.5), py(0.5), px(0.6), py(0.5));
    EXPECT_NEAR(axes().x_limits().min, -1.0, 1e-6);
    ASSERT_NE(history(), nullptr);
    EXPECT_EQ(history()->size(), 1u);
    EXPECT_EQ(history()->back_label(), "Pan");

    ASSERT_TRUE(undo_.undo());
    EXPECT_NEAR(axes().x_limits().min, 0.0, 1e-9);
    EXPECT_FALSE(handler_.can_view_back());   // the cursor moved with the undo
    ASSERT_TRUE(undo_.redo());
    EXPECT_NEAR(axes().x_limits().min, -1.0, 1e-6);
    EXPECT_TRUE(handler_.can_view_back());
}

TEST_F(ViewInputFixture, ClickWithoutDragRecordsNothing)
{
    handler_.set_tool_mode(ToolMode::Pan);
    drag(kLeft, px(0.5), py(0.5), px(0.5) + 1.0, py(0.5));
    EXPECT_EQ(history() ? history()->size() : 0u, 0u);
    EXPECT_EQ(undo_.undo_count(), 0u);
}

TEST_F(ViewInputFixture, MiddlePanAndRightDragZoomAreRecorded)
{
    drag(kMiddle, px(0.5), py(0.5), px(0.6), py(0.5));
    drag(1, px(0.5), py(0.5), px(0.7), py(0.5));
    ASSERT_NE(history(), nullptr);
    EXPECT_EQ(history()->size(), 2u);
    EXPECT_EQ(history()->back_label(), "Zoom");
    handler_.view_back();
    handler_.view_back();
    EXPECT_NEAR(axes().x_limits().min, 0.0, 1e-9);
    EXPECT_NEAR(axes().x_limits().max, 10.0, 1e-9);
}

TEST_F(ViewInputFixture, MouseButtons4And5StepBackAndForward)
{
    handler_.on_scroll(0.0, 1.0, px(0.5), py(0.5));
    const auto zoomed = axes().x_limits();
    handler_.on_mouse_button(kBack, kPress, 0, px(0.5), py(0.5));
    handler_.on_mouse_button(kBack, kRelease, 0, px(0.5), py(0.5));
    EXPECT_DOUBLE_EQ(axes().x_limits().max, 10.0);
    handler_.on_mouse_button(kForward, kPress, 0, px(0.5), py(0.5));
    EXPECT_DOUBLE_EQ(axes().x_limits().max, zoomed.max);
}

TEST_F(ViewInputFixture, LinkedAxesGoBackTogether)
{
    fig_      = std::make_unique<Figure>(FigureConfig{800, 600});
    auto& top = fig_->subplot(2, 1, 1);
    auto& bot = fig_->subplot(2, 1, 2);
    top.xlim(0.0, 10.0);
    top.ylim(0.0, 10.0);
    bot.xlim(0.0, 10.0);
    bot.ylim(-1.0, 1.0);
    fig_->compute_layout();
    handler_.set_figure(fig_.get());
    links_.link(&top, &bot, LinkAxis::X);

    const Rect& v = top.viewport();
    handler_.on_scroll(0.0, 2.0, v.x + v.w * 0.5, v.y + v.h * 0.5, kShift);
    EXPECT_DOUBLE_EQ(bot.x_limits().min, top.x_limits().min);
    EXPECT_LT(bot.x_limits().max, 10.0);
    EXPECT_DOUBLE_EQ(bot.y_limits().min, -1.0);

    ASSERT_TRUE(handler_.view_back());
    EXPECT_DOUBLE_EQ(top.x_limits().max, 10.0);
    EXPECT_DOUBLE_EQ(bot.x_limits().max, 10.0);
}

TEST_F(ViewInputFixture, InspectorEditsCoalescePerEditAndMoveLinkedAxes)
{
    fig_      = std::make_unique<Figure>(FigureConfig{800, 600});
    auto& top = fig_->subplot(2, 1, 1);
    auto& bot = fig_->subplot(2, 1, 2);
    top.xlim(0.0, 10.0);
    bot.xlim(0.0, 10.0);
    fig_->compute_layout();
    handler_.set_figure(fig_.get());
    links_.link(&top, &bot, LinkAxis::X);

    // One drag of the range field: three changes, one key.
    for (double hi : {11.0, 12.0, 13.0})
    {
        const auto old_x = top.x_limits();
        const auto old_y = top.y_limits();
        top.xlim(0.0, hi);
        handler_.commit_axes_limits_edit(fig_.get(), &top, old_x, old_y, "inspector-xlim:1");
    }
    EXPECT_DOUBLE_EQ(bot.x_limits().max, 13.0);
    ASSERT_NE(history(), nullptr);
    EXPECT_EQ(history()->size(), 1u);
    EXPECT_EQ(undo_.undo_count(), 1u);

    ASSERT_TRUE(undo_.undo());
    EXPECT_DOUBLE_EQ(top.x_limits().max, 10.0);
    EXPECT_DOUBLE_EQ(bot.x_limits().max, 10.0);
}

TEST_F(ViewInputFixture, ClosingTheFigureDropsItsHistory)
{
    handler_.on_scroll(0.0, 1.0, px(0.5), py(0.5));
    ASSERT_EQ(undo_.undo_count(), 1u);
    handler_.clear_figure_cache(fig_.get());
    EXPECT_EQ(handler_.view_history_store().find(fig_.get()), nullptr);
    const auto x = axes().x_limits();
    // The undo step can no longer reach the figure: a harmless no-op.
    EXPECT_TRUE(undo_.undo());
    EXPECT_DOUBLE_EQ(axes().x_limits().min, x.min);
}

// ─── Full window UI context: commands and shortcuts ─────────────────────────

#ifdef SPECTRA_USE_IMGUI

namespace
{

class NullBackend : public Backend
{
   public:
    bool           init(bool) override { return true; }
    void           shutdown() override {}
    void           wait_idle() override {}
    bool           create_surface(void*) override { return true; }
    bool           create_swapchain(uint32_t, uint32_t) override { return true; }
    bool           recreate_swapchain(uint32_t, uint32_t) override { return true; }
    bool           create_offscreen_framebuffer(uint32_t, uint32_t) override { return true; }
    PipelineHandle create_pipeline(PipelineType) override { return PipelineHandle{1}; }
    BufferHandle   create_buffer(BufferUsage, size_t) override { return BufferHandle{1}; }
    void           destroy_buffer(BufferHandle) override {}
    void           upload_buffer(BufferHandle, const void*, size_t, size_t) override {}
    TextureHandle  create_texture(uint32_t, uint32_t, const uint8_t*) override
    {
        return TextureHandle{1};
    }
    void     destroy_texture(TextureHandle) override {}
    bool     begin_frame(FrameProfiler*) override { return true; }
    void     end_frame(FrameProfiler*) override {}
    void     begin_render_pass(const Color&) override {}
    void     end_render_pass() override {}
    void     bind_pipeline(PipelineHandle) override {}
    void     bind_buffer(BufferHandle, uint32_t) override {}
    void     bind_index_buffer(BufferHandle) override {}
    void     bind_texture(TextureHandle, uint32_t) override {}
    void     push_constants(const SeriesPushConstants&) override {}
    void     set_viewport(float, float, float, float) override {}
    void     set_scissor(int32_t, int32_t, uint32_t, uint32_t) override {}
    void     set_line_width(float) override {}
    void     draw(uint32_t, uint32_t) override {}
    void     draw_instanced(uint32_t, uint32_t, uint32_t, uint32_t) override {}
    void     draw_indexed(uint32_t, uint32_t, int32_t) override {}
    bool     readback_framebuffer(uint8_t*, uint32_t, uint32_t) override { return false; }
    uint32_t swapchain_width() const override { return 0; }
    uint32_t swapchain_height() const override { return 0; }
};

class ViewUiFixture : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        ui::ThemeManager::set_current(&theme_mgr_);
        theme_mgr_.ensure_initialized();
        renderer_ = std::make_unique<Renderer>(backend_, theme_mgr_);
        session_  = std::make_unique<SessionRuntime>(backend_, *renderer_, registry_);

        auto fig = std::make_unique<Figure>(FigureConfig{.width = 800, .height = 600});
        fig->subplot(1, 1, 1);
        fig->compute_layout();
        figure_     = fig.get();
        initial_id_ = registry_.register_figure(std::move(fig));

        WindowUIContextBuildOptions options;
        options.registry                 = &registry_;
        options.theme_mgr                = &theme_mgr_;
        options.initial_figure_id        = initial_id_;
        options.active_figure            = &active_figure_;
        options.active_figure_id         = &active_figure_id_;
        options.session                  = session_.get();
        options.create_imgui_integration = true;
        ui_                              = build_window_ui_context(options);
        ASSERT_NE(ui_, nullptr);
        active_figure_ = figure_;

        ui_->input_handler.set_figure(figure_);
        ui_->input_handler.set_active_axes(ax());
        ax()->xlim(0.0, 10.0);
        ax()->ylim(0.0, 10.0);
    }

    void TearDown() override
    {
        ui_.reset();
        session_.reset();
        renderer_.reset();
        ui::ThemeManager::set_current(nullptr);
    }

    Axes* ax() { return figure_->axes()[0].get(); }
    bool  press(int key, int mods) { return ui_->shortcut_mgr.on_key(key, kPress, mods); }

    NullBackend                      backend_;
    ui::ThemeManager                 theme_mgr_;
    FigureRegistry                   registry_;
    std::unique_ptr<Renderer>        renderer_;
    std::unique_ptr<SessionRuntime>  session_;
    std::unique_ptr<WindowUIContext> ui_;
    Figure*                          figure_           = nullptr;
    Figure*                          active_figure_    = nullptr;
    FigureId                         active_figure_id_ = INVALID_FIGURE_ID;
    FigureId                         initial_id_       = INVALID_FIGURE_ID;
};

constexpr int kKeyLeft  = 263;
constexpr int kKeyRight = 262;
constexpr int kKeyZ     = 90;

}   // namespace

TEST_F(ViewUiFixture, AltArrowsWalkTheViewHistory)
{
    ASSERT_TRUE(ui_->cmd_registry.execute("view.pan_right"));
    ASSERT_TRUE(ui_->cmd_registry.execute("view.zoom_in"));
    const auto zoomed = ax()->x_limits();

    ASSERT_TRUE(press(kKeyLeft, kAlt));
    EXPECT_NEAR(ax()->x_limits().min, 1.0, 1e-9);
    ASSERT_TRUE(press(kKeyLeft, kAlt));
    EXPECT_NEAR(ax()->x_limits().min, 0.0, 1e-9);
    ASSERT_TRUE(press(kKeyRight, kAlt));
    ASSERT_TRUE(press(kKeyRight, kAlt));
    EXPECT_DOUBLE_EQ(ax()->x_limits().min, zoomed.min);

    // Plain arrows still pan.
    ASSERT_TRUE(press(kKeyLeft, 0));
    EXPECT_LT(ax()->x_limits().min, zoomed.min);
}

TEST_F(ViewUiFixture, PaletteCommandsAndCtrlZUndoViewChangesInOrder)
{
    ASSERT_NE(ui_->cmd_registry.find("view.back"), nullptr);
    ASSERT_NE(ui_->cmd_registry.find("view.forward"), nullptr);

    auto&       in = ui_->input_handler;
    const auto& v  = ax()->viewport();
    in.on_scroll(0.0, 1.0, v.x + v.w * 0.5, v.y + v.h * 0.5, kShift);   // zoom X
    ASSERT_TRUE(ui_->cmd_registry.execute("view.pan_up"));              // pan Y
    const auto after_x = ax()->x_limits();

    ASSERT_TRUE(ui_->cmd_registry.execute("view.back"));
    EXPECT_DOUBLE_EQ(ax()->y_limits().min, 0.0);
    ASSERT_TRUE(ui_->cmd_registry.execute("view.forward"));
    EXPECT_NEAR(ax()->y_limits().min, 1.0, 1e-9);

    // Ctrl+Z undoes the pan, then the zoom.
    ASSERT_TRUE(press(kKeyZ, kCtrl));
    EXPECT_DOUBLE_EQ(ax()->y_limits().min, 0.0);
    EXPECT_DOUBLE_EQ(ax()->x_limits().max, after_x.max);
    ASSERT_TRUE(press(kKeyZ, kCtrl));
    EXPECT_DOUBLE_EQ(ax()->x_limits().max, 10.0);
}

TEST_F(ViewUiFixture, ResetAndAutofitAreRecorded)
{
    ax()->xlim(3.0, 4.0);
    ASSERT_TRUE(ui_->cmd_registry.execute("view.autofit"));
    EXPECT_NE(ax()->x_limits().min, 3.0);
    ASSERT_TRUE(ui_->cmd_registry.execute("view.back"));
    EXPECT_DOUBLE_EQ(ax()->x_limits().min, 3.0);
    EXPECT_DOUBLE_EQ(ax()->x_limits().max, 4.0);

    // Reset view animates; Back cancels the animation and restores.
    ASSERT_TRUE(ui_->cmd_registry.execute("view.reset"));
    ASSERT_TRUE(ui_->cmd_registry.execute("view.back"));
    ui_->anim_controller.update(1.0f);
    EXPECT_DOUBLE_EQ(ax()->x_limits().min, 3.0);
    ASSERT_TRUE(ui_->cmd_registry.execute("view.forward"));
    ui_->anim_controller.update(1.0f);
    EXPECT_NE(ax()->x_limits().min, 3.0);
}

#endif   // SPECTRA_USE_IMGUI
