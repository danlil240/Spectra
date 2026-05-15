# Spectra Topics

Spectra Topics are ROS-style named data streams managed by the running
`spectra-backend` daemon. A publisher pushes samples to a topic at any time,
whether or not a UI window is open. When a UI agent (`spectra-window`) is
attached, users can see live topics in the **Topics panel** and drag them
onto an axes to plot them in real time.

This document covers the Phase 1–2 implementation described in
[`plans/SPECTRA_TOPICS_PLAN.md`](../plans/SPECTRA_TOPICS_PLAN.md).

---

## Concepts

| Concept | Description |
|---------|-------------|
| **Topic** | A bounded ring of `(x, y)` or `(x, y, z)` double samples keyed by a string name (e.g. `"imu/accel_x"`). Survives client disconnect. |
| **Publisher** | A process that creates a topic via `declare()` and pushes samples via `publish()`. |
| **Subscription** | A binding from a topic to a concrete `(figure_id, axes_index, series_index)` triple inside the daemon's figure model. Receives fan-out of every sample. |
| **Topic kind** | `Scalar2D` (default — `(x, y)` pairs) or `Scalar3D` (`(x, y, z)` triples). |
| **Pending subscription** | A subscription queued before the topic is declared. Promotes automatically when the matching `declare()` arrives. Used during workspace restore. |

---

## C++ Publisher

```cpp
#include <spectra/topic.hpp>

auto pub = spectra::Publisher::create("imu/accel_x",
                                      spectra::Publisher::Kind::Scalar2D,
                                      "m/s^2");
if (!pub) return 1;

for (int i = 0; i < 1000; ++i)
{
    pub->publish(t, a);            // single sample
}
pub->flush();                      // optional — coalesced by default
```

The publisher connects to the running daemon over the existing Unix-domain
socket. It never owns a `Figure` and never blocks the UI thread.

### Daemon discovery

`Publisher::create()` resolves the daemon socket in this order:

1. `Options::socket_path` if provided.
2. The `SPECTRA_SOCKET` environment variable.
3. Discovery: scans `$XDG_RUNTIME_DIR` (or `/tmp`) for `spectra-*.sock`
   files and tries each, newest first.
4. **Auto-spawn** (default on, disable with `Options::auto_spawn_daemon =
   false`): forks a fresh `spectra-backend` next to the publisher binary or
   on `$PATH`.

This means standalone publisher examples just work — running
`./topic_publisher` with no live UI auto-starts the backend.

See [`examples/topic_publisher.cpp`](../examples/topic_publisher.cpp).

## Python Publisher

```python
import spectra as sp

pub = sp.Publisher("imu/accel_x", kind="2d", unit="m/s^2")
pub.publish(t, a)                  # single sample
pub.publish([t0, t1], [a0, a1])    # batched
```

See [`python/examples/topic_publisher.py`](../python/examples/topic_publisher.py)
and [`python/examples/topic_subscriber.py`](../python/examples/topic_subscriber.py).

---

## UI: Topics panel

Toggle with **Ctrl+Shift+T** or *View → Topics*. The panel lists every topic
the daemon currently knows about, showing:

- Status dot (publisher online / offline)
- Topic name (drag source)
- Kind (`2D` / `3D`)
- Estimated rate (Hz, EWMA over inter-arrival times)
- **Plot** button — subscribes to the currently active figure, axes 0

### Drag-and-drop

Each row is an ImGui drag source with payload type `"SPECTRA_TOPIC"`. While
a drag is in progress, the canvas displays an overlay that highlights the
axes currently under the mouse. Dropping commits a subscription to that
axes; the daemon auto-creates a `LineSeries` (or `LineSeries3D` for 3D
topics) and replies with the resolved `series_index`. New samples flow in
via the normal `STATE_DIFF` path.

The drop handler lives in
[`ImGuiIntegration::draw_topic_drop_target`](../src/ui/imgui/imgui_panels.cpp).

---

