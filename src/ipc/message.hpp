#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <spectra/window_defaults.hpp>

namespace spectra::ipc
{

// ─── IPC ID types ────────────────────────────────────────────────────────────
using SessionId = uint64_t;
using WindowId  = uint64_t;
using ProcessId = uint64_t;
using RequestId = uint64_t;
using Revision  = uint64_t;

static constexpr SessionId INVALID_SESSION = 0;
static constexpr WindowId  INVALID_WINDOW  = 0;
static constexpr RequestId INVALID_REQUEST = 0;

// ─── Message types ───────────────────────────────────────────────────────────
enum class MessageType : uint16_t
{
    // Handshake
    HELLO   = 0x0001,
    WELCOME = 0x0002,

    // Request/Response
    RESP_OK  = 0x0010,
    RESP_ERR = 0x0011,

    // Control (Agent → Backend)
    REQ_CREATE_WINDOW = 0x0100,
    REQ_CLOSE_WINDOW  = 0x0101,
    REQ_DETACH_FIGURE = 0x0102,
    REQ_MOVE_FIGURE   = 0x0103,
    REQ_SNAPSHOT      = 0x0104,

    // Control (Backend → Agent)
    CMD_ASSIGN_FIGURES = 0x0200,
    CMD_REMOVE_FIGURE  = 0x0201,
    CMD_SET_ACTIVE     = 0x0202,
    CMD_CLOSE_WINDOW   = 0x0203,

    // State sync
    STATE_SNAPSHOT = 0x0300,
    STATE_DIFF     = 0x0301,
    ACK_STATE      = 0x0302,

    // Events (Agent → Backend)
    EVT_INPUT     = 0x0400,
    EVT_WINDOW    = 0x0401,
    EVT_TAB_DRAG  = 0x0402,
    EVT_HEARTBEAT = 0x0403,

    // Python → Backend: figure/series lifecycle
    REQ_CREATE_FIGURE   = 0x0500,
    REQ_DESTROY_FIGURE  = 0x0501,
    REQ_CREATE_AXES     = 0x0502,
    REQ_ADD_SERIES      = 0x0503,
    REQ_REMOVE_SERIES   = 0x0504,
    REQ_SET_DATA        = 0x0505,
    REQ_UPDATE_PROPERTY = 0x0506,
    REQ_SHOW            = 0x0507,
    REQ_CLOSE_FIGURE    = 0x0508,
    REQ_APPEND_DATA     = 0x0509,
    REQ_UPDATE_BATCH    = 0x050A,   // Multiple property changes in one message

    // Python → Backend: queries
    REQ_GET_SNAPSHOT  = 0x0510,
    REQ_LIST_FIGURES  = 0x0511,
    REQ_EXPORT_FIGURE = 0x0512,   // render a figure to a PNG/SVG file (RESP_OK / RESP_ERR)

    // Python → Backend: animation
    REQ_ANIM_START = 0x0520,
    REQ_ANIM_STOP  = 0x0521,

    // Python → Backend: session
    REQ_RECONNECT  = 0x0530,
    REQ_DISCONNECT = 0x0531,

    // Backend → Python: responses
    RESP_FIGURE_CREATED = 0x0540,
    RESP_AXES_CREATED   = 0x0541,
    RESP_SERIES_ADDED   = 0x0542,
    RESP_SNAPSHOT       = 0x0543,
    RESP_FIGURE_LIST    = 0x0544,

    // Backend → Python: events
    EVT_WINDOW_CLOSED    = 0x0550,
    EVT_FIGURE_DESTROYED = 0x0552,

