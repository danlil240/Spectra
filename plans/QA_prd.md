# Spectra QA Agent — Implementation Plan

## Context

Spectra has 120+ unit tests and golden image tests but lacks automated interactive testing — no tool exercises the real windowed UI by creating figures, clicking buttons, dragging tabs, resizing, and hammering the command system. The QA agent fills this gap: a standalone executable that launches a real GLFW-windowed Spectra app and drives it programmatically through randomized fuzzing and predefined stress scenarios, tracking crashes, Vulkan errors, frame time regressions, and memory growth.

## Approach: `App::step()` Refactor + Standalone QA Executable

The core blocker is that `App::run()` blocks in a while loop (`app_inproc.cpp:506-582`). The QA agent needs frame-by-frame control. The cleanest solution is to refactor `run_inproc()` into three phases — `init_runtime()` / `step()` / `shutdown_runtime()` — then rewrite `run_inproc()` to call them. Zero behavior change for existing users.

---

## Step 1: Add `App::step()` API

**Modify:** `include/spectra/app.hpp`

Add to `App` public section:
- `struct StepResult { bool should_exit; float frame_time_ms; uint64_t frame_number; }`
- `void init_runtime()` — all setup from `run_inproc()` lines 50-504
- `StepResult step()` — one iteration of the main loop (lines 506-581)
- `void shutdown_runtime()` — cleanup (lines 584-688)
- `WindowUIContext* ui_context()` — access to the per-window UI bundle
- `SessionRuntime* session()` — access to session runtime
- `FigureRegistry& figure_registry()` — already exists as `registry_`, just expose it

Add to `App` private section:
- `struct AppRuntime;` forward declaration
- `std::unique_ptr<AppRuntime> runtime_;`

**Create:** `src/ui/app/app_step.cpp`

Implement `init_runtime()`, `step()`, `shutdown_runtime()` by extracting code from `app_inproc.cpp`. The `AppRuntime` struct holds all local variables that currently live on `run_inproc()`'s stack:
- `CommandQueue`, `FrameScheduler`, `Animator`, `SessionRuntime`
- `GlfwAdapter`, `WindowManager` (ifdef GLFW)
- `WindowUIContext*`, headless `WindowUIContext`
- `FrameState`, frame counter

**Modify:** `src/ui/app/app_inproc.cpp`

Rewrite `run_inproc()` as:
```cpp
void App::run_inproc() {
    init_runtime();
    while (auto result = step(); !result.should_exit) {}
    shutdown_runtime();
}
```

### Key Implementation Details

The `AppRuntime` struct encapsulates all state from `run_inproc()`:
```cpp
struct App::AppRuntime {
    CommandQueue   cmd_queue;
    FrameScheduler scheduler;
    Animator       animator;
    SessionRuntime session;

    FrameState      frame_state;
    uint64_t        frame_number = 0;
    WindowUIContext* ui_ctx_ptr  = nullptr;

    std::unique_ptr<WindowUIContext> headless_ui_ctx;

#ifdef SPECTRA_USE_GLFW
    std::unique_ptr<GlfwAdapter>   glfw;
    std::unique_ptr<WindowManager> window_mgr;
#endif

#ifdef SPECTRA_USE_FFMPEG
    std::unique_ptr<VideoExporter> video_exporter;
    std::vector<uint8_t>           video_frame_pixels;
    bool                           is_recording = false;
#endif
};
```

The `init_runtime()` method performs all setup currently in `run_inproc()` lines 50-504:
1. Multi-figure setup, FrameState initialization
2. CommandQueue, FrameScheduler, Animator, SessionRuntime creation
3. GLFW adapter + WindowManager creation (windowed mode)
4. WindowUIContext wiring (ImGui, commands, shortcuts, input handler)
5. Offscreen framebuffer creation (headless mode)
6. Command registration via `register_standard_commands()`
7. Initial home limits capture

