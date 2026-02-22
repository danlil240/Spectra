#include <benchmark/benchmark.h>
#define _USE_MATH_DEFINES
#include <cmath>
#include <filesystem>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/plot_style.hpp>
#include <spectra/series.hpp>
#include <string>
#include <vector>

#include "ui/axis_link.hpp"
#include "ui/data_transform.hpp"
#include "ui/dock_system.hpp"
#include "ui/keyframe_interpolator.hpp"
#include "ui/recording_export.hpp"
#include "ui/shortcut_config.hpp"
#include "ui/split_view.hpp"
#include "ui/timeline_editor.hpp"
#include "ui/workspace.hpp"

using namespace spectra;

// ─── SplitView benchmarks ───────────────────────────────────────────────────

static void BM_SplitView_ComputeLayout(benchmark::State& state)
{
    SplitViewManager mgr;
    // Create a 4-pane split
    mgr.split_pane(0, SplitDirection::Horizontal, 1, 0.5f);
    mgr.split_pane(0, SplitDirection::Vertical, 2, 0.5f);
    mgr.split_pane(1, SplitDirection::Vertical, 3, 0.5f);

    Rect canvas{0, 0, 1920, 1080};

    for (auto _ : state)
    {
        mgr.update_layout(canvas);
        benchmark::DoNotOptimize(mgr.all_panes().size());
    }
}
BENCHMARK(BM_SplitView_ComputeLayout)->Unit(benchmark::kMicrosecond);

static void BM_SplitView_SplitAndClose(benchmark::State& state)
{
    for (auto _ : state)
    {
        SplitViewManager mgr;
        mgr.split_pane(0, SplitDirection::Horizontal, 1, 0.5f);
        mgr.split_pane(0, SplitDirection::Vertical, 2, 0.5f);
        mgr.close_pane(2);
        mgr.close_pane(1);
        benchmark::DoNotOptimize(mgr.pane_count());
    }
}
BENCHMARK(BM_SplitView_SplitAndClose)->Unit(benchmark::kMicrosecond);

static void BM_SplitView_Serialization(benchmark::State& state)
{
    SplitViewManager mgr;
    mgr.split_pane(0, SplitDirection::Horizontal, 1, 0.5f);
    mgr.split_pane(0, SplitDirection::Vertical, 2, 0.5f);
    mgr.split_pane(1, SplitDirection::Vertical, 3, 0.5f);
    mgr.update_layout(Rect{0, 0, 1920, 1080});

    for (auto _ : state)
    {
        std::string json = mgr.serialize();
        benchmark::DoNotOptimize(json);
    }
}
BENCHMARK(BM_SplitView_Serialization)->Unit(benchmark::kMicrosecond);

static void BM_SplitView_Deserialization(benchmark::State& state)
{
    SplitViewManager mgr;
    mgr.split_pane(0, SplitDirection::Horizontal, 1, 0.5f);
    mgr.split_pane(0, SplitDirection::Vertical, 2, 0.5f);
    mgr.split_pane(1, SplitDirection::Vertical, 3, 0.5f);
    mgr.update_layout(Rect{0, 0, 1920, 1080});
    std::string json = mgr.serialize();

    for (auto _ : state)
    {
        SplitViewManager loaded;
        loaded.deserialize(json);
        benchmark::DoNotOptimize(loaded.pane_count());
    }
}
BENCHMARK(BM_SplitView_Deserialization)->Unit(benchmark::kMicrosecond);

// ─── DockSystem benchmarks ──────────────────────────────────────────────────

static void BM_DockSystem_SplitRight(benchmark::State& state)
{
    for (auto _ : state)
    {
        DockSystem dock;
        dock.update_layout(Rect{0, 0, 1920, 1080});
        for (size_t i = 1; i <= 4; ++i)
        {
            dock.split_right(i, 0.5f);
        }
        benchmark::DoNotOptimize(dock.pane_count());
    }
}
BENCHMARK(BM_DockSystem_SplitRight)->Unit(benchmark::kMicrosecond);

