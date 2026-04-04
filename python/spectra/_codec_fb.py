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


def decode_fb_evt_figure_destroyed(data: bytes) -> Tuple[int, str]:
    buf = _strip(data) if _is_fb(data) else data
    fb = FBEvtFigDestroyed.EvtFigureDestroyedPayload.GetRootAs(buf, 0)
    return fb.FigureId(), (fb.Reason() or b"").decode("utf-8", errors="replace")
