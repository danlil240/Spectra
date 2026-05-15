# Spectra Topics — Publish/Subscribe Plan

> Goal: ROS-style named data streams. Publishers push samples to Spectra at any
> time (UI open or closed). When the UI runs, a Topics panel lists all live
> topics; the user drags one onto an axes to subscribe and plot live.

---

## 1. Design Summary

Three independent layers, each thin:

1. **Daemon-side Topic Registry** — buffered named streams that exist whether
   any UI/agent is attached. Owned by `spectra-backend` (`src/daemon/`).
2. **Wire protocol** — three new IPC messages on top of the existing
   FlatBuffers schema; data delivery reuses `REQ_APPEND_DATA`-style payloads.
3. **Topics panel + drag-drop** — ImGui dockable panel in the main UI,
   modeled directly on `src/adapters/ros2/ui/topic_list_panel.*` and
   `field_drag_drop.*` (the ROS adapter is the working prototype to copy from).

Publishers (C++/Python) connect to the daemon over the existing Unix socket and
push samples to a topic name. The daemon stores a bounded ring buffer per
topic and forwards new samples to every subscribed `(figure_id, axes_id,
series_id)` triple. Topics outlive figures and clients.

Key non-goals (kept out of this plan, can come later):
- Network publishers (only the existing local Unix socket transport).
- Type system beyond `float64` scalar pairs `(x, y)` and `(x, y, z)` 3D.
- Topic-level QoS, latching beyond a simple "keep last N" ring.

---

## 2. Architecture

```
┌─────────────┐   IPC (Unix socket, existing)   ┌────────────────────────┐
│  Publisher  │ ──────────────────────────────▶ │   spectra-backend      │
│ (py / c++)  │   REQ_PUBLISH_TOPIC             │  (long-running daemon) │
│             │   REQ_TOPIC_SAMPLES             │                        │
└─────────────┘                                  │  ┌──────────────────┐ │
                                                 │  │  TopicRegistry   │ │
                                                 │  │  name → Topic{   │ │
                                                 │  │    kind, ring,   │ │
                                                 │  │    subs[]        │ │
                                                 │  │  }               │ │
                                                 │  └──────────────────┘ │
                                                 │           │            │
                                                 │           ▼ fan-out    │
                                                 │  ┌──────────────────┐ │
                                                 │  │   FigureModel    │ │
                                                 │  │  Series::append  │ │
                                                 │  └──────────────────┘ │
                                                 └───────────┬────────────┘
                                                             │ STATE_DIFF
                                                             ▼
                                                 ┌────────────────────────┐
                                                 │     spectra-window     │
                                                 │  (UI agent / Renderer) │
                                                 │                        │
                                                 │  TopicsPanel (ImGui)   │
                                                 │     ├─ list topics     │
                                                 │     ├─ Hz, last seen   │
                                                 │     └─ drag source ───┼─▶ drop on Axes
                                                 │                        │   → REQ_SUBSCRIBE_TOPIC
                                                 └────────────────────────┘
```

The publisher never needs a `Figure`. It only needs the daemon socket.

---

## 3. Daemon Changes

Location: `src/daemon/`.

### 3.1 New module: `topic_registry.{hpp,cpp}`

```cpp
namespace spectra::daemon
{
struct TopicSample
{
    double x;
    double y;
    double z;   // unused for 2D, NaN-flagged
    uint64_t seq;
    uint64_t timestamp_ns;
};

enum class TopicKind : uint8_t { Scalar2D, Scalar3D };

struct TopicSubscription
{
    uint64_t session_id;
    uint64_t figure_id;
    uint32_t axes_index;
    uint32_t series_index;   // resolved at subscribe time
};

class TopicRegistry
{
public:
    // Publisher API (called from message handler thread)
    Result<void> declare(std::string_view name, TopicKind kind,
                         uint64_t owner_client_id, std::string_view unit = {});
    Result<void> publish(std::string_view name, std::span<const TopicSample>);
    void on_client_disconnect(uint64_t client_id);  // unregister owned topics

    // Subscriber API
    Result<void> subscribe(std::string_view name, TopicSubscription);
    void unsubscribe(uint64_t session_id, uint64_t figure_id,
                     uint32_t axes_index, uint32_t series_index);

    // Discovery
    struct TopicInfo
    {
        std::string name;
        TopicKind kind;
        std::string unit;
        double estimated_hz;
        uint64_t total_samples;
        uint64_t last_publish_ns;
        size_t subscriber_count;
        bool publisher_online;
    };
    std::vector<TopicInfo> snapshot() const;

private:
    struct Topic
    {
        TopicKind kind;
        std::string unit;
        uint64_t owner_client_id;
        // bounded ring of last N samples (config: 4096 default)
        boost::circular_buffer<TopicSample> ring;  // or hand-rolled
        std::vector<TopicSubscription> subs;
        // hz estimator: EWMA over inter-arrival times
        double ewma_dt_ns = 0.0;
        uint64_t last_publish_ns = 0;
        uint64_t total = 0;
    };
    mutable std::mutex m_;
    std::unordered_map<std::string, Topic> topics_;
};
}
```