static void BM_DockSystem_GetPaneInfos(benchmark::State& state)
{
    DockSystem dock;
    dock.update_layout(Rect{0, 0, 1920, 1080});
    dock.split_right(1, 0.5f);
    dock.split_figure_down(1, 2, 0.5f);
    dock.split_figure_down(0, 3, 0.5f);

    for (auto _ : state)
    {
        auto infos = dock.get_pane_infos();
        benchmark::DoNotOptimize(infos);
    }
}
BENCHMARK(BM_DockSystem_GetPaneInfos)->Unit(benchmark::kMicrosecond);

static void BM_DockSystem_DropTargetCompute(benchmark::State& state)
{
    DockSystem dock;
    dock.update_layout(Rect{0, 0, 1920, 1080});
    dock.split_right(1, 0.5f);

    dock.begin_drag(0, 100.0f, 100.0f);

    for (auto _ : state)
    {
        auto target = dock.update_drag(500.0f, 300.0f);
        benchmark::DoNotOptimize(target.zone);
    }
    dock.cancel_drag();
}
BENCHMARK(BM_DockSystem_DropTargetCompute)->Unit(benchmark::kNanosecond);

// ─── AxisLinkManager benchmarks ─────────────────────────────────────────────

static void BM_AxisLink_CreateGroupAndAdd(benchmark::State& state)
{
    for (auto _ : state)
    {
        AxisLinkManager   mgr;
        std::vector<Axes> axes(10);
        auto              gid = mgr.create_group("X Link", LinkAxis::X);
        for (auto& ax : axes)
        {
            mgr.add_to_group(gid, &ax);
        }
        benchmark::DoNotOptimize(mgr.group_count());
    }
}
BENCHMARK(BM_AxisLink_CreateGroupAndAdd)->Unit(benchmark::kMicrosecond);

static void BM_AxisLink_PropagateFrom(benchmark::State& state)
{
    AxisLinkManager   mgr;
    std::vector<Axes> axes(static_cast<size_t>(state.range(0)));
    for (auto& ax : axes)
    {
        ax.xlim(0, 10);
        ax.ylim(0, 10);
    }

    auto gid = mgr.create_group("Linked", LinkAxis::Both);
    for (auto& ax : axes)
    {
        mgr.add_to_group(gid, &ax);
    }

    for (auto _ : state)
    {
        axes[0].xlim(2.0f, 8.0f);
        mgr.propagate_limits(&axes[0], {2.0f, 8.0f}, {0.0f, 10.0f});
        // Reset for next iteration
        for (auto& ax : axes)
        {
            ax.xlim(0, 10);
            ax.ylim(0, 10);
        }
    }
}
BENCHMARK(BM_AxisLink_PropagateFrom)
    ->Arg(2)
    ->Arg(5)
    ->Arg(10)
    ->Arg(20)
    ->Unit(benchmark::kMicrosecond);

static void BM_AxisLink_IsLinked(benchmark::State& state)
{
    AxisLinkManager   mgr;
    std::vector<Axes> axes(10);
    auto              gid = mgr.create_group("Link", LinkAxis::X);
    for (auto& ax : axes)
    {
        mgr.add_to_group(gid, &ax);
    }

    for (auto _ : state)
    {
        bool linked = mgr.is_linked(&axes[5]);
        benchmark::DoNotOptimize(linked);
    }
}
BENCHMARK(BM_AxisLink_IsLinked)->Unit(benchmark::kNanosecond);

