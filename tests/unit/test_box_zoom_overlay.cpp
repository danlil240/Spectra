#include <gtest/gtest.h>

#include "ui/animation_controller.hpp"
#include "ui/gesture_recognizer.hpp"
#include "ui/input.hpp"
#include "ui/transition_engine.hpp"

using namespace spectra;

// ═══════════════════════════════════════════════════════════════════════════
// Test fixture: sets up a Figure + InputHandler with known viewport/limits
// ═══════════════════════════════════════════════════════════════════════════

class BoxZoomTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        fig_ = std::make_unique<Figure>(FigureConfig{800, 600});
        auto& ax = fig_->subplot(1, 1, 1);
        ax.xlim(0.0f, 10.0f);
        ax.ylim(0.0f, 10.0f);
        fig_->compute_layout();

        handler_.set_figure(fig_.get());
        handler_.set_active_axes(&ax);
        auto& vp = ax.viewport();
        handler_.set_viewport(vp.x, vp.y, vp.w, vp.h);
    }

    Axes& axes() { return *fig_->axes()[0]; }

    std::unique_ptr<Figure> fig_;
    InputHandler handler_;
};

// ═══════════════════════════════════════════════════════════════════════════
// BoxZoomRect state tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(BoxZoomTest, BoxZoomRectInitiallyInactive)
{
    const auto& bz = handler_.box_zoom_rect();
    EXPECT_FALSE(bz.active);
}

TEST_F(BoxZoomTest, BoxZoomRectActivatesOnRightClickInBoxZoomMode)
{
    handler_.set_tool_mode(ToolMode::BoxZoom);
    auto& vp = axes().viewport();
    double x0 = vp.x + vp.w * 0.25;
    double y0 = vp.y + vp.h * 0.25;

    handler_.on_mouse_button(0, 1, 0, x0, y0);  // left press in BoxZoom mode
    EXPECT_TRUE(handler_.box_zoom_rect().active);
    EXPECT_EQ(handler_.mode(), InteractionMode::Dragging);
}

TEST_F(BoxZoomTest, BoxZoomRectUpdatesOnMouseMove)
{
    handler_.set_tool_mode(ToolMode::BoxZoom);
    auto& vp = axes().viewport();
    double x0 = vp.x + vp.w * 0.25;
    double y0 = vp.y + vp.h * 0.25;
    double x1 = vp.x + vp.w * 0.75;
    double y1 = vp.y + vp.h * 0.75;

    handler_.on_mouse_button(0, 1, 0, x0, y0);
    handler_.on_mouse_move(x1, y1);

    const auto& bz = handler_.box_zoom_rect();
    EXPECT_DOUBLE_EQ(bz.x0, x0);
    EXPECT_DOUBLE_EQ(bz.y0, y0);
    EXPECT_DOUBLE_EQ(bz.x1, x1);
    EXPECT_DOUBLE_EQ(bz.y1, y1);
}

TEST_F(BoxZoomTest, BoxZoomRectDeactivatesOnRelease)
{
    handler_.set_tool_mode(ToolMode::BoxZoom);
    auto& vp = axes().viewport();
    double x0 = vp.x + vp.w * 0.25;
    double y0 = vp.y + vp.h * 0.25;
    double x1 = vp.x + vp.w * 0.75;
    double y1 = vp.y + vp.h * 0.75;

    handler_.on_mouse_button(0, 1, 0, x0, y0);
    handler_.on_mouse_move(x1, y1);
    handler_.on_mouse_button(0, 0, 0, x1, y1);

    EXPECT_FALSE(handler_.box_zoom_rect().active);
    EXPECT_EQ(handler_.mode(), InteractionMode::Idle);
}

