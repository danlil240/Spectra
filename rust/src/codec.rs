//! Frame header plus payload encoders and decoders.
//!
//! Requests are encoded as FlatBuffers (first payload byte `0x01`), like the
//! Python client. The few messages that only exist in the legacy TLV codec
//! (animation control, chunked data, `BLOB_RELEASE`) use TLV.
//! Decoders accept both encodings.

use flatbuffers::{FlatBufferBuilder, Follow, Verifiable, VerifierOptions};

use crate::error::{Error, Result};
use crate::generated::fb;
use crate::protocol as p;

// ─── Header ──────────────────────────────────────────────────────────────────

/// The fixed 40-byte frame header.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub(crate) struct Header {
    pub msg_type: u16,
    pub payload_len: u32,
    pub seq: u64,
    pub request_id: u64,
    pub session_id: u64,
    pub window_id: u64,
}

impl Header {
    pub fn encode(&self) -> [u8; p::HEADER_SIZE] {
        let mut out = [0u8; p::HEADER_SIZE];
        out[0..2].copy_from_slice(&p::MAGIC);
        out[2..4].copy_from_slice(&self.msg_type.to_le_bytes());
        out[4..8].copy_from_slice(&self.payload_len.to_le_bytes());
        out[8..16].copy_from_slice(&self.seq.to_le_bytes());
        out[16..24].copy_from_slice(&self.request_id.to_le_bytes());
        out[24..32].copy_from_slice(&self.session_id.to_le_bytes());
        out[32..40].copy_from_slice(&self.window_id.to_le_bytes());
        out
    }

    pub fn decode(b: &[u8]) -> Result<Self> {
        if b.len() < p::HEADER_SIZE {
            return Err(Error::protocol("short frame header"));
        }
        if b[0..2] != p::MAGIC {
            return Err(Error::protocol("bad frame magic"));
        }
        let u16_at = |i: usize| u16::from_le_bytes([b[i], b[i + 1]]);
        let u32_at = |i: usize| u32::from_le_bytes(b[i..i + 4].try_into().unwrap());
        let u64_at = |i: usize| u64::from_le_bytes(b[i..i + 8].try_into().unwrap());
        Ok(Header {
            msg_type: u16_at(2),
            payload_len: u32_at(4),
            seq: u64_at(8),
            request_id: u64_at(16),
            session_id: u64_at(24),
            window_id: u64_at(32),
        })
    }
}

/// A received frame.
#[derive(Debug, Clone)]
pub(crate) struct Message {
    pub header: Header,
    pub payload: Vec<u8>,
}

// ─── FlatBuffers helpers ─────────────────────────────────────────────────────

fn finish<T>(mut fbb: FlatBufferBuilder<'_>, root: flatbuffers::WIPOffset<T>) -> Vec<u8> {
    fbb.finish_minimal(root);
    let data = fbb.finished_data();
    let mut out = Vec::with_capacity(data.len() + 1);
    out.push(p::PAYLOAD_FORMAT_FLATBUFFERS);
    out.extend_from_slice(data);
    out
}

fn is_fb(data: &[u8]) -> bool {
    data.first() == Some(&p::PAYLOAD_FORMAT_FLATBUFFERS)
}

fn verifier_opts() -> VerifierOptions {
    VerifierOptions {
        max_depth: 128,
        max_tables: 100_000_000,
        max_apparent_size: 1 << 31,
        ignore_missing_null_terminator: false,
    }
}

/// Parse a FlatBuffers payload (with its 1-byte prefix) as table `T`.
fn root<'a, T>(data: &'a [u8]) -> Result<T::Inner>
where
    T: 'a + Follow<'a> + Verifiable,
{
    if !is_fb(data) {
        return Err(Error::protocol("expected a FlatBuffers payload"));
    }
    flatbuffers::root_with_opts::<T>(&verifier_opts(), &data[1..])
        .map_err(|e| Error::protocol(format!("malformed FlatBuffers payload: {e}")))
}

// ─── Request encoders ────────────────────────────────────────────────────────