static void BM_AxisLink_SharedCursorUpdate(benchmark::State& state)
{
    AxisLinkManager mgr;
    Axes            ax1, ax2;
    mgr.link(&ax1, &ax2, LinkAxis::Both);

    SharedCursor cursor;
    cursor.valid       = true;
    cursor.data_x      = 5.0f;
    cursor.data_y      = 3.0f;
    cursor.source_axes = &ax1;

    for (auto _ : state)
    {
        mgr.update_shared_cursor(cursor);
        auto received = mgr.shared_cursor_for(&ax2);
        benchmark::DoNotOptimize(received.valid);
    }
}
BENCHMARK(BM_AxisLink_SharedCursorUpdate)->Unit(benchmark::kNanosecond);

static void BM_AxisLink_Serialization(benchmark::State& state)
{
    AxisLinkManager   mgr;
    std::vector<Axes> axes(6);
    auto              g1 = mgr.create_group("X Link", LinkAxis::X);
    auto              g2 = mgr.create_group("Y Link", LinkAxis::Y);
    for (int i = 0; i < 3; ++i)
        mgr.add_to_group(g1, &axes[i]);
    for (int i = 3; i < 6; ++i)
        mgr.add_to_group(g2, &axes[i]);

    auto mapper = [&](const Axes* a) -> int
    {
        for (int i = 0; i < 6; ++i)
            if (&axes[i] == a)
                return i;
        return -1;
    };

    for (auto _ : state)
    {
        std::string json = mgr.serialize(mapper);
        benchmark::DoNotOptimize(json);
    }
}
BENCHMARK(BM_AxisLink_Serialization)->Unit(benchmark::kMicrosecond);

// ─── DataTransform benchmarks ───────────────────────────────────────────────

static void BM_DataTransform_SingleApply(benchmark::State& state)
{
    const size_t       N = static_cast<size_t>(state.range(0));
    std::vector<float> x(N), y(N);
    for (size_t i = 0; i < N; ++i)
    {
        x[i] = static_cast<float>(i) * 0.01f;
        y[i] = std::sin(x[i]);
    }

    DataTransform      tf(TransformType::Log10);
    std::vector<float> x_out, y_out;

    for (auto _ : state)
    {
        tf.apply_y(x, y, x_out, y_out);
        benchmark::DoNotOptimize(y_out.data());
    }
}
BENCHMARK(BM_DataTransform_SingleApply)
    ->Arg(100)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->Unit(benchmark::kMicrosecond);

static void BM_DataTransform_Pipeline3Steps(benchmark::State& state)
{
    const size_t       N = static_cast<size_t>(state.range(0));
    std::vector<float> x(N), y(N);
    for (size_t i = 0; i < N; ++i)
    {
        x[i] = static_cast<float>(i) * 0.01f;
        y[i] = 1.0f + std::sin(x[i]);
    }

    TransformPipeline pipeline("bench");
    pipeline.push_back(DataTransform(TransformType::Log10));
    pipeline.push_back(DataTransform(TransformType::Scale, TransformParams{.scale_factor = 2.0f}));
    pipeline.push_back(DataTransform(TransformType::Offset, TransformParams{.offset_value = 1.0f}));

    std::vector<float> x_out, y_out;

    for (auto _ : state)
    {
        pipeline.apply(x, y, x_out, y_out);
        benchmark::DoNotOptimize(y_out.data());
    }
}
BENCHMARK(BM_DataTransform_Pipeline3Steps)
    ->Arg(100)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->Unit(benchmark::kMicrosecond);

static void BM_DataTransform_Derivative(benchmark::State& state)
{
    const size_t       N = static_cast<size_t>(state.range(0));
    std::vector<float> x(N), y(N);
    for (size_t i = 0; i < N; ++i)
    {
        x[i] = static_cast<float>(i) * 0.001f;
        y[i] = std::sin(x[i] * 10.0f);
    }

    DataTransform      tf(TransformType::Derivative);
    std::vector<float> x_out, y_out;

    for (auto _ : state)
    {
        tf.apply_y(x, y, x_out, y_out);
        benchmark::DoNotOptimize(y_out.data());
    }
}
BENCHMARK(BM_DataTransform_Derivative)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->Unit(benchmark::kMicrosecond);

