# Spectra ROS — UI Fix Engineering Plan

> **Date:** 2026-03-04
> **Scope:** Fix duplicate menu, broken docking, lack of context flow, and layout persistence in `spectra-ros`. No rewrite — incremental changes only.

---

## 1. Current-State Audit

### 1.1 UI Render Pipeline (per frame)

```
WindowRuntime::update()
  └─ imgui_ui->new_frame()                          [NewFrame]
  └─ imgui_ui->build_ui(figure)                     [or build_empty_ui()]
       ├─ draw_command_bar()        ← SPECTRA menu (custom ##commandbar window, NOT ImGui menu bar)
       ├─ draw_nav_rail()           ← Spectra left toolbar (hidden by ROS main.cpp)
       ├─ draw_canvas(figure)       ← plot canvas (renders dummy 1×1 subplot)
       ├─ draw_plot_overlays()
       ├─ draw_inspector()          ← hidden by ROS main.cpp
       ├─ draw_status_bar()         ← Spectra status bar (always drawn)
       ├─ draw_pane_tab_headers()
       ├─ draw_split_view_splitters()
       ├─ ... (tooltips, box-zoom, measure)
       ├─ extra_draw_cb_()          ← *** ROS shell.draw() injected HERE ***
       │    ├─ draw_menu_bar()      ← ROS menu (BeginMainMenuBar — SECOND menu!)
       │    ├─ draw_nav_rail()      ← ROS nav rail
       │    ├─ draw_dockspace()     ← ROS dockspace + DockBuilder layout
       │    ├─ draw_topic_list()    ← all 13 ROS panels
       │    ├─ draw_plot_area()
       │    ├─ draw_topic_stats()
       │    ├─ ... (all other panels)
       │    └─ draw_status_bar()    ← ROS status bar (SECOND status bar!)
       ├─ draw_knobs_panel()
       └─ draw_command_palette()
  └─ imgui_ui->render()                             [ImGui::Render()]
```

### 1.2 Root Causes

| Symptom | Root Cause | File(s) |
|---------|-----------|---------|
| **Duplicate menu bar** | Spectra's `draw_command_bar()` is called unconditionally in `build_ui()` (imgui_integration.cpp:359) before the extra callback. ROS's `draw_menu_bar()` (ros_app_shell.cpp:858) uses `BeginMainMenuBar()`. Both render. The ROS main hides inspector/nav-rail but **cannot suppress command_bar** — no flag exists. | `src/ui/imgui/imgui_integration.cpp` L359, L711; `src/adapters/ros2/ros_app_shell.cpp` L858 |
| **Duplicate status bar** | Spectra's `draw_status_bar()` (imgui_integration.cpp:2486) is always rendered. ROS draws its own (ros_app_shell.cpp:810). No suppression mechanism exists. | `src/ui/imgui/imgui_integration.cpp` L2486; `src/adapters/ros2/ros_app_shell.cpp` L810 |
| **Docking "doesn't work"** | (a) Spectra's `##canvas` window renders beneath the dockspace using fixed `LayoutManager` positions — it occupies space the dockspace also claims. (b) `io.IniFilename` is not set to `nullptr` in the primary context init, so `imgui.ini` from the CWD may desynchronise layout. (c) The dockspace host uses `NoBackground|PassthruCentralNode`, making the Spectra canvas bleed through. | `src/ui/imgui/imgui_integration.cpp` L82 (missing `IniFilename=nullptr`); `src/adapters/ros2/ros_app_shell.cpp` L490–530 |
| **Feels random / no context** | No shared `WorkspaceState`. Each panel is wired via ad-hoc callbacks in `wire_panel_callbacks()`. The topic list fires `on_topic_selected()` which only updates `selected_topic_`/`selected_type_` on the shell — other panels don't observe this. `FieldDragDrop` payloads are consumed and **discarded** (ros_app_shell.cpp:424) without creating a series. | `src/adapters/ros2/ros_app_shell.cpp` `wire_panel_callbacks()`, `selected_topic_` / `selected_type_` members |
| **No layout persistence** | `IniFilename` not set to `nullptr` in primary `ImGuiIntegration::init()`, so ImGui defaults to `"imgui.ini"` in CWD — the Spectra canvas layout, not the ROS docking state. `RosSessionManager` saves subscriptions and panel visibility but **not** ImGui docking layout. `apply_default_dock_layout()` fires every launch, losing user rearrangements. | `src/ui/imgui/imgui_integration.cpp` L82; `src/adapters/ros2/ros_session.hpp` `RosSession` struct |
| **Dummy figure / canvas bleed** | `main.cpp` creates `fig.subplot(1,1,1)` to suppress Spectra's "Welcome" page. `draw_canvas()` renders as a full-area `ImGui::Begin("##canvas", ...)` with `NoInputs`, underlaying the dockspace and wasting GPU cycles. | `src/adapters/ros2/main.cpp` L93–94; `src/ui/imgui/imgui_integration.cpp` L2117 |