Threading: single mutex covers all topic state. Publish path is short
(append-to-ring + iterate subs); fan-out enqueues `Series::append` work onto
the existing `FigureModel` update path — **must reuse the same code path as
`handle_req_append_data`** so we don't fork the data ingestion logic.

### 3.2 `daemon_server.hpp` integration

Add `TopicRegistry topics_;` next to `FigureModel figures_;` in
`DaemonContext`. Wire `on_client_disconnect` into the existing client-slot
cleanup in `src/daemon/main.cpp` so owned topics are marked offline (kept in
registry but flagged `publisher_online=false` — subscribers keep their last
data; topic can be re-declared by a future publisher).

### 3.3 Message handlers

Extend `python_message_handler.hpp` (rename concept aside — it already routes
all client messages) with:

- `handle_req_declare_topic` → `topics_.declare(...)`
- `handle_req_publish_topic_samples` → `topics_.publish(...)` then fan-out
- `handle_req_subscribe_topic` → resolves `(figure, axes, series)`; if
  `series_index == UINT32_MAX`, **auto-creates** a new `LineSeries` on the
  target axes (uses topic name as label) and returns its index.
- `handle_req_unsubscribe_topic`
- `handle_req_list_topics` → returns `snapshot()` for the UI panel
- Broadcast `EVT_TOPIC_LIST_CHANGED` on declare/disconnect/sub-count changes
  so the UI panel auto-refreshes without polling.

---

## 4. IPC Protocol Changes

Files: `src/ipc/schemas/spectra_ipc.fbs`, `src/ipc/message.hpp`,
`src/ipc/codec.*`, mirror in `python/spectra/_codec.py`.

### 4.1 New message types (append, do not renumber existing)

| Type ID         | Name                          | Direction | Payload                                                   |
|-----------------|-------------------------------|-----------|-----------------------------------------------------------|
| `REQ_DECLARE_TOPIC`      | declare a topic       | C→D       | `name, kind, unit`                                        |
| `REQ_PUBLISH_TOPIC_SAMPLES` | push N samples     | C→D       | `name, samples[]` (interleaved doubles)                   |
| `REQ_SUBSCRIBE_TOPIC`    | subscribe an axes     | C→D       | `name, figure_id, axes_index, series_index?`              |
| `REQ_UNSUBSCRIBE_TOPIC`  | drop subscription     | C→D       | `figure_id, axes_index, series_index`                     |
| `REQ_LIST_TOPICS`        | discovery             | C→D       | (empty)                                                   |
| `RESP_TOPIC_LIST`        | list reply            | D→C       | `topics[] { name, kind, unit, hz, total, online, subs }`  |
| `RESP_SUBSCRIBE_OK`      | subscribe reply       | D→C       | `series_index` (resolved, possibly newly created)         |
| `EVT_TOPIC_LIST_CHANGED` | broadcast             | D→C       | (empty — clients re-query)                                |

All payloads go in the existing 40-byte header + FlatBuffers body format.
`REQ_PUBLISH_TOPIC_SAMPLES` is hot path — batch samples per call (publisher
side coalesces).

### 4.2 Versioning

Bump the IPC protocol minor version. Older clients that don't know these
types remain fully compatible; daemon ignores unknown messages from old
clients and the new types are additive.

---

## 5. Publisher API

### 5.1 Public C++ header — `include/spectra/topic.hpp`

```cpp
namespace spectra
{
class Publisher
{
public:
    enum class Kind { Scalar2D, Scalar3D };

    // Connects to the running daemon (auto-starts it in multiproc mode).
    static Result<Publisher> create(std::string_view topic_name,
                                    Kind kind = Kind::Scalar2D,
                                    std::string_view unit = {});

    void publish(double x, double y);
    void publish(double x, double y, double z);
    void publish(std::span<const double> xs, std::span<const double> ys);
    void flush();   // forces an IPC send (publisher coalesces internally)

    std::string_view name() const noexcept;
    ~Publisher();   // sends graceful disconnect
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
```

Implementation in `src/ipc/publisher_client.{hpp,cpp}`. Holds its own
`ClientTransport`, batches samples with a small ring + 16 ms flush timer (or
explicit `flush()`). No `Figure`, no `Axes`, no rendering.

### 5.2 Python — `python/spectra/topic.py`

```python
import spectra as sp

pub = sp.Publisher("imu/accel", kind="2d", unit="m/s^2")
pub.publish(t, a)                # single sample
pub.publish([t0, t1], [a0, a1])  # batch
```

