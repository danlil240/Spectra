#pragma once

#include <cstdint>

namespace spectra
{

// Stable figure identifier backed by FigureRegistry with monotonic IDs.
using FigureId = uint64_t;

// Sentinel value for "no figure" / invalid figure id.
inline constexpr FigureId INVALID_FIGURE_ID = ~FigureId{0};

class App;
class Figure;
class Axes;
class AxesBase;
class Axes3D;

class Series;
class LineSeries;
class ScatterSeries;
class LineSeries3D;
class ScatterSeries3D;
class SurfaceSeries;
class MeshSeries;

class Renderer;
class Backend;
class GpuBuffer;
class RingBuffer;

class Animator;
class AnimationController;
class TransitionEngine;
class RegionSelect;
class LegendInteraction;
struct Frame;
class FrameScheduler;
class AnimationBuilder;
class GestureRecognizer;
class Camera;
class Axes3DRenderer;

class BoxZoomOverlay;
class CommandRegistry;
class CommandPalette;
class ShortcutManager;
class UndoManager;
class FigureManager;
struct FigureState;
class TimelineEditor;
class RecordingSession;
class KeyframeInterpolator;
class AnimationChannel;
class AnimationCurveEditor;
class SplitPane;
class SplitViewManager;
class DockSystem;
class AxisLinkManager;
struct SharedCursor;
class DataTransform;
class TransformPipeline;
class TransformRegistry;
class ShortcutConfig;
class PluginManager;
struct PluginEntry;
class CameraAnimator;
class ModeTransition;
class KnobManager;
struct Knob;

class WindowManager;
class FigureRegistry;
struct WindowUIContext;
class TabDragController;

class ImageExporter;
class SvgExporter;
class VideoExporter;

struct Color;
struct Rect;

struct AxisStyle;
struct SeriesStyle;
struct LegendConfig;
struct FigureStyle;

}   // namespace spectra