### 1.3 Files/Modules That Must Change

| Module | File(s) | Change Required |
|--------|---------|-----------------|
| **Core UI Integration** | `src/ui/imgui/imgui_integration.hpp`, `src/ui/imgui/imgui_integration.cpp` | Add `set_command_bar_visible(bool)`, `set_status_bar_visible(bool)`, `set_canvas_visible(bool)` flags; gate the relevant `draw_*()` calls behind them |
| **ROS Entry Point** | `src/adapters/ros2/main.cpp` | After hiding inspector/nav-rail, also suppress command bar, status bar, canvas, pane tabs, split splitters |
| **ROS App Shell** | `src/adapters/ros2/ros_app_shell.hpp`, `src/adapters/ros2/ros_app_shell.cpp` | Introduce `RosWorkspaceState`; unify context wiring; add layout persistence calls |
| **ROS Session** | `src/adapters/ros2/ros_session.hpp`, `src/adapters/ros2/ros_session.cpp` | Add `imgui_ini_data` field; serialize/deserialize ImGui docking state |
| **ROS Panels** | `src/adapters/ros2/ui/*.cpp` | Accept `RosWorkspaceState*` in `draw()`; react to selection changes |
| **Layout Manager** | `src/ui/layout/layout_manager.hpp` | No structural change; existing visibility toggles are sufficient |

---

## 2. Target UI Architecture (Incremental)

### 2.1 Workspace Controller — `RosWorkspaceState`

**New struct** in `src/adapters/ros2/ros_app_shell.hpp`:

```cpp
struct RosWorkspaceState {
    // Selection context
    std::string selected_topic;
    std::string selected_type;
    std::string selected_field;      // fully-qualified field path
    int         active_subplot_idx = -1;

    // Playback
    bool   is_playing      = true;
    double playback_rate   = 1.0;
    double ros_time_latest = 0.0;

    // Events (consumed each frame)
    bool selection_changed = false;
    bool plot_requested    = false;

    // Methods
    void select_topic(const std::string& topic, const std::string& type);
    void select_field(const std::string& field);
    void request_plot();   // sets plot_requested = true
};
```

Owned by `RosAppShell`. Passed by pointer to all panel `draw()` calls so each panel can read and react to the current selection.

### 2.2 Panel Contract

Each ROS panel already uses a `draw(bool* p_open)` interface. Extend to:

```cpp
void draw(bool* p_open, const RosWorkspaceState* ctx = nullptr);
```

Panels that react to selection (`TopicEchoPanel`, `TopicStatsOverlay`, `SubplotManager`) check `ctx->selection_changed` and update their display. Backwards-compatible default parameter — no virtual base class needed.

### 2.3 Single DockSpace Root

Already exists in `RosAppShell::draw_dockspace()` (ros_app_shell.cpp:490). No structural change. The fix is ensuring **nothing else renders in that screen space** (suppress Spectra chrome, phase 1).

### 2.4 Unified Menu/Toolbar

The ROS `BeginMainMenuBar()` becomes the sole menu. Spectra's `draw_command_bar()` is suppressed via the new flag.

