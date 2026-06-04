"""FlatBuffers codec for IPC payloads.

Provides encode_fb_*/decode_fb_* functions that mirror the TLV functions in
_codec.py but use FlatBuffers-generated classes for serialization.

Wire format: Each FlatBuffers payload is prefixed with a single 0x01 byte to
distinguish it from legacy TLV (0x00 or raw tag byte).
"""

import struct
from typing import List, Optional, Tuple

import flatbuffers

from . import _protocol as P

# Make absolute imports of the form `from spectra.ipc.fb.X import X` (emitted
# by `flatc --python` when one FlatBuffers table references another) resolve
# to our `_fb_generated/spectra/ipc/fb` tree. Without this shim, only direct
# top-level table accesses work — any nested-table call (e.g. fb.Topics(i))
# fails with ModuleNotFoundError because the generated files use absolute
# package paths matching their schema namespace.
import os as _os
import sys as _sys
import types as _types

_fb_root = _os.path.join(_os.path.dirname(__file__), "_fb_generated", "spectra")
if "spectra.ipc" not in _sys.modules:
    _ipc = _types.ModuleType("spectra.ipc")
    _ipc.__path__ = [_os.path.join(_fb_root, "ipc")]
    _sys.modules["spectra.ipc"] = _ipc
if "spectra.ipc.fb" not in _sys.modules:
    _fb = _types.ModuleType("spectra.ipc.fb")
    _fb.__path__ = [_os.path.join(_fb_root, "ipc", "fb")]
    _sys.modules["spectra.ipc.fb"] = _fb

# ─── Generated FlatBuffers classes ────────────────────────────────────────────
from ._fb_generated.spectra.ipc.fb import HelloPayload as FBHello
from ._fb_generated.spectra.ipc.fb import WelcomePayload as FBWelcome
from ._fb_generated.spectra.ipc.fb import RespOkPayload as FBRespOk
from ._fb_generated.spectra.ipc.fb import RespErrPayload as FBRespErr
from ._fb_generated.spectra.ipc.fb import RespFigureCreatedPayload as FBRespFigCreated
from ._fb_generated.spectra.ipc.fb import RespAxesCreatedPayload as FBRespAxCreated
from ._fb_generated.spectra.ipc.fb import RespSeriesAddedPayload as FBRespSerAdded
from ._fb_generated.spectra.ipc.fb import RespFigureListPayload as FBRespFigList
from ._fb_generated.spectra.ipc.fb import EvtWindowClosedPayload as FBEvtWinClosed
from ._fb_generated.spectra.ipc.fb import EvtFigureDestroyedPayload as FBEvtFigDestroyed
from ._fb_generated.spectra.ipc.fb import ReqCreateFigurePayload as FBReqCreateFig
from ._fb_generated.spectra.ipc.fb import ReqCreateAxesPayload as FBReqCreateAxes
from ._fb_generated.spectra.ipc.fb import ReqAddSeriesPayload as FBReqAddSeries
from ._fb_generated.spectra.ipc.fb import ReqSetDataPayload as FBReqSetData
from ._fb_generated.spectra.ipc.fb import ReqAppendDataPayload as FBReqAppendData
from ._fb_generated.spectra.ipc.fb import ReqUpdatePropertyPayload as FBReqUpdProp
from ._fb_generated.spectra.ipc.fb import ReqUpdateBatchPayload as FBReqUpdBatch
from ._fb_generated.spectra.ipc.fb import ReqShowPayload as FBReqShow
from ._fb_generated.spectra.ipc.fb import ReqDestroyFigurePayload as FBReqDestroyFig
from ._fb_generated.spectra.ipc.fb import ReqRemoveSeriesPayload as FBReqRemSeries
from ._fb_generated.spectra.ipc.fb import ReqCloseFigurePayload as FBReqCloseFig
from ._fb_generated.spectra.ipc.fb import ReqReconnectPayload as FBReqReconnect
from ._fb_generated.spectra.ipc.fb import ReqDeclareTopicPayload as FBReqDeclareTopic
from ._fb_generated.spectra.ipc.fb import ReqPublishTopicSamplesPayload as FBReqPublishTopic
from ._fb_generated.spectra.ipc.fb import ReqSubscribeTopicPayload as FBReqSubscribeTopic
from ._fb_generated.spectra.ipc.fb import ReqUnsubscribeTopicPayload as FBReqUnsubscribeTopic
from ._fb_generated.spectra.ipc.fb import ReqListTopicsPayload as FBReqListTopics
from ._fb_generated.spectra.ipc.fb import RespTopicListPayload as FBRespTopicList
from ._fb_generated.spectra.ipc.fb import TopicInfoEntry as FBTopicInfoEntry
from ._fb_generated.spectra.ipc.fb import RespSubscribeTopicPayload as FBRespSubscribeTopic
from ._fb_generated.spectra.ipc.fb import EvtTopicListChangedPayload as FBEvtTopicListChanged