Wraps the same transport as `Session`. Works without `sp.figure()` ever being
called.

### 5.3 In-process mode

In `inproc` runtime mode there is no daemon. `Publisher` should still work
against an **in-process `TopicRegistry`** owned by the global `App` (or a
thread-local registry when no app exists yet, drained on app construction).
This keeps the publisher API identical across modes.

---

## 6. UI: Topics Panel + Drag-Drop

Location: new directory `src/ui/topics/` (parallel to `src/ui/panel/`).
Pattern: copy structure of `src/adapters/ros2/ui/topic_list_panel.*` and
`field_drag_drop.*`; do **not** reach into the ROS adapter at runtime — this
is the main UI, the ROS adapter remains an optional plugin.

### 6.1 Files

- `topics_panel.hpp/.cpp` — ImGui dockable panel. Columns: name, Hz, samples,
  status dot, subscriber count. Search/filter input. Refreshes on
  `EVT_TOPIC_LIST_CHANGED` or 1 Hz timer fallback.
- `topic_drag_drop.hpp/.cpp` — ImGui drag source on each row. Payload type
  `"SPECTRA_TOPIC"`, body = topic name + kind.
- `topic_drop_handler.hpp/.cpp` — drop target glue installed on axes draws
  (see §6.3).

### 6.2 Command + shortcut

Register a new command `view.toggle_topics_panel` in
`src/ui/commands/register_commands.*` with default shortcut (suggest
`Ctrl+Shift+T`). Adds a "Topics" entry to the View menu.

### 6.3 Drop target on axes

The cleanest hook is at the per-axes ImGui draw site (where axes hit-test
rects are already known). Add `BeginDragDropTarget()` around the axes content
area and accept payload `"SPECTRA_TOPIC"`. On accept, dispatch a command:

```cpp
struct SubscribeTopicCommand
{
    std::string topic_name;
    uint64_t figure_id;
    uint32_t axes_index;
};
```

Drained by the command queue at frame boundary; sends
`REQ_SUBSCRIBE_TOPIC` with `series_index = UINT32_MAX` (auto-create). Daemon
returns the new series index; UI updates legend on the next `STATE_DIFF`.

### 6.4 Optional polish (later)

Three drop "zones" per axes, like the ROS adapter:
`overlay-on-this-axes`, `new-subplot`, `new-figure`. Implement only the first
for v1.

---

## 7. Persistence

Workspace v3 format (`src/workspace/`) adds a `subscriptions: [topic_name]`
field per series. On load, the daemon attempts to re-subscribe. If the topic
is not currently online, the subscription is **kept pending** in
`TopicRegistry`; the moment a publisher declares the topic, fan-out resumes.

---

## 8. Phased Delivery

Each phase is shippable on its own and gated by its own tests.

### Phase 1 — Daemon + protocol + Publisher (no UI yet)  ✅ IMPLEMENTED
- `TopicRegistry` with ring + hz estimator (unit tests). ✅
- IPC schema additions, codec encode/decode (round-trip unit tests). ✅
- Handlers in daemon; `on_client_disconnect` cleanup. ✅
- C++ `Publisher` (`include/spectra/topic.hpp` + `src/ipc/publisher_client.cpp`). ✅
- Python `Publisher` (`python/spectra/topic.py`). ✅
- Integration test: publisher → daemon → manual `REQ_SUBSCRIBE_TOPIC` →
  series append observed in `FigureModel`. ⚠ smoke-tested manually
  (publisher + REQ_LIST_TOPICS round-trip confirmed); automated unit test
  still pending.
- Example: `examples/topic_publisher.cpp`, `python/examples/topic_publisher.py`. ✅
- Subscriber example (manual subscribe via `REQ_SUBSCRIBE_TOPIC`):
  `python/examples/topic_subscriber.py`. ✅

### Phase 2 — Topics panel + drag-drop
- `TopicsPanel` (read-only list with `REQ_LIST_TOPICS` polling).  ✅
- Drag-drop source on rows; "Plot" button performs auto-subscribe to active
  figure / axes 0.  ✅
- View-menu entry + shortcut (`Ctrl+Shift+T` → `panel.toggle_topics`).  ✅
- `EVT_TOPIC_LIST_CHANGED` broadcast wired to panel (forces immediate refresh).
  ✅
- Drop target attached to figure canvas + multi-zone (overlay / new subplot /
  new figure) drag-drop targets.  ⚠ partial — canvas overlay drop target is
  implemented (`ImGuiIntegration::draw_topic_drop_target` in
  `src/ui/imgui/imgui_panels.cpp`): while a `SPECTRA_TOPIC` payload is being
  dragged, the hovered axes is highlighted and dropping it subscribes that
  axes (axes-index resolved via `InputHandler::hit_test_all_axes`).
  Multi-zone (new-subplot / new-figure) drop targets still pending.