| Menu | Items |
|------|-------|
| **File** | Save Session, Load Session, Recent Sessions ▸, Export Screenshot, Exit |
| **View** | Toggle: Topic Monitor, Topic Echo, Statistics, Plot Area, Log Viewer, Diagnostics, Node Graph, TF Tree, Param Editor, Service Caller, Bag Info, Bag Playback; Separator; Nav Rail; Reset Dock Layout |
| **Layout** | Presets: Default, Debug, Monitor, BagReview; Reset Docked Layout |
| **Plots** | Clear All, Resume/Pause Scroll, Time Window ▸ |
| **Tools** | Screenshot, Record ▸ |
| **Help** | About Spectra-ROS, Keyboard Shortcuts |

### 2.5 Layout Persistence

Extend `RosSession` with one new field:

```cpp
struct RosSession {
    // ... existing fields ...
    std::string imgui_ini_data;   // ImGui::SaveIniSettingsToMemory() snapshot
};
```

- **On save:** call `ImGui::SaveIniSettingsToMemory()` and store result in session JSON under `"imgui_layout"`.
- **On load:** call `ImGui::LoadIniSettingsFromMemory()` **before** the first `DockSpace()` call, and set `dock_layout_initialized_ = true` to skip `apply_default_dock_layout()`.

Sessions stored at `~/.config/spectra/ros_sessions/` (existing `RosSessionManager` default path).

---

## 3. Default ROS Workspace Spec

```
┌──────────────────────────────────────────────────────────────────────────┐
│  File   View   Layout   Plots   Tools   Help         [Menu Bar — single] │
├────────┬─────────────────────────────────┬───────────────────────────────┤
│        │                                 │  Topic Statistics             │
│ Topic  │       Plot Area                 │  ─────────────────────────── │
│ Monitor│       (SubplotManager grid)     │  Selected: /cmd_vel           │
│        │       4×1 subplots              │  Type: geometry_msgs/Twist    │
│ ┌────┐ │                                 │  Hz: 10.0  BW: 2.4 KB/s      │
│ │ 🔍 │ │                                 │  Pubs: 1  Subs: 2            │
│ └────┘ │                                 ├───────────────────────────────┤
│ Topics:│                                 │  Node Graph                   │
│  ├ /tf │                                 │  (visualize connections)      │
│  ├ /cmd│                                 │                               │
│  └ /odom                                 │                               │
├────────┴────────────┬────────────────────┴───────────────────────────────┤
│  Topic Echo         │   ROS2 Log                                         │
│  (selected topic)   │   (rosout viewer)                                  │
│  data.linear.x: 0.5 │   [INFO] controller started                       │
│  data.linear.y: 0.0 │   [WARN] tf lookup timeout                        │
└─────────────────────┴────────────────────────────────────────────────────┘
│  Node: spectra_ros  │  Msgs: 1,234  │  Plots: 3  │  Buf: 48 KB │ Status │
└──────────────────────────────────────────────────────────────────────────┘
```

**Panel-to-Workflow Mapping:**

| Workflow | Primary Panel | Supporting Panels | Key Interaction |
|----------|--------------|-------------------|-----------------|
| W1: Explore topics | Topic Monitor (left) | Topic Statistics (right), Topic Echo (bottom) | Click topic → stats + echo auto-populate |
| W2: Plot a field | Topic Monitor (expand fields) | Plot Area (center) | Drag field → drop on subplot slot |
| W3: Manage plots/series | Plot Area (center) | Topic Statistics shows per-series controls (color, style, width) | Right-click subplot for time window, remove |
| W4: Playback (bag) | Bag Playback (bottom) | Plot Area auto-scrolls to bag time | Pause/resume, scrub timeline |
| W5: Diagnostics / echo | Topic Echo (bottom) | Log Viewer, Diagnostics (tabbed in bottom/right) | Auto-shows selected topic content |

---

## 4. Phased Execution Plan

---

### Phase 1 — Docking + Menu Unification (No New Features)

**Goal:** Single menu bar. No Spectra chrome bleeding through. Docking works.

#### Files to Change