// ═══════════════════════════════════════════════════════════════════════════
// Box zoom applies correct limits
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(BoxZoomTest, BoxZoomSetsLimitsCorrectly)
{
    handler_.set_tool_mode(ToolMode::BoxZoom);
    auto& vp = axes().viewport();

    double x0 = vp.x + vp.w * 0.25;
    double y0 = vp.y + vp.h * 0.25;
    double x1 = vp.x + vp.w * 0.75;
    double y1 = vp.y + vp.h * 0.75;

    handler_.on_mouse_button(0, 1, 0, x0, y0);
    handler_.on_mouse_move(x1, y1);
    handler_.on_mouse_button(0, 0, 0, x1, y1);

    // Without animation controller, limits are set instantly
    auto xlim = axes().x_limits();
    auto ylim = axes().y_limits();
    EXPECT_NEAR(xlim.min, 2.5f, 0.5f);
    EXPECT_NEAR(xlim.max, 7.5f, 0.5f);
    EXPECT_NEAR(ylim.min, 2.5f, 0.5f);
    EXPECT_NEAR(ylim.max, 7.5f, 0.5f);
}

TEST_F(BoxZoomTest, BoxZoomTooSmallIgnored)
{
    handler_.set_tool_mode(ToolMode::BoxZoom);
    auto& vp = axes().viewport();
    double x0 = vp.x + vp.w * 0.5;
    double y0 = vp.y + vp.h * 0.5;

    handler_.on_mouse_button(0, 1, 0, x0, y0);
    handler_.on_mouse_move(x0 + 2.0, y0 + 2.0);  // < 5px threshold
    handler_.on_mouse_button(0, 0, 0, x0 + 2.0, y0 + 2.0);

    auto xlim = axes().x_limits();
    EXPECT_NEAR(xlim.min, 0.0f, 0.01f);
    EXPECT_NEAR(xlim.max, 10.0f, 0.01f);
}