## Wire protocol

All messages use the existing 40-byte IPC header + FlatBuffers body. They
are additive — older clients that don't know these types remain compatible.

| Type | Direction | Purpose |
|------|-----------|---------|
| `REQ_DECLARE_TOPIC` | client → daemon | Declare a topic (name, kind, unit, ring capacity). |
| `REQ_PUBLISH_TOPIC_SAMPLES` | client → daemon | Push N interleaved samples. |
| `REQ_SUBSCRIBE_TOPIC` | client → daemon | Bind topic → `(figure, axes, series)`. `series_index == UINT32_MAX` auto-creates. |
| `REQ_UNSUBSCRIBE_TOPIC` | client → daemon | Drop subscription. |
| `REQ_LIST_TOPICS` | client → daemon | Request discovery snapshot. |
| `RESP_TOPIC_LIST` | daemon → client | Discovery reply. |
| `RESP_SUBSCRIBE_TOPIC` | daemon → client | Resolved `series_index`. |
| `EVT_TOPIC_LIST_CHANGED` | daemon → client | Broadcast — clients re-query. |

---

## Lifecycle

```
Publisher.create("imu/accel")
    └─▶ REQ_DECLARE_TOPIC ─────────▶ TopicRegistry::declare()
                                       │
                                       ├─ promote pending subs (if any)
                                       └─ EVT_TOPIC_LIST_CHANGED broadcast

UI drops topic on axes
    └─▶ REQ_SUBSCRIBE_TOPIC ───────▶ TopicRegistry::subscribe()
                                       └─ auto-create LineSeries on (fig, ax)
                                       ◀─ RESP_SUBSCRIBE_TOPIC (series_index)

Publisher.publish(x, y) (batched, 16 ms flush)
    └─▶ REQ_PUBLISH_TOPIC_SAMPLES ▶ TopicRegistry::publish()
                                       └─ fan out append-data ops to subs
                                       └─ STATE_DIFF ──────▶ all agents

Publisher disconnects (TCP close, process exit, ...)
    └─▶ TopicRegistry::on_client_disconnect()
           └─ mark topic offline (publisher_online = false)
           └─ subscriptions kept; topic remains discoverable
           └─ EVT_TOPIC_LIST_CHANGED broadcast

Publisher reconnects, declares again
    └─▶ DeclareResult::ReclaimedByOwner
           └─ subs intact, fan-out resumes
```

---

## Semantics

- **Ownership**: first publisher to declare a name owns it. A second
  declare from a different live publisher returns `Conflict` (HTTP 409
  semantics). If the original publisher has disconnected, any client may
  reclaim the topic.
- **Backpressure**: lossy. Ring overflow drops oldest samples. Publishers
  never block.
- **Hz estimator**: EWMA over inter-publish-call intervals (not per
  sample), so coalesced batches read as a single tick.
- **Disconnect cleanup**: topics persist; subscribers retain their last
  data. Re-declare resumes fan-out without UI intervention.

---

## Limitations (current)

- 2D / 3D scalar topics only. No vector, string, or compound message types.
- Network transport unsupported — local Unix socket only.
- In-process runtime mode (`SPECTRA_RUNTIME_MODE=inproc`) does not yet
  expose `Publisher`; multi-process mode required.
- Workspace v3 does not yet persist subscriptions across save/load
  (planned, see plan §7 Phase 3).
- Multi-zone drop target (overlay vs. new subplot vs. new figure) is
  planned but not implemented; current drop overlay subscribes to the
  hovered axes only.

---

## Testing

- Unit tests: `tests/unit/test_topic_registry.cpp`,
  `tests/unit/test_topic_codec.cpp`.
- Manual smoke test (multi-process):
  ```bash
  # terminal 1
  ./build/spectra-backend
  # terminal 2
  ./build/examples/topic_publisher
  # terminal 3
  ./build/spectra-window      # then Ctrl+Shift+T, drag a topic onto axes
  ```