static void BM_DataTransform_Normalize(benchmark::State& state)
{
    const size_t       N = static_cast<size_t>(state.range(0));
    std::vector<float> x(N), y(N);
    for (size_t i = 0; i < N; ++i)
    {
        x[i] = static_cast<float>(i);
        y[i] = std::sin(static_cast<float>(i) * 0.01f) * 100.0f;
    }

    DataTransform      tf(TransformType::Normalize);
    std::vector<float> x_out, y_out;

    for (auto _ : state)
    {
        tf.apply_y(x, y, x_out, y_out);
        benchmark::DoNotOptimize(y_out.data());
    }
}
BENCHMARK(BM_DataTransform_Normalize)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->Unit(benchmark::kMicrosecond);

static void BM_DataTransform_RegistryLookup(benchmark::State& state)
{
    auto& reg = TransformRegistry::instance();

    for (auto _ : state)
    {
        DataTransform dt;
        bool          found = reg.get_transform("square", dt);
        benchmark::DoNotOptimize(found);
    }
}
BENCHMARK(BM_DataTransform_RegistryLookup)->Unit(benchmark::kNanosecond);

// ─── KeyframeInterpolator benchmarks ────────────────────────────────────────

static void BM_KeyframeInterp_EvaluateLinear(benchmark::State& state)
{
    AnimationChannel ch("bench", 0.0f);
    int              num_kf = static_cast<int>(state.range(0));
    for (int i = 0; i < num_kf; ++i)
    {
        ch.add_keyframe(
            TypedKeyframe(static_cast<float>(i), static_cast<float>(i) * 0.5f, InterpMode::Linear));
    }

    float t = static_cast<float>(num_kf) * 0.5f;

    for (auto _ : state)
    {
        float val = ch.evaluate(t);
        benchmark::DoNotOptimize(val);
    }
}
BENCHMARK(BM_KeyframeInterp_EvaluateLinear)
    ->Arg(10)
    ->Arg(50)
    ->Arg(200)
    ->Arg(1000)
    ->Unit(benchmark::kNanosecond);

static void BM_KeyframeInterp_EvaluateCubicBezier(benchmark::State& state)
{
    AnimationChannel ch("bench", 0.0f);
    for (int i = 0; i < 20; ++i)
    {
        TypedKeyframe kf(static_cast<float>(i),
                         static_cast<float>(i) * 0.5f,
                         InterpMode::CubicBezier);
        kf.tangent_mode = TangentMode::Auto;
        ch.add_keyframe(kf);
    }
    ch.compute_auto_tangents();

    for (auto _ : state)
    {
        float val = ch.evaluate(10.5f);
        benchmark::DoNotOptimize(val);
    }
}
BENCHMARK(BM_KeyframeInterp_EvaluateCubicBezier)->Unit(benchmark::kNanosecond);

static void BM_KeyframeInterp_EvaluateSpring(benchmark::State& state)
{
    AnimationChannel ch("bench", 0.0f);
    ch.add_keyframe(TypedKeyframe(0.0f, 0.0f, InterpMode::Spring));
    ch.add_keyframe(TypedKeyframe(2.0f, 10.0f, InterpMode::Spring));

    for (auto _ : state)
    {
        float val = ch.evaluate(1.0f);
        benchmark::DoNotOptimize(val);
    }
}
BENCHMARK(BM_KeyframeInterp_EvaluateSpring)->Unit(benchmark::kNanosecond);