The `step()` method performs one loop iteration (lines 506-581):
1. Call `session.tick()` (scheduler, animations, window loop)
2. Handle video frame capture (if recording)
3. Process pending PNG export (interactive mode)
4. Check animation duration termination
5. GLFW event polling fallback
6. Return `StepResult` with frame timing

The `shutdown_runtime()` method performs cleanup (lines 584-688):
1. Finalize video recording
2. Process batch exports (headless PNG/SVG)
3. Release GLFW window handles, shutdown WindowManager
4. GPU wait idle

### Important: CMake Source Registration

**Modify:** `CMakeLists.txt` (root)

Add `src/ui/app/app_step.cpp` to the UI sources list (line 67-97 area):
```cmake
file(GLOB SPECTRA_UI_SOURCES
    src/ui/app/app.cpp
    src/ui/app/app_inproc.cpp
    src/ui/app/app_multiproc.cpp
    src/ui/app/app_step.cpp          # NEW
)
```

Or add it to the `foreach(_ui_src ...)` guarded list below.

---

## Step 2: Create QA Agent Executable

**Create:** `tests/qa/qa_agent.cpp` (~800-1000 lines, single file)

### CLI Interface
```
spectra_qa_agent [options]
  --seed <N>          RNG seed (default: time-based)
  --duration <sec>    Max runtime seconds (default: 120)
  --scenario <name>   Run single scenario (default: all)
  --fuzz-frames <N>   Random fuzzing frames (default: 3000)
  --output-dir <path> Report/screenshot dir (default: /tmp/spectra_qa)
  --no-fuzz           Skip fuzzing phase
  --no-scenarios      Skip scenarios phase
  --list-scenarios    List scenarios and exit
```

### Architecture (all in `qa_agent.cpp`)

**`QAAgent` class** — orchestrator:
- Owns `App` instance (windowed, not headless)
- Owns `ValidationGuard` (from `tests/util/validation_guard.hpp`) for Vulkan error monitoring
- Seeded `std::mt19937` for reproducible randomized testing
- Runs Phase 1 (predefined scenarios) then Phase 2 (random fuzzing)
- After each `app->step()`: checks frame time, Vulkan errors, memory RSS
- Captures screenshots via `backend->readback_framebuffer()` on anomalies

**Action system** — weighted random actions:
| Action | Weight | Description |
|--------|--------|-------------|
| ExecuteCommand | 20 | Random command from registry |
| MouseClick | 15 | Random (x,y) in window |
| MouseDrag | 10 | Random start→end drag |
| MouseScroll | 10 | Random scroll at random pos |
| KeyPress | 10 | Random key + modifiers |
| CreateFigure | 5 | New figure with random data (cap: 20) |
| CloseFigure | 3 | Close random figure (if >1) |
| SwitchTab | 8 | Switch to random figure tab |
| AddSeries | 8 | Random series type + data |
| UpdateData | 5 | Update existing series in-place |
| LargeDataset | 1 | 100K-1M points |
| SplitDock | 3 | Split active pane right/down |
| Toggle3D | 3 | 2D/3D mode switch |
| WaitFrames | 7 | 1-10 frame cooldown |

Actions execute via direct API calls on `ui_context()`:
- Commands: `ui_ctx->cmd_registry.execute(id)`
- Input: `ui_ctx->input_handler.on_mouse_button/move/scroll/key()`
- Figures: `app->figure(config)`, `fig_mgr->queue_close/create/switch()`
- Docking: `ui_ctx->dock_system.split_figure_right/down()`

### Key API Access Points

The QA agent accesses internal subsystems via the new `App::ui_context()` method, which returns a `WindowUIContext*` (defined in `src/ui/app/window_ui_context.hpp`). This struct bundles:

| Member | Type | Purpose |
|--------|------|---------|
| `cmd_registry` | `CommandRegistry` | Execute commands by ID |
| `input_handler` | `InputHandler` | Inject mouse/keyboard events |
| `fig_mgr` | `FigureManager*` | Create/close/switch figures |
| `dock_system` | `DockSystem` | Split panes |
| `undo_mgr` | `UndoManager` | Undo/redo |
| `timeline_editor` | `TimelineEditor` | Animation control |
| `figure_tabs` | `TabBar*` | Tab bar widget |
| `imgui_ui` | `ImGuiIntegration*` | Layout control |