FB_PREFIX = bytes([P.PAYLOAD_FORMAT_FLATBUFFERS])


def _is_fb(data: bytes) -> bool:
    """True if data starts with the FlatBuffers format prefix."""
    return len(data) > 0 and data[0] == P.PAYLOAD_FORMAT_FLATBUFFERS


def _strip(data: bytes) -> bytes:
    """Remove the 1-byte format prefix."""
    return data[1:]


def _finalize(builder: flatbuffers.Builder) -> bytes:
    """Return prefix + finished FlatBuffer bytes."""
    buf = builder.Output()
    return FB_PREFIX + bytes(buf)


# ─── Encode functions ─────────────────────────────────────────────────────────

def encode_fb_hello(client_type: str = "python", build: str = "") -> bytes:
    builder = flatbuffers.Builder(256)
    build_off = builder.CreateString(build)
    ct_off = builder.CreateString(client_type)
    FBHello.Start(builder)
    FBHello.AddProtocolMajor(builder, P.PROTOCOL_MAJOR)
    FBHello.AddProtocolMinor(builder, P.PROTOCOL_MINOR)
    FBHello.AddAgentBuild(builder, build_off)
    FBHello.AddCapabilities(builder, P.CAPABILITY_FLATBUFFERS)
    FBHello.AddClientType(builder, ct_off)
    root = builder.EndObject()
    builder.Finish(root)
    return _finalize(builder)


def encode_fb_req_create_figure(title: str = "", width: int = 1280, height: int = 720) -> bytes:
    builder = flatbuffers.Builder(256)
    title_off = builder.CreateString(title)
    FBReqCreateFig.Start(builder)
    FBReqCreateFig.AddTitle(builder, title_off)
    FBReqCreateFig.AddWidth(builder, width)
    FBReqCreateFig.AddHeight(builder, height)
    root = builder.EndObject()
    builder.Finish(root)
    return _finalize(builder)


def encode_fb_req_create_axes(
    figure_id: int, rows: int, cols: int, index: int, is_3d: bool = False
) -> bytes:
    builder = flatbuffers.Builder(128)
    FBReqCreateAxes.Start(builder)
    FBReqCreateAxes.AddFigureId(builder, figure_id)
    FBReqCreateAxes.AddGridRows(builder, rows)
    FBReqCreateAxes.AddGridCols(builder, cols)
    FBReqCreateAxes.AddGridIndex(builder, index)
    FBReqCreateAxes.AddIs3d(builder, is_3d)
    root = builder.EndObject()
    builder.Finish(root)
    return _finalize(builder)


def encode_fb_req_add_series(
    figure_id: int, axes_index: int, series_type: str, label: str = ""
) -> bytes:
    builder = flatbuffers.Builder(256)
    type_off = builder.CreateString(series_type)
    label_off = builder.CreateString(label)
    FBReqAddSeries.Start(builder)
    FBReqAddSeries.AddFigureId(builder, figure_id)
    FBReqAddSeries.AddAxesIndex(builder, axes_index)
    FBReqAddSeries.AddSeriesType(builder, type_off)
    FBReqAddSeries.AddLabel(builder, label_off)
    root = builder.EndObject()
    builder.Finish(root)
    return _finalize(builder)


def encode_fb_req_set_data(
    figure_id: int, series_index: int, data: List[float], dtype: int = 0
) -> bytes:
    builder = flatbuffers.Builder(256 + len(data) * 4)
    if data:
        FBReqSetData.StartDataVector(builder, len(data))
        for v in reversed(data):
            builder.PrependFloat32(v)
        data_off = builder.EndVector()
    FBReqSetData.Start(builder)
    FBReqSetData.AddFigureId(builder, figure_id)
    FBReqSetData.AddSeriesIndex(builder, series_index)
    FBReqSetData.AddDtype(builder, dtype)
    if data:
        FBReqSetData.AddData(builder, data_off)
    root = builder.EndObject()
    builder.Finish(root)
    return _finalize(builder)


