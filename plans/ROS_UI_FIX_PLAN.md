# Spectra ROS — UI Architecture & Maintenance Plan

> **Last updated:** 2026-06-07  
> **Status:** Phases 1–4 **complete** — this doc is the living reference for architecture, verification, and regressions.  
> **Shipped in:** `2b9aeb0` (UI overhaul), subsequent hardening in `982c49f` (ROS2 tests/bridge).  
> **Product roadmap:** [`SPECTRA_ROS_PRODUCT_PLAN.md`](SPECTRA_ROS_PRODUCT_PLAN.md) — 3-phase plan to become the world's first spatiotemporal ROS debug workbench.  
> **3D implementation audit:** [`archive/SPECTRA_ROS_STUDIO_PLAN.md`](archive/SPECTRA_ROS_STUDIO_PLAN.md)

---

## 1. Mission Snapshot

Original goal (March 2026): fix duplicate Spectra chrome, broken ImGui docking, disconnected panels, and missing layout persistence in `spectra-ros` — **without a rewrite**.

| Symptom (original) | Resolution | Key files |
|--------------------|------------|-----------|
| Duplicate menu / status bar | Spectra chrome suppressed in ROS entry; ROS owns `BeginMainMenuBar()` + status bar | `src/adapters/ros2/main.cpp`, `src/ui/imgui/imgui_integration.{hpp,cpp}` |
| Canvas bleed under dockspace | `set_canvas_visible(false)` + `set_render_figure_enabled(true)` for plot-area Vulkan draw | `main.cpp`, `window_runtime.cpp` |
| Docking broken / layout reset | `IniFilename = nullptr` on primary context; ROS session stores `imgui_layout` | `imgui_integration.cpp`, `ros_session.{hpp,cpp}`, `ros_app_shell.cpp` |
| Panels feel disconnected | `RosWorkspaceState` shared selection + per-frame event flags | `ros_app_shell.hpp`, panel `draw()` wiring |
| Drag-drop plots discarded | `FieldDragPayload` → `handle_plot_request()` → `SubplotManager` | `ros_app_shell_panel_wiring.cpp`, `ros_app_shell_drop_targets.cpp`, `topic_list_panel.cpp` |

### Completion checklist

| Area | Status | Notes |
|------|--------|-------|
| P1 — Chrome suppression + docking | ✅ Done | Flags in `ImGuiIntegration`; gated in `build_ui()` / `build_empty_ui()` |
| P2 — Workspace context | ✅ Done | `RosWorkspaceState`; echo pin via `TopicEchoPanel::manually_pinned_` |
| P3 — Layout persistence | ✅ Done | `RosSession::imgui_ini_data` ↔ JSON `"imgui_layout"`; auto-save on shutdown |
| P4 — rqt-class plotting UX | ✅ Done | Field tree, drag-drop, double-click, per-series controls in stats |
| P4 — Plot in &lt;10 s workflow | ⏳ Verify | Manual timed test — see §5 |
| 3D viewport toolbar + fixed frame | ✅ Done | `scene_viewport.cpp`, inspector empty state |
| Stretch (OccupancyGrid, measure, `package://`) | ✅ Done | `display/`, `tools/`, `resources/ros_package_resolver.cpp` |

---

## 2. Current Architecture

### 2.1 UI render pipeline (per frame)

```
WindowRuntime::update()
  └─ imgui_ui->new_frame()
  └─ imgui_ui->build_ui(figure)          [or build_empty_ui()]
       ├─ draw_command_bar()             ← gated by command_bar_visible_ (false in ROS)
       ├─ draw_nav_rail()                 ← hidden via LayoutManager (ROS)
       ├─ draw_canvas(figure)             ← gated by canvas_visible_ (false in ROS)
       ├─ draw_inspector()                ← hidden via LayoutManager (ROS)
       ├─ draw_status_bar()               ← gated by status_bar_visible_ (false in ROS)
       ├─ extra_draw_cb_()               ← RosAppShell::draw()
       │    ├─ draw_menu_bar()            ← sole menu bar
       │    ├─ draw_dockspace()           ← ImGui DockSpace + DockBuilder default
       │    ├─ panels (topic list, echo, stats, plots, 3D viewport, …)
       │    └─ draw_status_bar()          ← ROS status bar
       └─ draw_command_palette()
  └─ scene_render_callback (optional)     ← SceneRenderer GPU pass for 3D viewport
  └─ imgui_ui->render()
```

**ROS entry wiring** (`src/adapters/ros2/main.cpp`): after `enable_docking()`, suppress Spectra chrome and inject `shell.draw()` via `set_extra_draw_callback()`. Figure Vulkan rendering stays on (`set_render_figure_enabled(true)`) so the Plot Area panel can draw axes/series.

### 2.2 Workspace context — `RosWorkspaceState`

