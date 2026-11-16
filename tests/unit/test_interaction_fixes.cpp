// Interaction fixes: tool commands / keys select the tool everywhere (ImGui
// owns it), Esc cancels the gesture in progress before closing the palette,
// keyboard and middle-button pan respect log axes and linked axes, and the
// plot cursor follows the tool.

#include <gtest/gtest.h>

#include <memory>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>

#include "ui/input/input.hpp"
#include "ui/input/plot_cursor.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include <spectra/figure_registry.hpp>

    #include "render/backend.hpp"
    #include "render/renderer.hpp"
    #include "ui/app/session_runtime.hpp"
    #include "ui/app/window_ui_context.hpp"
    #include "ui/app/window_ui_context_builder.hpp"
    #include "ui/commands/command_registry.hpp"
    #include "ui/commands/shortcut_manager.hpp"
    #include "ui/data/axis_link.hpp"
    #include "ui/imgui/imgui_integration.hpp"
    #include "ui/overlay/annotation.hpp"
    #include "ui/theme/theme.hpp"
#endif

using namespace spectra;

namespace
{
constexpr int kPress    = 1;
constexpr int kRelease  = 0;
constexpr int kLeft     = 0;
constexpr int kMiddle   = 2;
constexpr int kKeyEsc   = 256;
constexpr int kKeyZ     = 90;
constexpr int kKeyP     = 80;
constexpr int kKeyN     = 78;
constexpr int kKeyI     = 73;
constexpr int kKeyM     = 77;
constexpr int kKeyS     = 83;
constexpr int kNoMods   = 0;
constexpr int kCtrlMods = 0x0002;
}   // namespace

// ─── Plot cursor ────────────────────────────────────────────────────────────

TEST(PlotCursor, FollowsTheTool)
{
    EXPECT_EQ(plot_cursor_for(ToolMode::Pan, true, false), PlotCursor::OpenHand);
    EXPECT_EQ(plot_cursor_for(ToolMode::BoxZoom, true, false), PlotCursor::Crosshair);
    EXPECT_EQ(plot_cursor_for(ToolMode::Measure, true, false), PlotCursor::Crosshair);
    EXPECT_EQ(plot_cursor_for(ToolMode::ROI, true, false), PlotCursor::Crosshair);
    EXPECT_EQ(plot_cursor_for(ToolMode::Annotate, true, false), PlotCursor::Text);
    EXPECT_EQ(plot_cursor_for(ToolMode::Select, true, false), PlotCursor::Default);
}

TEST(PlotCursor, DefaultOffThePlotGrabWhilePanning)
{
    EXPECT_EQ(plot_cursor_for(ToolMode::Pan, false, false), PlotCursor::Default);
    EXPECT_EQ(plot_cursor_for(ToolMode::Measure, false, false), PlotCursor::Default);
    // A pan drag keeps the grab cursor, even off the axes and in other tools
    // (middle-button pan).
    EXPECT_EQ(plot_cursor_for(ToolMode::Pan, false, true), PlotCursor::Grabbing);
    EXPECT_EQ(plot_cursor_for(ToolMode::Measure, true, true), PlotCursor::Grabbing);
}

TEST(PlotCursor, ToolCommandIds)
{
    EXPECT_STREQ(tool_command_id(ToolMode::Pan), "tool.pan");
    EXPECT_STREQ(tool_command_id(ToolMode::BoxZoom), "tool.box_zoom");
    EXPECT_STREQ(tool_command_id(ToolMode::Select), "tool.select");
    EXPECT_STREQ(tool_command_id(ToolMode::Measure), "tool.measure");
    EXPECT_STREQ(tool_command_id(ToolMode::Annotate), "tool.annotate");
    EXPECT_STREQ(tool_command_id(ToolMode::ROI), "tool.roi");
}

// ─── InputHandler: Esc and middle-button pan ────────────────────────────────

namespace
{
class InputFixture : public ::testing::Test
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
        const auto& vp = ax.viewport();
        handler_.set_viewport(vp.x, vp.y, vp.w, vp.h);
    }

    Axes&       axes() { return *fig_->axes()[0]; }
    const Rect& vp() { return axes().viewport(); }
    double      px(double fx) { return vp().x + vp().w * fx; }
    double      py(double fy) { return vp().y + vp().h * fy; }

    std::unique_ptr<Figure> fig_;
    InputHandler            handler_;
};
}   // namespace