def encode_fb_req_append_data(
    figure_id: int, series_index: int, data: List[float]
) -> bytes:
    builder = flatbuffers.Builder(256 + len(data) * 4)
    if data:
        FBReqAppendData.StartDataVector(builder, len(data))
        for v in reversed(data):
            builder.PrependFloat32(v)
        data_off = builder.EndVector()
    FBReqAppendData.Start(builder)
    FBReqAppendData.AddFigureId(builder, figure_id)
    FBReqAppendData.AddSeriesIndex(builder, series_index)
    if data:
        FBReqAppendData.AddData(builder, data_off)
    root = builder.EndObject()
    builder.Finish(root)
    return _finalize(builder)


def encode_fb_req_update_property(
    figure_id: int,
    axes_index: int = 0,
    series_index: int = 0,
    prop: str = "",
    f1: float = 0.0,
    f2: float = 0.0,
    f3: float = 0.0,
    f4: float = 0.0,
    bool_val: bool = False,
    str_val: str = "",
) -> bytes:
    builder = flatbuffers.Builder(512)
    prop_off = builder.CreateString(prop)
    str_off = builder.CreateString(str_val) if str_val else None
    FBReqUpdProp.Start(builder)
    FBReqUpdProp.AddFigureId(builder, figure_id)
    FBReqUpdProp.AddAxesIndex(builder, axes_index)
    FBReqUpdProp.AddSeriesIndex(builder, series_index)
    FBReqUpdProp.AddProperty(builder, prop_off)
    FBReqUpdProp.AddF1(builder, f1)
    FBReqUpdProp.AddF2(builder, f2)
    FBReqUpdProp.AddF3(builder, f3)
    FBReqUpdProp.AddF4(builder, f4)
    FBReqUpdProp.AddBoolVal(builder, bool_val)
    if str_off is not None:
        FBReqUpdProp.AddStrVal(builder, str_off)
    root = builder.EndObject()
    builder.Finish(root)
    return _finalize(builder)


def encode_fb_req_update_batch(updates: List[dict]) -> bytes:
    """Encode REQ_UPDATE_BATCH with nested property updates."""
    builder = flatbuffers.Builder(1024)
    offsets = []
    for upd in updates:
        prop_off = builder.CreateString(upd.get("prop", ""))
        str_val = upd.get("str_val", "")
        str_off = builder.CreateString(str_val) if str_val else None
        FBReqUpdProp.Start(builder)
        FBReqUpdProp.AddFigureId(builder, upd.get("figure_id", 0))
        FBReqUpdProp.AddAxesIndex(builder, upd.get("axes_index", 0))
        FBReqUpdProp.AddSeriesIndex(builder, upd.get("series_index", 0))
        FBReqUpdProp.AddProperty(builder, prop_off)
        FBReqUpdProp.AddF1(builder, upd.get("f1", 0.0))
        FBReqUpdProp.AddF2(builder, upd.get("f2", 0.0))
        FBReqUpdProp.AddF3(builder, upd.get("f3", 0.0))
        FBReqUpdProp.AddF4(builder, upd.get("f4", 0.0))
        FBReqUpdProp.AddBoolVal(builder, upd.get("bool_val", False))
        if str_off is not None:
            FBReqUpdProp.AddStrVal(builder, str_off)
        offsets.append(FBReqUpdProp.End(builder))
    FBReqUpdBatch.StartUpdatesVector(builder, len(offsets))
    for off in reversed(offsets):
        builder.PrependUOffsetTRelative(off)
    upds_off = builder.EndVector()
    FBReqUpdBatch.Start(builder)
    FBReqUpdBatch.AddUpdates(builder, upds_off)
    root = builder.EndObject()
    builder.Finish(root)
    return _finalize(builder)


def encode_fb_req_show(figure_id: int, window_id: int = 0) -> bytes:
    builder = flatbuffers.Builder(64)
    FBReqShow.Start(builder)
    FBReqShow.AddFigureId(builder, figure_id)
    FBReqShow.AddWindowId(builder, window_id)
    root = builder.EndObject()
    builder.Finish(root)
    return _finalize(builder)