- Manual QA script in plan-local `REPORT.md`.  ⚠ pending

### Phase 3 — Persistence + polish
- Workspace serialization of subscriptions.  ⚠ pending — requires a
  `topic_subscription` field on `Series` plus a new
  `figure_serializer` tag.  Daemon side is ready (see pending subs).
- Pending subscriptions resume on publisher re-declare.  ✅ —
  `TopicRegistry::subscribe_pending()` queues subscriptions while a topic
  is undeclared; `declare()` promotes them atomically.  Daemon handler
  accepts `REQ_SUBSCRIBE_TOPIC` for unknown topics when an explicit
  `series_index` is supplied (auto-create still requires a live topic so
  the series kind can be resolved).
- Multi-zone drop target on axes (overlay / new subplot / new figure).
  ⚠ pending — current canvas overlay subscribes to the hovered axes only.
  Adding new-subplot/new-figure zones requires new daemon commands (create
  subplot / create figure) wired through the command queue.
- Docs page: `docs/topics.md`.  ✅

### Phase 4 (optional) — In-process parity
- In-process `TopicRegistry` so `Publisher` works without daemon.
- Bridge between local registry and any later-started daemon.

---

## 9. Testing Plan

- **Unit**: `tests/unit/topic_registry_test.cpp` — declare/publish/subscribe,
  ring overflow, hz estimator, client disconnect cleanup, pending subs.
- **Unit**: codec round-trip for every new message type.
- **Integration**: `tests/unit/topic_ipc_test.cpp` spawns a daemon, drives a
  `Publisher`, subscribes a synthetic series, asserts samples arrive in
  order. Label `gpu`-free.
- **UI smoke**: scripted via `mcp_spectra-autom` — open app, publish from a
  helper process, assert topic appears in panel, drag onto axes, assert
  series count increases.
- **Golden**: one golden image of the Topics panel populated with three
  fixed topics.

---

## 10. Risks & Open Questions

1. **In-process mode parity** — easiest is to require multiproc for v1 and
   note this clearly. Decision needed before Phase 1.
2. **Backpressure** — what happens if a publisher outruns the daemon? Drop
   oldest in ring + return `RESP_OK` (lossy by design, like UDP topics). OK?
3. **Multi-publisher per topic name** — first declare wins, second gets
   `RESP_ERR`. Or allow last-declare-wins? Recommend **first wins** to match
   ROS semantics.
4. **Type beyond x/y(/z) doubles** — explicitly out of scope for v1. Future
   message-style topics can reuse the same registry with a typed payload.
5. **Auto-axis labels** — if subscribed topic has `unit`, prefill axis label
   when this is the only series on the axes. Nice-to-have for Phase 2.

---

## 11. Files Touched (Forecast)

New:
- `include/spectra/topic.hpp`
- `src/ipc/publisher_client.{hpp,cpp}`
- `src/daemon/topic_registry.{hpp,cpp}`
- `src/ui/topics/topics_panel.{hpp,cpp}`
- `src/ui/topics/topic_drag_drop.{hpp,cpp}`
- `python/spectra/topic.py`
- `examples/topic_publisher.cpp`
- `python/examples/topic_publisher.py`
- `tests/unit/topic_registry_test.cpp`
- `tests/unit/topic_ipc_test.cpp`
- `docs/topics.md`

Modified:
- `src/ipc/schemas/spectra_ipc.fbs` (additive)
- `src/ipc/message.hpp`, `src/ipc/codec.{hpp,cpp}`
- `python/spectra/_codec.py`
- `src/daemon/daemon_server.hpp`, `src/daemon/main.cpp`,
  `src/daemon/python_message_handler.hpp`
- `src/ui/commands/register_commands.*` (new command + shortcut)
- Axes draw site for drop target (see `src/ui/figures/*` or wherever axes
  ImGui rendering lives — pinpoint during Phase 2 spike)
- `src/workspace/*` (Phase 3)
- `CMakeLists.txt` for new sources

---

## 12. Reference: ROS Adapter as Working Template

The ROS adapter already implements every UI piece we need, just bound to
ROS topics instead of Spectra-internal topics:

- `src/adapters/ros2/ui/topic_list_panel.{hpp,cpp}` — panel layout, columns,
  status dots, filter — **copy structure**.
- `src/adapters/ros2/ui/field_drag_drop.{hpp,cpp}` — drag source, three-zone
  drop target on axes, command dispatch — **copy pattern, change payload**.
- `src/adapters/ros2/ros_plot_manager.cpp` (lines ~25–90) — exact recipe for
  "subscribe a stream → create or attach a `LineSeries` → append on each
  sample with thread-safe mode" — **the daemon-side fan-out should follow
  this same shape**.

If a question arises about UI behavior during implementation, the answer is
almost always "look at how the ROS adapter does it."