    // ─── Topics (pub/sub) ───────────────────────────────────────────────────
    REQ_DECLARE_TOPIC         = 0x0600,
    REQ_PUBLISH_TOPIC_SAMPLES = 0x0601,
    REQ_SUBSCRIBE_TOPIC       = 0x0602,
    REQ_UNSUBSCRIBE_TOPIC     = 0x0603,
    REQ_LIST_TOPICS           = 0x0604,
    RESP_TOPIC_LIST           = 0x0640,
    RESP_SUBSCRIBE_TOPIC      = 0x0641,
    EVT_TOPIC_LIST_CHANGED    = 0x0650,
};

// ─── Message envelope ────────────────────────────────────────────────────────
// Wire format: [Header (fixed 40 bytes)] [payload (variable)]
//
// Header layout:
//   bytes 0-1:   magic (0x53, 0x50 = "SP")
//   bytes 2-3:   message type (uint16_t LE)
//   bytes 4-7:   payload length (uint32_t LE)
//   bytes 8-15:  sequence number (uint64_t LE)
//   bytes 16-23: request_id (uint64_t LE)
//   bytes 24-31: session_id (uint64_t LE)
//   bytes 32-39: window_id (uint64_t LE)

static constexpr uint8_t MAGIC_0          = 0x53;   // 'S'
static constexpr uint8_t MAGIC_1          = 0x50;   // 'P'
static constexpr size_t  HEADER_SIZE      = 40;
static constexpr size_t  MAX_PAYLOAD_SIZE = 256 * 1024 * 1024;   // 256 MiB

struct MessageHeader
{
    MessageType type        = MessageType::HELLO;
    uint32_t    payload_len = 0;
    uint64_t    seq         = 0;
    RequestId   request_id  = INVALID_REQUEST;
    SessionId   session_id  = INVALID_SESSION;
    WindowId    window_id   = INVALID_WINDOW;
};

struct Message
{
    MessageHeader        header;
    std::vector<uint8_t> payload;
};

// ─── Handshake payloads ──────────────────────────────────────────────────────

static constexpr uint16_t PROTOCOL_MAJOR = 1;
static constexpr uint16_t PROTOCOL_MINOR = 1;

// Capability bits for HELLO handshake
static constexpr uint32_t CAPABILITY_FLATBUFFERS = 0x0001;

struct HelloPayload
{
    uint16_t    protocol_major = PROTOCOL_MAJOR;
    uint16_t    protocol_minor = PROTOCOL_MINOR;
    std::string agent_build;
    uint32_t    capabilities = 0;   // bitmask: CAPABILITY_FLATBUFFERS etc.
    std::string client_type;        // "python" or "agent" (empty = legacy agent)
};

struct WelcomePayload
{
    SessionId   session_id   = INVALID_SESSION;
    WindowId    window_id    = INVALID_WINDOW;
    ProcessId   process_id   = 0;
    uint32_t    heartbeat_ms = 5000;
    std::string mode;   // "inproc" or "multiproc"
};

// ─── Response payloads ───────────────────────────────────────────────────────

struct RespOkPayload
{
    RequestId request_id = INVALID_REQUEST;
};

struct RespErrPayload
{
    RequestId   request_id = INVALID_REQUEST;
    uint32_t    code       = 0;
    std::string message;
};

// ─── Control payloads ────────────────────────────────────────────────────────

// Backend → Agent: assign figures to this window.
struct CmdAssignFiguresPayload
{
    WindowId              window_id = INVALID_WINDOW;
    std::vector<uint64_t> figure_ids;
    uint64_t              active_figure_id = 0;
};

// Agent → Backend: request a new window be spawned.
struct ReqCreateWindowPayload
{
    WindowId template_window_id = INVALID_WINDOW;   // optional: clone layout from this window
};

// Agent → Backend: request this window be closed.
struct ReqCloseWindowPayload
{
    WindowId    window_id = INVALID_WINDOW;
    std::string reason;   // "user_close", "error", etc.
};

// Agent → Backend: detach a figure into a new window at the given screen position.
// Used for tab drag-and-drop across windows.
struct ReqDetachFigurePayload
{
    WindowId source_window_id = INVALID_WINDOW;
    uint64_t figure_id        = 0;
    uint32_t width            = 800;
    uint32_t height           = 600;
    int32_t  screen_x         = 0;   // drop position (screen coordinates)
    int32_t  screen_y         = 0;
};

// Backend → Agent: remove a figure from this window.
struct CmdRemoveFigurePayload
{
    WindowId window_id = INVALID_WINDOW;
    uint64_t figure_id = 0;
};

// Backend → Agent: set the active figure in this window.
struct CmdSetActivePayload
{
    WindowId window_id = INVALID_WINDOW;
    uint64_t figure_id = 0;
};

// Backend → Agent: close this window.
struct CmdCloseWindowPayload
{
    WindowId    window_id = INVALID_WINDOW;
    std::string reason;
};

// ─── State sync payloads ────────────────────────────────────────────────────

// Serialized 2D text annotation (spectra::TextAnnotation) within an axis state.
struct SnapshotAnnotationState
{
    float       x = 0.0f, y = 0.0f;   // anchor, data coordinates
    std::string text;
    bool        has_color = false;   // false = theme text colour
    float       color_r = 0.0f, color_g = 0.0f, color_b = 0.0f, color_a = 1.0f;
    float       font_size = 0.0f;   // px; 0 = axis-label size
    uint8_t     halign    = 0;      // TextAnnotation::HAlign (0=Left,1=Center,2=Right)
    uint8_t     valign    = 2;      // TextAnnotation::VAlign (0=Top,1=Middle,2=Bottom)
    bool        has_arrow = false;
    float       arrow_x = 0.0f, arrow_y = 0.0f;
    bool        visible = true;
    // Background box, halo and rotation (TextAnnotation::bbox / halo /
    // rotation); carried as the optional tail of pack_annotation().
    bool                 has_bbox        = false;
    float                bbox_pad        = 4.0f;
    bool                 has_bbox_fill   = false;
    std::array<float, 4> bbox_fill       = {0.0f, 0.0f, 0.0f, 1.0f};
    bool                 has_bbox_edge   = false;
    std::array<float, 4> bbox_edge       = {0.0f, 0.0f, 0.0f, 1.0f};
    float                bbox_edge_width = 1.0f;
    bool                 halo            = false;
    bool                 has_halo_color  = false;
    std::array<float, 4> halo_color      = {0.0f, 0.0f, 0.0f, 1.0f};
    float                halo_width      = 2.0f;
    float                rotation        = 0.0f;   // degrees, counter-clockwise