pub(crate) fn encode_hello(client_type: &str, build: &str) -> Vec<u8> {
    let mut fbb = FlatBufferBuilder::with_capacity(256);
    let build = fbb.create_string(build);
    let ct = fbb.create_string(client_type);
    let root = fb::HelloPayload::create(
        &mut fbb,
        &fb::HelloPayloadArgs {
            protocol_major: p::PROTOCOL_MAJOR,
            protocol_minor: p::PROTOCOL_MINOR,
            agent_build: Some(build),
            capabilities: p::CAPABILITY_FLATBUFFERS,
            client_type: Some(ct),
        },
    );
    finish(fbb, root)
}

pub(crate) fn encode_create_figure(title: &str, width: u32, height: u32) -> Vec<u8> {
    let mut fbb = FlatBufferBuilder::with_capacity(256);
    let title = fbb.create_string(title);
    let root = fb::ReqCreateFigurePayload::create(
        &mut fbb,
        &fb::ReqCreateFigurePayloadArgs {
            title: Some(title),
            width,
            height,
        },
    );
    finish(fbb, root)
}

pub(crate) fn encode_create_axes(
    figure_id: u64,
    rows: i32,
    cols: i32,
    index: i32,
    is_3d: bool,
) -> Vec<u8> {
    let mut fbb = FlatBufferBuilder::with_capacity(64);
    let root = fb::ReqCreateAxesPayload::create(
        &mut fbb,
        &fb::ReqCreateAxesPayloadArgs {
            figure_id,
            grid_rows: rows,
            grid_cols: cols,
            grid_index: index,
            is_3d,
        },
    );
    finish(fbb, root)
}

pub(crate) fn encode_add_series(
    figure_id: u64,
    axes_index: u32,
    series_type: &str,
    label: &str,
) -> Vec<u8> {
    let mut fbb = FlatBufferBuilder::with_capacity(256);
    let st = fbb.create_string(series_type);
    let label = fbb.create_string(label);
    let root = fb::ReqAddSeriesPayload::create(
        &mut fbb,
        &fb::ReqAddSeriesPayloadArgs {
            figure_id,
            axes_index,
            series_type: Some(st),
            label: Some(label),
        },
    );
    finish(fbb, root)
}

pub(crate) fn encode_set_data(figure_id: u64, series_index: u32, data: &[f32]) -> Vec<u8> {
    let mut fbb = FlatBufferBuilder::with_capacity(64 + data.len() * 4);
    let data = fbb.create_vector(data);
    let root = fb::ReqSetDataPayload::create(
        &mut fbb,
        &fb::ReqSetDataPayloadArgs {
            figure_id,
            series_index,
            dtype: 0,
            data: Some(data),
        },
    );
    finish(fbb, root)
}

pub(crate) fn encode_append_data(figure_id: u64, series_index: u32, data: &[f32]) -> Vec<u8> {
    let mut fbb = FlatBufferBuilder::with_capacity(64 + data.len() * 4);
    let data = fbb.create_vector(data);
    let root = fb::ReqAppendDataPayload::create(
        &mut fbb,
        &fb::ReqAppendDataPayloadArgs {
            figure_id,
            series_index,
            data: Some(data),
        },
    );
    finish(fbb, root)
}

/// One `REQ_UPDATE_PROPERTY`: a named property plus generic arguments.
#[derive(Debug, Clone, Default, PartialEq)]
pub(crate) struct PropertyUpdate {
    pub figure_id: u64,
    pub axes_index: u32,
    pub series_index: u32,
    pub prop: String,
    pub f1: f64,
    pub f2: f64,
    pub f3: f64,
    pub f4: f64,
    pub bool_val: bool,
    pub str_val: String,
    pub data: Option<Vec<f32>>,
}