| File | Change |
|------|--------|
| `src/ui/imgui/imgui_integration.hpp` | Add `bool command_bar_visible_ = true;`, `bool status_bar_visible_ = true;`, `bool canvas_visible_ = true;` members plus corresponding public setters. |
| `src/ui/imgui/imgui_integration.cpp` | `build_ui()` (~L359): gate `draw_command_bar()` behind `if (command_bar_visible_)`. Gate `draw_status_bar()` behind `if (status_bar_visible_)`. Gate `draw_canvas()`, `draw_pane_tab_headers()`, `draw_split_view_splitters()` behind `if (canvas_visible_)`. `build_empty_ui()` (~L711): gate `draw_command_bar()` behind `if (command_bar_visible_)`. `init()` (~L82): add `io.IniFilename = nullptr;` after `ImGui::SetCurrentContext(imgui_context_)` to prevent stale `imgui.ini` in CWD from interfering with the primary window context. |
| `src/adapters/ros2/main.cpp` | After the existing `lm.set_inspector_visible(false)` block (~L130), add: `ui_ctx->imgui_ui->set_command_bar_visible(false);` `ui_ctx->imgui_ui->set_status_bar_visible(false);` `ui_ctx->imgui_ui->set_canvas_visible(false);` |

#### New Structs/Classes
None.

#### Risk Assessment
- **Low.** Flags default to `true` — no change for the core Spectra app.
- Setting `IniFilename = nullptr` stops `imgui.ini` being read/written for the primary window context. Since Spectra has its own workspace persistence system (`WorkspaceData`), this is correct. **Verify** no core Spectra feature silently depended on `imgui.ini` (e.g. ImGui window positions for floating dialogs — these should instead be set via `SetNextWindowPos(ImGuiCond_FirstUseEver)`).

#### Acceptance Criteria
- Exactly one menu bar visible at top (ROS menu: View, Layout, Plots, Session, Tools).
- No Spectra command bar (icon row) visible.
- No Spectra status bar visible — only ROS status bar.
- Panels dock/undock by dragging title bar.
- View → Reset Dock Layout restores panels to default positions.
- Running `./build/bin/spectra-window` (normal Spectra): command bar, canvas, inspector all render normally.

#### Verification Steps
```bash
cmake --build build -j$(nproc)
./build/spectra-ros
```
1. Confirm single menu bar at top.
2. Drag "Topic Echo" title bar to right side of window — it should snap as a docked panel.
3. Drag it back to the bottom — it should re-dock.
4. View → Reset Dock Layout → panels snap back to default.
5. Run `./build/bin/spectra-window` → normal Spectra chrome intact.

#### Common Failure Symptoms
| Symptom | Fix |
|---------|-----|
| Command bar still visible | Check `build_empty_ui()` path was also gated (taken during startup if figure not yet ready) |
| Panels float but won't snap | Ensure no other `ImGui::Begin()` overlaps the dockspace area; verify `IMGUI_HAS_DOCK` is defined |
| Canvas bleed-through persists | Confirm `set_canvas_visible(false)` is called and that `draw_canvas()` check uses `canvas_visible_`, not a layout manager flag |

---

### Phase 2 — Workspace Controller + Context Wiring

**Goal:** Selecting a topic in Topic Monitor updates Topic Echo, Topic Statistics, and field picker. Panels feel connected.

#### Files to Change

| File | Change |
|------|--------|
| `src/adapters/ros2/ros_app_shell.hpp` | Declare `RosWorkspaceState` struct (see §2.1). Replace `selected_topic_` and `selected_type_` members with `RosWorkspaceState workspace_state_`. |
| `src/adapters/ros2/ros_app_shell.cpp` | `on_topic_selected()`: call `workspace_state_.select_topic(topic, type)` which sets `selection_changed = true`. `draw()`: pass `&workspace_state_` to panel `draw()` calls that accept it. At end of `draw()`: reset `workspace_state_.selection_changed = false` and `workspace_state_.plot_requested = false`. |
| `src/adapters/ros2/ui/topic_echo_panel.hpp/.cpp` | Accept `const RosWorkspaceState*` as optional second parameter in `draw()`. If `ctx->selection_changed && ctx->selected_topic != current_topic_ && !manually_pinned_`, auto-switch to echoing the selected topic. Add `bool manually_pinned_ = false;` toggled when user types a topic manually. |
| `src/adapters/ros2/ui/topic_stats_overlay.hpp/.cpp` | Accept `const RosWorkspaceState*` in `draw()`. Scroll to and highlight `ctx->selected_topic`. If nothing selected, render hint text: "Select a topic in the Topic Monitor". |
| `src/adapters/ros2/ui/topic_list_panel.hpp/.cpp` | Highlight `workspace_state_.selected_topic` in the tree. Existing `on_select` callback already fires `on_topic_selected()` — no logic change needed here besides reading `ctx->selected_topic` for highlight rendering. |
| `src/adapters/ros2/ros_app_shell.cpp` `draw_plot_area()` | When `workspace_state_.plot_requested` is true, auto-subscribe `workspace_state_.selected_field` and add it to the active subplot via `subplot_mgr_->add_series()`. |