    bool operator==(const SnapshotAnnotationState&) const = default;
};

// Serialized axis state within a figure snapshot.
struct SnapshotAxisState
{
    double      x_min = 0.0, x_max = 1.0;
    double      y_min = 0.0, y_max = 1.0;
    double      z_min = 0.0, z_max = 1.0;
    bool        grid_visible = true;
    bool        is_3d        = false;
    std::string x_label;
    std::string y_label;
    std::string title;
    // 2D text annotations (Axes::text / Axes::annotate), in draw order.
    std::vector<SnapshotAnnotationState> annotations;
    // 2D axis scales (spectra::ScaleType) and date-time tick mode.
    uint8_t x_scale    = 0;
    uint8_t y_scale    = 0;
    bool    x_datetime = false;
    bool    y_datetime = false;
    // How bar series share a position (spectra::BarMode).
    uint8_t bar_mode = 0;
    // 3D light (Axes3D::light_mode / fog / light_angles).
    uint8_t light_mode       = 0;   // spectra::LightMode
    float   fog              = 0.0f;
    bool    light_angles_set = false;
    float   light_azimuth    = 0.0f;
    float   light_elevation  = 0.0f;
    // Colorbar for the axes' colormapped series (AxesBase::show_colorbar).
    bool colorbar_visible = false;
    // Fixed ticks (Axes::xticks / yticks): when set, the positions and their
    // labels (empty = formatted values).
    bool                     xticks_set = false;
    bool                     yticks_set = false;
    std::vector<double>      xtick_positions;
    std::vector<double>      ytick_positions;
    std::vector<std::string> xtick_labels;
    std::vector<std::string> ytick_labels;
    // twinx(): index of the 2D axes this one is the twin of (it shares that
    // axes' plot area and x axis and takes no grid slot), -1 = a subplot.
    int32_t twin_of = -1;
};

// Subplot grid slot (0-based) of axes @p index: twins take no slot, so it is
// the number of non-twin axes before it.  For a twin, its host's slot.
inline size_t axes_grid_slot(const std::vector<SnapshotAxisState>& axes, size_t index)
{
    if (index < axes.size() && axes[index].twin_of >= 0
        && static_cast<size_t>(axes[index].twin_of) < index)
        return axes_grid_slot(axes, static_cast<size_t>(axes[index].twin_of));
    size_t slot = 0;
    for (size_t i = 0; i < index && i < axes.size(); ++i)
        if (axes[i].twin_of < 0)
            ++slot;
    return slot;
}

// Index of the twin of 2D axes @p host, or -1.
inline int32_t twin_axes_index(const std::vector<SnapshotAxisState>& axes, size_t host)
{
    for (size_t i = 0; i < axes.size(); ++i)
        if (axes[i].twin_of >= 0 && static_cast<size_t>(axes[i].twin_of) == host)
            return static_cast<int32_t>(i);
    return -1;
}

// SnapshotSeriesState::line_style / marker_style value meaning "not set".
inline constexpr uint8_t SERIES_STYLE_UNSET = 0xFF;

// Serialized series metadata within a figure snapshot.
struct SnapshotSeriesState
{
    std::string name;
    std::string type;   // series type name, see ReqAddSeriesPayload::series_type
    float       color_r = 1.0f, color_g = 1.0f, color_b = 1.0f, color_a = 1.0f;
    float       line_width  = 2.0f;
    float       marker_size = 6.0f;
    bool        visible     = true;
    float       opacity     = 1.0f;
    uint32_t    point_count = 0;
    uint32_t    axes_index  = 0;   // Which axes this series belongs to
    // Raw data (x, y interleaved floats for 2D; x, y, z for 3D)
    std::vector<float> data;
    // Data-palette slot for an auto-coloured series, -1 for an explicit colour.
    // When >= 0 the window resolves the colour from its own theme palette;
    // color_r..a then only carry the sender's resolved colour.
    int32_t color_index = -1;
    // MATLAB format-string colour letter (r g b c m y k w) of an explicitly
    // lettered series, 0 otherwise.  The window re-resolves the letter's tuned
    // shade for its own theme; color_r..a carry the sender's shade.
    uint8_t color_letter = 0;
    // Double-precision data origins (Series::x_offset()/y_offset()): logical
    // value = origin + data[i].  Non-zero when the sender stored large values
    // (e.g. epoch timestamps) as float offsets from a nearby origin.
    double x_origin = 0.0;
    double y_origin = 0.0;
    // Bar series: category names (BarSeries::categories()), empty otherwise.
    std::vector<std::string> categories;
    // spectra::LineStyle / spectra::MarkerStyle set by the sender (e.g. from a
    // format string "r--o"), or SERIES_STYLE_UNSET to keep the series type's
    // default (solid lines, circle scatter markers).
    uint8_t line_style   = SERIES_STYLE_UNSET;
    uint8_t marker_style = SERIES_STYLE_UNSET;
    // Line series: spectra::DrawStyle and spectra::DenseLineMode (0 = default).
    uint8_t draw_style = 0;
    uint8_t dense_mode = 0;
    // 3D styles: depth shade (SERIES_STYLE_UNSET = the type's default, else
    // 0 / 1), surface edges over faces, surface floor contour level count
    // (0 = no contour projection), surface edge colour (RGBA, defaults to
    // SurfaceSeries' default).
    uint8_t              depth_shade    = SERIES_STYLE_UNSET;
    bool                 show_edges     = false;
    int32_t              contour_levels = 0;
    std::array<float, 4> edge_color     = {0.0f, 0.0f, 0.0f, 0.35f};
    // Colormap of a surface / colormapped scatter (2D or 3D), see
    // colormap_state.hpp: spectra::ColormapType, or SERIES_STYLE_UNSET to keep
    // the series type's default; reversed ("_r"); an explicit value range
    // (otherwise the data range); for ColormapType::Custom the user colours as
    // RGBA floats, spread evenly over [0, 1] (Colormap::from_colors).
    uint8_t            colormap           = SERIES_STYLE_UNSET;
    bool               colormap_reversed  = false;
    bool               colormap_range_set = false;
    float              colormap_min       = 0.0f;
    float              colormap_max       = 1.0f;
    std::vector<float> colormap_colors;
    // Per-point values mapped through the colormap (ScatterSeries /
    // ScatterSeries3D::color_values()), empty when the series has none.
    std::vector<float> color_values;
};

// A single figure's full state.
struct SnapshotFigureState
{
    uint64_t    figure_id = 0;
    std::string title;
    uint32_t    width           = DEFAULT_WINDOW_WIDTH;
    uint32_t    height          = DEFAULT_WINDOW_HEIGHT;
    int32_t     grid_rows       = 1;
    int32_t     grid_cols       = 1;
    uint32_t    window_group    = 0;      // Figures with the same non-zero group share one window
    float       live_fps        = 0.0f;   // >0 = Python live-streaming at this FPS
    uint8_t     legend_position = 5;      // spectra::LegendPosition (5 = Best)
    // Figure layout: suptitle, FigureStyle::constrained_layout and the
    // spectra::ShareAxes modes of Figure::share_x() / share_y().
    std::string                      suptitle;
    uint8_t                          constrained_layout = 1;
    uint8_t                          share_x            = 0;
    uint8_t                          share_y            = 0;
    std::string                      style;   // built-in plot style name (StyleSheet), empty = none
    std::vector<SnapshotAxisState>   axes;
    std::vector<SnapshotSeriesState> series;
};

// Serialized knob definition within a state snapshot.
struct SnapshotKnobState
{
    std::string              name;
    uint8_t                  type    = 0;   // 0=Float, 1=Int, 2=Bool, 3=Choice
    float                    value   = 0.0f;
    float                    min_val = 0.0f;
    float                    max_val = 1.0f;
    float                    step    = 0.0f;
    std::vector<std::string> choices;   // only for type==Choice
};

// Backend → Agent: full state snapshot (sent on connect or resync).
struct StateSnapshotPayload
{
    Revision                         revision   = 0;
    SessionId                        session_id = INVALID_SESSION;
    std::vector<SnapshotFigureState> figures;
    std::vector<SnapshotKnobState>   knobs;   // interactive parameter knobs
};

// SET_SERIES_COLOR ops whose str_val is "C<n>" (n >= 0) give the series
// data-palette slot n (an auto colour, resolved by each window's theme and
// palette); f1..f4 then carry a provisional RGB for older receivers.
inline std::string palette_slot_tag(int32_t slot)
{
    return "C" + std::to_string(slot);
}

// Slot of a "C<n>" tag, or -1 when @p s is not one.
inline int32_t parse_palette_slot_tag(const std::string& s)
{
    if (s.size() < 2 || s.size() > 6 || s[0] != 'C')
        return -1;
    int32_t v = 0;
    for (size_t i = 1; i < s.size(); ++i)
    {
        if (s[i] < '0' || s[i] > '9')
            return -1;
        v = v * 10 + (s[i] - '0');
    }
    return v;
}

// A single property change operation within a state diff.
struct DiffOp
{
    enum class Type : uint8_t
    {
        SET_AXIS_LIMITS    = 1,
        SET_SERIES_COLOR   = 2,
        SET_SERIES_VISIBLE = 3,
        SET_FIGURE_TITLE   = 4,
        SET_GRID_VISIBLE   = 5,
        SET_LINE_WIDTH     = 6,
        SET_MARKER_SIZE    = 7,
        SET_OPACITY        = 8,
        ADD_FIGURE         = 10,
        REMOVE_FIGURE      = 11,
        SET_SERIES_DATA    = 12,   // data=interleaved floats, f1=x origin, f2=y origin (2D)
        SET_AXIS_XLABEL    = 13,   // str_val=label text
        SET_AXIS_YLABEL    = 14,   // str_val=label text
        SET_AXIS_TITLE     = 15,   // str_val=title text
        SET_SERIES_LABEL   = 16,   // str_val=label text
        REMOVE_SERIES      = 17,   // figure_id + series_index
        SET_KNOB_VALUE     = 20,   // str_val=knob name, f1=new value
        SET_AXIS_ZLIMITS   = 21,   // f1=z_min, f2=z_max
        ADD_SERIES         = 22,   // str_val=type, axes_index=axes, series_index=new index
        ADD_AXES =
            23,   // axes_index=new index, bool_val=is_3d, f1=grid_rows, f2=grid_cols (0 = unknown)
        SET_LIVE_FPS = 25,   // f1=target fps, bool_val=enable/disable
        // Text annotations: str_val=text, data=pack_annotation() floats.
        ADD_ANNOTATION    = 26,   // append to axes_index
        SET_ANNOTATION    = 27,   // replace annotation series_index of axes_index
        CLEAR_ANNOTATIONS = 28,   // remove every annotation of axes_index
        // f1=x scale, f2=y scale (spectra::ScaleType), f3=x date-time, f4=y date-time (0/1),
        // series_index=bar mode (spectra::BarMode)
        SET_AXIS_SCALE        = 29,
        SET_LEGEND_POSITION   = 30,   // f1=spectra::LegendPosition
        SET_SERIES_CATEGORIES = 31,   // bar category names: f1=count, str_val=names joined by 0x1F
        SET_FIGURE_SUPTITLE   = 32,   // str_val=suptitle ("" removes it)
        // f1=constrained layout (0/1), f2=share x, f3=share y (spectra::ShareAxes)
        SET_FIGURE_LAYOUT       = 33,
        SET_SERIES_LINE_STYLE   = 40,   // f1=spectra::LineStyle
        SET_SERIES_MARKER_STYLE = 41,   // f1=spectra::MarkerStyle
        // Line draw style: f1=spectra::DrawStyle, f2=spectra::DenseLineMode.
        SET_SERIES_DRAW_STYLE = 42,
        SET_FIGURE_STYLE =
            43,   // (32-42: other open PRs) str_val=built-in plot style name (Figure::apply_style)
        // 3D series style: f1=depth shade (SERIES_STYLE_UNSET / 0 / 1),
        // f2=edges over faces (0/1), f3=floor contour levels (0 = off),
        // data=surface edge colour RGBA (4 floats; empty keeps the current one).
        SET_SERIES_STYLE_3D = 44,
        // 3D axes light: f1=spectra::LightMode, f2=fog (0..1), f3/f4=light
        // azimuth/elevation in degrees (applied when bool_val).
        SET_AXES_LIGHT_3D = 45,
        // Agent → app: timeline transport of the window's active animated
        // figure.  series_index=spectra::RemotePlaybackState, f1=playhead
        // seconds.  Not stored in the backend model; only forwarded.
        SET_ANIM_PLAYBACK = 46,
        // Series colormap (colormap_state.hpp): f1=spectra::ColormapType
        // (SERIES_STYLE_UNSET keeps the current one), bool_val=reversed,
        // f2=explicit range (0/1), f3/f4=range min/max, data=Custom colours
        // (RGBA floats).
        SET_SERIES_COLORMAP = 47,
        // Per-point colormap values of a 2D / 3D scatter (matplotlib's
        // scatter(c=values)): data=one value per point; empty clears them.
        SET_SERIES_COLOR_VALUES = 48,
        // Colorbar of an axes (AxesBase::show_colorbar): bool_val=shown.
        SET_COLORBAR_VISIBLE = 49,
        // Fixed ticks of a 2D axes (axes_format_state.hpp): f1=axis (0 x,
        // 1 y), bool_val=set (false = automatic ticks), f2=origin, data=
        // positions - origin, f3=label count, str_val=labels joined by
        // CATEGORY_SEPARATOR.
        SET_AXIS_TICKS = 50,
    };

