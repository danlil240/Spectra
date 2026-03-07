# Spectra ROS Studio — RViz-Class Visualization Plan

> **Status:** DRAFT v1 with implementation audit — March 7, 2026  
> **Scope:** Evolve spectra-ros from rqt-replacement to next-generation RViz-class 3D visualization  
> **Principle:** Incremental expansion on existing architecture — no big-bang rewrite

---

## Table of Contents

1. [Current Code Audit](#1-current-code-audit)
2. [Target Architecture](#2-target-architecture)
3. [RViz MVP Feature List](#3-rviz-mvp-feature-list)
4. [Display Plugin Contract](#4-display-plugin-contract)
5. [Performance Plan](#5-performance-plan)
6. [Phased Roadmap](#6-phased-roadmap)
7. [Testing & Validation](#7-testing--validation)
8. [Risk Register](#8-risk-register)

---

## 0. Mission Status Snapshot

Implementation audit as of **March 7, 2026**:

| Mission | Status | Notes |
|---------|--------|-------|
| **Phase 1: Foundation** | **Mostly complete** | Core shell, session v2, display registry, `TfBuffer`, scene viewport shell, grid display, fixed-frame UI all exist. Remaining gap: the broader ROS2 test suite is not fully green. |
| **Phase 2: TF + Markers** | **Mostly complete** | TF and marker displays render through GPU pipelines (Marker3D, Line3D, PointCloud, TextDepth). All marker types rendered. Depth-tested frame labels implemented. Remaining: stress-performance validation. |
| **Phase 3: Point Cloud + LaserScan** | **Mostly complete** | Point cloud and laser scan displays plus adapters exist. GPU pipelines (PointCloud, Line3D) are wired and rendering through SceneRenderer. Missing: performance/memory hardening validation. |
| **Phase 4: Image + Path + Pose** | **Mostly complete** | Image, path, and pose displays exist and are session-persisted. `Image3D` pipeline with GLSL shaders and Vulkan texture streaming implemented. GPU billboard rendering wired through SceneRenderer. Missing: source-rate validation, encoding-switch verification. |
| **Phase 5: Robot Model + Polish** | **Mostly complete** | URDF parser, robot-model display, joint_state_adapter, and forward-kinematics articulation are implemented. Frame/joint axis toggles added. Missing: refined picking for robot links. |
| **Phase 6: Integration & Hardening** | **Mostly complete** | Test coverage expanded (91 total ROS display/adapter tests), bench_ros3d.cpp created, ctest 103/104 green (1 unrelated topic_discovery timeout). Remaining: stress/perf/memory validation. |

Legend:
- `Mostly complete`: core scope is implemented, with validation or hardening gaps remaining.
- `Partial`: meaningful implementation exists, but major planned components are still absent.
- `Not complete`: the phase acceptance bar is clearly not met.

---

## 1. Current Code Audit

### 1.1 Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│                        spectra-ros binary                          │
│                     src/adapters/ros2/main.cpp                     │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌──────────────┐     ┌───────────────┐     ┌──────────────────┐   │
│  │  RosAppShell │────►│  Ros2Bridge   │────►│ SingleThreaded   │   │
│  │  (shell.hpp) │     │ (bridge.hpp)  │     │ Executor (bg)    │   │
│  └──────┬───────┘     └───────────────┘     └──────────────────┘   │
│         │                                                           │
│  ┌──────┴───────────────────────────────────────────────────────┐   │
│  │                    Domain Services                           │   │
│  │  ┌────────────────┐  ┌──────────────────┐  ┌─────────────┐  │   │
│  │  │TopicDiscovery  │  │GenericSubscriber │  │MsgIntrospect│  │   │
│  │  │(2s wall-timer) │  │(CDR→FieldSample) │  │(runtime type│  │   │
│  │  │graph scan      │  │per-field SPSC RB │  │ reflection) │  │   │
│  │  └────────────────┘  └────────┬─────────┘  └─────────────┘  │   │
│  │                               │ SPSC (10K)                   │   │
│  │  ┌────────────────────────────▼──────────────────────────┐   │   │
│  │  │              Data Pipeline                            │   │   │
│  │  │  RosPlotManager → LineSeries::append(t, v)            │   │   │
│  │  │  SubplotManager → NxM grid, axis linking              │   │   │
│  │  │  ExpressionPlot → expression eval → series            │   │   │
│  │  │  BagPlayer → inject_sample() bypass                   │   │   │
│  │  └───────────────────────────────────────────────────────┘   │   │
│  └──────────────────────────────────────────────────────────────┘   │
│                                                                     │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │                     UI Panels (14)                           │   │
│  │  TopicList  TopicEcho  TopicStats  DiagnosticsPanel         │   │
│  │  NodeGraph  TfTree     ParamEditor ServiceCaller            │   │
│  │  BagInfo    BagPlayback LogViewer  ExpressionEditor         │   │
│  │  FieldDragDrop                                              │   │
│  │                                                               │   │
│  │  Layout: ImGui DockSpace (not core Spectra DockSystem)      │   │
│  │  State:  RosWorkspaceState (selected topic/field + events)  │   │
│  │  Persist: RosSession / RosSessionManager (.spectra-ros-session)│ │
│  └──────────────────────────────────────────────────────────────┘   │
│                                                                     │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │                  Core Spectra Engine                         │   │
│  │  App → Figure → Axes/Axes3D → Series                        │   │
│  │  Renderer → VulkanBackend (22 pipeline types)                │   │
│  │  Camera (orbit), math3d (vec3/mat4/quat)                    │   │
│  │  TextRenderer (bitmap atlas, depth-tested)                   │   │
│  └──────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

### 1.2 Existing ROS2 Adapter (`src/adapters/ros2/`)

| Component | File | Key Details |
|-----------|------|-------------|
| **Node lifecycle** | `ros2_bridge.hpp` | `Ros2Bridge` — single node, `SingleThreadedExecutor` on bg thread, 5-state lifecycle |
| **Topic discovery** | `topic_discovery.hpp` | `TopicDiscovery` — 2s wall-timer, differential callbacks, QoS enrichment |
| **Subscriptions** | `generic_subscriber.hpp` | `GenericSubscriber` — type-erased CDR, per-field `FieldExtractor` with SPSC `RingBuffer` (10K capacity) |
| **Introspection** | `message_introspector.hpp` | `MessageIntrospector` — `rosidl_typesupport_introspection_cpp`, `MessageSchema` tree, `FieldAccessor` |
| **Plot bridge** | `ros_plot_manager.hpp` | `RosPlotManager` — drain 4096 samples/poll → `LineSeries::append()`, auto-scroll via `presented_buffer` |
| **Subplots** | `subplot_manager.hpp` | `SubplotManager` — NxM grid, `AxisLinkManager` for shared X |
| **Expressions** | `expression_engine.hpp` | `ExpressionEngine` — math expression parser/evaluator over ROS field streams |
| **Bag playback** | `bag_player.hpp` | `BagPlayer` — rosbag2 reader, state machine, rate control, seek, `inject_sample()` |
| **Shell** | `ros_app_shell.hpp` | `RosAppShell` — owns everything, ImGui dockspace, 3 layout presets (Default, PlotOnly, Monitor), `RosWorkspaceState` |
| **Session** | `ros_session.hpp` | `RosSessionManager` — save/load `.spectra-ros-session` JSON, MRU list |

### 1.3 Current 3D Renderer Capabilities

Already implemented (core Spectra, not ROS-specific):

| Capability | Status | Details |
|------------|--------|---------|
| **Axes3D** | ✅ Complete | Bounding box [-3,+3]³, directional lighting, grid planes XY/XZ/YZ |
| **Camera** | ✅ Orbit only | Perspective + ortho, serialize/deserialize, `fit_to_bounds()` |
| **LineSeries3D** | ✅ | SSBO, screen-space width, dash, SDF AA |
| **ScatterSeries3D** | ✅ | SSBO, instanced, 10+ SDF marker shapes |
| **SurfaceSeries** | ✅ | Grid→mesh, Blinn-Phong, 7 GPU colormaps |
| **MeshSeries** | ✅ | Triangle mesh, Phong, wireframe |
| **Arrow3D pipeline** | ✅ | Axis indicator arrowheads |
| **Transparency** | ✅ | Painter's algorithm, separate depth-write-off pipelines |
| **Text (depth-tested)** | ✅ | `TextDepth` pipeline, bitmap atlas |
| **Math library** | ✅ | `vec3`, `vec4`, `mat4`, `quat`, `Ray`, `unproject()`, `slerp` |
| **Selection/picking** | ❌ | `unproject()` exists but no hit-testing framework |
| **Free-fly camera** | ❌ | Only orbit |
| **Point cloud pipeline** | ❌ | Scatter3D exists but no per-point color, LOD, or streaming |
| **Instance rendering** | ❌ (partial) | Scatter3D uses instancing for markers only |

### 1.4 Current TF Panel Limitations

`TfTreePanel` (`src/adapters/ros2/ui/tf_tree_panel.hpp`):

| Feature | Status |
|---------|--------|
| Subscribe /tf + /tf_static | ✅ |
| Tree structure (parent/child maps) | ✅ |
| Transform lookup between arbitrary frames | ✅ (via chain-to-root + common ancestor) |
| Per-frame Hz, age, stale detection | ✅ |
| **Time-buffered transforms** | ❌ Only latest per frame |
| **SLERP/LERP interpolation** | ❌ |
| **Fixed frame concept** | ❌ |
| **Extrapolation policies** | ❌ |

### 1.5 Constraints List (What Blocks RViz-Class Features)

| ID | Constraint | Impact | Resolution |
|----|-----------|--------|------------|
| **C1** | No display plugin interface | Cannot add visualization types modularly | Introduce `DisplayPlugin` ABC |
| **C2** | No scene graph | No entity management, spatial queries, or render ordering for 3D scene | Build `SceneGraph` |
| **C3** | No picking/selection framework | Cannot click 3D entities for inspection | Ray-cast against scene graph |
| **C4** | TF: no time buffer or interpolation | Sensor data cannot be transformed to correct time | Build `TfBuffer` with time history |
| **C5** | TF: no fixed frame | 3D scene has no reference frame | Add fixed frame selector + `TfBuffer::lookup(target, source, time)` |
| **C6** | No point cloud pipeline | Can't visualize PointCloud2 or LaserScan | New `PointCloud` pipeline with per-point color, LOD |
| **C7** | No texture streaming | Can't display camera images in 3D viewport | Add `ImageDisplay` with texture upload ring buffer |
| **C8** | No URDF parser | Can't show robot model | Minimal URDF parser (collision shapes) |
| **C9** | All panels hard-coded in `RosAppShell` | Adding new panels requires modifying shell | Panel registry with `DisplayPlugin::draw_inspector_ui()` |
| **C10** | Single render pass | 3D scene shares pass with UI | Acceptable — reuse existing pass, add new pipelines |
| **C11** | No free-fly camera | Doesn't block MVP (orbit sufficient) | Add after MVP |
| **C12** | Scatter3D has no per-point color | Point cloud coloring requires per-vertex color | Extend SSBO format or add new pipeline |

### 1.6 Modules to Extend vs Create

**Extend existing:**
- `src/render/backend.hpp` — add `PipelineType::PointCloud`, `PipelineType::Marker3D`, `PipelineType::Image3D`
- `src/render/vulkan/vk_backend.cpp` — add pipeline configs + shaders for new types
- `src/render/renderer.hpp` — add `render_scene()` method (Phase 2) alongside existing `render_figure()` / `render_figure_content()`. Phase 1 renders through Axes3D wrapper; Phase 2 introduces the scene-based dispatch path.
- `src/adapters/ros2/ros_app_shell.hpp` — add display panel, fixed frame, scene viewport management, new layout presets (RViz, RVizPlot)
- `src/adapters/ros2/ros_session.hpp` — migrate to nlohmann::json, bump to v2, persist display configs + fixed frame + camera pose (v1 backward compat preserved)
- `src/adapters/ros2/ui/tf_tree_panel.hpp` → factor out TF data into reusable `TfBuffer`
- `include/spectra/math3d.hpp` — add `Transform` (translation + rotation) with compose/inverse
- `include/spectra/camera.hpp` — add free-fly mode (post-MVP)

**Create new modules:**

| Module | Path | Purpose |
|--------|------|---------|
| `DisplayPlugin` interface | `src/adapters/ros2/display/display_plugin.hpp` | ABC for all visualization plugins |
| `DisplayRegistry` | `src/adapters/ros2/display/display_registry.hpp` | Registration, instantiation, lifecycle |
| `DisplaysPanel` | `src/adapters/ros2/ui/displays_panel.hpp` | RViz-style add/remove/configure displays |
| `SceneManager` | `src/adapters/ros2/scene/scene_manager.hpp` | Entity ownership, render dispatch, picking |
| `SceneViewport` | `src/adapters/ros2/ui/scene_viewport.hpp` | 3D viewport panel with camera control |
| `TfBuffer` | `src/adapters/ros2/tf/tf_buffer.hpp` | Time-aware transform cache + interpolation |
| `TfDisplay` | `src/adapters/ros2/display/tf_display.hpp` | 3D frame axes + labels |
| `GridDisplay` | `src/adapters/ros2/display/grid_display.hpp` | Ground plane grid (color, alpha, plane, offset) |
| `MarkerDisplay` | `src/adapters/ros2/display/marker_display.hpp` | visualization_msgs/Marker rendering |
| `PointCloudDisplay` | `src/adapters/ros2/display/pointcloud_display.hpp` | PointCloud2 with LOD + coloring |
| `LaserScanDisplay` | `src/adapters/ros2/display/laserscan_display.hpp` | LaserScan → point/line rendering |
| `ImageDisplay` | `src/adapters/ros2/display/image_display.hpp` | Camera image texture streaming |
| `PathDisplay` | `src/adapters/ros2/display/path_display.hpp` | nav_msgs/Path line strip |
| `PoseDisplay` | `src/adapters/ros2/display/pose_display.hpp` | PoseStamped arrow rendering |
| `RobotModelDisplay` | `src/adapters/ros2/display/robot_model_display.hpp` | Simplified URDF (collision shapes) |
| `UrdfParser` | `src/adapters/ros2/urdf/urdf_parser.hpp` | Minimal URDF XML parser |
| Point cloud shader | `src/gpu/shaders/pointcloud.vert/.frag` | Per-point color, configurable point size |
| Marker shader | `src/gpu/shaders/marker3d.vert/.frag` | Instanced marker primitives |

---

## 2. Target Architecture

### 2.1 Layer Diagram

```
┌──────────────────────────────────────────────────────────────────┐
│                      Spectra ROS Studio                          │
├──────────────────────────────────────────────────────────────────┤
│                                                                    │
│  ┌──────────────────────────────────────────────────────────┐    │
│  │                    UI Layer                              │    │
│  │                                                          │    │
│  │  ┌───────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐  │    │
│  │  │ Displays  │ │  Scene   │ │Inspector │ │Existing  │  │    │
│  │  │ Panel     │ │ Viewport │ │  Panel   │ │ROS Panels│  │    │
│  │  │(add/rm/   │ │(orbit/   │ │(selected │ │(14 exist)│  │    │
│  │  │ config)   │ │ pick/    │ │ entity   │ │          │  │    │
│  │  │           │ │ gizmos)  │ │ props)   │ │          │  │    │
│  │  └─────┬─────┘ └────┬─────┘ └────┬─────┘ └──────────┘  │    │
│  └────────┼─────────────┼────────────┼──────────────────────┘    │
│           │             │            │                            │
│  ┌────────▼─────────────▼────────────▼──────────────────────┐    │
│  │                Visualization Layer                        │    │
│  │                                                          │    │
│  │  ┌────────────────────┐  ┌────────────────────────────┐  │    │
│  │  │  DisplayRegistry   │  │      SceneManager          │  │    │
│  │  │  - register<T>()   │  │  - entities[] (flat vec)   │  │    │
│  │  │  - create(type_id) │  │  - add/remove entity       │  │    │
│  │  │  - list_types()    │  │  - render(camera, tf)      │  │    │
│  │  │  - factory map     │  │  - pick(ray) → entity      │  │    │
│  │  └────────┬───────────┘  └────────────────────────────┘  │    │
│  │           │                         ▲                    │    │
│  │  ┌────────▼─────────────────────────┤                    │    │
│  │  │    DisplayPlugin (ABC)           │                    │    │
│  │  │  - on_enable() / on_disable()    │                    │    │
│  │  │  - on_message(topic, msg, schema)│                    │    │
│  │  │  - on_update(dt, tf_buffer)      │                    │    │
│  │  │  - submit_renderables(scene)     │                    │    │
│  │  │  - draw_inspector_ui()           │                    │    │
│  │  │  - serialize() / deserialize()   │                    │    │
│  │  └──────────────────────────────────┘                    │    │
│  └──────────────────────────────────────────────────────────┘    │
│                                                                    │
│  ┌──────────────────────────────────────────────────────────┐    │
│  │              Transform Layer                              │    │
│  │                                                          │    │
│  │  ┌─────────────────────────────────────────────────────┐ │    │
│  │  │  TfBuffer                                           │ │    │
│  │  │  - insert(parent, child, transform, time)           │ │    │
│  │  │  - lookup(target, source, time) → Transform         │ │    │
│  │  │  - can_transform(target, source, time) → bool       │ │    │
│  │  │  - all_frames() → vector<string>                    │ │    │
│  │  │  - frame_graph() → adjacency list                   │ │    │
│  │  │  - set_fixed_frame(frame_id)                        │ │    │
│  │  │                                                     │ │    │
│  │  │  Internals: per-edge circular buffer (256 entries)  │ │    │
│  │  │  SLERP rotation, LERP translation                   │ │    │
│  │  │  Static transforms: single entry, never expire      │ │    │
│  │  └─────────────────────────────────────────────────────┘ │    │
│  └──────────────────────────────────────────────────────────┘    │
│                                                                    │
│  ┌──────────────────────────────────────────────────────────┐    │
│  │              ROS Data Layer (existing + extended)          │    │
│  │                                                          │    │
│  │  Ros2Bridge → TopicDiscovery → GenericSubscriber          │    │
│  │  MessageIntrospector → FieldAccessor                      │    │
│  │                                                          │    │
│  │  NEW: MessageAdapter<T> — typed deserialization adapters  │    │
│  │    sensor_msgs/PointCloud2 → PointCloudFrame              │    │
│  │    sensor_msgs/LaserScan   → LaserScanFrame               │    │
│  │    sensor_msgs/Image       → ImageFrame                   │    │
│  │    visualization_msgs/Marker → MarkerData                 │    │
│  │    geometry_msgs/PoseStamped → PoseData                   │    │
│  │    nav_msgs/Path → PathData                               │    │
│  │                                                          │    │
│  │  Buffering: SPSC ring buffers (existing)                  │    │
│  │  Decimation: configurable per display                     │    │
│  │  Throttling: frame-rate-aware drain limits                │    │
│  └──────────────────────────────────────────────────────────┘    │
│                                                                    │
│  ┌──────────────────────────────────────────────────────────┐    │
│  │              Core Spectra Engine (unchanged)              │    │
│  │  App, Figure, Axes, Axes3D, Renderer, VulkanBackend       │    │
│  │  Camera, math3d, TextRenderer, CommandRegistry            │    │
│  └──────────────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────────────┘
```

### 2.2 ROS Data Layer (Extended)

The existing data pipeline (GenericSubscriber → SPSC → RosPlotManager → LineSeries) continues unchanged for 2D plotting. For 3D displays, we add a parallel typed message path:

**New: `MessageAdapter<T>` pattern** (`src/adapters/ros2/messages/`)

Each display type needs structured message data beyond numeric field extraction. The adapter pattern converts raw CDR bytes into typed internal frames:

```
ROS2 Executor Thread                      Render/App Thread
────────────────────                      ──────────────────
GenericSubscription::on_message(CDR bytes)
  → MessageAdapter<T>::decode(bytes)
  → SPSC<TypedFrame>::push()
                                    ──── SPSC Ring Buffer ────►
                                          DisplayPlugin::on_update()
                                            → SPSC<TypedFrame>::pop()
                                            → update internal state
                                            → submit_renderables(scene)
```

**Typed frames** to create:

| Internal Frame | Source Message | Key Fields |
|----------------|---------------|------------|
| `PointCloudFrame` | `sensor_msgs/PointCloud2` | positions (vec3[]), colors (rgba[]), intensities, ring, stamp, frame_id |
| `LaserScanFrame` | `sensor_msgs/LaserScan` | ranges[], intensities[], angle_min/max/increment, stamp, frame_id |
| `ImageFrame` | `sensor_msgs/Image` | width, height, encoding, data ptr, stamp, frame_id |
| `MarkerData` | `visualization_msgs/Marker` | type, pose, scale, color, points[], mesh_resource, lifetime, frame_id |
| `PoseData` | `geometry_msgs/PoseStamped` | position (vec3), orientation (quat), stamp, frame_id |
| `PathData` | `nav_msgs/Path` | poses[] (vec of position+orientation), stamp, frame_id |
| `TfData` | `tf2_msgs/TFMessage` | transforms[] (parent, child, translation, rotation, stamp) |

Each adapter is a standalone header in `src/adapters/ros2/messages/` and may use either:
- **Typed subscriptions** (`rclcpp::Subscription<T>`) for complex binary formats where field-level CDR parsing is error-prone or performance-critical (PointCloud2, Image, DiagnosticArray). This is the approach already taken by `DiagnosticsPanel`.
- **Runtime introspection** via `rosidl_typesupport_introspection_cpp` field offsets for simpler messages (PoseStamped, Path).

The choice is per-adapter — there is no blanket "runtime-only" rule. Typed subscriptions are preferred when the message format is complex or high-throughput.

### 2.3 Transform Layer

**New: `TfBuffer`** (`src/adapters/ros2/tf/tf_buffer.hpp`)

Replaces the per-frame latest-only storage in `TfTreePanel` with a time-aware transform cache:

**Data structure:**
- `edges_`: `unordered_map<FramePair, TransformHistory>` where `FramePair = {parent, child}` (canonicalized)
- `TransformHistory`: circular buffer of `{timestamp_ns, translation (vec3), rotation (quat)}`, capacity 256 per edge
- `static_transforms_`: separate map for `/tf_static` (single entry per edge, never expires)
- `frame_graph_`: adjacency list for BFS path finding

**API:**
- `insert(parent_frame, child_frame, transform, timestamp, is_static)` — called from ROS executor thread (mutex-protected insert)
- `lookup(target_frame, source_frame, timestamp) → optional<Transform>` — finds path via BFS, composes transforms, interpolates at requested time
- `can_transform(target, source, time) → bool` — fast check
- `all_frames() → vector<string>` — for fixed frame selector
- `frame_graph() → adjacency_list` — for TF tree display panel
- `set_cache_duration(seconds)` — prune entries older than duration (default 10s)

**Interpolation:** For each edge at a given time:
1. Binary search in circular buffer for bracketing timestamps
2. Compute `t = (query_time - t0) / (t1 - t0)`
3. Translation: `lerp(pos0, pos1, t)`
4. Rotation: `slerp(quat0, quat1, t)` using existing `quat_slerp()` from `math3d.hpp`
5. Compose transforms along the path from source → (common ancestor) → target

**Thread safety:** Single mutex for insert/lookup. Insert is O(1) amortized (circular buffer push). Lookup is O(path_length × log(buffer_size)).

**Fixed frame:** Stored in `RosWorkspaceState` as `std::string fixed_frame`. All displays transform data into fixed frame before rendering. Default: auto-detect from first TF root seen.

### 2.4 Scene Manager

**New: `SceneManager`** (`src/adapters/ros2/scene/scene_manager.hpp`)

Lightweight entity container — NOT a full scene graph tree. The TF tree already provides the spatial hierarchy.

```
SceneManager
├── entities_: vector<SceneEntity>
│   SceneEntity = { entity_id, display_plugin*, renderables[] }
│   Renderable = variant<PointCloudRenderable, MeshRenderable, LineRenderable, ...>
├── add_entity(display, renderables) → entity_id
├── remove_entity(entity_id)
├── update_entity(entity_id, renderables)
├── render_all(camera, frame_ubo) — dispatches to GPU
│   1. Collect all renderables
│   2. Sort: opaque front-to-back, transparent back-to-front
│   3. Bind pipelines + draw
├── pick(ray) → optional<PickResult>
│   - Ray-AABB test against entity bounding boxes
│   - Return closest hit: {entity_id, display_plugin*, world_pos}
└── clear()
```

**Renderable types** (discriminated union / variant):

| Renderable | GPU Resources | Pipeline |
|------------|--------------|----------|
| `PointCloudRenderable` | SSBO of `{vec3 pos, uint32 color_packed}` | `PipelineType::PointCloud` (new) |
| `LineStripRenderable` | SSBO of `vec4[]` (existing format) | `PipelineType::Line3D` |
| `MeshRenderable` | VBO of `{vec3 pos, vec3 normal}` + IBO | `PipelineType::Mesh3D` |
| `ArrowRenderable` | Pre-built arrow mesh + instance transform | `PipelineType::Arrow3D` |
| `AxesRenderable` | 3 arrows (RGB = XYZ) at a transform | `PipelineType::Arrow3D` × 3 |
| `GridRenderable` | Line-list VBO | `PipelineType::Grid3D` |
| `ImageRenderable` | Textured quad (new) | `PipelineType::Image3D` (new) |
| `TextRenderable` | Text label at 3D position | `PipelineType::TextDepth` |
| `BoxRenderable` | 12-edge wireframe or solid mesh | `PipelineType::Mesh3D` |
| `CylinderRenderable` | Generated cylinder mesh | `PipelineType::Mesh3D` |
| `SphereRenderable` | Generated sphere mesh | `PipelineType::Mesh3D` |

### 2.5 Workspace System (Extended)

Extend `RosSession` to persist 3D scene state.

> **Migration note:** The current session serializer is hand-rolled JSON (`serialize()`/`deserialize()` with manual string concatenation and custom helpers like `json_escape()`, `json_get_string()`, etc.) at `SESSION_FORMAT_VERSION = 1`. The display/camera/scene extensions below require nested objects and arrays that are painful to hand-roll. **Phase 1 must add `nlohmann/json.hpp` to `third_party/` and migrate `RosSession` to use it**, bumping the format version to 2. The deserializer should detect v1 sessions and load them via the legacy path (preserving existing subscriptions/expressions/panels) while writing v2 on save.

**Additional fields in session JSON (v2):**
- `fixed_frame`: string
- `camera_pose`: `{azimuth, elevation, distance, target: [x,y,z], projection: "perspective"|"orthographic", fov}`
- `displays`: array of `{type_id, enabled, topic, config: {...}}`
  - Each display type defines its own config schema (colors, sizes, decay times, etc.)
- `scene_background_color`: `[r, g, b, a]`

**Workspace presets** (extend `LayoutPreset` enum):
- `RViz` — Scene Viewport + Displays Panel + Inspector + TF Tree
- `RVizPlot` — Scene Viewport + Displays + Plot Area + Topic List
- `Monitor` — (existing) monitor-focused panels, plot hidden

---

## 3. RViz MVP Feature List

### 3.1 MVP Display Types (Priority Order)

| Priority | Display | Topic Type | Complexity | Value |
|----------|---------|-----------|------------|-------|
| **P0** | Grid | (none — built-in) | Low | Essential visual reference. Config: cell size, cell count, color, alpha, plane (xy/xz/yz), 3D offset |
| **P0** | TF Axes | `/tf`, `/tf_static` | Medium | Core ROS concept, enables all others |
| **P1** | Marker/MarkerArray | `visualization_msgs/Marker[Array]` | High | Universal ROS viz primitive |
| **P1** | PointCloud2 | `sensor_msgs/PointCloud2` | High | Most requested sensor viz |
| **P2** | LaserScan | `sensor_msgs/LaserScan` | Medium | Common 2D sensor |
| **P2** | Image | `sensor_msgs/Image` | Medium | Camera feeds |
| **P2** | Path | `nav_msgs/Path` | Low | Navigation visualization |
| **P2** | Pose | `geometry_msgs/PoseStamped` | Low | Robot pose arrow |
| **P3** | RobotModel (simplified) | URDF param + `/joint_states` | High | Robot context (collision shapes only) |

### 3.2 MVP Tools & Interactions

| Tool | Description |
|------|-------------|
| **3D Viewport** | Dockable ImGui panel with orbit camera (left drag = rotate, right drag = pan, scroll = zoom) |
| **Fixed Frame selector** | Dropdown in toolbar — all displays transform into this frame |
| **Displays Panel** | Tree view: Add Display → pick type → configure topic, color, size. Enable/disable checkbox per display. Drag to reorder |
| **Inspector** | Click entity in 3D → shows display name, topic, frame_id, last message timestamp, type-specific stats |
| **Selection highlight** | Clicked entity outlined or brightened |
| **Topic auto-complete** | Display topic selector shows discovered topics filtered by compatible message types |

### 3.3 MVP Non-Goals (Deferred)

- Interactive tools (Measure, 2D Nav Goal, Pose Estimate)
- Multiple 3D viewports
- Mesh resource loading (STL/DAE from `package://`)
- Occupancy grid
- Action client UI
- Bag recording from 3D view
- Free-fly camera
- GPU-based picking (CPU ray-AABB is sufficient for MVP)

---

## 4. Display Plugin Contract

### 4.1 Abstract Interface

Location: `src/adapters/ros2/display/display_plugin.hpp`

```
class DisplayPlugin {
public:
    // --- Identity ---
    virtual std::string type_id() const = 0;      // e.g. "pointcloud2"
    virtual std::string display_name() const = 0;  // e.g. "PointCloud2"
    virtual std::string icon() const = 0;          // icon identifier

    // --- Lifecycle ---
    virtual void on_enable(DisplayContext& ctx) = 0;   // subscribe, allocate GPU
    virtual void on_disable() = 0;                      // unsubscribe, free GPU
    virtual void on_destroy() = 0;                      // final cleanup

    // --- Data ---
    // Called on app thread each frame. Poll SPSC queues, update internal state.
    virtual void on_update(float dt, const TfBuffer& tf, const std::string& fixed_frame) = 0;

    // --- Rendering ---
    // Submit renderables to scene manager. Called after on_update().
    virtual void submit_renderables(SceneManager& scene) = 0;

    // --- UI ---
    // Draw display-specific properties in inspector panel.
    virtual void draw_inspector_ui() = 0;

    // --- Configuration ---
    virtual void set_topic(const std::string& topic) = 0;
    virtual std::string topic() const = 0;
    virtual std::vector<std::string> compatible_message_types() const = 0;

    // --- Persistence ---
    // Requires nlohmann/json.hpp (added to third_party/ in Phase 1)
    virtual nlohmann::json serialize() const = 0;
    virtual void deserialize(const nlohmann::json& j) = 0;

    // --- State ---
    bool enabled() const;
    void set_enabled(bool e);
    std::string status_text() const;      // "OK", "No data", "TF error", etc.
    DisplayStatus status() const;         // Ok, Warn, Error, Disabled

    virtual ~DisplayPlugin() = default;

protected:
    bool enabled_ = true;
    std::string status_text_ = "No data";
    DisplayStatus status_ = DisplayStatus::Disabled;
};
```

### 4.2 DisplayContext (Provided to plugins on enable)

```
struct DisplayContext {
    rclcpp::Node::SharedPtr node;              // for subscriptions
    MessageIntrospector& introspector;         // for schema lookup
    TfBuffer& tf_buffer;                       // for transforms
    Renderer& renderer;                        // for GPU resource allocation
    Backend& backend;                          // for buffer/texture creation
    std::string fixed_frame;                   // current fixed frame
};
```

### 4.3 DisplayRegistry

Location: `src/adapters/ros2/display/display_registry.hpp`

```
class DisplayRegistry {
public:
    // Registration (called at startup)
    template<typename T>
    void register_display();         // T must derive from DisplayPlugin

    // Factory
    std::unique_ptr<DisplayPlugin> create(const std::string& type_id) const;

    // Query
    std::vector<DisplayTypeInfo> list_types() const;
    // DisplayTypeInfo = { type_id, display_name, icon, compatible_types[] }
};
```

Built-in displays registered in `RosAppShell::init()`:

```
registry.register_display<GridDisplay>();
registry.register_display<TfDisplay>();
registry.register_display<MarkerDisplay>();
registry.register_display<PointCloudDisplay>();
registry.register_display<LaserScanDisplay>();
registry.register_display<ImageDisplay>();
registry.register_display<PathDisplay>();
registry.register_display<PoseDisplay>();
registry.register_display<RobotModelDisplay>();
```

### 4.4 Display Instance Lifecycle

```
[Created]
    │
    ▼ set_topic(), deserialize()
[Configured]
    │
    ▼ on_enable(ctx)  ←── subscribe to topic, allocate GPU resources
[Active]
    │
    ├─► on_update(dt, tf, fixed_frame)  ←── every frame, poll data
    │       └─► submit_renderables(scene)
    │
    ├─► draw_inspector_ui()  ←── when selected in displays panel
    │
    ▼ on_disable()  ←── unsubscribe, release GPU (keeps config)
[Disabled]
    │
    ▼ on_destroy()  ←── final cleanup
[Destroyed]
```

---

## 5. Performance Plan

### 5.1 Per-Display Performance Targets

| Display | Expected Input Rate | Max Points/Frame | Buffer Strategy | Decimation | GPU Upload | Memory Budget |
|---------|-------------------|-------------------|-----------------|------------|------------|---------------|
| **TF Axes** | 10-500 Hz (per frame) | 100 frames × 3 arrows | Latest-only per frame | None (< 1K vertices) | Static VBO (arrow mesh) + per-instance UBO | 1 MB |
| **Grid** | Static | 1 grid | None | None | Static VBO, upload once | 0.1 MB |
| **Marker** | 1-30 Hz | 10K markers | SPSC (1K capacity) | Drop oldest beyond budget | Per-marker mesh, batch similar types | 50 MB |
| **PointCloud2** | 10-30 Hz | 500K pts/frame, keep 1 frame | SPSC (4 frames) | Random subsample if > 500K | Double-buffered SSBO, staging upload | 100 MB |
| **LaserScan** | 10-40 Hz | 2K pts/scan, trail of N scans | SPSC (32 scans) | Keep last N scans (configurable, default 1) | Ring SSBO with N slots | 10 MB |
| **Image** | 15-60 Hz | 1 frame (up to 4K) | SPSC (2 frames) | Skip if > 1 frame behind | Staging buffer → VkImage, ring of 2 textures | 50 MB per camera |
| **Path** | 1-10 Hz | 10K waypoints | SPSC (4) | Decimate if > 10K points (LTTB) | SSBO upload per path msg | 5 MB |
| **Pose** | 1-50 Hz | 1 latest | SPSC (4) | Latest only | Single arrow instance | 0.1 MB |
| **RobotModel** | Static + 50-200 Hz joints | Mesh depends on URDF | N/A (static mesh) + joint SPSC | None (collision shapes are simple) | Static VBO + per-joint transform UBO | 20 MB |

### 5.2 Hard Performance Rules

1. **ROS callbacks never block rendering.** All message processing on executor thread limited to: decode → push into SPSC. Target: < 100µs per callback for point clouds, < 10µs for transforms.

2. **SPSC queues from ROS → app thread.** One queue per display instance. Overwrite-on-full semantics (drop oldest) — never stall producer.

3. **Frame-rate-aware drain limits.** Each `on_update()` drains at most N messages per frame:
   - Point cloud: 1 per frame (display latest, drop intermediate)
   - Markers: up to 100 per frame, process remainder next frame
   - TF: drain all (lightweight)
   - Image: 1 per frame (latest)
   - Scan: up to 4 per frame

4. **GPU upload budget per frame.** Cap total staging→device transfers to 32 MB/frame. If exceeded, defer remaining uploads to next frame. Track via `SceneManager::frame_upload_bytes_`.

5. **Deferred GPU resource deletion.** Use existing deferred deletion pattern (`pending_buffer_frees_` with frame stamp). Never `vkFree` on app thread.

6. **Memory pressure:** Each display type has a `max_memory_mb` config (user-adjustable). Display enters `DisplayStatus::Warn` at 80% and `Error` at 100% — stops accepting new data until freed.

### 5.3 Point Cloud Pipeline (Most Complex)

```
ROS Executor Thread                    App/Render Thread
───────────────────                    ──────────────────
on_message(CDR bytes)
  → parse PointCloud2 header
  → extract position offsets (x,y,z field descriptors)
  → extract color (rgb/rgba/intensity field)
  → if point_count > max_points:
      random_subsample(points, max_points)
  → pack into PointCloudFrame {
      vec3[] positions,
      uint32[] colors_packed,
      point_count,
      stamp, frame_id
    }
  → SPSC::push(frame)               SPSC::pop(frame)
                                     → tf_buffer.lookup(fixed_frame, frame.frame_id, frame.stamp)
                                     → transform all positions (SIMD if available)
                                     → upload to SSBO via staging buffer
                                     → submit PointCloudRenderable to SceneManager

GPU: PointCloud vertex shader
  → read from SSBO: position (vec3) + color (uint32)
  → billboard quad per point (like scatter3d but with per-point color)
  → configurable point_size via push constant
  → depth test ON, depth write ON (opaque) or OFF (transparent)
```

**LOD strategy for point clouds:**
- Configurable `max_points` per display (default 500K)
- If incoming frame exceeds limit: random subsample on executor thread
- Future: spatial octree decimation on app thread for camera-distance LOD

### 5.4 Image Display Pipeline

```
ROS Executor Thread                    App/Render Thread
───────────────────                    ──────────────────
on_message(CDR bytes)
  → parse Image header (width, height, encoding)
  → if encoding != "rgb8":
      convert to rgb8 (bgr8→rgb8, mono8→rgb8, etc.)
  → ImageFrame { width, height, rgb8_data, stamp, frame_id }
  → SPSC::push(frame)               SPSC::pop(frame)        
                                     → staging_upload to VkImage
                                     → update descriptor set
                                     → submit ImageRenderable (textured quad in 3D
                                       or fullscreen in 2D image panel)

GPU: Image3D pipeline
  → Textured quad at display-configured position/size
  → Combined image sampler from uploaded VkImage
  → Optional: billboard (always face camera) or fixed plane
```

**Texture ring:** 2 VkImages per ImageDisplay, alternating. Current frame renders from texture N while next frame uploads to texture N+1. Avoids stalling GPU.

---

## 6. Phased Roadmap

### Phase 1: Foundation (2-3 weeks)

**Mission status (March 7, 2026):** `Mostly complete`

**What is implemented now:**
- `RosAppShell` owns `DisplayRegistry`, `SceneManager`, fixed-frame state, RViz/RViz+Plot layouts, scene viewport, displays panel, and inspector panel.
- `RosSession` was migrated to v2 JSON with backward-compatible v1 loading, and now persists fixed frame, displays, dock layout, and viewport camera/background state.
- `TfBuffer` was factored out of `TfTreePanel`, and dedicated unit tests exist.
- `display_plugin`, `display_registry`, `scene_manager`, `displays_panel`, `scene_viewport`, and `grid_display` modules all exist.

**Known gaps vs the original Phase 1 target:**
- The scene viewport is still a lightweight ImGui preview rather than a true Axes3D/Vulkan-backed 3D viewport.
- The plan's “`ctest -L ros2 --output-on-failure` all green” verification bar is not met yet.

**Goal:** Plugin skeleton + TfBuffer + scene viewport shell + first display (Grid). The framework is wired end-to-end with one concrete display type rendering through the existing Axes3D/Grid3D pipeline. Scene-based rendering (`render_scene()`) is deferred to Phase 2.

**Files/modules touched:**
- EXTEND: `src/adapters/ros2/ros_app_shell.hpp/.cpp` — add `DisplayRegistry`, `SceneManager`, fixed frame state, new `RViz` and `RVizPlot` layout presets (added to existing `LayoutMode` enum alongside Default/PlotOnly/Monitor)
- EXTEND: `src/adapters/ros2/ros_session.hpp/.cpp` — migrate to nlohmann::json, bump format to v2, add display configs + fixed frame + camera to session JSON (v1 backward-compat read path preserved)
- EXTEND: `src/adapters/ros2/ui/tf_tree_panel.hpp/.cpp` — factor TF data storage out into `TfBuffer`
- EXTEND: `include/spectra/math3d.hpp` — add `struct Transform { vec3 translation; quat rotation; }` with `compose()`, `inverse()`, `to_mat4()`
- ADD: `third_party/nlohmann/json.hpp` — single-header JSON library (required for nested display config serialization; the existing hand-rolled serializer cannot handle the complexity)

**New modules created:**
- `src/adapters/ros2/display/display_plugin.hpp` — ABC
- `src/adapters/ros2/display/display_registry.hpp` — factory + type list
- `src/adapters/ros2/tf/tf_buffer.hpp` — time-aware TF cache (factored from `TfTreePanel`)
- `src/adapters/ros2/scene/scene_manager.hpp` — entity container (empty render dispatch — `render_all()` is a no-op in Phase 1)
- `src/adapters/ros2/ui/displays_panel.hpp/.cpp` — displays list (add/remove/enable, no 3D yet)
- `src/adapters/ros2/ui/scene_viewport.hpp/.cpp` — dockable ImGui panel with camera orbit (wraps an Axes3D instance for rendering; scene viewport owns its own Figure + Axes3D + Camera, separate from plot figures)
- `src/adapters/ros2/display/grid_display.hpp/.cpp` — first plugin (renders via the scene viewport's Axes3D using the existing Grid3D pipeline — does NOT require `render_scene()`, which is added in Phase 2)

**Acceptance criteria:**
1. User sees "Displays" panel in dock layout with an "Add Display" button
2. User can add a "Grid" display — ground plane grid renders in 3D viewport
3. Fixed frame dropdown appears in toolbar (populated from TfBuffer)
4. Session save/load persists display list + fixed frame
5. `TfBuffer` unit tests pass: insert/lookup/interpolation/can_transform
6. Grid display renders at 60 FPS with no hitches

**Acceptance status (March 7, 2026, updated):**
- `[x]` Displays panel exists with add/remove/configure workflow.
- `[x]` Grid display renders through `SceneRenderer` via `Grid3D` pipeline.
- `[x]` Fixed frame selector exists in the main toolbar.
- `[x]` Session save/load persists display list and fixed frame.
- `[x]` `TfBuffer` unit tests exist and pass.
- `[ ]` 60 FPS / no hitch validation has not been established in this document or test suite.

**Verification steps:**
1. Launch `spectra-ros`. The new "RViz" layout preset (created in this phase, added to the LayoutMode enum alongside the existing Default/PlotOnly/Monitor presets) is selectable from the layout dropdown.
2. Select "RViz" layout → verify 3D viewport panel appears with orbit camera
3. Click "Add Display" → "Grid" → ground plane visible in the scene viewport's Axes3D
4. In a terminal: `ros2 run tf2_ros static_transform_publisher 0 0 0 0 0 0 world base_link`
5. Fixed frame dropdown shows "world" and "base_link"
6. Save session → restart → grid + fixed frame restored (session is now v2 format; verify a v1 session from before the migration still loads correctly with existing subscriptions preserved)
7. Run `ctest -L ros2 --output-on-failure` — all green
8. Verify existing TfTreePanel still shows Hz, age, and stale detection correctly after the TfBuffer refactor

**Risks & mitigations:**
- Risk: Factoring `TfBuffer` from `TfTreePanel` breaks existing TF UI → Mitigation: `TfTreePanel` becomes a thin UI wrapper over shared `TfBuffer`. Integration test: TfTreePanel still shows Hz, age, stale correctly.
- Risk: Scene viewport camera conflicts with existing Axes3D mouse handling → Mitigation: Scene viewport owns its own `Camera` instance and its own Figure/Axes3D, fully separate from plot figure cameras
- Risk: Session format migration breaks existing saved sessions → Mitigation: Deserializer detects format version; v1 sessions load via legacy hand-rolled path, all saves write v2 (nlohmann::json). Add unit test for v1→v2 migration round-trip.

---

### Phase 2: TF + Markers (2-4 weeks)

**Mission status (March 7, 2026):** `Mostly complete`

**What is implemented now:**
- `tf_display`, `marker_display`, `marker_adapter`, `tf_adapter`, and `inspector_panel` exist.
- `SceneManager::pick()` exists with ray-vs-AABB picking.
- Marker lifetime expiration and marker deletion behavior are covered by unit tests.
- All marker types (ARROW, CUBE, SPHERE, CYLINDER, LINE_STRIP, LINE_LIST, POINTS, TEXT_VIEW_FACING) render through GPU pipelines.
- Depth-tested TF frame labels render via TextDepth pipeline.

**Known gaps vs the original Phase 2 target:**
- ~~`src/render/backend.hpp` does not define `PipelineType::Marker3D`.~~ **RESOLVED**: `PipelineType::Marker3D` (opaque + transparent) exists in `vk_backend.cpp` with `marker3d.vert/.frag` shaders.
- ~~`Renderer::render_scene()` does not exist.~~ **RESOLVED**: `SceneRenderer::render()` is a standalone method that takes `SceneManager` entities and dispatches them through Vulkan pipelines (Grid3D, Marker3D, PointCloud, Line3D). This approach was chosen over adding `render_scene()` to the core `Renderer` class to keep ROS-specific rendering separate from the core engine.
- ~~`src/adapters/ros2/scene/mesh_primitives.hpp/.cpp` is absent.~~ **RESOLVED**: `mesh_primitives.hpp` with `generate_cube()`, `generate_sphere()`, `generate_cylinder()`, `generate_arrow()`, `generate_cone()` is fully implemented.
- ~~`src/gpu/shaders/marker3d.vert/.frag` is absent.~~ **RESOLVED**: Both shaders exist and compile to SPIR-V.
- ~~**Remaining gap**: Marker types LINE_STRIP, LINE_LIST, POINTS, and TEXT_VIEW_FACING are parsed by the adapter but not rendered.~~ **RESOLVED**: `MarkerDisplay` now submits LINE_STRIP/LINE_LIST as `ScenePolyline` entities (rendered via Line3D pipeline), POINTS as `ScenePointSet` entities (rendered via PointCloud pipeline), and TEXT_VIEW_FACING as depth-tested text labels (rendered via TextDepth pipeline in SceneRenderer pass 5). A double-rendering bug where these types were also drawn as default cubes in pass 2 has been fixed.

**Goal:** TF axes rendering in 3D + Marker/MarkerArray display — the two most fundamental RViz display types.

**Files/modules touched:**
- EXTEND: `src/render/backend.hpp` — add `PipelineType::Marker3D`
- EXTEND: `src/render/vulkan/vk_backend.cpp` — new pipeline config for Marker3D (instanced primitives)
- EXTEND: `src/render/renderer.hpp/.cpp` — add `render_scene()` method that takes SceneManager renderables + camera + TF UBO. This is the transition point away from the Phase 1 Axes3D-backed rendering: displays now submit renderables to SceneManager, and `render_scene()` dispatches them through the Vulkan backend. The existing `render_figure()` / `render_figure_content()` paths remain unchanged for 2D plots.
- EXTEND: `src/adapters/ros2/scene/scene_manager.hpp/.cpp` — implement `render_all()`, `pick()` with ray-AABB
- EXTEND: `src/adapters/ros2/display/grid_display.hpp/.cpp` — migrate from Axes3D grid to SceneManager renderable (GridRenderable → Line3D pipeline)

**New modules created:**
- `src/adapters/ros2/display/tf_display.hpp/.cpp` — subscribe /tf + /tf_static, render frame axes in 3D
- `src/adapters/ros2/display/marker_display.hpp/.cpp` — visualization_msgs/Marker + MarkerArray
- `src/adapters/ros2/messages/marker_adapter.hpp` — CDR→MarkerData conversion
- `src/adapters/ros2/messages/tf_adapter.hpp` — CDR→TfData conversion (factored from tf_tree_panel)
- `src/adapters/ros2/ui/inspector_panel.hpp/.cpp` — selected entity property viewer
- `src/gpu/shaders/marker3d.vert/.frag` — instanced marker primitives (cubes, spheres, cylinders, arrows, line strips, triangle lists)
- Primitive mesh generators: `src/adapters/ros2/scene/mesh_primitives.hpp` — `generate_cube()`, `generate_sphere()`, `generate_cylinder()`, `generate_arrow()`, `generate_cone()`

**Acceptance criteria:**
1. TF Display: every frame in TfBuffer renders as RGB axes in 3D viewport
2. Frame labels visible (depth-tested text)
3. TF axes transform correctly when fixed frame changes
4. Marker Display: supports marker types ARROW, CUBE, SPHERE, CYLINDER, LINE_STRIP, LINE_LIST, POINTS, TEXT_VIEW_FACING
5. MarkerArray: batch of markers renders efficiently
6. Marker lifetime: markers expire after `lifetime` duration
7. Click entity in 3D → Inspector panel shows entity details
8. 100 markers rendering at 30 Hz — maintains 60 FPS

**Acceptance status (March 7, 2026, updated):**
- `[x]` TF display submits frame entities with RGB axis arrows rendered through `Marker3D` pipeline via `SceneRenderer`.
- `[x]` Depth-tested frame labels are rendered via TextDepth pipeline integration in `SceneRenderer` pass 5.
- `[x]` Fixed-frame transform is applied via `TfBuffer::lookup_transform()` in all displays before submitting entities.
- `[x]` Marker display renders ARROW, CUBE, SPHERE, CYLINDER, CONE primitives through Marker3D GPU pipeline.
- `[x]` Marker types LINE_STRIP, LINE_LIST rendered as polylines through Line3D GPU pipeline. POINTS rendered through PointCloud pipeline. TEXT_VIEW_FACING rendered as depth-tested text labels via TextDepth pipeline.
- `[x]` MarkerArray subscription exists alongside single-marker subscription.
- `[x]` Inspector selection works via ray-AABB picking in `SceneViewport` → `SceneManager`.
- `[ ]` Marker stress-performance target is not validated.

**Verification steps:**
1. `ros2 run tf2_ros static_transform_publisher 0 0 1 0 0 0 world camera_link`
2. Add TF Display → frame axes visible at world origin and at (0,0,1) for camera_link
3. Change fixed frame to "camera_link" → world axes move to (0,0,-1)
4. Publish markers: `ros2 topic pub /markers visualization_msgs/Marker ...`
5. Markers appear in 3D, correctly positioned in fixed frame
6. Click a marker → Inspector shows topic, frame_id, type, timestamp
7. Marker with `lifetime = 2s` disappears after 2 seconds
8. Run stress: 500 markers at 10 Hz — p95 frame time < 16ms

**Risks & mitigations:**
- Risk: Marker type coverage is large (11 types in RViz) → Mitigation: MVP supports 8 types (defer MESH_RESOURCE, TRIANGLE_LIST transparency)
- Risk: Marker namespace/id management is complex → Mitigation: HashMap<namespace+id, MarkerEntity> with expiration sweep each frame

---

### Phase 3: Point Cloud + LaserScan (3-4 weeks)

**Mission status (March 7, 2026):** `Mostly complete`

**What is implemented now:**
- `pointcloud_display`, `laserscan_display`, `pointcloud_adapter`, and `laserscan_adapter` exist.
- Unit tests cover adapter/display basics, TF-fixed-frame resolution, and config round-trips.

**Known gaps vs the original Phase 3 target:**
- ~~`PipelineType::PointCloud` is absent from the renderer backend.~~ **RESOLVED**: Exists and is wired in `SceneRenderer`.
- ~~`src/gpu/shaders/pointcloud.vert/.frag` is absent.~~ **RESOLVED**: Both shaders exist.
- ~~`SceneManager` still handles generic `SceneEntity` records, not dedicated `PointCloudRenderable` / `LaserScanRenderable` objects.~~ Architecture decision: `SceneEntity` with `ScenePointSet` is dispatched by `SceneRenderer` to the GPU pipeline.
- ~~The current displays emit summary entities into the scene preview rather than GPU point/line submissions.~~ **RESOLVED**: Displays emit `ScenePointSet` entities rendered through PointCloud GPU pipeline.
- The plan’s performance and memory-budget claims are not backed by current test or benchmark evidence.

**Goal:** GPU-accelerated point cloud and laser scan rendering with LOD and configurable coloring.

**Files/modules touched:**
- EXTEND: `src/render/backend.hpp` — add `PipelineType::PointCloud`
- EXTEND: `src/render/vulkan/vk_backend.cpp` — point cloud pipeline (SSBO vec3+uint32 color)
- EXTEND: `src/adapters/ros2/scene/scene_manager.hpp` — handle `PointCloudRenderable`, `LaserScanRenderable`

**New modules created:**
- `src/gpu/shaders/pointcloud.vert/.frag` — per-point color from SSBO, configurable point size, depth test
- `src/adapters/ros2/display/pointcloud_display.hpp/.cpp` — PointCloud2 display with:
  - Color modes: flat, intensity, height (z), ring, RGB from message
  - Configurable point size (1-10 px)
  - Max points limit with random subsample
  - Decay time (keep N seconds of scans, or 1 frame)
- `src/adapters/ros2/display/laserscan_display.hpp/.cpp` — LaserScan display:
  - Render as points or lines
  - Color by intensity or range
  - Configurable range filter (min/max)
  - Trail: keep last N scans with fading opacity
- `src/adapters/ros2/messages/pointcloud_adapter.hpp` — parse PointCloud2 field descriptors, extract xyz + color
- `src/adapters/ros2/messages/laserscan_adapter.hpp` — convert polar to cartesian, pack positions + colors

**Acceptance criteria:**
1. Add PointCloud2 display, select topic → point cloud renders in 3D viewport
2. Color mode selector works: flat / intensity / height / RGB
3. Point size slider adjusts rendered size
4. 100K point cloud at 10 Hz — sustained 60 FPS
5. 500K point cloud at 10 Hz — sustained 30 FPS with decimation active
6. LaserScan display renders as points or lines
7. Laser scan trail: last 10 scans visible with fading opacity
8. Both displays transform correctly with fixed frame via TfBuffer
9. Memory usage stays within configured budget

**Acceptance status (March 7, 2026, updated):**
- `[x]` PointCloud2 display renders through the PointCloud GPU pipeline with SSBO per-point color upload.
- `[x]` Color modes (Flat, Intensity, Height, RGB) and point-size configuration exist and work.
- `[ ]` 100K / 500K performance targets are not validated.
- `[x]` LaserScan display renders as points (PointCloud pipeline) or lines (Line3D pipeline) with trail support and fading opacity.
- `[x]` Fixed-frame transform logic exists in both displays.
- `[ ]` Memory-budget enforcement is not implemented as described.

**Verification steps:**
1. Play a rosbag with PointCloud2 data: `ros2 bag play <bag> --loop`
2. Add PointCloud2 display, select topic
3. Verify point cloud appears, correctly oriented in fixed frame
4. Switch color modes — visual change confirmed
5. Monitor FPS in status bar during 100K@10Hz playback
6. Similarly test LaserScan with a 2D lidar bag
7. Check `VmRSS` growth over 5 minutes — should plateau
8. Run `ctest -L ros2 --output-on-failure` — all green

**Risks & mitigations:**
- Risk: PointCloud2 field layouts vary (x/y/z at different offsets, different types) → Mitigation: `pointcloud_adapter.hpp` uses runtime field descriptor parsing, handles float32/float64, big/little endian
- Risk: 500K points at 30 Hz = 15M points/sec GPU upload → Mitigation: capped at 500K/frame, staging buffer reuse, skip intermediate frames
- Risk: Memory growth from point cloud history → Mitigation: configurable decay (default: keep 1 frame only), explicit memory cap per display

---

### Phase 4: Image + Path + Pose (2-3 weeks)

**Mission status (March 7, 2026):** `Mostly complete`

**What is implemented now:**
- `image_display`, `path_display`, `pose_display`, `image_adapter`, and `path_adapter` exist.
- Image display supports an ImGui preview window and a billboard-style scene entity.
- Path and pose displays submit scene entities and are covered by targeted tests.
- Session persistence now includes these displays through the generic display save/load path.

**Known gaps vs the original Phase 4 target:**
- ~~`PipelineType::Image3D` is absent from the renderer backend.~~ **RESOLVED**: Added to `PipelineType` enum, pipeline created with `text_pipeline_layout_` for texture binding.
- ~~`src/gpu/shaders/image3d.vert/.frag` is absent.~~ **RESOLVED**: Both shaders created — vertex transforms 3D quad with UVs, fragment samples `sampler2D` texture.
- ~~The planned Vulkan texture ring-buffer streaming path is not implemented; image display currently relies on CPU preview data and simplified billboard metadata.~~ **RESOLVED**: `ImageAdapter` now retains full-resolution RGBA data when in billboard mode. `SceneRenderer` creates/updates `TextureHandle` via `create_texture()` and draws textured quads through the `Image3D` pipeline. `ImageDisplay::submit_renderables()` populates `SceneImage` with full-res data.

**Goal:** Camera image display, navigation path, and pose arrow — completing the sensor+nav display set.

**Files/modules touched:**
- EXTEND: `src/render/backend.hpp` — add `PipelineType::Image3D` (textured quad)
- EXTEND: `src/render/vulkan/vk_backend.cpp` — image pipeline, texture creation/destroy for streaming

**New modules created:**
- `src/gpu/shaders/image3d.vert/.frag` — textured quad with depth test
- `src/adapters/ros2/display/image_display.hpp/.cpp` — sensor_msgs/Image display:
  - Modes: 2D panel (ImGui::Image) and 3D billboard
  - Encoding support: rgb8, bgr8, mono8, rgba8, 16UC1
  - Texture ring buffer (2 VkImages)
  - Transport hints config (raw, compressed — future)
- `src/adapters/ros2/display/path_display.hpp/.cpp` — nav_msgs/Path:
  - Line strip through path poses
  - Configurable color, width, alpha
  - Optional pose arrows at each waypoint
- `src/adapters/ros2/display/pose_display.hpp/.cpp` — geometry_msgs/PoseStamped:
  - Arrow at pose position/orientation
  - Configurable color, shaft length/width, head length/width
- `src/adapters/ros2/messages/image_adapter.hpp` — Image encoding conversion
- `src/adapters/ros2/messages/path_adapter.hpp` — Path/Pose extraction

**Acceptance criteria:**
1. Image display shows camera feed in a dockable 2D panel
2. Image display updates at source rate (up to 30 Hz) without frame drops
3. Path display shows line strip through navigation waypoints
4. Pose display shows orientation arrow, correctly transformed
5. All three displays persist in session save/load
6. Image display handles encoding switching (e.g., mono8 ↔ rgb8) gracefully

**Acceptance status (March 7, 2026, updated):**
- `[x]` Image display shows live images in a dockable ImGui panel with CPU-based rendering.
- `[ ]` Source-rate/no-frame-drop behavior has not been validated against the target.
- `[x]` **GPU texture streaming implemented** — Image3D pipeline exists, shaders compiled, `SceneRenderer` creates/updates textures and renders textured billboard quads through the Image3D pipeline. Full-res RGBA uploaded via `create_texture()`.
- `[x]` Path display renders through Line3D GPU pipeline with configurable color/width/alpha.
- `[x]` Pose display renders as arrow through Marker3D GPU pipeline.
- `[x]` Displays persist through the generic session system.
- `[~]` Encoding handling exists for the MVP subset (rgb8/bgr8/rgba8/mono8/mono16), but graceful runtime switching has not been verified.

**Verification steps:**
1. Run camera node: `ros2 run image_tools cam2image`
2. Add Image display → live video feed in panel
3. Publish path: `ros2 topic pub /plan nav_msgs/Path ...`
4. Add Path display → line strip visible in 3D
5. Verify FPS holds at 60 during 30Hz image streaming

**Risks & mitigations:**
- Risk: Image encoding jungle (Bayer, YUV, compressed) → Mitigation: MVP supports rgb8/bgr8/mono8/rgba8/16UC1 only, others show "unsupported encoding" warning
- Risk: VkImage creation per frame is expensive → Mitigation: Pre-allocate 2 VkImages at max resolution, reuse

---

### Phase 5: Robot Model + Polish (3-5 weeks)

**Mission status (March 7, 2026):** `Mostly complete`

**What is implemented now:**
- `urdf_parser` exists and parses simplified collision geometry.
- `robot_model_display` exists, loads robot description text, and submits collision-shape scene entities.
- `joint_state_adapter.hpp` parses sensor_msgs/JointState into `JointStateFrame`.
- Robot model display subscribes to `/joint_states`, runs forward kinematics (revolute, continuous, prismatic, fixed joints), and articulates the model in real-time.
- Config serialization includes the joint topic.
- Unit tests cover config round-trip, URDF parsing, collision entity submission, FK identity, revolute rotation, prismatic translation, and FK-composed entity transforms (8 tests total).

**Known gaps vs the original Phase 5 target:**
- ~~`src/adapters/ros2/messages/joint_state_adapter.hpp` is absent.~~ Created.
- ~~The robot model display does not subscribe to `/joint_states` or articulate the model.~~ Implemented with full FK.
- The display does not yet implement the planned collision/frame/joint-axis toggles.
- Refined per-triangle picking for robot links is not implemented in `SceneManager`.

**Goal:** Simplified URDF robot model display + selection/picking polish + performance hardening.

**Files/modules touched:**
- EXTEND: `src/adapters/ros2/scene/scene_manager.hpp` — refined picking (per-triangle for mesh entities)
- EXTEND: `src/adapters/ros2/scene/mesh_primitives.hpp` — reuse for URDF collision shapes

**New modules created:**
- `src/adapters/ros2/urdf/urdf_parser.hpp/.cpp` — minimal URDF XML parser:
  - Parse `<robot>` → `<link>` → `<collision>` → `<geometry>` (box/cylinder/sphere)
  - Parse `<joint>` → parent/child links, joint type, axis, limits
  - Ignore `<visual>`, `<mesh>` resources (simplified MVP)
  - Output: `RobotDescription { links[], joints[] }` with collision geometry
- `src/adapters/ros2/display/robot_model_display.hpp/.cpp` — displays URDF collision shapes:
  - Subscribes to `/robot_description` parameter (string)
  - Subscribes to `/joint_states` for articulation
  - Renders collision shapes (box/cylinder/sphere) per link
  - Transforms each link using TfBuffer (or joint_states FK)
  - Per-link color (default: semi-transparent grey, configurable)
  - Toggle: show collision shapes, show frame axes, show joint axes
- `src/adapters/ros2/messages/joint_state_adapter.hpp` — parse sensor_msgs/JointState

**Acceptance criteria:**
1. Load URDF from `/robot_description` parameter — collision shapes appear
2. Joint state updates articulate the model in real-time
3. Each link correctly positioned via TfBuffer
4. Toggle collision shapes / frame axes / joint axes
5. Selection: click a link → Inspector shows link name, joint name, joint position
6. Performance: 50-DOF robot at 100 Hz joint states — 60 FPS maintained

**Acceptance status (March 7, 2026, updated):**
- `[x]` URDF collision shapes (box/cylinder/sphere) load and render through Marker3D GPU pipeline.
- `[x]` Joint-state articulation implemented — robot model subscribes to `/joint_states` and runs FK.
- `[x]` Link transforms driven by forward kinematics (revolute/continuous/prismatic/fixed joints).
- `[x]` `show_collision_shapes_` toggle exists and frame/joint axis toggles are implemented (`show_frame_axes_`, `show_joint_axes_`).
- `[~]` Generic scene picking works for robot entities, but link-specific joint name/position inspection is absent.
- `[ ]` Performance target is not validated.

**Verification steps:**
1. Launch robot_state_publisher with a URDF
2. Add RobotModel display → collision shapes visible
3. Publish joint_states → model articulates
4. Click a link → Inspector shows details
5. Toggle collision shapes off → shapes disappear, frame axes remain
6. Run with complex URDF (e.g., Panda arm) — 60 FPS maintained

**Risks & mitigations:**
- Risk: URDF parsing edge cases (xacro, mesh references, complex joints) → Mitigation: MVP handles box/cylinder/sphere geometry only, xacro must be pre-processed, log warnings for unsupported features
- Risk: FK computation for non-TF-published links is complex → Mitigation: Prefer TF for link transforms (standard ROS practice). FK fallback only for links not published to TF

---

### Phase 6: Integration & Hardening (2-3 weeks)

**Mission status (March 7, 2026):** `Mostly complete`

**What is implemented now:**
- All display modules and focused unit tests exist with expanded coverage.
- Session save/load includes displays, fixed frame, dock layout, and viewport camera/background state.
- bench_ros3d.cpp benchmark suite created covering TfBuffer, PointCloud, LaserScan, Marker, and SceneManager.
- Test counts: pointcloud 24, laserscan 17, image 21, marker 29 (total 91 display/adapter tests).

**Known blockers vs the original Phase 6 target:**
- Stress/performance/memory targets not validated with live ROS data.
- `unit_test_topic_discovery` has a pre-existing intermittent timeout (unrelated to display tests).

**Goal:** End-to-end robustness, stress testing, documentation, workspace polish.

**Files/modules touched:**
- EXTEND: `src/adapters/ros2/ros_app_shell.hpp` — unified toolbar, workspace presets, help/about
- EXTEND: `src/adapters/ros2/ros_session.hpp` — full 3D session round-trip
- EXTEND: `tests/unit/` — new tests for all display types
- EXTEND: `tests/bench/` — new ROS3D benchmarks

**New modules created:**
- `tests/unit/test_tf_buffer.cpp` — 60+ tests for TfBuffer (interpolation, BFS, edge cases)
- `tests/unit/test_display_registry.cpp` — registration, factory, lifecycle
- `tests/unit/test_pointcloud_adapter.cpp` — field parsing, endianness, subsampling
- `tests/unit/test_marker_display.cpp` — marker types, lifetime, namespace management
- `tests/unit/test_scene_manager.cpp` — add/remove entities, picking, render dispatch
- `tests/unit/test_urdf_parser.cpp` — URDF parsing, joint types, collision geometry
- `tests/bench/bench_ros3d.cpp` — point cloud upload, marker batch, TF lookup throughput

**Acceptance criteria:**
1. All 9 display types functional end-to-end
2. Workspace save/load round-trips perfectly (displays + camera + fixed frame)
3. Stress test passes: 30Hz point cloud (100K) + 60Hz TF (100 frames) + 30Hz image (640×480) + 10Hz markers (200) running simultaneously for 5 minutes
4. Memory growth < 5 MB over 5 minutes (steady state)
5. P95 frame time < 16ms, P99 < 20ms during stress test
6. Zero Vulkan validation errors in debug build
7. All new unit tests pass
8. `ctest -L ros2 --output-on-failure` — 100% pass

**Acceptance status (March 7, 2026, updated):**
- `[x]` All nine display classes exist and render through GPU pipelines. Marker LINE_STRIP/LIST/POINTS/TEXT_VIEW_FACING are now fully rendered. Robot model supports collision/frame/joint axis toggles.
- `[~]` Workspace save/load covers much of the 3D session state, but "perfect round-trip" is not proven.
- `[ ]` Stress/performance/memory targets are not validated.
- `[ ]` Zero Vulkan validation errors have not been established in this plan.
- `[x]` All 91 ROS2 display/adapter unit tests pass (pointcloud 24, laserscan 17, image 21, marker 29).
- `[x]` bench_ros3d.cpp benchmark suite created with TfBuffer, PointCloud, LaserScan, Marker batch, and SceneManager benchmarks.
- `[x]` `ctest --test-dir build -LE gpu` — 103/104 pass (1 unrelated topic_discovery intermittent timeout).

**Verification steps:**
1. Launch full stress test (script provided):
   ```bash
   # Terminal 1: TF
   ros2 run tf2_ros static_transform_publisher 0 0 0 0 0 0 world base_link
   # Terminal 2: Point cloud
   ros2 bag play pointcloud_bag.db3 --loop --rate 1.0
   # Terminal 3: Image
   ros2 run image_tools cam2image --ros-args -p frequency:=30.0
   # Terminal 4: Markers
   ros2 run spectra_ros_test marker_publisher --ros-args -p count:=200 -p rate:=10.0
   ```
2. In Spectra ROS Studio: add all 4 display types
3. Monitor FPS counter and memory for 5 minutes
4. Save workspace → close → reopen → load workspace → verify restoration
5. Run `ctest -L ros2 --output-on-failure`
6. Run with `VK_LAYER_KHRONOS_validation` enabled — zero errors

---

## 7. Testing & Validation

### 7.1 Test Categories

| Category | Tool | Target |
|----------|------|--------|
| **Unit tests** | Google Test | Each new module: TfBuffer, DisplayRegistry, adapters, parsers, scene manager |
| **Integration tests** | Google Test + ROS2 fixture | Display lifecycle: enable → receive data → render → disable |
| **Benchmark tests** | Google Benchmark | Point cloud upload, TF lookup, marker batch, image texture upload |
| **Stress tests** | QA agent / manual | Multi-display concurrent, 5-minute sustained |
| **Memory tests** | Valgrind / ASan | No leaks, no unbounded growth |
| **GPU validation** | VK_LAYER_KHRONOS_validation | Zero validation errors |
| **Golden tests** | Headless PNG comparison | Grid, TF axes, markers — pixel regression |

### 7.2 Unit Test Plan

| Test File | Module | Min Tests | Key Scenarios |
|-----------|--------|-----------|---------------|
| `test_tf_buffer.cpp` | TfBuffer | 60 | Insert, lookup, interpolation (SLERP/LERP), BFS path, static frames, cache expiry, thread safety |
| `test_display_registry.cpp` | DisplayRegistry | 20 | Register, create, list, duplicate ID rejection, unknown type |
| `test_display_lifecycle.cpp` | DisplayPlugin | 30 | enable → update → disable cycle, topic change, error recovery |
| `test_pointcloud_adapter.cpp` | PointCloudAdapter | 25 | Field extraction, endianness, subsampling, edge cases (empty cloud, NaN) |
| `test_marker_display.cpp` | MarkerDisplay | 40 | All 8 marker types, lifetime expiry, ADD/MODIFY/DELETE/DELETEALL actions, namespace management |
| `test_laserscan_adapter.cpp` | LaserScanAdapter | 15 | Polar→cartesian, range filter, intensity mapping |
| `test_image_adapter.cpp` | ImageAdapter | 15 | Encoding conversion (rgb8, bgr8, mono8, rgba8, 16UC1) |
| `test_scene_manager.cpp` | SceneManager | 25 | Add/remove entities, pick ray-AABB, render order (opaque/transparent) |
| `test_urdf_parser.cpp` | UrdfParser | 30 | Box/cylinder/sphere collision, joint types (revolute/prismatic/fixed), malformed XML |
| `test_path_pose_display.cpp` | PathDisplay, PoseDisplay | 15 | Line strip generation, arrow rendering, TF transform |
| `test_session_migration.cpp` | RosSession v1→v2 | 10 | Load v1 session (hand-rolled JSON), save as v2, round-trip, backward compat, missing version field |

### 7.3 Stress Test Scenarios

**Scenario 1: "Moderate Robot" (baseline)**
- 100 TF frames at 50 Hz
- 1 PointCloud2 (50K points) at 10 Hz
- 1 Image (640×480 rgb8) at 30 Hz
- 50 Markers at 10 Hz
- Duration: 5 minutes
- Pass: P95 < 16ms, memory growth < 5 MB

**Scenario 2: "Heavy Perception"**
- 100 TF frames at 100 Hz
- 3 PointCloud2 (200K points each) at 10 Hz
- 2 Images (1280×720) at 30 Hz
- 200 Markers at 10 Hz
- 4 LaserScans (720 points each) at 40 Hz
- Duration: 5 minutes
- Pass: P95 < 20ms, P99 < 33ms, memory growth < 10 MB

**Scenario 3: "Stress Cloud"**
- 1 PointCloud2 (1M points) at 30 Hz
- Duration: 2 minutes
- Pass: Decimation active, render at ≥ 30 FPS, no OOM, no validation errors

### 7.4 Validation Checklist (Per Phase)

- [ ] Zero Vulkan validation layer errors (`VK_LAYER_KHRONOS_validation`)
- [ ] Zero ASan/UBSan errors (`ctest -LE gpu` with sanitizers)
- [ ] `FrameScheduler::frame_stats().hitch_count == 0` over 60 seconds at idle
- [ ] `VmRSS` delta < 1 MB over 60 seconds with no active topics
- [ ] Session save → load round-trip: pixel-identical 3D viewport
- [ ] All new unit tests pass deterministically (3 consecutive runs)

---

## 8. Risk Register

| ID | Risk | Likelihood | Impact | Mitigation |
|----|------|-----------|--------|------------|
| **R1** | Point cloud GPU upload > 32 MB/frame causes stutter | Medium | High | Frame upload budget cap; skip intermediate frames; configurable `max_points` |
| **R2** | TfBuffer mutex contention under high TF rate | Low | Medium | Lock-free SPSC for TF inserts (from executor → dedicated TF processing). Mutex only for lookup. |
| **R3** | Marker lifetime management leaks entities | Medium | Medium | Sweep expired markers each frame (`SceneManager::sweep_expired(now)`). Unit test coverage. |
| **R4** | URDF parser fails on real-world URDFs | High | Low (MVP uses collision only) | Ship with clear error reporting. Support box/cylinder/sphere only. Log unsupported elements as warnings. |
| **R5** | Image texture upload causes pipeline stall | Medium | High | Ring of 2 pre-allocated textures. Never create/destroy textures at runtime after initial allocation. Resize only if resolution changes. |
| **R6** | Existing TfTreePanel regression during TfBuffer refactor | Medium | Medium | TfTreePanel becomes thin UI over TfBuffer. Full test coverage before refactor. Integration test: TfTreePanel still shows Hz, age, stale correctly. |
| **R7** | Scene viewport mouse handling conflicts with ImGui docking | Low | Medium | ImGui `IsWindowHovered()` guard: only process 3D input when viewport focused. Existing pattern in Axes3D mouse handling. |
| **R8** | Memory growth from display history buffers | Medium | High | Each display has `max_memory_mb` config. SceneManager tracks per-display allocation. Alarm at 80%, stop at 100%. |
| **R9** | Multi-display render ordering artifacts (Z-fighting, transparency) | Medium | Medium | Painter's algorithm (existing). Opaque front-to-back, transparent back-to-front (existing pattern). Grid at slight negative offset to avoid Z-fight with ground-plane objects. |
| **R10** | Plugin interface changes during development break existing displays | Low | Medium | Freeze `DisplayPlugin` ABC after Phase 1. Add new capabilities via optional virtual methods with default no-op implementations. |
| **R11** | CDR message parsing for PointCloud2/Image is fragile | Medium | Medium | Exhaustive unit tests for field descriptor parsing. Test with little/big endian, different field orderings, padded/compact layouts. |
| **R12** | Simplified URDF (no meshes) looks unappealing | High | Low | Communicate clearly in UI: "Collision shapes" label. Post-MVP: add STL mesh loading via `assimp`. |
| **R13** | Session v1→v2 migration loses user data | Medium | High | Deserializer detects version; v1 sessions load via legacy hand-rolled path. All existing fields (subscriptions, expressions, panels, ImGui layout) preserved. Dedicated `test_session_migration.cpp` unit tests. |
| **R14** | nlohmann::json header increases compile time | Low | Low | Single-header included only in session/display serialization TUs, not in hot paths. Forward-declare `nlohmann::json` in headers where possible. |

---

## Appendix A: New Pipeline Types Summary

| Pipeline | Shader | Vertex Format | Topology | Depth | Use |
|----------|--------|---------------|----------|-------|-----|
| `PointCloud` | `pointcloud.vert/frag` | SSBO: `{vec3 pos, uint32 rgba}` | triangle_list (billboard quads) | Test: yes, Write: yes | Point cloud + scatter |
| `PointCloud_Transparent` | Same | Same | Same | Test: yes, Write: no | Transparent points |
| `Marker3D` | `marker3d.vert/frag` | VBO: `{vec3 pos, vec3 normal}` + instance SSBO | triangle_list | Test: yes, Write: yes | Marker cubes/spheres/cylinders |
| `Marker3D_Transparent` | Same | Same | Same | Test: yes, Write: no | Transparent markers |
| `Image3D` | `image3d.vert/frag` | VBO: `{vec3 pos, vec2 uv}` | triangle_list | Test: yes, Write: no | Camera image quads |

## Appendix B: Transform Struct Addition to math3d.hpp

Add to `include/spectra/math3d.hpp`:

```
struct Transform {
    vec3 translation = {0, 0, 0};
    quat rotation = {0, 0, 0, 1};  // identity

    mat4 to_mat4() const;                      // compose rotation + translation
    static Transform from_mat4(const mat4& m); // decompose
    Transform inverse() const;                  // invert
    Transform compose(const Transform& child) const; // parent * child
    vec3 transform_point(const vec3& p) const;  // rotate + translate
    vec3 transform_vector(const vec3& v) const; // rotate only
    static Transform lerp(const Transform& a, const Transform& b, float t);
    // Uses LERP for translation, SLERP for rotation
};
```

## Appendix C: Session Schema Extension

> **Migration:** Current sessions (v1) use hand-rolled JSON with `json_escape()` / `json_get_string()` helpers. This schema uses `nlohmann::json` (added in Phase 1 as `third_party/nlohmann/json.hpp`). The deserializer must detect `"version": 1` (or missing version) and load via the legacy path, preserving existing subscriptions/expressions/panels. All saves write v2 format.

```json
{
  "version": 2,
  "fixed_frame": "world",
  "camera": {
    "azimuth": 0.785,
    "elevation": 0.524,
    "distance": 10.0,
    "target": [0, 0, 0],
    "projection": "perspective",
    "fov": 45.0
  },
  "displays": [
    {
      "type_id": "grid",
      "enabled": true,
      "config": {
        "cell_size": 1.0,
        "cell_count": 20,
        "color": [0.3, 0.3, 0.3],
        "alpha": 0.5,
        "plane": "xy",
        "offset": [0.0, 0.0, 0.0]
      }
    },
    {
      "type_id": "pointcloud2",
      "enabled": true,
      "topic": "/velodyne_points",
      "config": {
        "color_mode": "intensity",
        "point_size": 2,
        "max_points": 500000,
        "decay_time": 0.0,
        "flat_color": [1, 1, 1, 1]
      }
    },
    {
      "type_id": "tf",
      "enabled": true,
      "config": {
        "show_labels": true,
        "axis_length": 0.3,
        "update_interval": 0.1
      }
    }
  ],
  "panels": {
    "displays": true,
    "inspector": true,
    "tf_tree": true,
    "topic_list": false
  },
  "existing_ros_session": { "...existing RosSession fields..." }
}
```

## Appendix D: File Creation Checklist

### New Files (Phase 1-5)

```
third_party/
└── nlohmann/
    └── json.hpp                    # Phase 1 (single-header JSON library)

src/adapters/ros2/
├── display/
│   ├── display_plugin.hpp          # Phase 1
│   ├── display_registry.hpp/.cpp   # Phase 1
│   ├── grid_display.hpp/.cpp       # Phase 1
│   ├── tf_display.hpp/.cpp         # Phase 2
│   ├── marker_display.hpp/.cpp     # Phase 2
│   ├── pointcloud_display.hpp/.cpp # Phase 3
│   ├── laserscan_display.hpp/.cpp  # Phase 3
│   ├── image_display.hpp/.cpp      # Phase 4
│   ├── path_display.hpp/.cpp       # Phase 4
│   ├── pose_display.hpp/.cpp       # Phase 4
│   └── robot_model_display.hpp/.cpp # Phase 5
├── messages/
│   ├── tf_adapter.hpp              # Phase 2
│   ├── marker_adapter.hpp          # Phase 2
│   ├── pointcloud_adapter.hpp      # Phase 3
│   ├── laserscan_adapter.hpp       # Phase 3
│   ├── image_adapter.hpp           # Phase 4
│   ├── path_adapter.hpp            # Phase 4
│   └── joint_state_adapter.hpp     # Phase 5
├── scene/
│   ├── scene_manager.hpp/.cpp      # Phase 1 (skeleton), Phase 2 (render+pick)
│   └── mesh_primitives.hpp/.cpp    # Phase 2
├── tf/
│   └── tf_buffer.hpp/.cpp          # Phase 1
├── urdf/
│   └── urdf_parser.hpp/.cpp        # Phase 5
└── ui/
    ├── displays_panel.hpp/.cpp     # Phase 1
    ├── scene_viewport.hpp/.cpp     # Phase 1
    └── inspector_panel.hpp/.cpp    # Phase 2

src/gpu/shaders/
├── pointcloud.vert/.frag           # Phase 3
├── marker3d.vert/.frag             # Phase 2
└── image3d.vert/.frag              # Phase 4

tests/unit/
├── test_tf_buffer.cpp              # Phase 1
├── test_display_registry.cpp       # Phase 1
├── test_session_migration.cpp      # Phase 1 (v1→v2 round-trip, backward compat)
├── test_display_lifecycle.cpp      # Phase 2
├── test_marker_display.cpp         # Phase 2
├── test_pointcloud_adapter.cpp     # Phase 3
├── test_laserscan_adapter.cpp      # Phase 3
├── test_image_adapter.cpp          # Phase 4
├── test_scene_manager.cpp          # Phase 2
├── test_urdf_parser.cpp            # Phase 5
└── test_path_pose_display.cpp      # Phase 4

tests/bench/
└── bench_ros3d.cpp                 # Phase 6
```

Total: ~47 new files across 6 phases.