def encode_fb_req_destroy_figure(figure_id: int) -> bytes:
    builder = flatbuffers.Builder(64)
    FBReqDestroyFig.Start(builder)
    FBReqDestroyFig.AddFigureId(builder, figure_id)
    root = builder.EndObject()
    builder.Finish(root)
    return _finalize(builder)


def encode_fb_req_remove_series(figure_id: int, series_index: int) -> bytes:
    builder = flatbuffers.Builder(64)
    FBReqRemSeries.Start(builder)
    FBReqRemSeries.AddFigureId(builder, figure_id)
    FBReqRemSeries.AddSeriesIndex(builder, series_index)
    root = builder.EndObject()
    builder.Finish(root)
    return _finalize(builder)


def encode_fb_req_close_figure(figure_id: int) -> bytes:
    builder = flatbuffers.Builder(64)
    FBReqCloseFig.Start(builder)
    FBReqCloseFig.AddFigureId(builder, figure_id)
    root = builder.EndObject()
    builder.Finish(root)
    return _finalize(builder)


def encode_fb_req_reconnect(session_id: int, session_token: str = "") -> bytes:
    builder = flatbuffers.Builder(128)
    tok_off = builder.CreateString(session_token)
    FBReqReconnect.Start(builder)
    FBReqReconnect.AddSessionId(builder, session_id)
    FBReqReconnect.AddSessionToken(builder, tok_off)
    root = builder.EndObject()
    builder.Finish(root)
    return _finalize(builder)


# ─── Decode functions ─────────────────────────────────────────────────────────

def decode_fb_welcome(data: bytes) -> dict:
    buf = _strip(data) if _is_fb(data) else data
    fb = FBWelcome.WelcomePayload.GetRootAs(buf, 0)
    return {
        "session_id": fb.SessionId(),
        "window_id": fb.WindowId(),
        "process_id": fb.ProcessId(),
        "heartbeat_ms": fb.HeartbeatMs(),
        "mode": (fb.Mode() or b"").decode("utf-8", errors="replace"),
    }


def decode_fb_resp_ok(data: bytes) -> int:
    buf = _strip(data) if _is_fb(data) else data
    fb = FBRespOk.RespOkPayload.GetRootAs(buf, 0)
    return fb.RequestId()


def decode_fb_resp_err(data: bytes) -> Tuple[int, int, str]:
    buf = _strip(data) if _is_fb(data) else data
    fb = FBRespErr.RespErrPayload.GetRootAs(buf, 0)
    return fb.RequestId(), fb.Code(), (fb.Message() or b"").decode("utf-8", errors="replace")


def decode_fb_resp_figure_created(data: bytes) -> Tuple[int, int]:
    buf = _strip(data) if _is_fb(data) else data
    fb = FBRespFigCreated.RespFigureCreatedPayload.GetRootAs(buf, 0)
    return fb.RequestId(), fb.FigureId()


def decode_fb_resp_axes_created(data: bytes) -> Tuple[int, int]:
    buf = _strip(data) if _is_fb(data) else data
    fb = FBRespAxCreated.RespAxesCreatedPayload.GetRootAs(buf, 0)
    return fb.RequestId(), fb.AxesIndex()


def decode_fb_resp_series_added(data: bytes) -> Tuple[int, int]:
    buf = _strip(data) if _is_fb(data) else data
    fb = FBRespSerAdded.RespSeriesAddedPayload.GetRootAs(buf, 0)
    return fb.RequestId(), fb.SeriesIndex()


def decode_fb_resp_figure_list(data: bytes) -> Tuple[int, List[int]]:
    buf = _strip(data) if _is_fb(data) else data
    fb = FBRespFigList.RespFigureListPayload.GetRootAs(buf, 0)
    ids = []
    if fb.FigureIdsLength():
        for i in range(fb.FigureIdsLength()):
            ids.append(fb.FigureIds(i))
    return fb.RequestId(), ids


def decode_fb_evt_window_closed(data: bytes) -> Tuple[int, int, str]:
    buf = _strip(data) if _is_fb(data) else data
    fb = FBEvtWinClosed.EvtWindowClosedPayload.GetRootAs(buf, 0)
    return (
        fb.FigureId(),
        fb.WindowId(),
        (fb.Reason() or b"").decode("utf-8", errors="replace"),
    )