static void BM_KeyframeInterp_SampleChannel(benchmark::State& state)
{
    AnimationChannel ch("bench", 0.0f);
    for (int i = 0; i < 10; ++i)
    {
        ch.add_keyframe(TypedKeyframe(static_cast<float>(i),
                                      std::sin(static_cast<float>(i)),
                                      InterpMode::Linear));
    }

    for (auto _ : state)
    {
        auto samples = ch.sample(0.0f, 9.0f, static_cast<uint32_t>(state.range(0)));
        benchmark::DoNotOptimize(samples.data());
    }
}
BENCHMARK(BM_KeyframeInterp_SampleChannel)
    ->Arg(100)
    ->Arg(500)
    ->Arg(2000)
    ->Unit(benchmark::kMicrosecond);

static void BM_KeyframeInterp_EvaluateAll(benchmark::State& state)
{
    KeyframeInterpolator interp;
    std::vector<float>   targets(static_cast<size_t>(state.range(0)), 0.0f);

    for (size_t i = 0; i < targets.size(); ++i)
    {
        auto ch_id = interp.add_channel("ch_" + std::to_string(i));
        interp.bind(ch_id, "target_" + std::to_string(i), &targets[i]);
        interp.add_keyframe(ch_id, TypedKeyframe(0.0f, 0.0f, InterpMode::Linear));
        interp.add_keyframe(ch_id, TypedKeyframe(5.0f, 10.0f, InterpMode::Linear));
    }

    for (auto _ : state)
    {
        interp.evaluate(2.5f);
    }
    benchmark::DoNotOptimize(targets.data());
}
BENCHMARK(BM_KeyframeInterp_EvaluateAll)
    ->Arg(1)
    ->Arg(5)
    ->Arg(20)
    ->Arg(50)
    ->Unit(benchmark::kMicrosecond);

static void BM_KeyframeInterp_Serialization(benchmark::State& state)
{
    KeyframeInterpolator interp;
    for (int i = 0; i < 5; ++i)
    {
        auto ch_id = interp.add_channel("ch_" + std::to_string(i));
        for (int j = 0; j < 20; ++j)
        {
            interp.add_keyframe(ch_id,
                                TypedKeyframe(static_cast<float>(j),
                                              std::sin(static_cast<float>(j)),
                                              InterpMode::Linear));
        }
    }

    for (auto _ : state)
    {
        std::string json = interp.serialize();
        benchmark::DoNotOptimize(json);
    }
}
BENCHMARK(BM_KeyframeInterp_Serialization)->Unit(benchmark::kMicrosecond);

static void BM_KeyframeInterp_Deserialization(benchmark::State& state)
{
    KeyframeInterpolator interp;
    for (int i = 0; i < 5; ++i)
    {
        auto ch_id = interp.add_channel("ch_" + std::to_string(i));
        for (int j = 0; j < 20; ++j)
        {
            interp.add_keyframe(ch_id,
                                TypedKeyframe(static_cast<float>(j),
                                              std::sin(static_cast<float>(j)),
                                              InterpMode::Linear));
        }
    }
    std::string json = interp.serialize();

    for (auto _ : state)
    {
        KeyframeInterpolator loaded;
        loaded.deserialize(json);
        benchmark::DoNotOptimize(loaded.channel_count());
    }
}
BENCHMARK(BM_KeyframeInterp_Deserialization)->Unit(benchmark::kMicrosecond);

// ─── TimelineEditor benchmarks ──────────────────────────────────────────────

static void BM_Timeline_Advance(benchmark::State& state)
{
    TimelineEditor timeline;
    timeline.set_duration(10.0f);
    timeline.set_fps(60.0f);

    KeyframeInterpolator interp;
    auto                 ch = interp.add_channel("val");
    interp.add_keyframe(ch, TypedKeyframe(0.0f, 0.0f, InterpMode::Linear));
    interp.add_keyframe(ch, TypedKeyframe(10.0f, 100.0f, InterpMode::Linear));
    timeline.set_interpolator(&interp);
    timeline.play();

    for (auto _ : state)
    {
        timeline.advance(1.0f / 60.0f);
    }
    benchmark::DoNotOptimize(timeline.playhead());
}
BENCHMARK(BM_Timeline_Advance)->Unit(benchmark::kMicrosecond);