impl PropertyUpdate {
    pub fn new(figure_id: u64, prop: &str) -> Self {
        PropertyUpdate {
            figure_id,
            prop: prop.to_string(),
            ..Default::default()
        }
    }
    pub fn axes(mut self, axes_index: u32) -> Self {
        self.axes_index = axes_index;
        self
    }
    pub fn series(mut self, series_index: u32) -> Self {
        self.series_index = series_index;
        self
    }
    pub fn f(mut self, f1: f64) -> Self {
        self.f1 = f1;
        self
    }
    pub fn f2(mut self, f1: f64, f2: f64) -> Self {
        self.f1 = f1;
        self.f2 = f2;
        self
    }
    pub fn f3(mut self, f1: f64, f2: f64, f3: f64) -> Self {
        self.f1 = f1;
        self.f2 = f2;
        self.f3 = f3;
        self
    }
    pub fn f4(mut self, f1: f64, f2: f64, f3: f64, f4: f64) -> Self {
        self.f1 = f1;
        self.f2 = f2;
        self.f3 = f3;
        self.f4 = f4;
        self
    }
    pub fn b(mut self, v: bool) -> Self {
        self.bool_val = v;
        self
    }
    pub fn s(mut self, v: impl Into<String>) -> Self {
        self.str_val = v.into();
        self
    }
    pub fn data(mut self, d: Vec<f32>) -> Self {
        self.data = Some(d);
        self
    }

    fn build<'a>(
        &self,
        fbb: &mut FlatBufferBuilder<'a>,
    ) -> flatbuffers::WIPOffset<fb::ReqUpdatePropertyPayload<'a>> {
        let prop = fbb.create_string(&self.prop);
        let str_val = (!self.str_val.is_empty()).then(|| fbb.create_string(&self.str_val));
        let data = match &self.data {
            Some(d) if !d.is_empty() => Some(fbb.create_vector(d)),
            _ => None,
        };
        fb::ReqUpdatePropertyPayload::create(
            fbb,
            &fb::ReqUpdatePropertyPayloadArgs {
                figure_id: self.figure_id,
                axes_index: self.axes_index,
                series_index: self.series_index,
                property: Some(prop),
                f1: self.f1,
                f2: self.f2,
                f3: self.f3,
                f4: self.f4,
                bool_val: self.bool_val,
                str_val,
                data,
            },
        )
    }
}

pub(crate) fn encode_update_property(u: &PropertyUpdate) -> Vec<u8> {
    let cap = 256 + u.data.as_ref().map_or(0, |d| d.len() * 4);
    let mut fbb = FlatBufferBuilder::with_capacity(cap);
    let root = u.build(&mut fbb);
    finish(fbb, root)
}

pub(crate) fn encode_update_batch(updates: &[PropertyUpdate]) -> Vec<u8> {
    let mut fbb = FlatBufferBuilder::with_capacity(256 * updates.len().max(1));
    let offsets: Vec<_> = updates.iter().map(|u| u.build(&mut fbb)).collect();
    let vec = fbb.create_vector(&offsets);
    let root = fb::ReqUpdateBatchPayload::create(
        &mut fbb,
        &fb::ReqUpdateBatchPayloadArgs { updates: Some(vec) },
    );
    finish(fbb, root)
}

pub(crate) fn encode_show(figure_id: u64, window_id: u64) -> Vec<u8> {
    let mut fbb = FlatBufferBuilder::with_capacity(64);
    let root = fb::ReqShowPayload::create(
        &mut fbb,
        &fb::ReqShowPayloadArgs {
            figure_id,
            window_id,
        },
    );
    finish(fbb, root)
}

pub(crate) fn encode_destroy_figure(figure_id: u64) -> Vec<u8> {
    let mut fbb = FlatBufferBuilder::with_capacity(64);
    let root = fb::ReqDestroyFigurePayload::create(
        &mut fbb,
        &fb::ReqDestroyFigurePayloadArgs { figure_id },
    );
    finish(fbb, root)
}

pub(crate) fn encode_close_figure(figure_id: u64) -> Vec<u8> {
    let mut fbb = FlatBufferBuilder::with_capacity(64);
    let root =
        fb::ReqCloseFigurePayload::create(&mut fbb, &fb::ReqCloseFigurePayloadArgs { figure_id });
    finish(fbb, root)
}