def decode_fb_req_update_batch(data: bytes) -> List[dict]:
    buf = _strip(data) if _is_fb(data) else data
    fb = FBReqUpdBatch.ReqUpdateBatchPayload.GetRootAs(buf, 0)
    updates = []
    if fb.UpdatesLength():
        for i in range(fb.UpdatesLength()):
            upd = fb.Updates(i)
            updates.append(
                {
                    "figure_id": upd.FigureId(),
                    "axes_index": upd.AxesIndex(),
                    "series_index": upd.SeriesIndex(),
                    "prop": (upd.Property() or b"").decode("utf-8", errors="replace"),
                    "f1": upd.F1(),
                    "f2": upd.F2(),
                    "f3": upd.F3(),
                    "f4": upd.F4(),
                    "bool_val": upd.BoolVal(),
                    "str_val": (upd.StrVal() or b"").decode("utf-8", errors="replace"),
                }
            )
    return updates


def decode_fb_evt_figure_destroyed(data: bytes) -> Tuple[int, str]:
    buf = _strip(data) if _is_fb(data) else data
    fb = FBEvtFigDestroyed.EvtFigureDestroyedPayload.GetRootAs(buf, 0)
    return fb.FigureId(), (fb.Reason() or b"").decode("utf-8", errors="replace")


# ─── Topics (pub/sub) ─────────────────────────────────────────────────────────

def encode_fb_req_declare_topic(
    name: str, kind: int = 0, unit: str = "", ring_capacity: int = 4096
) -> bytes:
    builder = flatbuffers.Builder(256)
    name_off = builder.CreateString(name)
    unit_off = builder.CreateString(unit)
    FBReqDeclareTopic.Start(builder)
    FBReqDeclareTopic.AddName(builder, name_off)
    FBReqDeclareTopic.AddKind(builder, kind)
    FBReqDeclareTopic.AddUnit(builder, unit_off)
    FBReqDeclareTopic.AddRingCapacity(builder, ring_capacity)
    root = builder.EndObject()
    builder.Finish(root)
    return _finalize(builder)


def encode_fb_req_publish_topic_samples(name: str, samples: List[float]) -> bytes:
    builder = flatbuffers.Builder(256 + len(samples) * 8)
    name_off = builder.CreateString(name)
    if samples:
        FBReqPublishTopic.StartSamplesVector(builder, len(samples))
        for v in reversed(samples):
            builder.PrependFloat64(v)
        samples_off = builder.EndVector()
    FBReqPublishTopic.Start(builder)
    FBReqPublishTopic.AddName(builder, name_off)
    if samples:
        FBReqPublishTopic.AddSamples(builder, samples_off)
    root = builder.EndObject()
    builder.Finish(root)
    return _finalize(builder)


def encode_fb_req_subscribe_topic(
    name: str, figure_id: int, axes_index: int, series_index: int = 0xFFFFFFFF
) -> bytes:
    builder = flatbuffers.Builder(256)
    name_off = builder.CreateString(name)
    FBReqSubscribeTopic.Start(builder)
    FBReqSubscribeTopic.AddName(builder, name_off)
    FBReqSubscribeTopic.AddFigureId(builder, figure_id)
    FBReqSubscribeTopic.AddAxesIndex(builder, axes_index)
    FBReqSubscribeTopic.AddSeriesIndex(builder, series_index)
    root = builder.EndObject()
    builder.Finish(root)
    return _finalize(builder)


def encode_fb_req_unsubscribe_topic(
    name: str, figure_id: int, axes_index: int, series_index: int
) -> bytes:
    builder = flatbuffers.Builder(256)
    name_off = builder.CreateString(name)
    FBReqUnsubscribeTopic.Start(builder)
    FBReqUnsubscribeTopic.AddName(builder, name_off)
    FBReqUnsubscribeTopic.AddFigureId(builder, figure_id)
    FBReqUnsubscribeTopic.AddAxesIndex(builder, axes_index)
    FBReqUnsubscribeTopic.AddSeriesIndex(builder, series_index)
    root = builder.EndObject()
    builder.Finish(root)
    return _finalize(builder)


