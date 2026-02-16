#pragma once

#include <cstddef>

namespace spectra
{

// Stable figure identifier. Currently a size_t (positional index) as a
// migration shim.  Agent C (full) will upgrade this to uint64_t backed by
// FigureRegistry with monotonic IDs.
using FigureId = size_t;

// Sentinel value for "no figure" / invalid figure id.
inline constexpr FigureId INVALID_FIGURE_ID = static_cast<FigureId>(-1);

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
class Timeline;
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

class ImageExporter;
class SvgExporter;
class VideoExporter;

struct Color;
struct Rect;

struct AxisStyle;
struct SeriesStyle;
struct LegendConfig;
struct FigureStyle;

}  // namespace spectra