static void BM_Timeline_AddRemoveKeyframes(benchmark::State& state)
{
    TimelineEditor timeline;
    timeline.set_duration(100.0f);
    auto track = timeline.add_track("bench");

    for (auto _ : state)
    {
        for (int i = 0; i < 50; ++i)
        {
            timeline.add_keyframe(track, static_cast<float>(i) * 0.1f);
        }
        for (int i = 0; i < 50; ++i)
        {
            timeline.remove_keyframe(track, static_cast<float>(i) * 0.1f);
        }
    }
    benchmark::DoNotOptimize(timeline.total_keyframe_count());
}
BENCHMARK(BM_Timeline_AddRemoveKeyframes)->Unit(benchmark::kMicrosecond);

static void BM_Timeline_Serialization(benchmark::State& state)
{
    TimelineEditor timeline;
    timeline.set_duration(10.0f);
    KeyframeInterpolator interp;
    timeline.set_interpolator(&interp);

    for (int t = 0; t < 5; ++t)
    {
        auto id = timeline.add_animated_track("track_" + std::to_string(t), 0.0f);
        for (int k = 0; k < 10; ++k)
        {
            timeline.add_animated_keyframe(id, static_cast<float>(k), static_cast<float>(k) * 0.5f);
        }
    }

    for (auto _ : state)
    {
        std::string json = timeline.serialize();
        benchmark::DoNotOptimize(json);
    }
}
BENCHMARK(BM_Timeline_Serialization)->Unit(benchmark::kMicrosecond);

// ─── ShortcutConfig benchmarks ──────────────────────────────────────────────

static void BM_ShortcutConfig_SetOverride(benchmark::State& state)
{
    ShortcutConfig config;
    int            i = 0;

    for (auto _ : state)
    {
        config.set_override("cmd." + std::to_string(i % 100),
                            "Ctrl+" + std::to_string(i % 26 + 65));
        ++i;
    }
    benchmark::DoNotOptimize(config.override_count());
}
BENCHMARK(BM_ShortcutConfig_SetOverride)->Unit(benchmark::kNanosecond);

static void BM_ShortcutConfig_Serialization(benchmark::State& state)
{
    ShortcutConfig config;
    for (int i = 0; i < 30; ++i)
    {
        config.set_override("cmd." + std::to_string(i), "Ctrl+Shift+" + std::to_string(i));
    }

    for (auto _ : state)
    {
        std::string json = config.serialize();
        benchmark::DoNotOptimize(json);
    }
}
BENCHMARK(BM_ShortcutConfig_Serialization)->Unit(benchmark::kMicrosecond);

static void BM_ShortcutConfig_Deserialization(benchmark::State& state)
{
    ShortcutConfig config;
    for (int i = 0; i < 30; ++i)
    {
        config.set_override("cmd." + std::to_string(i), "Ctrl+Shift+" + std::to_string(i));
    }
    std::string json = config.serialize();

    for (auto _ : state)
    {
        ShortcutConfig loaded;
        loaded.deserialize(json);
        benchmark::DoNotOptimize(loaded.override_count());
    }
}
BENCHMARK(BM_ShortcutConfig_Deserialization)->Unit(benchmark::kMicrosecond);

// ─── PlotStyle benchmarks ───────────────────────────────────────────────────

static void BM_PlotStyle_ParseFormatString(benchmark::State& state)
{
    for (auto _ : state)
    {
        auto s1 = parse_format_string("r--o");
        auto s2 = parse_format_string("b:*");
        auto s3 = parse_format_string("g-.s");
        auto s4 = parse_format_string("k");
        benchmark::DoNotOptimize(s1.line_style);
        benchmark::DoNotOptimize(s2.marker_style);
        benchmark::DoNotOptimize(s3.color);
        benchmark::DoNotOptimize(s4.line_style);
    }
}
BENCHMARK(BM_PlotStyle_ParseFormatString)->Unit(benchmark::kNanosecond);