def encode_fb_req_list_topics() -> bytes:
    builder = flatbuffers.Builder(64)
    FBReqListTopics.Start(builder)
    root = builder.EndObject()
    builder.Finish(root)
    return _finalize(builder)


def decode_fb_resp_topic_list(data: bytes):
    buf = _strip(data) if _is_fb(data) else data
    fb = FBRespTopicList.RespTopicListPayload.GetRootAs(buf, 0)
    topics = []
    for i in range(fb.TopicsLength()):
        e = fb.Topics(i)
        topics.append({
            "name": (e.Name() or b"").decode("utf-8", errors="replace"),
            "kind": e.Kind(),
            "unit": (e.Unit() or b"").decode("utf-8", errors="replace"),
            "publisher_online": bool(e.PublisherOnline()),
            "total_samples": e.TotalSamples(),
            "estimated_hz": e.EstimatedHz(),
            "last_publish_ns": e.LastPublishNs(),
            "subscriber_count": e.SubscriberCount(),
        })
    return {"request_id": fb.RequestId(), "topics": topics}


def decode_fb_resp_subscribe_topic(data: bytes):
    buf = _strip(data) if _is_fb(data) else data
    fb = FBRespSubscribeTopic.RespSubscribeTopicPayload.GetRootAs(buf, 0)
    return {
        "request_id": fb.RequestId(),
        "series_index": fb.SeriesIndex(),
    }


def decode_fb_evt_topic_list_changed(data: bytes):
    # Currently a flag-only event; daemon broadcasts after any change.
    return None


# ─── Request payload decoders (for testing and round-trip verification) ──────

def decode_fb_req_append_data(data: bytes) -> dict:
    """Decode REQ_APPEND_DATA FlatBuffers payload → dict with figure_id, series_index, data."""
    raw = _strip(data)
    buf = bytearray(raw)
    fb = FBReqAppendData.ReqAppendDataPayload.GetRootAs(buf, 0)
    return {
        "figure_id": fb.FigureId(),
        "series_index": fb.SeriesIndex(),
        "data": [fb.Data(i) for i in range(fb.DataLength())],
    }


def decode_fb_req_remove_series(data: bytes) -> dict:
    """Decode REQ_REMOVE_SERIES FlatBuffers payload → dict with figure_id, series_index."""
    raw = _strip(data)
    buf = bytearray(raw)
    fb = FBReqRemSeries.ReqRemoveSeriesPayload.GetRootAs(buf, 0)
    return {"figure_id": fb.FigureId(), "series_index": fb.SeriesIndex()}


def decode_fb_req_close_figure(data: bytes) -> dict:
    """Decode REQ_CLOSE_FIGURE FlatBuffers payload → dict with figure_id."""
    raw = _strip(data)
    buf = bytearray(raw)
    fb = FBReqCloseFig.ReqCloseFigurePayload.GetRootAs(buf, 0)
    return {"figure_id": fb.FigureId()}


def decode_fb_req_reconnect(data: bytes) -> dict:
    """Decode REQ_RECONNECT FlatBuffers payload → dict with session_id, session_token."""
    raw = _strip(data)
    buf = bytearray(raw)
    fb = FBReqReconnect.ReqReconnectPayload.GetRootAs(buf, 0)
    token_raw = fb.SessionToken()
    return {
        "session_id": fb.SessionId(),
        "session_token": token_raw.decode("utf-8") if token_raw else "",
    }


def decode_fb_req_update_property(data: bytes) -> dict:
    """Decode REQ_UPDATE_PROPERTY FlatBuffers payload → dict with all fields."""
    raw = _strip(data)
    buf = bytearray(raw)
    fb = FBReqUpdProp.ReqUpdatePropertyPayload.GetRootAs(buf, 0)
    prop_raw = fb.Property()
    str_val_raw = fb.StrVal()
    return {
        "figure_id": fb.FigureId(),
        "axes_index": fb.AxesIndex(),
        "series_index": fb.SeriesIndex(),
        "prop": prop_raw.decode("utf-8") if prop_raw else "",
        "f1": fb.F1(),
        "f2": fb.F2(),
        "f3": fb.F3(),
        "f4": fb.F4(),
        "bool_val": fb.BoolVal(),
        "str_val": str_val_raw.decode("utf-8") if str_val_raw else "",
    }