#### New Structs/Classes
- `RosWorkspaceState` (~20 lines, in `ros_app_shell.hpp`)

#### Risk Assessment
- **Medium.** Changing `draw()` signatures of 3+ panels. Mitigated by using a default `nullptr` parameter — all existing call sites continue to compile without changes.
- Topic echo auto-switch may confuse users who manually selected a different topic. Mitigated by the `manually_pinned_` flag (reset when user clears the field or selects from Topic Monitor).

#### Acceptance Criteria
- Click `/cmd_vel` in Topic Monitor → Topic Echo switches to `/cmd_vel` and Topic Statistics highlights `/cmd_vel`.
- Click a different topic → both panels update.
- Manually type a topic in Topic Echo's filter → panel stays pinned and does not auto-switch.
- No empty/confused panels with stale or placeholder state.

#### Verification Steps
```bash
./build/spectra-ros --topics /tf
```
1. In Topic Monitor, click `/cmd_vel`.
2. Check: Topic Echo shows `/cmd_vel` data stream.
3. Check: Topic Statistics panel highlights `/cmd_vel` row.
4. Click `/tf` → both panels update.
5. In Topic Echo, manually type `/rosout` → pin icon or visual indication appears.
6. Click `/cmd_vel` in Topic Monitor → Topic Echo stays on `/rosout`.

---

### Phase 3 — Default Layout + Layout Persistence

**Goal:** First-run layout matches the workspace spec from §3. Layout survives restart.

#### Files to Change

| File | Change |
|------|--------|
| `src/adapters/ros2/ros_app_shell.cpp` `apply_default_dock_layout()` | Revise split ratios to match the §3 spec: Left=0.18 (Topic Monitor), Right=0.24 (Statistics + Node Graph stacked), Bottom=0.30 (Topic Echo + ROS2 Log tabbed). Reduce current 6-split tree to 4 clean splits. Dock Node Graph to `dock_right_bottom`, Log to `dock_bottom` as a tab alongside Topic Echo. |
| `src/adapters/ros2/ros_session.hpp` | Add `std::string imgui_ini_data;` to `RosSession`. |
| `src/adapters/ros2/ros_session.cpp` | `serialize()`: call `size_t sz = 0; const char* ini = ImGui::SaveIniSettingsToMemory(&sz);` and store as JSON string under `"imgui_layout"`. `deserialize()`: read `"imgui_layout"` into `session.imgui_ini_data`. |
| `src/adapters/ros2/ros_app_shell.cpp` `apply_session()` | If `session.imgui_ini_data` is non-empty, call `ImGui::LoadIniSettingsFromMemory(data.c_str(), data.size())` and set `dock_layout_initialized_ = true` to skip rebuild. |
| `src/adapters/ros2/ros_app_shell.cpp` `capture_session()` | Populate `session.imgui_ini_data` by calling `ImGui::SaveIniSettingsToMemory()`. |
| `src/adapters/ros2/ros_app_shell.cpp` `shutdown()` | Call `capture_session()` + `session_mgr_->auto_save(session)` for clean-shutdown auto-save. |
| `src/adapters/ros2/ros_app_shell.cpp` `poll()` | If `ImGui::GetIO().WantSaveIniSettings` is true, trigger a deferred auto-save (write to `autosave.json` via session manager, ~60s debounce). |