static void BM_PlotStyle_ToFormatString(benchmark::State& state)
{
    PlotStyle style;
    style.line_style   = LineStyle::Dashed;
    style.marker_style = MarkerStyle::Circle;
    style.color        = colors::red;

    for (auto _ : state)
    {
        std::string fmt = to_format_string(style);
        benchmark::DoNotOptimize(fmt);
    }
}
BENCHMARK(BM_PlotStyle_ToFormatString)->Unit(benchmark::kNanosecond);

static void BM_PlotStyle_DashPattern(benchmark::State& state)
{
    for (auto _ : state)
    {
        auto p1 = get_dash_pattern(LineStyle::Dashed, 2.0f);
        auto p2 = get_dash_pattern(LineStyle::Dotted, 1.5f);
        auto p3 = get_dash_pattern(LineStyle::DashDot, 3.0f);
        auto p4 = get_dash_pattern(LineStyle::DashDotDot, 2.0f);
        benchmark::DoNotOptimize(p1.total);
        benchmark::DoNotOptimize(p2.total);
        benchmark::DoNotOptimize(p3.total);
        benchmark::DoNotOptimize(p4.total);
    }
}
BENCHMARK(BM_PlotStyle_DashPattern)->Unit(benchmark::kNanosecond);

// ─── Workspace v3 benchmarks ────────────────────────────────────────────────

static WorkspaceData make_phase3_workspace(int num_figures, int series_per_fig)
{
    WorkspaceData data;
    data.theme_name                    = "dark";
    data.active_figure_index           = 0;
    data.panels.inspector_visible      = true;
    data.panels.inspector_width        = 320.0f;
    data.interaction.crosshair_enabled = true;
    data.dock_state                    = "{\"root\":{\"leaf\":0}}";
    data.axis_link_state               = "{\"groups\":[]}";
    data.data_palette_name             = "okabe_ito";
    data.timeline.playhead             = 1.0f;
    data.timeline.duration             = 10.0f;
    data.timeline.fps                  = 60.0f;

    for (int f = 0; f < num_figures; ++f)
    {
        WorkspaceData::FigureState fig;
        fig.title            = "Figure " + std::to_string(f + 1);
        fig.width            = 1280;
        fig.height           = 720;
        fig.custom_tab_title = "Tab " + std::to_string(f + 1);

        WorkspaceData::AxisState ax;
        ax.x_min   = 0;
        ax.x_max   = 10;
        ax.y_min   = -1;
        ax.y_max   = 1;
        ax.title   = "Axes";
        ax.x_label = "X";
        ax.y_label = "Y";
        fig.axes.push_back(ax);

        for (int s = 0; s < series_per_fig; ++s)
        {
            WorkspaceData::SeriesState ss;
            ss.name         = "Series " + std::to_string(s);
            ss.type         = (s % 2 == 0) ? "line" : "scatter";
            ss.line_style   = s % 5;
            ss.marker_style = s % 18;
            ss.opacity      = 0.8f;
            ss.dash_pattern = {8.0f, 4.0f};
            fig.series.push_back(ss);
        }
        data.figures.push_back(fig);

        WorkspaceData::TransformState ts;
        ts.figure_index = static_cast<size_t>(f);
        ts.axes_index   = 0;
        ts.steps.push_back({1, 0.0f, true});
        data.transforms.push_back(ts);
    }

    for (int i = 0; i < 5; ++i)
    {
        WorkspaceData::ShortcutOverride so;
        so.command_id   = "cmd." + std::to_string(i);
        so.shortcut_str = "Ctrl+" + std::to_string(i);
        data.shortcut_overrides.push_back(so);
    }

    return data;
}