TEST_F(InputFixture, EscCancelsCtrlBoxZoomInPanMode)
{
    handler_.set_tool_mode(ToolMode::Pan);
    handler_.on_mouse_button(kLeft, kPress, kCtrlMods, px(0.2), py(0.2));
    handler_.on_mouse_move(px(0.6), py(0.6));
    ASSERT_TRUE(handler_.box_zoom_rect().active);
    EXPECT_TRUE(handler_.has_active_gesture());

    handler_.on_key(kKeyEsc, kPress, kNoMods);
    EXPECT_FALSE(handler_.box_zoom_rect().active);
    EXPECT_EQ(handler_.mode(), InteractionMode::Idle);

    // Releasing the button afterwards must not zoom.
    handler_.on_mouse_button(kLeft, kRelease, kCtrlMods, px(0.6), py(0.6));
    EXPECT_DOUBLE_EQ(axes().x_limits().min, 0.0);
    EXPECT_DOUBLE_EQ(axes().x_limits().max, 10.0);
}

TEST_F(InputFixture, EscCancelsMeasureInProgressThenDismissesResult)
{
    handler_.set_tool_mode(ToolMode::Measure);

    // Two-click mode: first click placed, second pending -> in progress.
    handler_.on_mouse_button(kLeft, kPress, kNoMods, px(0.3), py(0.3));
    handler_.on_mouse_button(kLeft, kRelease, kNoMods, px(0.3), py(0.3));
    ASSERT_TRUE(handler_.has_measure_result());
    EXPECT_TRUE(handler_.has_active_gesture());
    EXPECT_TRUE(handler_.cancel_active_gesture());
    EXPECT_FALSE(handler_.has_measure_result());
    EXPECT_FALSE(handler_.has_active_gesture());

    // Finished measurement (drag): not a gesture, but Esc dismisses it.
    handler_.on_mouse_button(kLeft, kPress, kNoMods, px(0.2), py(0.2));
    handler_.on_mouse_move(px(0.7), py(0.7));
    handler_.on_mouse_button(kLeft, kRelease, kNoMods, px(0.7), py(0.7));
    ASSERT_TRUE(handler_.has_measure_result());
    EXPECT_FALSE(handler_.cancel_active_gesture());
    EXPECT_TRUE(handler_.has_measure_result());
    handler_.on_key(kKeyEsc, kPress, kNoMods);
    EXPECT_FALSE(handler_.has_measure_result());
}

TEST_F(InputFixture, EscDuringMeasureDragStopsTheDrag)
{
    handler_.set_tool_mode(ToolMode::Measure);
    handler_.on_mouse_button(kLeft, kPress, kNoMods, px(0.2), py(0.2));
    ASSERT_TRUE(handler_.is_measure_dragging());
    handler_.on_key(kKeyEsc, kPress, kNoMods);
    EXPECT_FALSE(handler_.is_measure_dragging());
    EXPECT_EQ(handler_.mode(), InteractionMode::Idle);
    handler_.on_mouse_button(kLeft, kRelease, kNoMods, px(0.7), py(0.7));
    EXPECT_FALSE(handler_.has_measure_result());
}

TEST_F(InputFixture, MiddlePanOnLogAxisStaysPositive)
{
    axes().yscale(ScaleType::Log10);
    axes().ylim(1.0, 1000.0);
    handler_.set_tool_mode(ToolMode::Select);

    // Drag up by a full viewport height: the view moves three decades down
    // (a linear shift of the old span would reach -998).
    handler_.on_mouse_button(kMiddle, kPress, kNoMods, px(0.5), py(0.9));
    handler_.on_mouse_move(px(0.5), py(-0.1));
    const auto y = axes().y_limits();
    EXPECT_GT(y.min, 0.0);
    EXPECT_NEAR(y.min, 1e-3, 1e-6);
    EXPECT_NEAR(y.max, 1.0, 1e-6);
    handler_.on_mouse_button(kMiddle, kRelease, kNoMods, px(0.5), py(-0.1));
}

#ifdef SPECTRA_USE_IMGUI