#### New Structs/Classes
None (one field added to existing struct).

#### Risk Assessment
- **Medium.** `ImGui::SaveIniSettingsToMemory()` / `LoadIniSettingsFromMemory()` operate on the current context — must be called from the render thread while the ROS dockspace context is active. Since ROS shares the primary window context, this is correct.  
- `LoadIniSettingsFromMemory()` must be called **before** the first `DockSpace()` frame after load, otherwise ImGui ignores it. Sequence: `apply_session()` → sets `imgui_ini_data` → next `draw_dockspace()` call checks it and calls `LoadIni...` before `DockSpace(...)`.
- Auto-save on crash/kill won't trigger. Mitigated by `WantSaveIniSettings` periodic debounce.
- DPI changes between sessions may produce layout artifacts — this is an inherent ImGui DnD limitation. "Reset Layout" is the documented escape hatch.

#### Acceptance Criteria
- First-run layout matches the §3 spec diagram.
- Rearrange panels → Session → Save → close → reopen → Session → Load → panels at saved positions.
- Close window cleanly → reopen → auto-saved layout restores.
- Layout → Reset Dock Layout → default arrangement.

#### Verification Steps
```bash
./build/spectra-ros
```
1. Rearrange: undock Topic Echo, move to right side.
2. Session → Save Session → `/tmp/test_session.json`.
3. Close. Reopen.
4. Session → Load Session → `/tmp/test_session.json`.
5. Check: Topic Echo is on the right side.
6. Layout → Reset Dock Layout → Topic Echo back at bottom.
7. Close cleanly. Reopen without explicit load.
8. Check: auto-saved layout from `~/.config/spectra/ros_sessions/autosave.json` is restored.

#### Common Failure Symptoms
| Symptom | Fix |
|---------|-----|
| Panels revert to default on every launch | `LoadIniSettingsFromMemory` called after `DockSpace()` — move it earlier in `draw()` before `draw_dockspace()` |
| Session file has no `"imgui_layout"` key | `capture_session()` not calling `SaveIniSettingsToMemory` or it's returning empty (call before `ImGui::NewFrame()` is invalid — ensure it's called inside a frame or after `Render()`) |
| Auto-save file grows unexpectedly large | Only write when `WantSaveIniSettings` is true; don't write every frame |

---

### Phase 4 — "Replace rqt Plotting" Polish

**Goal:** A user can plot a topic field in <10 seconds without confusion.

#### Files to Change

| File | Change |
|------|--------|
| `src/adapters/ros2/ui/topic_list_panel.cpp` | When a topic is expanded in the tree, show numeric leaf nodes for each field path. Each field: (a) double-click → `workspace_state_.select_field(field); workspace_state_.request_plot();`, (b) start ImGui drag with payload type `"SPECTRA_FIELD"` carrying `FieldDragPayload`. |
| `src/adapters/ros2/ui/field_drag_drop.cpp` | Fix the payload consumer: currently payload is consumed and discarded (`ros_app_shell.cpp:424`). Route consumed payload to `handle_plot_request(payload, PlotTarget::CurrentAxes)` which calls `subplot_mgr_->add_series()`. |
| `src/adapters/ros2/subplot_manager.cpp` | Inside each subplot slot's `ImGui::Begin()` window, add `ImGui::BeginDragDropTarget()`. On accepting `"SPECTRA_FIELD"` payload, call `add_series(payload.topic, payload.field, this_slot_index)`. Highlight the subplot border with `GetWindowDrawList()->AddRect()` while a drag payload is hovering. |
| `src/adapters/ros2/ros_app_shell.cpp` | In Plots menu: add "Add Selected Field to Plot" `MenuItem` that reads `workspace_state_.selected_field` and calls `subplot_mgr_->add_series()`. Disable the item when `selected_field` is empty. |
| `src/adapters/ros2/ui/topic_stats_overlay.cpp` | When a subplot has an active series from the selected topic, show per-series controls at the bottom of the panel: color picker swatch, line/scatter toggle, line width slider, enable/disable checkbox, remove button. |

