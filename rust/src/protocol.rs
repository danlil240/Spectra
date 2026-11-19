//! Wire constants of the Spectra IPC protocol.
//!
//! Mirrors `src/ipc/message.hpp` / `src/ipc/codec.hpp` on the C++ side and
//! `python/spectra/_protocol.py`. Every value here must match those files.

#![allow(dead_code)]

/// Two magic bytes at the start of every frame ('S' 'P').
pub const MAGIC: [u8; 2] = [0x53, 0x50];
/// Size of the fixed frame header in bytes.
pub const HEADER_SIZE: usize = 40;
/// Largest payload the backend accepts in one frame.
pub const MAX_PAYLOAD_SIZE: usize = 256 * 1024 * 1024;

pub const PROTOCOL_MAJOR: u16 = 1;
pub const PROTOCOL_MINOR: u16 = 1;

/// Default window size (include/spectra/window_defaults.hpp).
pub const DEFAULT_WINDOW_WIDTH: u32 = 1500;
pub const DEFAULT_WINDOW_HEIGHT: u32 = 900;

pub const CAPABILITY_FLATBUFFERS: u32 = 0x0001;

/// First payload byte selecting the encoding.
pub const PAYLOAD_FORMAT_TLV: u8 = 0x00;
pub const PAYLOAD_FORMAT_FLATBUFFERS: u8 = 0x01;

// ─── Message types ───────────────────────────────────────────────────────────

pub const HELLO: u16 = 0x0001;
pub const WELCOME: u16 = 0x0002;

pub const RESP_OK: u16 = 0x0010;
pub const RESP_ERR: u16 = 0x0011;

pub const STATE_SNAPSHOT: u16 = 0x0300;
pub const STATE_DIFF: u16 = 0x0301;

pub const REQ_CREATE_FIGURE: u16 = 0x0500;
pub const REQ_DESTROY_FIGURE: u16 = 0x0501;
pub const REQ_CREATE_AXES: u16 = 0x0502;
pub const REQ_ADD_SERIES: u16 = 0x0503;
pub const REQ_REMOVE_SERIES: u16 = 0x0504;
pub const REQ_SET_DATA: u16 = 0x0505;
pub const REQ_UPDATE_PROPERTY: u16 = 0x0506;
pub const REQ_SHOW: u16 = 0x0507;
pub const REQ_CLOSE_FIGURE: u16 = 0x0508;
pub const REQ_APPEND_DATA: u16 = 0x0509;
pub const REQ_UPDATE_BATCH: u16 = 0x050A;

pub const REQ_GET_SNAPSHOT: u16 = 0x0510;
pub const REQ_LIST_FIGURES: u16 = 0x0511;
pub const REQ_EXPORT_FIGURE: u16 = 0x0512;

pub const REQ_ANIM_START: u16 = 0x0520;
pub const REQ_ANIM_STOP: u16 = 0x0521;

pub const REQ_RECONNECT: u16 = 0x0530;
pub const REQ_DISCONNECT: u16 = 0x0531;

pub const RESP_FIGURE_CREATED: u16 = 0x0540;
pub const RESP_AXES_CREATED: u16 = 0x0541;
pub const RESP_SERIES_ADDED: u16 = 0x0542;
pub const RESP_SNAPSHOT: u16 = 0x0543;
pub const RESP_FIGURE_LIST: u16 = 0x0544;

pub const EVT_WINDOW_CLOSED: u16 = 0x0550;
pub const EVT_FIGURE_DESTROYED: u16 = 0x0552;
pub const ANIM_TICK: u16 = 0x0560;
pub const BLOB_RELEASE: u16 = 0x0570;

pub const REQ_DECLARE_TOPIC: u16 = 0x0600;
pub const REQ_PUBLISH_TOPIC_SAMPLES: u16 = 0x0601;
pub const REQ_SUBSCRIBE_TOPIC: u16 = 0x0602;
pub const REQ_UNSUBSCRIBE_TOPIC: u16 = 0x0603;
pub const REQ_LIST_TOPICS: u16 = 0x0604;
pub const RESP_TOPIC_LIST: u16 = 0x0640;
pub const RESP_SUBSCRIBE_TOPIC: u16 = 0x0641;
pub const EVT_TOPIC_LIST_CHANGED: u16 = 0x0650;