TEST_F(InputFixture, MiddlePanPropagatesToLinkedAxes)
{
    auto& other = fig_->subplot(2, 1, 2);
    fig_->compute_layout();
    other.xlim(0.0, 10.0);
    AxisLinkManager links;
    links.link(&axes(), &other, LinkAxis::X);
    handler_.set_axis_link_manager(&links);

    handler_.on_mouse_button(kMiddle, kPress, kNoMods, px(0.5), py(0.5));
    handler_.on_mouse_move(px(0.4), py(0.5));   // drag left: view moves right
    EXPECT_GT(axes().x_limits().min, 0.0);
    EXPECT_DOUBLE_EQ(other.x_limits().min, axes().x_limits().min);
    EXPECT_DOUBLE_EQ(other.x_limits().max, axes().x_limits().max);
}

// ─── Full window UI context: commands, shortcuts, palette ───────────────────

namespace
{

// Backend that does nothing; enough to construct a Renderer / SessionRuntime.
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

class WindowUiFixture : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        ui::ThemeManager::set_current(&theme_mgr_);
        theme_mgr_.ensure_initialized();
        renderer_ = std::make_unique<Renderer>(backend_, theme_mgr_);
        session_  = std::make_unique<SessionRuntime>(backend_, *renderer_, registry_);

        auto fig = std::make_unique<Figure>(FigureConfig{.width = 800, .height = 600});
        fig->subplot(2, 1, 1);
        fig->subplot(2, 1, 2);
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
        ASSERT_NE(ui_->imgui_ui, nullptr);
        active_figure_ = figure_;

        ui_->input_handler.set_figure(figure_);
        ui_->input_handler.set_active_axes(top());
    }

    void TearDown() override
    {
        ui_.reset();
        session_.reset();
        renderer_.reset();
        ui::ThemeManager::set_current(nullptr);
    }

    Axes* top() { return figure_->axes()[0].get(); }
    Axes* bottom() { return figure_->axes()[1].get(); }

    bool press(int key, int mods = kNoMods) { return ui_->shortcut_mgr.on_key(key, kPress, mods); }

    // What the window loop does every frame.
    void frame_sync() { ui_->input_handler.set_tool_mode(ui_->imgui_ui->get_interaction_mode()); }

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

}   // namespace

// ─── Tool commands -> mode ───────────────────────────────────────────────────

TEST_F(WindowUiFixture, ToolKeysSelectTheToolAndSurviveTheFrameSync)
{
    struct Case
    {
        int      key;
        ToolMode mode;
    };
    const Case cases[] = {{kKeyZ, ToolMode::BoxZoom},
                          {kKeyM, ToolMode::Measure},
                          {kKeyS, ToolMode::Select},
                          {kKeyN, ToolMode::Annotate},
                          {kKeyI, ToolMode::ROI},
                          {kKeyP, ToolMode::Pan}};
    for (const auto& c : cases)
    {
        ASSERT_TRUE(press(c.key)) << c.key;
        frame_sync();   // used to overwrite the tool with the stale rail mode
        EXPECT_EQ(ui_->imgui_ui->get_interaction_mode(), c.mode) << c.key;
        EXPECT_EQ(ui_->input_handler.tool_mode(), c.mode) << c.key;
    }
}

TEST_F(WindowUiFixture, PaletteToolCommandsDriveTheRailMode)
{
    EXPECT_TRUE(ui_->cmd_registry.execute("tool.measure"));
    EXPECT_EQ(ui_->imgui_ui->get_interaction_mode(), ToolMode::Measure);
    EXPECT_EQ(ui_->input_handler.tool_mode(), ToolMode::Measure);

    // And the rail (set_interaction_mode) drives the input handler directly.
    ui_->imgui_ui->set_interaction_mode(ToolMode::ROI);
    EXPECT_EQ(ui_->input_handler.tool_mode(), ToolMode::ROI);
}