Defined in `src/adapters/ros2/ros_app_shell.hpp`. Owned by `RosAppShell`; panels read it (or receive `const RosWorkspaceState*`) to react without mutual coupling.

| Field | Purpose |
|-------|---------|
| `selected_topic`, `selected_type`, `selected_field` | Current selection |
| `active_subplot_idx` | Target slot for new series (-1 = auto) |
| `fixed_frame` | TF fixed frame for 3D displays |
| `selection_changed`, `plot_requested` | Per-frame events; cleared via `reset_events()` at end of `draw()` |

**Panel contract:** `void draw(bool* p_open, const RosWorkspaceState* ctx = nullptr)` — default `nullptr` keeps call sites backwards-compatible.

### 2.3 Layout persistence

| Mechanism | Detail |
|-----------|--------|
| ImGui ini | `io.IniFilename = nullptr` — no CWD `imgui.ini` clash |
| Session field | `RosSession::imgui_ini_data` serialized as `"imgui_layout"` |
| Save | `ImGui::SaveIniSettingsToMemory()` in `capture_session()` |
| Load | `ImGui::LoadIniSettingsFromMemory()` **before** first `DockSpace()` after session apply |
| Storage | `~/.config/spectra/ros_sessions/` (+ `autosave.json` on clean shutdown) |

### 2.4 Module map

| Module | Files |
|--------|-------|
| Entry + chrome flags | `main.cpp` |
| Shell orchestration | `ros_app_shell.{hpp,cpp}` |
| Menus / modals | `ros_app_shell_menus.cpp` |
| Panel callbacks | `ros_app_shell_panel_wiring.cpp` |
| Drag-drop targets | `ros_app_shell_drop_targets.{hpp,cpp}` |
| Session I/O | `ros_session.{hpp,cpp}` |
| Plot grid | `subplot_manager.cpp`, `ros_plot_manager.cpp` |
| Panels | `src/adapters/ros2/ui/*.cpp` |
| 3D scene | `ui/scene_viewport.cpp`, `scene/scene_renderer.cpp`, `display/*` |
| Core UI flags | `src/ui/imgui/imgui_integration.{hpp,cpp}` |

### 2.5 Default workspace layout

```
┌──────────────────────────────────────────────────────────────────────────┐
│  File   View   Layout   Plots   Tools   Help         [Menu Bar — single] │
├────────┬─────────────────────────────────┬───────────────────────────────┤
│ Topic  │       Plot Area (subplots)        │  Topic Statistics             │
│ Monitor│                                 │  Node Graph (stacked)           │
├────────┴────────────┬────────────────────┴───────────────────────────────┤
│  Topic Echo         │   ROS2 Log                                         │
└─────────────────────┴────────────────────────────────────────────────────┘
│  ROS status bar (node, msg count, plots, buffer, connection)             │
└──────────────────────────────────────────────────────────────────────────┘
```

Built by `apply_default_dock_layout()` in `ros_app_shell.cpp`. **Layout → Reset Dock Layout** restores this; user arrangements persist via session auto-save.

### 2.6 Workflow map

| Workflow | Primary panel | Supporting panels | Interaction |
|----------|---------------|-------------------|-------------|
| Explore topics | Topic Monitor | Topic Statistics, Topic Echo | Click topic → echo + stats follow |
| Plot a field | Topic Monitor (field tree) | Plot Area | Drag field → subplot; double-click → auto-plot |
| Manage series | Plot Area | Topic Statistics (`draw_series_controls`) | Color, style, width, remove |
| Bag playback | Bag Playback | Plot Area time sync | Scrub / pause / resume |
| 3D inspection | Scene Viewport | Displays panel, TF tree | Fixed frame, display plugins |

---

## 3. Completed Implementation (reference)

Use this when bisecting regressions or onboarding — not as an active task list.

### Phase 1 — Chrome + docking

- `ImGuiIntegration`: `command_bar_visible_`, `status_bar_visible_`, `canvas_visible_` + setters; gates in `build_ui()` and `build_empty_ui()`.
- `main.cpp`: all three flags `false`; inspector/nav-rail/tab bar hidden via `LayoutManager`.
- `init()`: `io.IniFilename = nullptr`.

### Phase 2 — Context wiring

- `RosWorkspaceState` replaces ad-hoc `selected_topic_` / `selected_type_`.
- `TopicEchoPanel`: auto-follow selection unless `manually_pinned_`.
- `TopicStatsOverlay`: highlight selected topic; `draw_series_controls()` for active subplot.

### Phase 3 — Layout persistence

- `RosSession::imgui_ini_data` + JSON `"imgui_layout"`.
- `apply_session()` / `capture_session()` / shutdown auto-save; debounced save on `WantSaveIniSettings`.

### Phase 4 — Plotting polish