### Registered Commands Available (from `src/ui/app/register_commands.cpp`)

| ID | Shortcut | Description |
|----|----------|-------------|
| `view.reset` | R | Reset view |
| `view.autofit` | A | Auto-fit axes |
| `view.toggle_grid` | G | Toggle grid |
| `view.toggle_crosshair` | C | Toggle crosshair |
| `view.toggle_legend` | L | Toggle legend |
| `view.toggle_border` | B | Toggle border |
| `view.fullscreen` | F | Fullscreen canvas |
| `view.zoom_in` | | Zoom in |
| `view.zoom_out` | | Zoom out |
| `view.toggle_3d` | 3 | Toggle 2D/3D |
| `figure.new` | Ctrl+T | New figure |
| `figure.close` | Ctrl+W | Close figure |
| `figure.tab_1`-`figure.tab_9` | 1-9 | Switch tabs |
| `figure.next_tab` | Ctrl+Tab | Next tab |
| `figure.prev_tab` | Ctrl+Shift+Tab | Prev tab |
| `edit.undo` | Ctrl+Z | Undo |
| `edit.redo` | Ctrl+Shift+Z | Redo |
| `file.export_png` | Ctrl+S | Export PNG |
| `file.export_svg` | Ctrl+Shift+S | Export SVG |
| `anim.toggle_play` | Space | Play/pause |
| `anim.step_back` | [ | Step backward |
| `anim.step_forward` | ] | Step forward |

### Predefined Scenarios (10)

1. **`rapid_figure_lifecycle`** — Create 20 figures, switch randomly for 60 frames, close all but 1
2. **`stress_docking`** — 4 figures, split into 2x2 grid, add tabs, rapid switching, close splits
3. **`massive_datasets`** — 1M-point line + 5x100K series, pan/zoom/toggle overlays, monitor FPS
4. **`undo_redo_stress`** — 50 undoable ops, undo all, redo all, partial undo + new ops
5. **`animation_stress`** — Animated figure, rapid play/pause toggling every 5 frames for 300 frames
6. **`mode_switching`** — Toggle 2D/3D 10 times with data + orbit/pan between each
7. **`input_storm`** — 500 random mouse events + 100 key presses in rapid succession
8. **`command_exhaustion`** — Execute every registered command, then 3x random order
9. **`series_mixing`** — One of each series type, toggle visibility, remove/re-add
10. **`resize_stress`** — 30 rapid window resizes including extreme sizes (100x100, 4096x2160)

### Monitoring (per-frame, after each `step()`)

- **Frame time**: Exponential moving average; spike = >3x EMA -> Warning + screenshot
- **Vulkan validation**: `ValidationGuard` error/warning count delta -> Error + messages
- **Memory RSS**: Check every 60 frames via `/proc/self/status`; >100MB growth -> Warning
- **Device lost**: Check backend status -> Critical

### Reporter Output

**`qa_report.txt`** — human-readable summary:
```
Spectra QA Agent Report
=======================
Seed: 12345
Duration: 120.3s
Total frames: 7200
Scenarios: 10 passed, 0 failed
Fuzz frames: 3000

Frame Time Statistics:
  Average: 4.2ms
  P95: 8.1ms
  P99: 14.3ms
  Max: 45.2ms
  Spikes (>3x avg): 12

Vulkan Validation:
  Errors: 0
  Warnings: 2

Memory:
  Peak RSS: 312MB
  Final RSS: 298MB

Issues (3 total):
  [WARNING] frame_time: Frame 2341 took 45.2ms (10.7x average)
            Screenshot: screenshot_frame2341_frame_spike.png
            Context: CreateLargeDataset (1M points)
  ...

Seed for reproduction: 12345
```