pub(crate) fn encode_remove_series(figure_id: u64, series_index: u32) -> Vec<u8> {
    let mut fbb = FlatBufferBuilder::with_capacity(64);
    let root = fb::ReqRemoveSeriesPayload::create(
        &mut fbb,
        &fb::ReqRemoveSeriesPayloadArgs {
            figure_id,
            series_index,
        },
    );
    finish(fbb, root)
}

pub(crate) fn encode_export_figure(
    figure_id: u64,
    path: &str,
    format: &str,
    width: u32,
    height: u32,
    theme: &str,
) -> Vec<u8> {
    let mut fbb = FlatBufferBuilder::with_capacity(256);
    let path = fbb.create_string(path);
    let format = fbb.create_string(format);
    let theme = fbb.create_string(theme);
    let root = fb::ReqExportFigurePayload::create(
        &mut fbb,
        &fb::ReqExportFigurePayloadArgs {
            figure_id,
            path: Some(path),
            format: Some(format),
            width,
            height,
            theme: Some(theme),
        },
    );
    finish(fbb, root)
}

pub(crate) fn encode_reconnect(session_id: u64, token: &str) -> Vec<u8> {
    let mut fbb = FlatBufferBuilder::with_capacity(128);
    let token = fbb.create_string(token);
    let root = fb::ReqReconnectPayload::create(
        &mut fbb,
        &fb::ReqReconnectPayloadArgs {
            session_id,
            session_token: Some(token),
        },
    );
    finish(fbb, root)
}

pub(crate) fn encode_declare_topic(
    name: &str,
    kind: u8,
    unit: &str,
    ring_capacity: u32,
) -> Vec<u8> {
    let mut fbb = FlatBufferBuilder::with_capacity(256);
    let name = fbb.create_string(name);
    let unit = fbb.create_string(unit);
    let root = fb::ReqDeclareTopicPayload::create(
        &mut fbb,
        &fb::ReqDeclareTopicPayloadArgs {
            name: Some(name),
            kind,
            unit: Some(unit),
            ring_capacity,
        },
    );
    finish(fbb, root)
}

pub(crate) fn encode_publish_topic_samples(name: &str, samples: &[f64]) -> Vec<u8> {
    let mut fbb = FlatBufferBuilder::with_capacity(128 + samples.len() * 8);
    let name = fbb.create_string(name);
    let samples = (!samples.is_empty()).then(|| fbb.create_vector(samples));
    let root = fb::ReqPublishTopicSamplesPayload::create(
        &mut fbb,
        &fb::ReqPublishTopicSamplesPayloadArgs {
            name: Some(name),
            samples,
        },
    );
    finish(fbb, root)
}

pub(crate) fn encode_subscribe_topic(
    name: &str,
    figure_id: u64,
    axes_index: u32,
    series_index: u32,
) -> Vec<u8> {
    let mut fbb = FlatBufferBuilder::with_capacity(128);
    let name = fbb.create_string(name);
    let root = fb::ReqSubscribeTopicPayload::create(
        &mut fbb,
        &fb::ReqSubscribeTopicPayloadArgs {
            name: Some(name),
            figure_id,
            axes_index,
            series_index,
        },
    );
    finish(fbb, root)
}

pub(crate) fn encode_unsubscribe_topic(
    figure_id: u64,
    axes_index: u32,
    series_index: u32,
) -> Vec<u8> {
    let mut fbb = FlatBufferBuilder::with_capacity(64);
    let root = fb::ReqUnsubscribeTopicPayload::create(
        &mut fbb,
        &fb::ReqUnsubscribeTopicPayloadArgs {
            figure_id,
            axes_index,
            series_index,
        },
    );
    finish(fbb, root)
}

pub(crate) fn encode_list_topics() -> Vec<u8> {
    let mut fbb = FlatBufferBuilder::with_capacity(32);
    let root = fb::ReqListTopicsPayload::create(
        &mut fbb,
        &fb::ReqListTopicsPayloadArgs { placeholder: 0 },
    );
    finish(fbb, root)
}