// ─── TLV tags (legacy codec; still used for animation and chunked data) ─────

pub const TAG_REQUEST_ID: u8 = 0x30;
pub const TAG_ERROR_CODE: u8 = 0x31;
pub const TAG_ERROR_MESSAGE: u8 = 0x32;
pub const TAG_FIGURE_ID: u8 = 0x40;
pub const TAG_WINDOW_ID: u8 = 0x21;
pub const TAG_REASON: u8 = 0x44;
pub const TAG_SERIES_INDEX: u8 = 0x82;
pub const TAG_F1: u8 = 0x83;
pub const TAG_F2: u8 = 0x84;
pub const TAG_F3: u8 = 0x85;
pub const TAG_DTYPE: u8 = 0xA3;
pub const TAG_BLOB_INLINE: u8 = 0xB0;
pub const TAG_BLOB_SHM: u8 = 0xB2;
pub const TAG_CHUNK_INDEX: u8 = 0xB3;
pub const TAG_CHUNK_COUNT: u8 = 0xB4;
pub const TAG_TOTAL_COUNT: u8 = 0xB5;

/// Raw float data above this many bytes is split into chunked `REQ_SET_DATA`
/// messages (128 MiB, as in the Python client).
pub const CHUNK_SIZE: usize = 128 * 1024 * 1024;

/// `series_index` of an "annotation" update that appends a new annotation.
pub const ANNOTATION_APPEND: u32 = 0xFFFF_FFFF;
/// `series_index` of a topic subscription that creates a new series.
pub const SUBSCRIBE_NEW_SERIES: u32 = 0xFFFF_FFFF;
/// Separator between bar category names (`ipc::CATEGORY_SEPARATOR`).
pub const CATEGORY_SEPARATOR: char = '\u{1f}';
/// First float of the optional fill-style trailer of bar/histogram/box/violin payloads.
pub const STAT_STYLE_TAG: f32 = 21332.0;

/// Human-readable name of a request type, for error messages.
pub fn message_name(msg_type: u16) -> &'static str {
    match msg_type {
        REQ_CREATE_FIGURE => "REQ_CREATE_FIGURE",
        REQ_DESTROY_FIGURE => "REQ_DESTROY_FIGURE",
        REQ_CREATE_AXES => "REQ_CREATE_AXES",
        REQ_ADD_SERIES => "REQ_ADD_SERIES",
        REQ_REMOVE_SERIES => "REQ_REMOVE_SERIES",
        REQ_SET_DATA => "REQ_SET_DATA",
        REQ_UPDATE_PROPERTY => "REQ_UPDATE_PROPERTY",
        REQ_SHOW => "REQ_SHOW",
        REQ_CLOSE_FIGURE => "REQ_CLOSE_FIGURE",
        REQ_APPEND_DATA => "REQ_APPEND_DATA",
        REQ_UPDATE_BATCH => "REQ_UPDATE_BATCH",
        REQ_GET_SNAPSHOT => "REQ_GET_SNAPSHOT",
        REQ_LIST_FIGURES => "REQ_LIST_FIGURES",
        REQ_EXPORT_FIGURE => "REQ_EXPORT_FIGURE",
        REQ_ANIM_START => "REQ_ANIM_START",
        REQ_ANIM_STOP => "REQ_ANIM_STOP",
        REQ_RECONNECT => "REQ_RECONNECT",
        REQ_DECLARE_TOPIC => "REQ_DECLARE_TOPIC",
        REQ_PUBLISH_TOPIC_SAMPLES => "REQ_PUBLISH_TOPIC_SAMPLES",
        REQ_SUBSCRIBE_TOPIC => "REQ_SUBSCRIBE_TOPIC",
        REQ_UNSUBSCRIBE_TOPIC => "REQ_UNSUBSCRIBE_TOPIC",
        REQ_LIST_TOPICS => "REQ_LIST_TOPICS",
        _ => "request",
    }
}