#### New Structs/Classes
None. `FieldDragPayload` already exists in `src/adapters/ros2/ui/field_drag_drop.hpp`. Payload type string `"SPECTRA_FIELD"` — define as a `constexpr` shared across files.

#### Risk Assessment
- **Medium-high.** Drag-drop across ImGui panels requires `BeginDragDropTarget()` to be called inside the target window's active `Begin()`/`End()` block. If the subplot is rendered inside a child window or has a clipping rect, the target may not register — test with a topic with many fields.
- Series colour assignment needs a palette (verify `SubplotManager` cycle is deterministic).
- Numeric leaf nodes for fields require `MessageIntrospector` to enumerate all numeric fields per topic type — this likely already exists; verify API.

#### Acceptance Criteria
- Launch `spectra-ros --topics /cmd_vel`. Expand `/cmd_vel` in Topic Monitor. Fields appear as child nodes.
- Drag `linear.x` → drop onto first subplot → series appears scrolling in real-time.
- Total time from launch to first visible plot: < 10 seconds.
- Double-click `angular.z` → added to next available subplot automatically.
- Right-click subplot → context menu offers: Time Window, Remove Series, Color.
- Topic Statistics panel shows color/width controls for series in the active subplot.

#### Verification Steps
```bash
ros2 topic pub /cmd_vel geometry_msgs/msg/Twist "{ linear: { x: 0.5 } }" --rate 10 &
./build/spectra-ros
```
1. Expand `/cmd_vel` in Topic Monitor → `linear.x`, `linear.y`, `angular.z` visible.
2. Drag `linear.x` → drop onto subplot 1. Data line appears.
3. Drag `angular.z` → drop onto subplot 3. Data line appears.
4. Double-click `linear.y` → added to next subplot.
5. Time the full sequence from app launch to first plot visible.
6. Right-click subplot 1 → context menu with time window and remove options.

---

## 5. Verification Summary

### Build Commands

```bash
# Debug build with ROS2
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSPECTRA_USE_ROS2=ON
cmake --build build -j$(nproc)

# Sanitizer build
cmake -B build-asan -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
    -DSPECTRA_USE_ROS2=ON
cmake --build build-asan -j$(nproc)

# Non-GPU regression tests (run after every phase)
ctest --test-dir build -LE gpu --output-on-failure
```

### Cross-Phase Acceptance Matrix

| Phase | Criterion | Pass Condition |
|-------|-----------|----------------|
| P1 | Single menu | Only `BeginMainMenuBar` renders; `##commandbar` window not created |
| P1 | No canvas bleed | Background behind dockspace is solid; no phantom plot axes visible |
| P1 | Docking works | Panel dragged to edge snaps into dockspace |
| P1 | Spectra regression | `spectra-window` shows normal command bar + canvas + inspector |
| P2 | Context flows | Click topic → echo + stats update within same frame |
| P2 | No empty panels | Topic Echo shows hint text when nothing selected |
| P3 | Persistence | Custom arrangement survives restart via auto-save |
| P3 | Reset works | Layout → Reset restores §3 default |
| P4 | Plot in <10s | Timed test: launch → drag field → plot visible in under 10 seconds |
| P4 | Drag-drop visual | Subplot border highlights on drag hover |

### Common Failure Symptoms & Fixes

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| Panels float but won't snap | DockSpace host window flags wrong, or overlapping window steals area | Verify `IMGUI_HAS_DOCK` defined; check no other `Begin()` occupies same rect |
| Menu bar renders twice | `command_bar_visible_` flag not checked in `build_empty_ui()` | Gate both `build_ui()` and `build_empty_ui()` command bar calls |
| Layout ignored on load | `LoadIniSettingsFromMemory()` called after `DockSpace()` | Move load call to before `draw_dockspace()` — ideally gated as a one-shot at start of `draw()` |
| Drag-drop payload not received | Payload type string mismatch | Define `constexpr const char* kFieldPayloadType = "SPECTRA_FIELD";` in shared header |
| Auto-save file grows per frame | `WantSaveIniSettings` not checked | Only call `SaveIniSettingsToMemory()` when `io.WantSaveIniSettings` is true |