TEST_F(BoxZoomTest, BoxZoomCancelledByEscape)
{
    handler_.set_tool_mode(ToolMode::BoxZoom);
    auto& vp = axes().viewport();
    double x0 = vp.x + vp.w * 0.25;
    double y0 = vp.y + vp.h * 0.25;

    handler_.on_mouse_button(0, 1, 0, x0, y0);
    EXPECT_EQ(handler_.mode(), InteractionMode::Dragging);

    handler_.on_key(256, 1, 0);  // KEY_ESCAPE
    EXPECT_EQ(handler_.mode(), InteractionMode::Idle);
    EXPECT_FALSE(handler_.box_zoom_rect().active);

    auto xlim = axes().x_limits();
    EXPECT_NEAR(xlim.min, 0.0f, 0.01f);
    EXPECT_NEAR(xlim.max, 10.0f, 0.01f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Ctrl+left-drag box zoom in Pan mode
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(BoxZoomTest, CtrlLeftDragStartsBoxZoomInPanMode)
{
    handler_.set_tool_mode(ToolMode::Pan);
    auto& vp = axes().viewport();
    double x0 = vp.x + vp.w * 0.25;
    double y0 = vp.y + vp.h * 0.25;

    // Pass Ctrl modifier directly via mods parameter
    handler_.on_mouse_button(0, 1, 0x0002, x0, y0);  // left press with Ctrl
    EXPECT_TRUE(handler_.box_zoom_rect().active);
    EXPECT_EQ(handler_.mode(), InteractionMode::Dragging);
}

TEST_F(BoxZoomTest, CtrlLeftDragUpdatesBoxZoomRect)
{
    handler_.set_tool_mode(ToolMode::Pan);
    auto& vp = axes().viewport();
    double x0 = vp.x + vp.w * 0.2;
    double y0 = vp.y + vp.h * 0.2;
    double x1 = vp.x + vp.w * 0.8;
    double y1 = vp.y + vp.h * 0.8;

    handler_.on_mouse_button(0, 1, 0x0002, x0, y0);
    handler_.on_mouse_move(x1, y1);

    const auto& bz = handler_.box_zoom_rect();
    EXPECT_DOUBLE_EQ(bz.x1, x1);
    EXPECT_DOUBLE_EQ(bz.y1, y1);
}

TEST_F(BoxZoomTest, CtrlLeftDragAppliesBoxZoomOnRelease)
{
    handler_.set_tool_mode(ToolMode::Pan);
    auto& vp = axes().viewport();
    double x0 = vp.x + vp.w * 0.25;
    double y0 = vp.y + vp.h * 0.25;
    double x1 = vp.x + vp.w * 0.75;
    double y1 = vp.y + vp.h * 0.75;

    handler_.on_mouse_button(0, 1, 0x0002, x0, y0);
    handler_.on_mouse_move(x1, y1);
    handler_.on_mouse_button(0, 0, 0, x1, y1);

    EXPECT_FALSE(handler_.box_zoom_rect().active);
    EXPECT_EQ(handler_.mode(), InteractionMode::Idle);

    auto xlim = axes().x_limits();
    auto ylim = axes().y_limits();
    EXPECT_NEAR(xlim.min, 2.5f, 0.5f);
    EXPECT_NEAR(xlim.max, 7.5f, 0.5f);
    EXPECT_NEAR(ylim.min, 2.5f, 0.5f);
    EXPECT_NEAR(ylim.max, 7.5f, 0.5f);
}

TEST_F(BoxZoomTest, CtrlLeftDragCancelledByEscape)
{
    handler_.set_tool_mode(ToolMode::Pan);
    auto& vp = axes().viewport();
    double x0 = vp.x + vp.w * 0.25;
    double y0 = vp.y + vp.h * 0.25;

    handler_.on_mouse_button(0, 1, 0x0002, x0, y0);
    EXPECT_TRUE(handler_.box_zoom_rect().active);

    handler_.on_key(256, 1, 0);  // Escape
    EXPECT_FALSE(handler_.box_zoom_rect().active);
    EXPECT_EQ(handler_.mode(), InteractionMode::Idle);

    auto xlim = axes().x_limits();
    EXPECT_NEAR(xlim.min, 0.0f, 0.01f);
    EXPECT_NEAR(xlim.max, 10.0f, 0.01f);
}

TEST_F(BoxZoomTest, NormalLeftDragStillPansWithoutCtrl)
{
    handler_.set_tool_mode(ToolMode::Pan);
    auto& vp = axes().viewport();
    double cx = vp.x + vp.w * 0.5;
    double cy = vp.y + vp.h * 0.5;

    // No Ctrl key — should pan, not box zoom
    handler_.on_mouse_button(0, 1, 0, cx, cy);
    EXPECT_FALSE(handler_.box_zoom_rect().active);
    EXPECT_EQ(handler_.mode(), InteractionMode::Dragging);

    handler_.on_mouse_move(cx + vp.w * 0.1, cy);
    handler_.on_mouse_button(0, 0, 0, cx + vp.w * 0.1, cy);

    // X limits should have shifted (panned)
    auto xlim = axes().x_limits();
    EXPECT_LT(xlim.min, 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Animated box zoom with AnimationController
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(BoxZoomTest, AnimatedBoxZoomWithAnimController)
{
    AnimationController anim_ctrl;
    handler_.set_animation_controller(&anim_ctrl);
    handler_.set_tool_mode(ToolMode::BoxZoom);

    auto& vp = axes().viewport();
    double x0 = vp.x + vp.w * 0.25;
    double y0 = vp.y + vp.h * 0.25;
    double x1 = vp.x + vp.w * 0.75;
    double y1 = vp.y + vp.h * 0.75;

    handler_.on_mouse_button(0, 1, 0, x0, y0);
    handler_.on_mouse_move(x1, y1);
    handler_.on_mouse_button(0, 0, 0, x1, y1);

    // Animation should be active
    EXPECT_TRUE(anim_ctrl.has_active_animations());

    // Step past animation duration
    anim_ctrl.update(0.5f);
    EXPECT_FALSE(anim_ctrl.has_active_animations());

    auto xlim = axes().x_limits();
    EXPECT_NEAR(xlim.min, 2.5f, 0.5f);
    EXPECT_NEAR(xlim.max, 7.5f, 0.5f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Animated box zoom with TransitionEngine (preferred)
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(BoxZoomTest, TransitionEnginePreferredOverAnimController)
{
    AnimationController anim_ctrl;
    TransitionEngine trans_engine;
    handler_.set_animation_controller(&anim_ctrl);
    handler_.set_transition_engine(&trans_engine);
    handler_.set_tool_mode(ToolMode::BoxZoom);

    auto& vp = axes().viewport();
    double x0 = vp.x + vp.w * 0.25;
    double y0 = vp.y + vp.h * 0.25;
    double x1 = vp.x + vp.w * 0.75;
    double y1 = vp.y + vp.h * 0.75;

    handler_.on_mouse_button(0, 1, 0, x0, y0);
    handler_.on_mouse_move(x1, y1);
    handler_.on_mouse_button(0, 0, 0, x1, y1);

    // TransitionEngine should have the animation, not AnimationController
    EXPECT_TRUE(trans_engine.has_active_animations());
    EXPECT_FALSE(anim_ctrl.has_active_animations());

    trans_engine.update(0.5f);
    EXPECT_FALSE(trans_engine.has_active_animations());

    auto xlim = axes().x_limits();
    EXPECT_NEAR(xlim.min, 2.5f, 0.5f);
    EXPECT_NEAR(xlim.max, 7.5f, 0.5f);
}

TEST_F(BoxZoomTest, TransitionEngineUsedForCtrlDragBoxZoom)
{
    TransitionEngine trans_engine;
    handler_.set_transition_engine(&trans_engine);
    handler_.set_tool_mode(ToolMode::Pan);

    auto& vp = axes().viewport();
    double x0 = vp.x + vp.w * 0.25;
    double y0 = vp.y + vp.h * 0.25;
    double x1 = vp.x + vp.w * 0.75;
    double y1 = vp.y + vp.h * 0.75;

    handler_.on_mouse_button(0, 1, 0x0002, x0, y0);  // Ctrl
    handler_.on_mouse_move(x1, y1);
    handler_.on_mouse_button(0, 0, 0, x1, y1);

    EXPECT_TRUE(trans_engine.has_active_animations());
    trans_engine.update(0.5f);

    auto xlim = axes().x_limits();
    EXPECT_NEAR(xlim.min, 2.5f, 0.5f);
    EXPECT_NEAR(xlim.max, 7.5f, 0.5f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Double-click auto-fit
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(BoxZoomTest, DoubleClickAutoFitWithAnimController)
{
    AnimationController anim_ctrl;
    GestureRecognizer gesture;
    handler_.set_animation_controller(&anim_ctrl);
    handler_.set_gesture_recognizer(&gesture);
    handler_.set_tool_mode(ToolMode::Pan);

    // Zoom in first
    axes().xlim(3.0f, 7.0f);
    axes().ylim(3.0f, 7.0f);

    auto& vp = axes().viewport();
    double cx = vp.x + vp.w * 0.5;
    double cy = vp.y + vp.h * 0.5;

    // First click
    handler_.on_mouse_button(0, 1, 0, cx, cy);
    handler_.on_mouse_button(0, 0, 0, cx, cy);

    // Second click (double-click) — immediately after
    handler_.on_mouse_button(0, 1, 0, cx, cy);

    // Animation should be active (auto-fit)
    EXPECT_TRUE(anim_ctrl.has_active_animations());

    // Complete animation
    anim_ctrl.update(0.5f);

    // Limits should have changed from zoomed state
    auto xlim = axes().x_limits();
    EXPECT_NE(xlim.min, 3.0f);
    EXPECT_NE(xlim.max, 7.0f);
}

TEST_F(BoxZoomTest, DoubleClickAutoFitWithTransitionEngine)
{
    TransitionEngine trans_engine;
    GestureRecognizer gesture;
    handler_.set_transition_engine(&trans_engine);
    handler_.set_gesture_recognizer(&gesture);
    handler_.set_tool_mode(ToolMode::Pan);

    // Zoom in first
    axes().xlim(3.0f, 7.0f);
    axes().ylim(3.0f, 7.0f);

    auto& vp = axes().viewport();
    double cx = vp.x + vp.w * 0.5;
    double cy = vp.y + vp.h * 0.5;

    // First click
    handler_.on_mouse_button(0, 1, 0, cx, cy);
    handler_.on_mouse_button(0, 0, 0, cx, cy);

    // Second click (double-click)
    handler_.on_mouse_button(0, 1, 0, cx, cy);

    // TransitionEngine should have the animation
    EXPECT_TRUE(trans_engine.has_active_animations());

    trans_engine.update(0.5f);

    auto xlim = axes().x_limits();
    EXPECT_NE(xlim.min, 3.0f);
    EXPECT_NE(xlim.max, 7.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// TransitionEngine integration for scroll zoom cancel
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(BoxZoomTest, ScrollCancelsTransitionEngineAnimations)
{
    TransitionEngine trans_engine;
    handler_.set_transition_engine(&trans_engine);

    auto& vp = axes().viewport();
    double cx = vp.x + vp.w * 0.5;
    double cy = vp.y + vp.h * 0.5;

    // Start a limit animation
    trans_engine.animate_limits(axes(), {2, 8}, {2, 8}, 1.0f);
    EXPECT_TRUE(trans_engine.has_active_animations());

    // Scroll should cancel it
    handler_.on_scroll(0.0, 1.0, cx, cy);
    trans_engine.update(0.01f);  // GC
    EXPECT_FALSE(trans_engine.has_active_animations());
}

// ═══════════════════════════════════════════════════════════════════════════
// has_active_animations with TransitionEngine
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(BoxZoomTest, HasActiveAnimationsChecksTransitionEngine)
{
    TransitionEngine trans_engine;
    handler_.set_transition_engine(&trans_engine);

    EXPECT_FALSE(handler_.has_active_animations());

    trans_engine.animate_limits(axes(), {2, 8}, {2, 8}, 1.0f);
    EXPECT_TRUE(handler_.has_active_animations());

    trans_engine.cancel_all();
    trans_engine.update(0.01f);
    EXPECT_FALSE(handler_.has_active_animations());
}

TEST_F(BoxZoomTest, HasActiveAnimationsChecksBothEngines)
{
    AnimationController anim_ctrl;
    TransitionEngine trans_engine;
    handler_.set_animation_controller(&anim_ctrl);
    handler_.set_transition_engine(&trans_engine);

    EXPECT_FALSE(handler_.has_active_animations());

    anim_ctrl.animate_axis_limits(axes(), {2, 8}, {2, 8}, 1.0f, ease::linear);
    EXPECT_TRUE(handler_.has_active_animations());
}

// ═══════════════════════════════════════════════════════════════════════════
// update() drives both engines
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(BoxZoomTest, UpdateDrivesBothEngines)
{
    AnimationController anim_ctrl;
    TransitionEngine trans_engine;
    handler_.set_animation_controller(&anim_ctrl);
    handler_.set_transition_engine(&trans_engine);

    Axes ax2;
    ax2.xlim(0, 10);
    ax2.ylim(0, 10);

    anim_ctrl.animate_axis_limits(ax2, {5, 5}, {5, 5}, 0.1f, ease::linear);
    trans_engine.animate_limits(axes(), {5, 5}, {5, 5}, 0.1f);

    EXPECT_TRUE(anim_ctrl.has_active_animations());
    EXPECT_TRUE(trans_engine.has_active_animations());

    handler_.update(0.5f);

    EXPECT_FALSE(anim_ctrl.has_active_animations());
    EXPECT_FALSE(trans_engine.has_active_animations());
}

// ═══════════════════════════════════════════════════════════════════════════
// Keyboard shortcut R (reset view) with TransitionEngine
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(BoxZoomTest, ResetViewUsesTransitionEngine)
{
    TransitionEngine trans_engine;
    handler_.set_transition_engine(&trans_engine);

    axes().xlim(3.0f, 7.0f);
    axes().ylim(3.0f, 7.0f);

    // Press R
    handler_.on_key(82, 1, 0);

    EXPECT_TRUE(trans_engine.has_active_animations());

    trans_engine.update(0.5f);

    auto xlim = axes().x_limits();
    EXPECT_NE(xlim.min, 3.0f);
    EXPECT_NE(xlim.max, 7.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Keyboard shortcut A (auto-fit) with TransitionEngine
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(BoxZoomTest, AutoFitKeyUsesTransitionEngine)
{
    TransitionEngine trans_engine;
    handler_.set_transition_engine(&trans_engine);

    axes().xlim(3.0f, 7.0f);
    axes().ylim(3.0f, 7.0f);

    // Press A
    handler_.on_key(65, 1, 0);

    EXPECT_TRUE(trans_engine.has_active_animations());

    trans_engine.update(0.5f);

    auto xlim = axes().x_limits();
    EXPECT_NE(xlim.min, 3.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Edge cases
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(BoxZoomTest, BoxZoomWithReversedDrag)
{
    // Drag from bottom-right to top-left
    handler_.set_tool_mode(ToolMode::BoxZoom);
    auto& vp = axes().viewport();
    double x0 = vp.x + vp.w * 0.75;
    double y0 = vp.y + vp.h * 0.75;
    double x1 = vp.x + vp.w * 0.25;
    double y1 = vp.y + vp.h * 0.25;

    handler_.on_mouse_button(0, 1, 0, x0, y0);
    handler_.on_mouse_move(x1, y1);
    handler_.on_mouse_button(0, 0, 0, x1, y1);

    // Should still zoom to the correct region (min/max normalized)
    auto xlim = axes().x_limits();
    auto ylim = axes().y_limits();
    EXPECT_NEAR(xlim.min, 2.5f, 0.5f);
    EXPECT_NEAR(xlim.max, 7.5f, 0.5f);
    EXPECT_NEAR(ylim.min, 2.5f, 0.5f);
    EXPECT_NEAR(ylim.max, 7.5f, 0.5f);
}

TEST_F(BoxZoomTest, BoxZoomWithNoActiveAxesIsNoop)
{
    // Clear both figure and active axes so hit-testing can't find any axes
    handler_.set_figure(nullptr);
    handler_.set_active_axes(nullptr);
    handler_.set_tool_mode(ToolMode::BoxZoom);

    handler_.on_mouse_button(0, 1, 0, 100.0, 100.0);
    EXPECT_EQ(handler_.mode(), InteractionMode::Idle);
    EXPECT_FALSE(handler_.box_zoom_rect().active);
}

TEST_F(BoxZoomTest, CtrlDragBoxZoomDoesNotPan)
{
    handler_.set_tool_mode(ToolMode::Pan);
    auto& vp = axes().viewport();
    double x0 = vp.x + vp.w * 0.25;
    double y0 = vp.y + vp.h * 0.25;
    double x1 = vp.x + vp.w * 0.75;
    double y1 = vp.y + vp.h * 0.75;

    auto xlim_before = axes().x_limits();

    handler_.on_mouse_button(0, 1, 0x0002, x0, y0);  // Ctrl
    handler_.on_mouse_move(x1, y1);

    // During Ctrl+drag, limits should NOT have changed (no panning)
    auto xlim_during = axes().x_limits();
    EXPECT_FLOAT_EQ(xlim_during.min, xlim_before.min);
    EXPECT_FLOAT_EQ(xlim_during.max, xlim_before.max);

    handler_.on_mouse_button(0, 0, 0, x1, y1);

    // After release, limits should have changed (box zoom applied)
    auto xlim_after = axes().x_limits();
    EXPECT_NE(xlim_after.min, xlim_before.min);
}

TEST_F(BoxZoomTest, TransitionEngineFallbackToAnimController)
{
    // When only AnimationController is set (no TransitionEngine),
    // it should still work
    AnimationController anim_ctrl;
    handler_.set_animation_controller(&anim_ctrl);
    handler_.set_tool_mode(ToolMode::BoxZoom);

    auto& vp = axes().viewport();
    double x0 = vp.x + vp.w * 0.25;
    double y0 = vp.y + vp.h * 0.25;
    double x1 = vp.x + vp.w * 0.75;
    double y1 = vp.y + vp.h * 0.75;

    handler_.on_mouse_button(0, 1, 0, x0, y0);
    handler_.on_mouse_move(x1, y1);
    handler_.on_mouse_button(0, 0, 0, x1, y1);

    EXPECT_TRUE(anim_ctrl.has_active_animations());
    anim_ctrl.update(0.5f);

    auto xlim = axes().x_limits();
    EXPECT_NEAR(xlim.min, 2.5f, 0.5f);
    EXPECT_NEAR(xlim.max, 7.5f, 0.5f);
}