// ─── TLV (legacy codec) ──────────────────────────────────────────────────────

#[derive(Default)]
pub(crate) struct TlvEncoder {
    buf: Vec<u8>,
}

impl TlvEncoder {
    fn put_raw(&mut self, tag: u8, bytes: &[u8]) {
        self.buf.push(tag);
        self.buf
            .extend_from_slice(&(bytes.len() as u32).to_le_bytes());
        self.buf.extend_from_slice(bytes);
    }
    pub fn put_u16(&mut self, tag: u8, v: u16) {
        self.put_raw(tag, &v.to_le_bytes());
    }
    pub fn put_u32(&mut self, tag: u8, v: u32) {
        self.put_raw(tag, &v.to_le_bytes());
    }
    pub fn put_u64(&mut self, tag: u8, v: u64) {
        self.put_raw(tag, &v.to_le_bytes());
    }
    pub fn put_f32(&mut self, tag: u8, v: f32) {
        self.put_u32(tag, v.to_bits());
    }
    /// `[count u32][f32 ...]` blob.
    pub fn put_float_blob(&mut self, tag: u8, data: &[f32]) {
        self.buf.push(tag);
        self.buf
            .extend_from_slice(&((4 + data.len() * 4) as u32).to_le_bytes());
        self.buf
            .extend_from_slice(&(data.len() as u32).to_le_bytes());
        for v in data {
            self.buf.extend_from_slice(&v.to_le_bytes());
        }
    }
    pub fn take(self) -> Vec<u8> {
        self.buf
    }
}

/// Iterates `(tag, value bytes)` over a TLV payload.
pub(crate) struct TlvDecoder<'a> {
    data: &'a [u8],
    pos: usize,
}

impl<'a> TlvDecoder<'a> {
    pub fn new(data: &'a [u8]) -> Self {
        TlvDecoder { data, pos: 0 }
    }
}