TEST_F(WindowUiFixture, ToolKeysDoNotCollideAndShowInTooltips)
{
    const auto& sm = ui_->shortcut_mgr;
    EXPECT_EQ(sm.command_for_shortcut(Shortcut::from_string("N")), "tool.annotate");
    EXPECT_EQ(sm.command_for_shortcut(Shortcut::from_string("I")), "tool.roi");
    EXPECT_EQ(sm.command_for_shortcut(Shortcut::from_string("A")), "view.autofit");

    EXPECT_EQ(ui_->imgui_ui->tool_tooltip("Pan", ToolMode::Pan), "Pan (P)");
    EXPECT_EQ(ui_->imgui_ui->tool_tooltip("Zoom", ToolMode::BoxZoom), "Zoom (Z)");
    EXPECT_EQ(ui_->imgui_ui->tool_tooltip("ROI", ToolMode::ROI), "ROI (I)");
    EXPECT_EQ(ui_->imgui_ui->tool_tooltip("Annotate", ToolMode::Annotate), "Annotate (N)");

    // Rebinding the key updates the tooltip; unbinding drops it.
    ui_->shortcut_mgr.unbind_command("tool.pan");
    EXPECT_EQ(ui_->imgui_ui->tool_tooltip("Pan", ToolMode::Pan), "Pan");
}

// ─── Esc ordering ────────────────────────────────────────────────────────────

TEST_F(WindowUiFixture, EscCancelsGestureBeforeClosingPaletteThenDismissesResults)
{
    auto&       input = ui_->input_handler;
    const auto& vp    = top()->viewport();
    const auto  xlim0 = top()->x_limits();
    const auto  at    = [&](double fx, double fy)
    { return std::pair{vp.x + vp.w * fx, vp.y + vp.h * fy}; };

    // A finished measurement, then a box-zoom drag in progress, with the
    // palette open.
    ASSERT_TRUE(press(kKeyM));
    auto [ax0, ay0] = at(0.2, 0.2);
    auto [ax1, ay1] = at(0.6, 0.6);
    input.on_mouse_button(kLeft, kPress, kNoMods, ax0, ay0);
    input.on_mouse_move(ax1, ay1);
    input.on_mouse_button(kLeft, kRelease, kNoMods, ax1, ay1);
    ASSERT_TRUE(input.has_measure_result());

    ASSERT_TRUE(press(kKeyZ));
    frame_sync();
    input.on_mouse_button(kLeft, kPress, kNoMods, ax0, ay0);
    input.on_mouse_move(ax1, ay1);
    ASSERT_TRUE(input.box_zoom_rect().active);
    ui_->cmd_palette.open();
    ASSERT_TRUE(ui_->cmd_palette.is_open());

    // Esc #1: the drag only.
    EXPECT_TRUE(press(kKeyEsc));
    EXPECT_FALSE(input.box_zoom_rect().active);
    EXPECT_EQ(input.mode(), InteractionMode::Idle);
    EXPECT_TRUE(ui_->cmd_palette.is_open());

    // Esc #2: the palette.
    EXPECT_TRUE(press(kKeyEsc));
    EXPECT_FALSE(ui_->cmd_palette.is_open());

    // Esc #3: the measurement left on screen (tool switched back to Measure:
    // leaving Measure keeps the result data, so check it is cleared by Esc).
    input.set_tool_mode(ToolMode::Measure);
    input.on_mouse_button(kLeft, kPress, kNoMods, ax0, ay0);
    input.on_mouse_move(ax1, ay1);
    input.on_mouse_button(kLeft, kRelease, kNoMods, ax1, ay1);
    ASSERT_TRUE(input.has_measure_result());
    EXPECT_TRUE(press(kKeyEsc));
    EXPECT_FALSE(input.has_measure_result());

    // The view never changed.
    EXPECT_DOUBLE_EQ(top()->x_limits().min, xlim0.min);
    EXPECT_DOUBLE_EQ(top()->x_limits().max, xlim0.max);
}

TEST_F(WindowUiFixture, EscCancelsRoiDragAndPendingAnnotation)
{
    auto&       input = ui_->input_handler;
    const auto& vp    = top()->viewport();

    ASSERT_TRUE(press(kKeyI));
    input.on_mouse_button(kLeft, kPress, kNoMods, vp.x + vp.w * 0.2, vp.y + vp.h * 0.2);
    input.on_mouse_move(vp.x + vp.w * 0.6, vp.y + vp.h * 0.6);
    ASSERT_TRUE(input.has_active_gesture());
    EXPECT_TRUE(press(kKeyEsc));
    EXPECT_FALSE(input.has_active_gesture());
    EXPECT_FALSE(ui_->data_interaction->has_region_selection());
    input.on_mouse_button(kLeft, kRelease, kNoMods, vp.x + vp.w * 0.6, vp.y + vp.h * 0.6);
    EXPECT_FALSE(ui_->data_interaction->has_region_selection());

    // Pending (empty) annotation being typed: Esc drops it.
    auto&        notes = ui_->data_interaction->annotations();
    const size_t i     = notes.add(1.0f, 1.0f, top());
    ASSERT_TRUE(notes.is_editing()) << "a new annotation starts in edit mode";
    (void)i;
    EXPECT_TRUE(press(kKeyEsc));
    EXPECT_FALSE(notes.is_editing());
    EXPECT_EQ(notes.count(), 0u);
}