**`qa_report.json`** — machine-readable version for CI integration.

### Signal Handling

Install `SIGSEGV`/`SIGABRT` handler that writes partial report + seed to stderr before `_exit(2)`.

### Screenshot Capture

```cpp
void capture_screenshot(const std::string& reason) {
    auto* backend = app_->backend();
    uint32_t w = backend->swapchain_width();
    uint32_t h = backend->swapchain_height();
    std::vector<uint8_t> pixels(w * h * 4);
    if (backend->readback_framebuffer(pixels.data(), w, h)) {
        std::string path = output_dir_ + "/screenshot_frame"
            + std::to_string(frame_number_) + "_" + sanitize(reason) + ".png";
        ImageExporter::write_png(path, pixels.data(), w, h);
    }
}
```

---

## Step 3: CMake Integration

**Modify:** `tests/CMakeLists.txt`

Add gated build target at the end of the file:
```cmake
# QA Agent (interactive stress test / fuzzer)
option(SPECTRA_BUILD_QA_AGENT "Build QA stress testing agent" OFF)
if(SPECTRA_BUILD_QA_AGENT)
    add_executable(spectra_qa_agent qa/qa_agent.cpp)
    target_link_libraries(spectra_qa_agent PRIVATE spectra)
    target_compile_features(spectra_qa_agent PRIVATE cxx_std_20)
    target_include_directories(spectra_qa_agent PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/util
    )
    target_compile_definitions(spectra_qa_agent PRIVATE
        SPECTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
    # Not registered as a CTest — run manually
endif()
```

Not a CTest — run manually: `./build/tests/spectra_qa_agent --seed 42 --duration 60`

---

## Files Summary

| Action | File | Description |
|--------|------|-------------|
| Modify | `include/spectra/app.hpp` | Add step API declarations (StepResult, init_runtime, step, shutdown_runtime, ui_context, session, figure_registry, AppRuntime forward decl) |
| Create | `src/ui/app/app_step.cpp` | Implement init_runtime/step/shutdown by extracting from app_inproc.cpp |
| Modify | `src/ui/app/app_inproc.cpp` | Refactor run_inproc() to call init_runtime/step/shutdown |
| Modify | `CMakeLists.txt` (root) | Add app_step.cpp to SPECTRA_UI_SOURCES glob or guarded list |
| Create | `tests/qa/qa_agent.cpp` | The QA agent program (~800-1000 lines) |
| Modify | `tests/CMakeLists.txt` | Add SPECTRA_BUILD_QA_AGENT option and build target |

## Existing Code to Reuse

| File | What to Reuse |
|------|---------------|
| `tests/util/validation_guard.hpp` | Vulkan validation layer monitoring (RAII, thread-safe message collection) |
| `tests/util/gpu_hang_detector.hpp` | GPU hang detection with configurable timeout |
| `src/io/png_export.cpp` | `ImageExporter::write_png()` for screenshot capture |
| `src/ui/app/window_ui_context.hpp` | `WindowUIContext` struct bundles all UI subsystems for direct API access |
| `src/ui/app/register_commands.hpp` | `CommandBindings` struct and `register_standard_commands()` |
| `src/ui/app/session_runtime.hpp` | `SessionRuntime` with `tick()`, `should_exit()`, `request_exit()` |
| `src/ui/app/window_runtime.hpp` | `FrameState` struct, `WindowRuntime` update/render cycle |

## Verification

1. Build: `cmake -B build -G Ninja -DSPECTRA_BUILD_QA_AGENT=ON && cmake --build build -j$(nproc)`
2. Existing tests still pass: `ctest --test-dir build -LE gpu --output-on-failure`
3. Run QA agent: `./build/tests/spectra_qa_agent --seed 42 --duration 30 --output-dir /tmp/spectra_qa`
4. Check report: `cat /tmp/spectra_qa/qa_report.txt`
5. Verify `run_inproc()` refactor: existing examples still work (e.g., `./build/examples/basic_line`)
