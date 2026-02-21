"""IPC protocol constants mirroring src/ipc/message.hpp and codec.hpp."""

import struct

# ─── Wire format ──────────────────────────────────────────────────────────────

MAGIC = b"\x53\x50"  # 'S' 'P'
HEADER_SIZE = 40
MAX_PAYLOAD_SIZE = 256 * 1024 * 1024  # 256 MiB

PROTOCOL_MAJOR = 1
PROTOCOL_MINOR = 0

# Header struct: magic(2) + type(u16) + payload_len(u32) + seq(u64)
#                + request_id(u64) + session_id(u64) + window_id(u64)
HEADER_FMT = "<2sHI QQ QQ"
assert struct.calcsize(HEADER_FMT) == HEADER_SIZE

# ─── Message types ────────────────────────────────────────────────────────────

# Handshake
HELLO = 0x0001
WELCOME = 0x0002

# Request/Response
RESP_OK = 0x0010
RESP_ERR = 0x0011

# State sync
STATE_SNAPSHOT = 0x0300
STATE_DIFF = 0x0301
ACK_STATE = 0x0302

# Python → Backend: figure/series lifecycle
REQ_CREATE_FIGURE = 0x0500
REQ_DESTROY_FIGURE = 0x0501
REQ_CREATE_AXES = 0x0502
REQ_ADD_SERIES = 0x0503
REQ_REMOVE_SERIES = 0x0504
REQ_SET_DATA = 0x0505
REQ_UPDATE_PROPERTY = 0x0506
REQ_SHOW = 0x0507
REQ_CLOSE_FIGURE = 0x0508
REQ_APPEND_DATA = 0x0509
REQ_UPDATE_BATCH = 0x050A

# Python → Backend: queries
REQ_GET_SNAPSHOT = 0x0510
REQ_LIST_FIGURES = 0x0511

# Python → Backend: animation
REQ_ANIM_START = 0x0520
REQ_ANIM_STOP = 0x0521

# Python → Backend: session
REQ_RECONNECT = 0x0530
REQ_DISCONNECT = 0x0531

# Backend → Python: responses
RESP_FIGURE_CREATED = 0x0540
RESP_AXES_CREATED = 0x0541
RESP_SERIES_ADDED = 0x0542
RESP_SNAPSHOT = 0x0543
RESP_FIGURE_LIST = 0x0544

# Backend → Python: events
EVT_WINDOW_CLOSED = 0x0550
EVT_FIGURE_DESTROYED = 0x0552
ANIM_TICK = 0x0560
BLOB_RELEASE = 0x0570

# ─── TLV tags (must match codec.hpp exactly) ──────────────────────────────────

# Handshake tags
TAG_PROTOCOL_MAJOR = 0x10
TAG_PROTOCOL_MINOR = 0x11
TAG_AGENT_BUILD = 0x12
TAG_CAPABILITIES = 0x13
TAG_CLIENT_TYPE = 0x14

# Welcome tags
TAG_SESSION_ID = 0x20
TAG_WINDOW_ID = 0x21
TAG_PROCESS_ID = 0x22
TAG_HEARTBEAT_MS = 0x23
TAG_MODE = 0x24

# Response tags
TAG_REQUEST_ID = 0x30
TAG_ERROR_CODE = 0x31
TAG_ERROR_MESSAGE = 0x32

# Control tags
TAG_FIGURE_ID = 0x40
TAG_FIGURE_IDS = 0x41
TAG_ACTIVE_FIGURE = 0x42
TAG_FIGURE_COUNT = 0x45
TAG_REASON = 0x44

# State sync tags
TAG_REVISION = 0x50
TAG_TITLE = 0x60
TAG_WIDTH = 0x61
TAG_HEIGHT = 0x62
TAG_GRID_ROWS = 0x63
TAG_GRID_COLS = 0x64
TAG_X_MIN = 0x65
TAG_X_MAX = 0x66
TAG_Y_MIN = 0x67
TAG_Y_MAX = 0x68
TAG_GRID_VISIBLE = 0x69
TAG_SERIES_TYPE = 0x6D
TAG_POINT_COUNT = 0x76
TAG_SERIES_DATA = 0x77

# DiffOp tags
TAG_OP_TYPE = 0x80
TAG_AXES_INDEX = 0x81
TAG_SERIES_INDEX = 0x82
TAG_F1 = 0x83
TAG_F2 = 0x84
TAG_F3 = 0x85
TAG_F4 = 0x86
TAG_BOOL_VAL = 0x87
TAG_STR_VAL = 0x88
TAG_OP_DATA = 0x89

# Python-specific tags
TAG_GRID_INDEX = 0xA1
TAG_SERIES_LABEL = 0xA2
TAG_DTYPE = 0xA3
TAG_PROPERTY_NAME = 0xA4
TAG_SESSION_TOKEN = 0xA5
TAG_IS_3D = 0xA6
TAG_BLOB_INLINE = 0xB0
TAG_BATCH_ITEM = 0xB1
TAG_BLOB_SHM = 0xB2
TAG_CHUNK_INDEX = 0xB3
TAG_CHUNK_COUNT = 0xB4
TAG_TOTAL_COUNT = 0xB5

# Chunked transfer threshold (128 MiB of raw float data)
CHUNK_SIZE = 128 * 1024 * 1024
