"""Session persistence — save/restore session state to/from disk.

Allows a Python client to save the current session state (all figures,
axes, series metadata) to a JSON file and restore it later, even after
the backend has been restarted.

This is a client-side convenience; the authoritative state always lives
in the backend's FigureModel.
"""

import json
import time
from typing import List, Dict, Any

from . import _protocol as P
from . import _codec as codec


def save_session(session, path: str) -> None:
    """Save the current session state to a JSON file.

    Captures:
      - session_id
      - socket_path
      - all figures (id, title, axes, series metadata)
      - timestamp
    """
    state: Dict[str, Any] = {
        "version": 1,
        "timestamp": time.time(),
        "session_id": session.session_id,
        "socket_path": session._socket_path,
        "figures": [],
    }

    for fig in session.figures:
        fig_data: Dict[str, Any] = {
            "id": fig.id,
            "title": fig.title,
            "axes": [],
        }
        for ax in fig.axes:
            ax_data: Dict[str, Any] = {
                "index": ax.index,
                "series": [],
            }
            for s in ax.series:
                s_data = {
                    "index": s.index,
                    "type": s.series_type,
                    "label": s.label,
                }
                ax_data["series"].append(s_data)
            fig_data["axes"].append(ax_data)
        state["figures"].append(fig_data)

    with open(path, "w") as f:
        json.dump(state, f, indent=2)


def load_session_metadata(path: str) -> Dict[str, Any]:
    """Load session metadata from a JSON file without connecting.

    Returns the parsed state dict for inspection.
    """
    with open(path, "r") as f:
        return json.load(f)


def restore_session(session, path: str) -> List[int]:
    """Restore a session from a saved JSON file.

    This reconnects to the backend (or starts a new one) and recreates
    figures/axes/series as described in the saved state. Data is NOT
    restored (only structure and metadata).

    Returns list of new figure IDs.
    """
    with open(path, "r") as f:
        state = json.load(f)

    if state.get("version", 0) != 1:
        raise ValueError(f"Unsupported session file version: {state.get('version')}")

    new_figure_ids = []

    for fig_data in state.get("figures", []):
        fig = session.figure(title=fig_data.get("title", ""))
        new_figure_ids.append(fig.id)

        for ax_data in fig_data.get("axes", []):
            # Create axes (use 1,1,1 as default subplot)
            ax = fig.subplot(1, 1, 1)

            for s_data in ax_data.get("series", []):
                series_type = s_data.get("type", "line")
                label = s_data.get("label", "")
                # Create empty series — data must be re-populated by the user
                from ._series import Series
                payload = codec.encode_req_add_series(
                    figure_id=fig.id,
                    axes_index=ax.index,
                    series_type=series_type,
                    label=label,
                )
                resp = session._request(P.REQ_ADD_SERIES, payload)
                _, series_index = codec.decode_resp_series_added(resp["payload"])
                series = Series(session, fig.id, series_index, series_type, label)
                ax._series_list.append(series)

    return new_figure_ids