impl<'a> Iterator for TlvDecoder<'a> {
    type Item = (u8, &'a [u8]);
    fn next(&mut self) -> Option<Self::Item> {
        if self.pos + 5 > self.data.len() {
            return None;
        }
        let tag = self.data[self.pos];
        let len =
            u32::from_le_bytes(self.data[self.pos + 1..self.pos + 5].try_into().unwrap()) as usize;
        let start = self.pos + 5;
        if start + len > self.data.len() {
            return None;
        }
        self.pos = start + len;
        Some((tag, &self.data[start..start + len]))
    }
}

fn tlv_u32(v: &[u8]) -> u32 {
    if v.len() >= 4 {
        u32::from_le_bytes(v[0..4].try_into().unwrap())
    } else {
        0
    }
}

fn tlv_u64(v: &[u8]) -> u64 {
    if v.len() >= 8 {
        u64::from_le_bytes(v[0..8].try_into().unwrap())
    } else {
        0
    }
}

pub(crate) fn encode_anim_start(figure_id: u64, fps: f32, duration: f32) -> Vec<u8> {
    let mut e = TlvEncoder::default();
    e.put_u64(p::TAG_FIGURE_ID, figure_id);
    e.put_f32(p::TAG_F1, fps);
    e.put_f32(p::TAG_F2, duration);
    e.take()
}

pub(crate) fn encode_anim_stop(figure_id: u64) -> Vec<u8> {
    let mut e = TlvEncoder::default();
    e.put_u64(p::TAG_FIGURE_ID, figure_id);
    e.take()
}

/// One chunk of a chunked `REQ_SET_DATA` (TLV only).
pub(crate) fn encode_set_data_chunk(
    figure_id: u64,
    series_index: u32,
    chunk: &[f32],
    chunk_index: u32,
    chunk_count: u32,
    total_count: u32,
) -> Vec<u8> {
    let mut e = TlvEncoder::default();
    e.put_u64(p::TAG_FIGURE_ID, figure_id);
    e.put_u32(p::TAG_SERIES_INDEX, series_index);
    e.put_u16(p::TAG_DTYPE, 0);
    e.put_u32(p::TAG_CHUNK_INDEX, chunk_index);
    e.put_u32(p::TAG_CHUNK_COUNT, chunk_count);
    e.put_u32(p::TAG_TOTAL_COUNT, total_count);
    e.put_float_blob(p::TAG_BLOB_INLINE, chunk);
    e.take()
}

// ─── Response / event decoders ───────────────────────────────────────────────

#[derive(Debug, Clone, Default)]
pub(crate) struct Welcome {
    pub session_id: u64,
    pub window_id: u64,
    pub mode: String,
}

pub(crate) fn decode_welcome(data: &[u8]) -> Result<Welcome> {
    if is_fb(data) {
        let w = root::<fb::WelcomePayload>(data)?;
        return Ok(Welcome {
            session_id: w.session_id(),
            window_id: w.window_id(),
            mode: w.mode().unwrap_or_default().to_string(),
        });
    }
    let mut out = Welcome::default();
    for (tag, v) in TlvDecoder::new(data) {
        match tag {
            0x20 => out.session_id = tlv_u64(v),
            0x21 => out.window_id = tlv_u64(v),
            0x24 => out.mode = String::from_utf8_lossy(v).into_owned(),
            _ => {}
        }
    }
    Ok(out)
}

pub(crate) fn decode_resp_ok(data: &[u8]) -> u64 {
    if is_fb(data) {
        return root::<fb::RespOkPayload>(data)
            .map(|r| r.request_id())
            .unwrap_or(0);
    }
    TlvDecoder::new(data)
        .find(|(t, _)| *t == p::TAG_REQUEST_ID)
        .map(|(_, v)| tlv_u64(v))
        .unwrap_or(0)
}

pub(crate) fn decode_resp_err(data: &[u8]) -> (u64, u32, String) {
    if is_fb(data) {
        if let Ok(r) = root::<fb::RespErrPayload>(data) {
            return (
                r.request_id(),
                r.code(),
                r.message().unwrap_or_default().to_string(),
            );
        }
        return (0, 0, "malformed error response".into());
    }
    let (mut rid, mut code, mut msg) = (0, 0, String::new());
    for (tag, v) in TlvDecoder::new(data) {
        match tag {
            p::TAG_REQUEST_ID => rid = tlv_u64(v),
            p::TAG_ERROR_CODE => code = tlv_u32(v),
            p::TAG_ERROR_MESSAGE => msg = String::from_utf8_lossy(v).into_owned(),
            _ => {}
        }
    }
    (rid, code, msg)
}

pub(crate) fn decode_figure_created(data: &[u8]) -> Result<u64> {
    if is_fb(data) {
        return Ok(root::<fb::RespFigureCreatedPayload>(data)?.figure_id());
    }
    Ok(TlvDecoder::new(data)
        .find(|(t, _)| *t == p::TAG_FIGURE_ID)
        .map(|(_, v)| tlv_u64(v))
        .unwrap_or(0))
}

pub(crate) fn decode_axes_created(data: &[u8]) -> Result<u32> {
    if is_fb(data) {
        return Ok(root::<fb::RespAxesCreatedPayload>(data)?.axes_index());
    }
    Ok(TlvDecoder::new(data)
        .find(|(t, _)| *t == 0x81)
        .map(|(_, v)| tlv_u32(v))
        .unwrap_or(0))
}

pub(crate) fn decode_series_added(data: &[u8]) -> Result<u32> {
    if is_fb(data) {
        return Ok(root::<fb::RespSeriesAddedPayload>(data)?.series_index());
    }
    Ok(TlvDecoder::new(data)
        .find(|(t, _)| *t == p::TAG_SERIES_INDEX)
        .map(|(_, v)| tlv_u32(v))
        .unwrap_or(0))
}

pub(crate) fn decode_figure_list(data: &[u8]) -> Result<Vec<u64>> {
    if is_fb(data) {
        let r = root::<fb::RespFigureListPayload>(data)?;
        return Ok(r
            .figure_ids()
            .map(|v| v.iter().collect())
            .unwrap_or_default());
    }
    Ok(TlvDecoder::new(data)
        .filter(|(t, _)| *t == 0x41)
        .map(|(_, v)| tlv_u64(v))
        .collect())
}

pub(crate) fn decode_window_closed(data: &[u8]) -> (u64, u64, String) {
    if is_fb(data) {
        if let Ok(r) = root::<fb::EvtWindowClosedPayload>(data) {
            return (
                r.figure_id(),
                r.window_id(),
                r.reason().unwrap_or_default().to_string(),
            );
        }
        return (0, 0, String::new());
    }
    let (mut fid, mut wid, mut reason) = (0, 0, String::new());
    for (tag, v) in TlvDecoder::new(data) {
        match tag {
            p::TAG_FIGURE_ID => fid = tlv_u64(v),
            p::TAG_WINDOW_ID => wid = tlv_u64(v),
            p::TAG_REASON => reason = String::from_utf8_lossy(v).into_owned(),
            _ => {}
        }
    }
    (fid, wid, reason)
}

pub(crate) fn decode_figure_destroyed(data: &[u8]) -> u64 {
    if is_fb(data) {
        return root::<fb::EvtFigureDestroyedPayload>(data)
            .map(|r| r.figure_id())
            .unwrap_or(0);
    }
    TlvDecoder::new(data)
        .find(|(t, _)| *t == p::TAG_FIGURE_ID)
        .map(|(_, v)| tlv_u64(v))
        .unwrap_or(0)
}

/// A topic known to the backend broker (`REQ_LIST_TOPICS`).
#[derive(Debug, Clone, PartialEq)]
pub struct TopicInfo {
    /// Topic name, e.g. `"sensor/imu/accel_x"`.
    pub name: String,
    /// `true` for (x, y, z) samples, `false` for (x, y).
    pub is_3d: bool,
    /// Display unit given by the publisher.
    pub unit: String,
    /// Publish rate estimated by the broker, in Hz.
    pub estimated_hz: f64,
    /// Samples published so far.
    pub total_samples: u64,
    /// Time of the last publish, nanoseconds since the Unix epoch.
    pub last_publish_ns: u64,
    /// Number of plots subscribed to the topic.
    pub subscriber_count: u32,
    /// Whether a publisher is connected right now.
    pub publisher_online: bool,
}

pub(crate) fn decode_topic_list(data: &[u8]) -> Result<Vec<TopicInfo>> {
    let r = root::<fb::RespTopicListPayload>(data)?;
    let mut out = Vec::new();
    if let Some(topics) = r.topics() {
        for e in topics.iter() {
            out.push(TopicInfo {
                name: e.name().unwrap_or_default().to_string(),
                is_3d: e.kind() == 1,
                unit: e.unit().unwrap_or_default().to_string(),
                estimated_hz: e.estimated_hz(),
                total_samples: e.total_samples(),
                last_publish_ns: e.last_publish_ns(),
                subscriber_count: e.subscriber_count(),
                publisher_online: e.publisher_online(),
            });
        }
    }
    Ok(out)
}

pub(crate) fn decode_subscribe_topic(data: &[u8]) -> Result<u32> {
    Ok(root::<fb::RespSubscribeTopicPayload>(data)?.series_index())
}

/// Knob values and figure ids from a state snapshot.
#[derive(Debug, Clone, Default)]
pub(crate) struct SnapshotSummary {
    pub figure_ids: Vec<u64>,
    pub knobs: Vec<(String, f32)>,
}

pub(crate) fn decode_snapshot_summary(data: &[u8]) -> Result<SnapshotSummary> {
    let mut out = SnapshotSummary::default();
    if !is_fb(data) {
        // Legacy TLV snapshot: figure blobs (tag 0x53) with nested figure ids.
        for (tag, v) in TlvDecoder::new(data) {
            if tag == 0x53 {
                if let Some((_, id)) = TlvDecoder::new(v).find(|(t, _)| *t == p::TAG_FIGURE_ID) {
                    out.figure_ids.push(tlv_u64(id));
                }
            }
        }
        return Ok(out);
    }
    let snap = root::<fb::StateSnapshotPayload>(data)?;
    if let Some(figs) = snap.figures() {
        out.figure_ids = figs.iter().map(|f| f.figure_id()).collect();
    }
    if let Some(knobs) = snap.knobs() {
        for k in knobs.iter() {
            if let Some(name) = k.name() {
                out.knobs.push((name.to_string(), k.value()));
            }
        }
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn header_roundtrip() {
        let h = Header {
            msg_type: p::REQ_SET_DATA,
            payload_len: 1234,
            seq: 7,
            request_id: 99,
            session_id: 5,
            window_id: 3,
        };
        let bytes = h.encode();
        assert_eq!(&bytes[0..2], b"SP");
        assert_eq!(Header::decode(&bytes).unwrap(), h);
    }

    #[test]
    fn header_rejects_bad_magic() {
        let mut bytes = Header::default().encode();
        bytes[0] = b'X';
        assert!(Header::decode(&bytes).is_err());
    }

    #[test]
    fn update_property_roundtrip() {
        let u = PropertyUpdate::new(42, "xlim")
            .axes(2)
            .f2(-1.5, 3.0)
            .s("hi")
            .data(vec![1.0, 2.0]);
        let bytes = encode_update_property(&u);
        assert_eq!(bytes[0], p::PAYLOAD_FORMAT_FLATBUFFERS);
        let r = root::<fb::ReqUpdatePropertyPayload>(&bytes).unwrap();
        assert_eq!(r.figure_id(), 42);
        assert_eq!(r.axes_index(), 2);
        assert_eq!(r.property(), Some("xlim"));
        assert_eq!(r.f1(), -1.5);
        assert_eq!(r.f2(), 3.0);
        assert_eq!(r.str_val(), Some("hi"));
        assert_eq!(r.data().unwrap().iter().collect::<Vec<_>>(), vec![1.0, 2.0]);
    }

    #[test]
    fn batch_roundtrip() {
        let ups = vec![
            PropertyUpdate::new(1, "xlabel").s("t"),
            PropertyUpdate::new(1, "grid").b(true),
        ];
        let bytes = encode_update_batch(&ups);
        let r = root::<fb::ReqUpdateBatchPayload>(&bytes).unwrap();
        let v = r.updates().unwrap();
        assert_eq!(v.len(), 2);
        assert_eq!(v.get(0).property(), Some("xlabel"));
        assert!(v.get(1).bool_val());
    }

    #[test]
    fn set_data_roundtrip() {
        let bytes = encode_set_data(3, 4, &[0.5, 1.5, 2.5]);
        let r = root::<fb::ReqSetDataPayload>(&bytes).unwrap();
        assert_eq!(r.figure_id(), 3);
        assert_eq!(r.series_index(), 4);
        assert_eq!(
            r.data().unwrap().iter().collect::<Vec<_>>(),
            vec![0.5, 1.5, 2.5]
        );
    }

    #[test]
    fn tlv_chunk_layout() {
        let bytes = encode_set_data_chunk(1, 2, &[1.0, 2.0], 0, 3, 6);
        let fields: Vec<_> = TlvDecoder::new(&bytes).collect();
        assert_eq!(fields.len(), 7);
        let blob = fields[6].1;
        assert_eq!(u32::from_le_bytes(blob[0..4].try_into().unwrap()), 2);
        assert_eq!(blob.len(), 12);
    }

    #[test]
    fn hello_has_flatbuffers_capability() {
        let bytes = encode_hello("rust", "test");
        let r = root::<fb::HelloPayload>(&bytes).unwrap();
        assert_eq!(r.client_type(), Some("rust"));
        assert_eq!(
            r.capabilities() & p::CAPABILITY_FLATBUFFERS,
            p::CAPABILITY_FLATBUFFERS
        );
    }
}