TEST_F(WindowUiFixture, AnnotationDragCancelRestoresTheStartOffset)
{
    AnnotationManager mgr;
    const size_t      i              = mgr.add(1.0f, 2.0f, nullptr);
    const float       y0             = mgr.annotations()[i].offset_y;
    mgr.annotations_mut()[i].editing = false;
    mgr.begin_drag(i, 10.0f, 10.0f);
    mgr.update_drag(40.0f, 25.0f);
    EXPECT_FLOAT_EQ(mgr.annotations()[i].offset_x, 30.0f);
    mgr.cancel_drag();
    EXPECT_FALSE(mgr.is_dragging());
    EXPECT_FLOAT_EQ(mgr.annotations()[i].offset_x, 0.0f);
    EXPECT_FLOAT_EQ(mgr.annotations()[i].offset_y, y0);
}

// ─── Keyboard pan / zoom: log axes, linked axes, undo ───────────────────────

TEST_F(WindowUiFixture, ArrowPanOnLogAxisNeverGoesNonPositive)
{
    top()->yscale(ScaleType::Log10);
    top()->ylim(1.0, 100.0);
    for (int i = 0; i < 40; ++i)
        ASSERT_TRUE(ui_->cmd_registry.execute("view.pan_down"));
    const auto y = top()->y_limits();
    EXPECT_GT(y.min, 0.0);
    // 40 steps of 10% of two decades = eight decades down, ratio kept.
    EXPECT_NEAR(std::log10(y.min), -8.0, 1e-6);
    EXPECT_NEAR(std::log10(y.max) - std::log10(y.min), 2.0, 1e-9);

    // +/- zoom on the log axis keeps it positive too.
    for (int i = 0; i < 10; ++i)
        ASSERT_TRUE(ui_->cmd_registry.execute("view.zoom_out"));
    EXPECT_GT(top()->y_limits().min, 0.0);
}

TEST_F(WindowUiFixture, ArrowPanAndZoomMoveLinkedAxesAndUndoTogether)
{
    top()->xlim(0.0, 10.0);
    bottom()->xlim(0.0, 10.0);
    bottom()->ylim(-1.0, 1.0);
    ui_->axis_link_mgr.link(top(), bottom(), LinkAxis::X);

    ASSERT_TRUE(ui_->cmd_registry.execute("view.pan_right"));
    EXPECT_NEAR(top()->x_limits().min, 1.0, 1e-9);
    EXPECT_NEAR(bottom()->x_limits().min, 1.0, 1e-9);
    EXPECT_NEAR(bottom()->x_limits().max, 11.0, 1e-9);
    EXPECT_DOUBLE_EQ(bottom()->y_limits().min, -1.0);   // only X is linked

    ASSERT_TRUE(ui_->cmd_registry.execute("view.zoom_in"));
    EXPECT_DOUBLE_EQ(bottom()->x_limits().min, top()->x_limits().min);
    EXPECT_DOUBLE_EQ(bottom()->x_limits().max, top()->x_limits().max);

    // Each keyboard step is one undo entry covering both axes.
    ASSERT_TRUE(ui_->undo_mgr.undo());
    EXPECT_NEAR(bottom()->x_limits().min, 1.0, 1e-9);
    ASSERT_TRUE(ui_->undo_mgr.undo());
    EXPECT_NEAR(top()->x_limits().min, 0.0, 1e-9);
    EXPECT_NEAR(bottom()->x_limits().min, 0.0, 1e-9);
    EXPECT_NEAR(bottom()->x_limits().max, 10.0, 1e-9);
}

#endif   // SPECTRA_USE_IMGUI