    Type     type         = Type::SET_AXIS_LIMITS;
    uint64_t figure_id    = 0;
    uint32_t axes_index   = 0;
    uint32_t series_index = 0;
    // Scalar values (interpretation depends on type)
    double      f1 = 0.0, f2 = 0.0, f3 = 0.0, f4 = 0.0;
    bool        bool_val = false;
    std::string str_val;
    // Bulk data (for SET_SERIES_DATA)
    std::vector<float> data;
};

// Backend → Agent: incremental state diff.
struct StateDiffPayload
{
    Revision            base_revision = 0;
    Revision            new_revision  = 0;
    std::vector<DiffOp> ops;
};

// Agent → Backend: acknowledge state revision.
struct AckStatePayload
{
    Revision revision = 0;
};

// ─── Input event payloads ───────────────────────────────────────────────────

// Agent → Backend: input event from the window.
struct EvtInputPayload
{
    enum class InputType : uint8_t
    {
        KEY_PRESS    = 1,
        KEY_RELEASE  = 2,
        MOUSE_BUTTON = 3,
        MOUSE_MOVE   = 4,
        SCROLL       = 5,
    };

    WindowId  window_id  = INVALID_WINDOW;
    InputType input_type = InputType::KEY_PRESS;
    int32_t   key        = 0;     // GLFW key code or mouse button
    int32_t   mods       = 0;     // modifier bits
    double    x          = 0.0;   // cursor x or scroll x
    double    y          = 0.0;   // cursor y or scroll y
    uint64_t  figure_id  = 0;     // which figure the input targets
    uint32_t  axes_index = 0;     // which axes within the figure
};

// ─── Python request payloads ────────────────────────────────────────────────

// Python → Backend: create a new figure.
struct ReqCreateFigurePayload
{
    std::string title;
    uint32_t    width  = DEFAULT_WINDOW_WIDTH;
    uint32_t    height = DEFAULT_WINDOW_HEIGHT;
};

// Python → Backend: destroy a figure.
struct ReqDestroyFigurePayload
{
    uint64_t figure_id = 0;
};

// Python → Backend: create axes in a figure (subplot).
struct ReqCreateAxesPayload
{
    uint64_t figure_id  = 0;
    int32_t  grid_rows  = 1;
    int32_t  grid_cols  = 1;
    int32_t  grid_index = 1;   // 1-based MATLAB-style
    bool     is_3d      = false;
    // >= 0: create (or get) the twinx() twin of that 2D axes instead of a
    // subplot; the grid fields are then ignored.
    int32_t twin_of = -1;
};

// Python → Backend: add a series to a figure.
// series_type: "line", "scatter", "line3d", "scatter3d", "surface", "mesh", or
// one of the 2D statistical types "bar", "histogram", "band", "stem", "box",
// "violin", whose REQ_SET_DATA carries the payload documented in
// ui/app/figure_snapshot.hpp.
struct ReqAddSeriesPayload
{
    uint64_t    figure_id  = 0;
    uint32_t    axes_index = 0;
    std::string series_type;
    std::string label;
};

// Python → Backend: remove a series.
struct ReqRemoveSeriesPayload
{
    uint64_t figure_id    = 0;
    uint32_t series_index = 0;
};

// Python → Backend: set series data (bulk transfer).
struct ReqSetDataPayload
{
    uint64_t           figure_id    = 0;
    uint32_t           series_index = 0;
    uint8_t            dtype        = 0;   // 0=float32, 1=float64
    std::vector<float> data;               // interleaved x,y (or x,y,z for 3D)

