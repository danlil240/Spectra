#pragma once

#include <cstdint>
#include <string>
#include <vector>

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
    HELLO           = 0x0001,
    WELCOME         = 0x0002,

    // Request/Response
    RESP_OK         = 0x0010,
    RESP_ERR        = 0x0011,

    // Control (Agent → Backend)
    REQ_CREATE_WINDOW  = 0x0100,
    REQ_CLOSE_WINDOW   = 0x0101,
    REQ_DETACH_FIGURE  = 0x0102,
    REQ_MOVE_FIGURE    = 0x0103,
    REQ_SNAPSHOT       = 0x0104,

    // Control (Backend → Agent)
    CMD_ASSIGN_FIGURES = 0x0200,
    CMD_REMOVE_FIGURE  = 0x0201,
    CMD_SET_ACTIVE     = 0x0202,
    CMD_CLOSE_WINDOW   = 0x0203,

    // State sync
    STATE_SNAPSHOT  = 0x0300,
    STATE_DIFF      = 0x0301,
    ACK_STATE       = 0x0302,

    // Events (Agent → Backend)
    EVT_INPUT       = 0x0400,
    EVT_WINDOW      = 0x0401,
    EVT_TAB_DRAG    = 0x0402,
    EVT_HEARTBEAT   = 0x0403,
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

static constexpr uint8_t MAGIC_0 = 0x53;  // 'S'
static constexpr uint8_t MAGIC_1 = 0x50;  // 'P'
static constexpr size_t  HEADER_SIZE = 40;
static constexpr size_t  MAX_PAYLOAD_SIZE = 16 * 1024 * 1024;  // 16 MiB

struct MessageHeader
{
    MessageType type       = MessageType::HELLO;
    uint32_t    payload_len = 0;
    uint64_t    seq         = 0;
    RequestId   request_id  = INVALID_REQUEST;
    SessionId   session_id  = INVALID_SESSION;
    WindowId    window_id   = INVALID_WINDOW;
};

struct Message
{
    MessageHeader header;
    std::vector<uint8_t> payload;
};

// ─── Handshake payloads ──────────────────────────────────────────────────────

static constexpr uint16_t PROTOCOL_MAJOR = 1;
static constexpr uint16_t PROTOCOL_MINOR = 0;

struct HelloPayload
{
    uint16_t protocol_major = PROTOCOL_MAJOR;
    uint16_t protocol_minor = PROTOCOL_MINOR;
    std::string agent_build;
    uint32_t capabilities = 0;  // bitmask, reserved for future use
};

struct WelcomePayload
{
    SessionId session_id = INVALID_SESSION;
    WindowId  window_id  = INVALID_WINDOW;
    ProcessId process_id = 0;
    uint32_t  heartbeat_ms = 5000;
    std::string mode;  // "inproc" or "multiproc"
};

// ─── Response payloads ───────────────────────────────────────────────────────

struct RespOkPayload
{
    RequestId request_id = INVALID_REQUEST;
};

struct RespErrPayload
{
    RequestId request_id = INVALID_REQUEST;
    uint32_t  code = 0;
    std::string message;
};

// ─── Control payloads ────────────────────────────────────────────────────────

// Backend → Agent: assign figures to this window.
struct CmdAssignFiguresPayload
{
    WindowId window_id = INVALID_WINDOW;
    std::vector<uint64_t> figure_ids;
    uint64_t active_figure_id = 0;
};

// Agent → Backend: request a new window be spawned.
struct ReqCreateWindowPayload
{
    WindowId template_window_id = INVALID_WINDOW;  // optional: clone layout from this window
};

// Agent → Backend: request this window be closed.
struct ReqCloseWindowPayload
{
    WindowId window_id = INVALID_WINDOW;
    std::string reason;  // "user_close", "error", etc.
};

// Agent → Backend: detach a figure into a new window at the given screen position.
// Used for tab drag-and-drop across windows.
struct ReqDetachFigurePayload
{
    WindowId source_window_id = INVALID_WINDOW;
    uint64_t figure_id = 0;
    uint32_t width = 800;
    uint32_t height = 600;
    int32_t screen_x = 0;   // drop position (screen coordinates)
    int32_t screen_y = 0;
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
    WindowId window_id = INVALID_WINDOW;
    std::string reason;
};

// ─── State sync payloads ────────────────────────────────────────────────────

// Serialized axis state within a figure snapshot.
struct SnapshotAxisState
{
    float x_min = 0.0f, x_max = 1.0f;
    float y_min = 0.0f, y_max = 1.0f;
    bool grid_visible = true;
    std::string x_label;
    std::string y_label;
    std::string title;
};

// Serialized series metadata within a figure snapshot.
struct SnapshotSeriesState
{
    std::string name;
    std::string type;  // "line", "scatter", "line3d", "scatter3d", "surface", "mesh"
    float color_r = 1.0f, color_g = 1.0f, color_b = 1.0f, color_a = 1.0f;
    float line_width = 2.0f;
    float marker_size = 6.0f;
    bool visible = true;
    float opacity = 1.0f;
    uint32_t point_count = 0;
    // Raw data (x, y interleaved floats for 2D; x, y, z for 3D)
    std::vector<float> data;
};

// A single figure's full state.
struct SnapshotFigureState
{
    uint64_t figure_id = 0;
    std::string title;
    uint32_t width = 1280;
    uint32_t height = 720;
    int32_t grid_rows = 1;
    int32_t grid_cols = 1;
    std::vector<SnapshotAxisState> axes;
    std::vector<SnapshotSeriesState> series;
};

// Backend → Agent: full state snapshot (sent on connect or resync).
struct StateSnapshotPayload
{
    Revision revision = 0;
    SessionId session_id = INVALID_SESSION;
    std::vector<SnapshotFigureState> figures;
};

// A single property change operation within a state diff.
struct DiffOp
{
    enum class Type : uint8_t
    {
        SET_AXIS_LIMITS   = 1,
        SET_SERIES_COLOR  = 2,
        SET_SERIES_VISIBLE = 3,
        SET_FIGURE_TITLE  = 4,
        SET_GRID_VISIBLE  = 5,
        SET_LINE_WIDTH    = 6,
        SET_MARKER_SIZE   = 7,
        SET_OPACITY       = 8,
        ADD_FIGURE        = 10,
        REMOVE_FIGURE     = 11,
        SET_SERIES_DATA   = 12,
    };

    Type type = Type::SET_AXIS_LIMITS;
    uint64_t figure_id = 0;
    uint32_t axes_index = 0;
    uint32_t series_index = 0;
    // Scalar values (interpretation depends on type)
    float f1 = 0.0f, f2 = 0.0f, f3 = 0.0f, f4 = 0.0f;
    bool bool_val = false;
    std::string str_val;
    // Bulk data (for SET_SERIES_DATA)
    std::vector<float> data;
};

// Backend → Agent: incremental state diff.
struct StateDiffPayload
{
    Revision base_revision = 0;
    Revision new_revision = 0;
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

    WindowId window_id = INVALID_WINDOW;
    InputType input_type = InputType::KEY_PRESS;
    int32_t key = 0;       // GLFW key code or mouse button
    int32_t mods = 0;      // modifier bits
    double x = 0.0;        // cursor x or scroll x
    double y = 0.0;        // cursor y or scroll y
    uint64_t figure_id = 0;  // which figure the input targets
    uint32_t axes_index = 0; // which axes within the figure
};

}  // namespace spectra::ipc