static void BM_WorkspaceV3_SaveSmall(benchmark::State& state)
{
    auto data = make_phase3_workspace(1, 3);
    auto path = std::filesystem::temp_directory_path() / "spectra_bench_ws3_small.spectra";

    for (auto _ : state)
    {
        Workspace::save(path.string(), data);
    }
    std::filesystem::remove(path);
}
BENCHMARK(BM_WorkspaceV3_SaveSmall)->Unit(benchmark::kMicrosecond);

static void BM_WorkspaceV3_SaveLarge(benchmark::State& state)
{
    auto data = make_phase3_workspace(10, 5);
    auto path = std::filesystem::temp_directory_path() / "spectra_bench_ws3_large.spectra";

    for (auto _ : state)
    {
        Workspace::save(path.string(), data);
    }
    std::filesystem::remove(path);
}
BENCHMARK(BM_WorkspaceV3_SaveLarge)->Unit(benchmark::kMicrosecond);

static void BM_WorkspaceV3_LoadSmall(benchmark::State& state)
{
    auto data = make_phase3_workspace(1, 3);
    auto path = std::filesystem::temp_directory_path() / "spectra_bench_ws3_load_small.spectra";
    Workspace::save(path.string(), data);

    for (auto _ : state)
    {
        WorkspaceData loaded;
        Workspace::load(path.string(), loaded);
        benchmark::DoNotOptimize(loaded.figures.size());
    }
    std::filesystem::remove(path);
}
BENCHMARK(BM_WorkspaceV3_LoadSmall)->Unit(benchmark::kMicrosecond);

static void BM_WorkspaceV3_LoadLarge(benchmark::State& state)
{
    auto data = make_phase3_workspace(10, 5);
    auto path = std::filesystem::temp_directory_path() / "spectra_bench_ws3_load_large.spectra";
    Workspace::save(path.string(), data);

    for (auto _ : state)
    {
        WorkspaceData loaded;
        Workspace::load(path.string(), loaded);
        benchmark::DoNotOptimize(loaded.figures.size());
    }
    std::filesystem::remove(path);
}
BENCHMARK(BM_WorkspaceV3_LoadLarge)->Unit(benchmark::kMicrosecond);

// ─── GIF quantization benchmark ─────────────────────────────────────────────

static void BM_GIF_MedianCut(benchmark::State& state)
{
    constexpr uint32_t   W = 320, H = 240;
    std::vector<uint8_t> rgba(W * H * 4);
    for (size_t i = 0; i < rgba.size(); i += 4)
    {
        rgba[i + 0] = static_cast<uint8_t>((i / 4) % 256);
        rgba[i + 1] = static_cast<uint8_t>((i / 4 + 85) % 256);
        rgba[i + 2] = static_cast<uint8_t>((i / 4 + 170) % 256);
        rgba[i + 3] = 255;
    }

    for (auto _ : state)
    {
        auto palette = RecordingSession::median_cut(rgba.data(), W * H, 256);
        benchmark::DoNotOptimize(palette);
    }
}
BENCHMARK(BM_GIF_MedianCut)->Unit(benchmark::kMicrosecond);

static void BM_GIF_QuantizeFrame(benchmark::State& state)
{
    constexpr uint32_t   W = 320, H = 240;
    std::vector<uint8_t> rgba(W * H * 4);
    for (size_t i = 0; i < rgba.size(); i += 4)
    {
        rgba[i + 0] = static_cast<uint8_t>((i / 4) % 256);
        rgba[i + 1] = static_cast<uint8_t>((i / 4 + 85) % 256);
        rgba[i + 2] = static_cast<uint8_t>((i / 4 + 170) % 256);
        rgba[i + 3] = 255;
    }

    std::vector<uint8_t> palette, indexed;

    for (auto _ : state)
    {
        RecordingSession::quantize_frame(rgba.data(), W, H, 256, palette, indexed);
        benchmark::DoNotOptimize(indexed.data());
    }
}
BENCHMARK(BM_GIF_QuantizeFrame)->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