- Field tree in Topic Monitor; `FieldDragPayload` drag sources.
- Subplot `BeginDragDropTarget()` in `subplot_manager.cpp` / `ros_app_shell_drop_targets.cpp`.
- Plots menu: “Add Selected Field to Plot” when `selected_field` set.

---

## 4. Open Items

Active product work is tracked in [`SPECTRA_ROS_PRODUCT_PLAN.md`](SPECTRA_ROS_PRODUCT_PLAN.md). Maintenance-only items:

| Item | Priority | Owner skill | Action |
|------|----------|-------------|--------|
| **P4 timed workflow** — Phase A gate **A1** in product plan | High | `qa-ros-performance-agent` | Automate `scenario_first_plot`; median ≤10 s |
| **ROS QA design-review** on UI changes | Medium | `qa-designer-agent` | `spectra_ros_qa_agent --design-review` after panel/layout edits |
| **Spectra core regression** | High (on UI flag changes) | `build-and-test` | Confirm `spectra-window` still shows command bar + canvas + inspector |

---

## 5. Verification

### Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSPECTRA_USE_ROS2=ON
cmake --build build -j$(nproc)
ctest --test-dir build -LE gpu --output-on-failure -j$(nproc)
```

### Manual — chrome + docking

```bash
./build/spectra-ros
```

1. Single menu bar at top (ROS menus only).
2. Drag “Topic Echo” to dock right → snaps; drag back to bottom.
3. **View → Reset Dock Layout** → default arrangement.
4. `./build/spectra-window` → Spectra command bar, canvas, inspector all visible.

### Manual — context flow

```bash
./build/spectra-ros --topics /cmd_vel
```

1. Click `/cmd_vel` in Topic Monitor → Echo + Statistics update.
2. Type `/rosout` in Echo → pin; click another topic → Echo stays pinned.

### Manual — layout persistence

1. Rearrange panels → **Session → Save** → close → **Session → Load** → layout restored.
2. Clean close → reopen → `~/.config/spectra/ros_sessions/autosave.json` layout restored.

### Manual — P4 timed plot workflow

```bash
ros2 topic pub /cmd_vel geometry_msgs/msg/Twist "{ linear: { x: 0.5 } }" --rate 10 &
./build/spectra-ros
```

Time from app launch to first visible plot after dragging `linear.x` onto subplot 1. **Pass:** &lt;10 s.

### Automated ROS QA

```bash
./build/tests/spectra_ros_qa_agent --design-review --output-dir /tmp/spectra_ros_qa
```

### Acceptance matrix

| Criterion | Pass condition |
|-----------|----------------|
| Single menu | No `##commandbar` window in ROS mode |
| No canvas bleed | Solid background behind dockspace; no phantom axes |
| Docking | Panel drag to edge snaps into dockspace |
| Core regression | `spectra-window` unchanged |
| Context flow | Topic click updates echo + stats same frame |
| Persistence | Custom layout survives restart |
| Reset | Layout → Reset restores §2.5 default |
| Plot UX | Drag-drop highlight + series appears; double-click adds field |

---

## 6. Troubleshooting

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| Command bar still visible | `build_empty_ui()` path not gated | Check `command_bar_visible_` in both `build_ui()` and `build_empty_ui()` |
| Panels float, won't snap | Overlapping `ImGui::Begin()` or missing `IMGUI_HAS_DOCK` | Verify dockspace host rect; confirm `enable_docking()` before first `NewFrame()` |
| Layout ignored on load | `LoadIniSettingsFromMemory()` after `DockSpace()` | Load in `apply_session()` / one-shot at start of `draw()` before `draw_dockspace()` |
| Plot area blank | `set_render_figure_enabled(false)` | `main.cpp` sets true; shell toggles via `plot_area_visible()` in extra callback |
| Drag-drop no-op | Payload type mismatch | `FieldDragPayload` + shared payload id in `field_drag_drop.hpp` |
| Auto-save grows every frame | Save without `WantSaveIniSettings` | Only persist ini when `io.WantSaveIniSettings` is true |
| Duplicate status bar | `status_bar_visible_` still true | `main.cpp` must call `set_status_bar_visible(false)` |

---

## 7. Agent Update Protocol

When changing ROS UI behavior:

1. Update the **Completion checklist** (§1) or **Open items** (§4) in this file.
2. Run targeted tests: `ctest -R ros` or `./build/tests/unit_test_ros_session`.
3. For visual changes: `spectra_ros_qa_agent --design-review`.
4. For 3D/display work: cross-link progress in `SPECTRA_ROS_STUDIO_PLAN.md`.

**Historical implementation spec** (pre-completion step-by-step phases): preserved in [`plans/archive/ROS_UI_FIX_PLAN.md`](archive/ROS_UI_FIX_PLAN.md).
ROADMAP.md