    // Chunked transfer (TLV only; Python splits arrays > CHUNK_SIZE). A message
    // with chunk_count <= 1 is a complete, unchunked set_data. Otherwise `data`
    // holds chunk `chunk_index` of `chunk_count`, and `total_count` is the float
    // count of the fully reassembled array.
    uint32_t chunk_index = 0;
    uint32_t chunk_count = 1;
    uint32_t total_count = 0;

    bool is_chunked() const { return chunk_count > 1; }
};

// Python → Backend: update a property on figure/axes/series.
struct ReqUpdatePropertyPayload
{
    uint64_t    figure_id    = 0;
    uint32_t    axes_index   = 0;
    uint32_t    series_index = 0;
    std::string property;   // e.g. "color", "xlim", "title", "grid", "width", "opacity"
    double      f1 = 0.0, f2 = 0.0, f3 = 0.0, f4 = 0.0;
    bool        bool_val = false;
    std::string str_val;
    // Bulk float payload ("annotation": pack_annotation() layout).
    std::vector<float> data;
};

// Python → Backend: show a figure (spawn agent window).
// If window_id is non-zero/valid, the figure is added as a tab to the existing window
// instead of spawning a new agent.
struct ReqShowPayload
{
    uint64_t figure_id = 0;
    WindowId window_id = INVALID_WINDOW;   // target window for tab (0 = new window)
};

// Python → Backend: close a figure's window (keep figure in model).
struct ReqCloseFigurePayload
{
    uint64_t figure_id = 0;
};

// Python → Backend: append data to existing series (streaming).
struct ReqAppendDataPayload
{
    uint64_t           figure_id    = 0;
    uint32_t           series_index = 0;
    std::vector<float> data;   // interleaved x,y pairs to append
};

// Python → Backend: batch property updates (multiple REQ_UPDATE_PROPERTY in one message).
struct ReqUpdateBatchPayload
{
    std::vector<ReqUpdatePropertyPayload> updates;
};

// Python → Backend: reconnect to existing session.
struct ReqReconnectPayload
{
    SessionId   session_id = INVALID_SESSION;
    std::string session_token;
};

// Python → Backend: write a figure to an image file, rendered offscreen by the
// backend (no window needed).  The backend replies RESP_OK once the file is
// written, or RESP_ERR (400 bad request, 404 unknown figure, 500 write/render
// failure, 503 no render backend).
struct ReqExportFigurePayload
{
    uint64_t    figure_id = 0;
    std::string path;         // destination file; written atomically (temp file + rename)
    std::string format;       // "png" or "svg"; empty = from the path's extension (default png)
    uint32_t    width  = 0;   // PNG size in pixels; 0 = the figure's size
    uint32_t    height = 0;
    std::string theme;   // e.g. "light"; empty = the user's saved theme
};

// Python → Backend: start live streaming / animation at target FPS.
struct ReqAnimStartPayload
{
    uint64_t figure_id = 0;
    float    fps       = 60.0f;
    float    duration  = 0.0f;   // 0 = infinite
};

// ─── Python response payloads ───────────────────────────────────────────────

// Backend → Python: figure created.
struct RespFigureCreatedPayload
{
    RequestId request_id = INVALID_REQUEST;
    uint64_t  figure_id  = 0;
};

// Backend → Python: axes created.
struct RespAxesCreatedPayload
{
    RequestId request_id = INVALID_REQUEST;
    uint32_t  axes_index = 0;
};

// Backend → Python: series added.
struct RespSeriesAddedPayload
{
    RequestId request_id   = INVALID_REQUEST;
    uint32_t  series_index = 0;
};

// Backend → Python: list of figure IDs.
struct RespFigureListPayload
{
    RequestId             request_id = INVALID_REQUEST;
    std::vector<uint64_t> figure_ids;
};

// Backend → Python: window was closed by user.
struct EvtWindowClosedPayload
{
    uint64_t    figure_id = 0;
    WindowId    window_id = INVALID_WINDOW;
    std::string reason;
};

// Backend → Python: figure was destroyed (e.g. timeout).
struct EvtFigureDestroyedPayload
{
    uint64_t    figure_id = 0;
    std::string reason;
};

// ─── Topics (pub/sub) ───────────────────────────────────────────────────────

enum class TopicKind : uint8_t
{
    Scalar2D = 0,
    Scalar3D = 1,
};

// Client → Backend: declare a named topic.
struct ReqDeclareTopicPayload
{
    std::string name;
    TopicKind   kind = TopicKind::Scalar2D;
    std::string unit;
    uint32_t    ring_capacity = 4096;
};

// Client → Backend: push N samples to a topic.
// Sample layout (float64 interleaved):
//   Scalar2D: [x0,y0, x1,y1, ...]
//   Scalar3D: [x0,y0,z0, x1,y1,z1, ...]
struct ReqPublishTopicSamplesPayload
{
    std::string         name;
    std::vector<double> samples;
};

// Client → Backend: subscribe a series on (figure, axes) to a topic.
// If series_index == UINT32_MAX, the daemon auto-creates a new line series
// and returns its index in RespSubscribeTopicPayload.
struct ReqSubscribeTopicPayload
{
    std::string name;
    uint64_t    figure_id    = 0;
    uint32_t    axes_index   = 0;
    uint32_t    series_index = 0xFFFFFFFFu;
};

// Client → Backend: drop a topic subscription.
struct ReqUnsubscribeTopicPayload
{
    uint64_t figure_id    = 0;
    uint32_t axes_index   = 0;
    uint32_t series_index = 0;
};

// Client → Backend: list all known topics.
struct ReqListTopicsPayload
{
};

struct TopicInfoEntry
{
    std::string name;
    TopicKind   kind = TopicKind::Scalar2D;
    std::string unit;
    double      estimated_hz     = 0.0;
    uint64_t    total_samples    = 0;
    uint64_t    last_publish_ns  = 0;
    uint32_t    subscriber_count = 0;
    bool        publisher_online = false;
};

// Backend → Client: list of topics.
struct RespTopicListPayload
{
    RequestId                   request_id = INVALID_REQUEST;
    std::vector<TopicInfoEntry> topics;
};

// Backend → Client: subscribe acknowledgement (with resolved series_index).
struct RespSubscribeTopicPayload
{
    RequestId request_id   = INVALID_REQUEST;
    uint32_t  series_index = 0;
};

// Backend → Client: topic list changed (broadcast).
struct EvtTopicListChangedPayload
{
};

}   // namespace spectra::ipc
