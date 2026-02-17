# Tab Tear-Off to New Window — Engineering Plan

**Date:** 2026-02-17
**Author:** Cascade (Principal Graphics Architect)
**Status:** PLAN ONLY — Do not implement

---


# EXECUTION RULES (MANDATORY)

1) This document is the single source of truth. After every work session, the agent MUST:
   - Update this plan in-place:
     a) Mark completed items with ✅ and date
     b) Add a short "What changed" note per completed item
     c) Add a "Next steps" list for the next session
     d) Update the risk register if new risks appear

2) After every session, the agent MUST provide to the user:
   - "How to verify" steps using existing examples (do NOT create a new example unless none exists).
   - Exact commands to run (build + run), and the precise UI interactions to test.
   - Expected results and common failure symptoms.

3) Every PR must be small and single-purpose:
   - One phase step per PR (or sub-step), no drive-by refactors.
   - Must include a verification checklist in the PR description.

4) If the agent gets blocked, they MUST:
   - State the blocking dependency
   - Propose two alternative paths (minimal and ideal)
   - Choose one and proceed unless it violates correctness.


## 1. CURRENT-STATE ARCHITECTURE SUMMARY

### 1A. Text Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                        App::run()                               │
│  (src/ui/app.cpp — monolithic 2553-line function)               │
│                                                                 │
│  owns:  FigureRegistry ─── stable-ID figure storage (mutex)     │
│         VulkanBackend  ─── Vulkan device, shared GPU resources   │
│         Renderer       ─── pipelines, series GPU data, UBOs     │
│                                                                 │
│  stack-local in run():                                          │
│         GlfwAdapter    ─── primary OS window + input callbacks   │
│         WindowManager  ─── secondary window lifecycle            │
│         ImGuiIntegration ─ single ImGui context, all UI drawing │
│         FigureManager  ─── figure lifecycle, tab bar sync        │
│         DockSystem     ─── split-view tree (SplitViewManager)    │
│         TabBar         ─── top-level tab bar widget              │
│         InputHandler   ─── mouse/keyboard → axes mutations       │
│         + 15 more subsystems (undo, shortcuts, timeline, etc.)  │
└─────────────────────────────────────────────────────────────────┘

Vulkan resource ownership:
┌──────────────────────────────────────────────────────────────┐
│ VulkanBackend (shared, device-lifetime)                      │
│  ├─ VkInstance, VkDevice, VkPhysicalDevice                   │
│  ├─ VkCommandPool (shared across all windows)                │
│  ├─ VkDescriptorPool, descriptor set layouts                 │
│  ├─ VkPipelineLayout, VkPipeline map                         │
│  ├─ Buffer/Texture registries (SSBO, UBO, textures)          │
│  └─ primary_window_ (WindowContext — embedded, not heap)      │
│                                                              │
│ WindowContext (per-window, swapchain-lifetime)                │
│  ├─ void* glfw_window                                        │
│  ├─ VkSurfaceKHR                                             │
│  ├─ SwapchainContext (swapchain, images, views, render pass,  │
│  │   framebuffers, depth, MSAA)                               │
│  ├─ vector<VkCommandBuffer> (from shared pool)               │
│  ├─ vector<VkSemaphore> × 2 (image_available, render_done)  │
│  ├─ vector<VkFence> (in_flight_fences)                       │
│  ├─ BufferHandle frame_ubo_buffer                            │
│  ├─ FigureId assigned_figure_index                           │
│  └─ resize state, focus state, should_close                  │
└──────────────────────────────────────────────────────────────┘

UI rendering pipeline (single frame):
┌─────────────────────────────────────────────────────────────────┐
│ 1. glfwPollEvents()                                             │
│ 2. ImGuiIntegration::new_frame()   — ImGui_ImplGlfw_NewFrame() │
│ 3. ImGuiIntegration::build_ui()    — command bar, nav rail,     │
│    tab headers, canvas, inspector, status bar, overlays         │
│ 4. DockSystem::update_layout(canvas_rect)                       │
│ 5. compute_subplot_layout() per pane                            │
│ 6. backend->begin_frame()          — fence wait, acquire image  │
│ 7. renderer->begin_render_pass()   — vkCmdBeginRenderPass      │
│ 8. renderer->render_figure_content() per pane figure            │
│ 9. ImGuiIntegration::render()      — ImGui_ImplVulkan_RenderDrawData │
│ 10. renderer->end_render_pass()    — vkCmdEndRenderPass        │
│ 11. backend->end_frame()           — submit, present            │
│                                                                 │
│ For secondary windows (lines 2306-2368 of app.cpp):             │
│ 12. For each secondary WindowContext:                           │
│     a. vk->set_active_window(wctx)                              │
│     b. backend->begin_frame() / render / end_frame()            │
│     c. NO ImGui rendering (raw figure only)                     │
│ 13. vk->set_active_window(&primary)  — restore                  │
└─────────────────────────────────────────────────────────────────┘
```

### 1B. Current UI Structure

**Tab construction:**
- `TabBar` (src/ui/tab_bar.hpp:22) — custom-drawn widget using ImGui draw lists. NOT ImGui's built-in tab bar. Draws tabs, close buttons, add button, scroll buttons, context menu.
- `ImGuiIntegration::draw_pane_tab_headers()` — draws per-pane tab headers when split view is active. Has its own `PaneTabDragState` for cross-pane tab dragging.
- Tab bar lives in the `LayoutManager` tab bar zone (but currently hidden — line 1989: `set_tab_bar_visible(false)`). Unified pane tab headers handle all tabs.

**Figure representation:**
- `FigureRegistry` (src/ui/figure_registry.hpp) — thread-safe registry with monotonic `uint64_t` IDs (`FigureId`). Owns `unique_ptr<Figure>`. Never reuses IDs.
- `FigureManager` (src/ui/figure_manager.hpp) — manages ordered list of `FigureId`s, per-figure `FigureState` (axis snapshots, inspector state, scroll positions). Wired to `TabBar` for sync.
- `FigureId` typedef = `uint64_t`, sentinel = `INVALID_FIGURE_ID = ~0ULL`.

**Split behavior:**
- `DockSystem` (src/ui/dock_system.hpp) orchestrates `SplitViewManager` which owns a tree of `SplitPane` nodes. Each leaf pane holds a `vector<FigureId>` (multi-figure per pane) and an `active_local_` index.
- Drag-to-dock: `DockSystem::begin_drag/update_drag/end_drag` computes `DropTarget` with `DropZone` (Left/Right/Top/Bottom/Center). Creates new split panes on drop.
- Tab bar vertical drag (>30px) triggers `on_tab_drag_out_` callback → `dock_system.begin_drag()`.

**Input routing:**
- `GlfwAdapter` owns the primary window's GLFW user pointer and callbacks (cursor, button, scroll, key, resize).
- Callbacks capture `InputHandler` + `ImGuiIntegration` + `DockSystem` by reference. Route through ImGui capture checks first.
- `WindowManager` installs its own GLFW callbacks on secondary windows (framebuffer size, close, focus only — NO input callbacks for mouse/key).
- Focus tracked per `WindowContext::is_focused`.

### 1C. Current Rendering Structure

**Render loop:** Single monolithic `App::run()` function (2553 lines). All objects stack-local.

**Swapchain ownership:** `WindowContext` owns the `vk::SwapchainContext` (swapchain handle, images, views, framebuffers, depth, MSAA). Primary `WindowContext` is embedded in `VulkanBackend` (`primary_window_` member). Secondary contexts are heap-allocated by `WindowManager`.

**Frame resources:** Per-`WindowContext`: command buffers (allocated from shared `command_pool_`), semaphores × 2 per swapchain image, fences per swapchain image, `frame_ubo_buffer`.

**Resize handling:** Primary window: debounced in `on_resize` callback (recreates swapchain inline, renders immediate frame). Also debounced in main loop. Secondary windows: debounced in main loop (lines 2330-2342), uses `recreate_swapchain_for()`.

**ImGui backend:** Single `ImGuiIntegration` instance. `init()` calls `ImGui_ImplGlfw_InitForVulkan(window)` + `ImGui_ImplVulkan_Init()`. Tied to primary window's render pass and swapchain image count. Secondary windows get NO ImGui rendering.

### 1D. Current Object Ownership / Lifetime

| Object | Owner | Lifetime |
|--------|-------|----------|
| `Figure` | `FigureRegistry` (via `unique_ptr`) | Until `unregister_figure()` |
| `FigureState` | `FigureManager::states_` map | Until figure closed |
| `ImGui context` | Global singleton (`ImGui::CreateContext()`) | App lifetime |
| `GLFWwindow*` (primary) | `GlfwAdapter` | Until `shutdown()` |
| `GLFWwindow*` (secondary) | `WindowManager` | Until `destroy_window()` |
| `WindowContext` (primary) | `VulkanBackend::primary_window_` (embedded) | Backend lifetime |
| `WindowContext` (secondary) | `WindowManager::windows_` (vector of unique_ptr) | Until window destroyed |
| `VkSurfaceKHR` per window | `WindowContext` | Swapchain lifetime |
| Pipelines, descriptors | `VulkanBackend` | Device lifetime |
| Series GPU buffers | `Renderer::series_gpu_data_` | Until series removed |

**On GLFW window close:** Secondary: `WindowManager::destroy_window()` → `VulkanBackend::destroy_window_context()` (waits fences, destroys Vulkan resources) → `glfwDestroyWindow()`. Primary: sets `should_close = true`, `App::run()` exits main loop → `window_mgr->shutdown()` destroys all remaining secondaries → `glfw->shutdown()` destroys primary → `backend->wait_idle()` + destructors.

---

## 2. SINGLE-WINDOW ASSUMPTIONS LIST

These are code locations that assume a single OS window, single ImGui context, or single active figure pipeline, and will need modification:

| # | Location | Assumption | Impact |
|---|----------|-----------|--------|
| S1 | `App::run()` lines 624-627: `imgui_ui->init(*vk, glfw_window)` | ImGui initialized with primary window only | Secondary windows have no ImGui |
| S2 | `App::run()` lines 1932-1938: `imgui_ui->new_frame()` | Single ImGui frame per render loop iteration | Cannot build UI for multiple windows |
| S3 | `App::run()` lines 1983: `imgui_ui->build_ui(*active_figure)` | UI built for one figure in one window | Secondary windows get raw figure rendering only |
| S4 | `App::run()` lines 2306-2367: secondary window render loop | NO ImGui, NO input routing, figure-only rendering | New windows are bare canvases with no chrome |
| S5 | `GlfwAdapter` callbacks (lines 330-618) | All input callbacks capture stack-local objects by reference | Cannot route input to secondary windows' UI |
| S6 | `ImGuiIntegration::init()` | `ImGui_ImplGlfw_InitForVulkan(window)` — binds to one GLFW window | ImGui input polling reads from primary window only |
| S7 | `WindowContext::assigned_figure_index` — single FigureId | One figure per secondary window | Need multi-figure (tabs) per window |
| S8 | `FigureManager` wired to single `TabBar` | One tab bar, one ordered figure list | Need per-window figure lists |
| S9 | `DockSystem` / `SplitViewManager` — one instance | One split tree for the whole app | Need per-window split state |
| S10 | `InputHandler` — one instance | Routes all input to one active figure/axes | Need per-window input handler |
| S11 | `LayoutManager` — one instance inside ImGuiIntegration | One layout (command bar, nav rail, inspector, canvas) | Need per-window layout |
| S12 | `ShortcutManager` / `CommandRegistry` — one each | Global shortcut state, focus not window-aware | Minor — can remain global with focus gating |
| S13 | `UndoManager` — one instance | Global undo stack | Can remain global (single undo history is fine) |
| S14 | `ThemeManager::instance()` — global singleton | One theme | OK — shared across windows (desired) |
| S15 | `VulkanBackend::primary_window_` — embedded member | Primary context is special, not heap-allocated | Complicates uniform iteration |
| S16 | Main loop termination: `glfw->should_close()` only checks primary | Closing primary kills app | Need "any window open" check (already exists in `WindowManager::any_window_open()` but not used for loop termination) |
| S17 | `Renderer` — single instance, uses `backend_->current_command_buffer()` | Renders into whatever `active_window_` is set | OK — already window-agnostic via `set_active_window()` |
| S18 | `Workspace::capture()` takes flat figure list | No per-window state in workspace | Need window→figure mapping in serialization |

---

## 3. TARGET ARCHITECTURE DESIGN

### 3A. WindowContext Enhancement

The existing `WindowContext` struct (src/render/vulkan/window_context.hpp) is already well-designed. Extend it:

```
WindowContext (enhanced)
├── [existing Vulkan resources — unchanged]
├── vector<FigureId> assigned_figures   // REPLACE single assigned_figure_index
├── FigureId active_figure_id           // Active tab in this window
├── unique_ptr<ImGuiIntegration> imgui  // Per-window ImGui (if strategy 2)
├── unique_ptr<DockSystem> dock_system  // Per-window split state
├── unique_ptr<InputHandler> input      // Per-window input routing
├── unique_ptr<FigureManager> fig_mgr   // Per-window figure management
├── bool is_primary                     // True for the original window
└── string title                        // OS window title
```

### 3B. WindowManager Enhancement

Existing `WindowManager` (src/ui/window_manager.hpp) already has `create_window()`, `detach_figure()`, `move_figure()`, `destroy_window()`, per-window GLFW callbacks. Enhancements needed:

- **`detach_figure_with_ui()`** — Creates window AND initializes ImGui context + all UI subsystems for it.
- **`render_all_windows()`** — Drives begin_frame/build_ui/render/end_frame for every window.
- **Per-window input callback installation** — Mouse, keyboard, scroll (not just framebuffer/close/focus as today).
- **Window reattach** — Move figure from secondary back to primary (optional, simplest behavior is destroy on close).

### 3C. FigureManager / Ownership Model

`FigureRegistry` remains the single source of truth for figure ownership (thread-safe, stable IDs). This is already correct.

**Ownership transfer = ID reassignment only:**
- Detach: remove `FigureId` from source window's `assigned_figures` → add to new window's `assigned_figures`.
- No pointer movement. No GPU resource invalidation. `FigureRegistry` doesn't change.
- Each window's `FigureManager` maintains its own `ordered_ids_` and `states_` for the figures assigned to it.

**Window close policy (simplest correct behavior):**
- Closing a window **destroys its figures** (unregisters from `FigureRegistry`).
- Exception: if it's the last window, close means app exit (all figures destroyed naturally).
- Rationale: "return to another window" requires UI for choosing destination, undo support, etc. — over-engineered for V1. Can be added later as an option.

### 3D. ImGui Context Strategy

**Decision: Option 2 — One ImGui context per OS window.**

Trade-offs:

| | Single shared context | Per-window context |
|---|---|---|
| **Pros** | Shared font atlas, shared style. Simpler if using ImGui viewports. | Complete isolation. No cross-window state leaks. Each window has its own docking root. Matches "identical window" requirement perfectly. |
| **Cons** | ImGui viewport system needs platform backend per window. Docking across windows requires ImGui multi-viewport (experimental, buggy with Vulkan custom render passes). Input routing is complex. | Must duplicate font atlas upload (one-time, ~200KB). Must set theme per context. Slightly more memory. |
| **Risk** | HIGH — ImGui multi-viewport with custom Vulkan render passes is fragile. | LOW — Well-understood pattern. Each window is self-contained. |

**Justification:** The requirement says each new window must be "identical to the original window (same ImGui layout style, same renderer configuration, same shortcuts, same theme, same docking behavior)." Per-window contexts achieve this trivially — each window gets a fresh `ImGuiIntegration` instance configured identically. No shared mutable state to synchronize.

**Implementation:**
- Each `WindowContext` gets a `unique_ptr<ImGuiIntegration>`.
- `ImGuiIntegration::init()` takes a `GLFWwindow*` — already parameterized.
- Font atlas: loaded once, texture uploaded per-context (reuse same pixel data).
- Theme: applied at init from `ThemeManager::instance()` (global singleton, read-only).
- Before each window's frame: `ImGui::SetCurrentContext(wctx->imgui_context)`.

### 3F. ImGui + Vulkan Render Pass Compatibility Constraint (HARD REQUIREMENT)

**Problem:** `ImGui_ImplVulkan_Init()` takes a `VkRenderPass` (line 104 of `imgui_integration.cpp`). Today this is `backend.render_pass()` which returns `active_window_->swapchain.render_pass`. Each window's render pass is created by `vk::create_render_pass()` using the surface format returned by `choose_surface_format()` for that specific `VkSurfaceKHR`. On the same GPU, different surfaces (e.g. different monitors, different color spaces) can yield **different** `VkSurfaceFormatKHR` values, producing **incompatible** render passes.

**Constraint:** Every agent touching ImGui initialization or swapchain creation MUST follow this strategy:

1. **Per-window ImGui initialization must use that window's own render pass.** `ImGuiIntegration::init()` already takes `VulkanBackend&` and reads `backend.render_pass()`. Before calling `init()` for a secondary window, `set_active_window(wctx)` must be called so `render_pass()` returns the correct per-window render pass.

2. **Swapchain format consistency enforcement:** In `WindowManager::create_window_with_ui()`, after swapchain creation, **assert** that `wctx->swapchain.image_format == primary_window_.swapchain.image_format`. If they differ (rare — only on exotic multi-monitor setups), log a warning and force the secondary surface to use the primary's format via explicit `VkSwapchainCreateInfoKHR::imageFormat` override. This guarantees pipeline compatibility (shared `VkPipelineLayout` and `VkPipeline` objects depend on render pass compatibility).

3. **Per-window `ImageCount` for ImGui init:** `ImGui_ImplVulkan_InitInfo::ImageCount` must match the specific window's swapchain image count. Different surfaces can have different min/max image counts. Read from `wctx->swapchain.images.size()`, not from the primary.

4. **`on_swapchain_recreated()` per window:** When any window's swapchain is recreated, only that window's ImGui backend needs updating. Do NOT call the primary window's `on_swapchain_recreated()` when a secondary window resizes.

5. **Render pass lifetime:** ImGui holds a raw `VkRenderPass` handle. If swapchain recreation destroys and recreates the render pass (it does — `vk::destroy_swapchain()` then `vk::create_swapchain()`), ImGui must be notified. Current `on_swapchain_recreated()` does NOT re-init ImGui's render pass — **this is a latent bug** that is harmless today (primary window always has same format after recreate) but will bite in multi-window. **Fix required:** add `ImGui_ImplVulkan_SetRenderPass()` call in `on_swapchain_recreated()`, or destroy+reinit the ImGui Vulkan backend on render pass change.

**Test:** Create a window, resize it, verify no validation errors about render pass compatibility. Repeat with `VK_LAYER_KHRONOS_validation` enabled.

### 3E. Drag Preview Policy (No Chrome)

During drag (state `DraggingDetached`):

1. **Source window** continues rendering normally (minus the dragged figure's tab).
2. **Drag overlay** is rendered as a small floating OS tooltip window (GLFW undecorated window, ~300×200px) showing a thumbnail of the figure content.
   - Created at drag start, destroyed at drag end.
   - Positioned at cursor with offset.
   - Rendered with a minimal `begin_frame/render_figure_content/end_frame` cycle (no ImGui, no chrome).
   - Uses its own `WindowContext` with a tiny swapchain.
3. **Alternatively (simpler):** No preview window — just change the cursor to a "drag" icon and show a ghost tab (semi-transparent rectangle following cursor, drawn via ImGui overlay on the source window). This avoids creating a transient Vulkan swapchain.

**Recommended approach: Ghost tab overlay (simpler, no transient swapchain).**
- While `DraggingDetached`, draw a semi-transparent rectangle at cursor position using `ImGui::GetForegroundDrawList()`.
- Rectangle shows tab title + small figure thumbnail (optional: just the title).
- All toolbars/menus/chrome hidden for the dragged content only (other tabs continue rendering normally).
- This satisfies R1 ("show ONLY figure content and tab header") without the complexity of a temporary OS window.

---

## 4. DRAG STATE MACHINE SPEC

```
                     ┌──────────┐
            ESC      │          │
         ┌──────────►│  Cancel  │
         │           │          │
         │           └──────────┘
         │                 ▲
         │                 │ ESC / right-click
         │                 │
    ┌────┴─────┐     ┌────┴──────────────┐
    │          │     │                    │
    │   Idle   │────►│ DragStartCandidate │
    │          │     │                    │
    └──────────┘     └────┬──────────────┘
         ▲                │
         │                │ mouse move > 10px threshold
         │                ▼
         │          ┌─────────────────────┐
         │          │                     │
         │          │  DraggingDetached   │
         │          │  (ghost tab visible)│
         │          │                     │
         │          └──┬──────────┬───────┘
         │             │          │
         │   mouse up  │          │ mouse up
         │   INSIDE    │          │ OUTSIDE
         │   window    │          │ window
         │             ▼          ▼
         │      ┌──────────┐  ┌──────────────┐
         │      │DropInside│  │ DropOutside  │
         │      │(dock/    │  │(spawn new    │
         │      │ split)   │  │ window)      │
         │      └────┬─────┘  └──────┬───────┘
         │           │               │
         └───────────┴───────────────┘
                  → Idle
```

### State Definitions

**Idle:**
- Normal tab bar interaction. No drag in progress.
- Rendered: normal UI with all chrome.

**DragStartCandidate:**
- Trigger: `mouse_down` on a tab header.
- Purpose: distinguish click-to-select from drag. Wait for mouse movement.
- Threshold: 10px total displacement from click origin.
- If `mouse_up` before threshold → treat as tab click (select tab), go to Idle.
- Data: `source_window_id`, `figure_id`, `click_origin(x,y)`.

**DraggingDetached:**
- Trigger: mouse moved > threshold from DragStartCandidate.
- Rendered: ghost tab overlay follows cursor. Source window hides the dragged tab from its header. Drop zone indicators shown on hovered windows.
- Data: `source_window_id`, `figure_id`, `current_mouse(x,y)`, `hovered_window_id`.
- Per frame: call `glfwGetCursorPos()` on focused window → convert to screen coords.

**DropInside:**
- Trigger: `mouse_up` while cursor is inside ANY open window's bounds.
- If inside source window → delegate to `DockSystem::end_drag()` (existing split/dock behavior).
- If inside a different window → move figure to that window (add to its figure list).
- Transition: immediate → Idle.
- Side effects: figure ID transferred, tab bars updated, split state updated.

**DropOutside:**
- Trigger: `mouse_up` while cursor is outside ALL open windows.
- Side effects:
  1. Remove `figure_id` from source window's figure list.
  2. `WindowManager::detach_figure_with_ui()` — creates new OS window at cursor screen position.
  3. Initialize ImGui + all subsystems for new window.
  4. Assign `figure_id` to new window.
- Transition: immediate → Idle.

**Cancel:**
- Trigger: `ESC` key, right-click, or window focus loss during drag.
- Side effects: destroy ghost overlay, restore dragged tab to original position.
- Transition: immediate → Idle.

### "Outside window" Detection

```cpp
bool is_outside_all_windows(float screen_x, float screen_y) {
    for (auto* wctx : window_mgr->windows()) {
        int wx, wy, ww, wh;
        glfwGetWindowPos((GLFWwindow*)wctx->glfw_window, &wx, &wy);
        glfwGetWindowSize((GLFWwindow*)wctx->glfw_window, &ww, &wh);
        if (screen_x >= wx && screen_x < wx + ww &&
            screen_y >= wy && screen_y < wy + wh) {
            return false;  // Inside this window
        }
    }
    return true;
}
```

Uses `glfwGetWindowPos` + `glfwGetWindowSize` (screen coordinates). Works across monitors and DPI because GLFW reports in screen coords.

---

## 5. PHASED EXECUTION PLAN

### Phase 1: Per-Window UI Subsystem Extraction (No New UX)

**Goal:** Refactor so that UI subsystems (ImGuiIntegration, FigureManager, DockSystem, InputHandler) can be instantiated per-window, without actually creating multiple windows yet.

⚠️ **This phase is split into two mandatory PRs to reduce merge risk and unblock downstream agents early.**

---

#### Phase 1 — PR1: Extract Factory (`create_window_ui`) — Zero Behavior Change

✅ **COMPLETED** — 2026-02-17 (Agent C, Day 0)

**What changed:**
- Created `src/ui/window_ui_context.hpp` — `WindowUIContext` struct bundling all per-window UI subsystems (ImGuiIntegration, FigureManager, DockSystem, InputHandler, TabBar, DataInteraction, BoxZoomOverlay, AxisLinkManager, TimelineEditor, KeyframeInterpolator, AnimationCurveEditor, ModeTransition, CommandRegistry, ShortcutManager, UndoManager, CommandPalette, AnimationController, GestureRecognizer, resize state).
- `App::run()` now creates a `unique_ptr<WindowUIContext>` and uses reference aliases (`auto& imgui_ui = ui_ctx->imgui_ui;` etc.) so the rest of the function body is unchanged.
- `WindowContext` (src/render/vulkan/window_context.hpp) extended with: `vector<FigureId> assigned_figures`, `FigureId active_figure_id`, `bool is_primary`, `string title`.
- `WindowUIContext` forward-declared in `include/spectra/fwd.hpp`.
- `FigureManager` ownership: `fig_mgr_owned` (unique_ptr) + `fig_mgr` (raw ptr) in `WindowUIContext`.
- Approach: reference aliases instead of a factory function — simpler, zero behavior change, callback captures remain valid.
- All 69 tests pass. All 179 build targets compile. Zero new warnings.

**Scope:** Move subsystem creation/wiring OUT of `App::run()` into a reusable factory.

**Changes:**
- Define `WindowUIContext` struct in new file `src/ui/window_ui_context.hpp`. Contains `unique_ptr` members for: `ImGuiIntegration`, `FigureManager`, `DockSystem`, `InputHandler`, `TabBar`, `DataInteraction`, `BoxZoomOverlay`, `AxisLinkManager`, `TimelineEditor`, `KeyframeInterpolator`, `AnimationCurveEditor`, `ModeTransition`, `CommandRegistry`, `ShortcutManager`, `UndoManager`, `CommandPalette`.
- Create free function `create_window_ui(WindowContext&, FigureRegistry&, VulkanBackend&, Renderer&, GlfwAdapter&) → WindowUIContext` that moves the ~800 lines of subsystem creation + callback wiring (app.cpp lines 218-720 and 731-864) into this factory.
- `App::run()` calls `create_window_ui()` and uses the returned struct. All existing stack-local variables become members of `WindowUIContext`.
- `WindowContext` gets new fields: `vector<FigureId> assigned_figures`, `FigureId active_figure_id`.
- **NO changes to the main loop body.** The main loop still directly references `WindowUIContext` members where it previously referenced stack-locals.
- **NO changes to rendering code, input callbacks, or secondary window handling.**

**Acceptance criteria:**
- Bit-for-bit identical behavior: tabs, splits, inspector, shortcuts, animation, resize — all unchanged.
- All existing tests pass.
- `App::run()` shrinks by ~600 lines (subsystem creation moved out).
- `WindowUIContext` compiles and is usable as a standalone type (can be instantiated again in Phase 2).
- Zero new warnings under `-Wall -Wextra`.

**Risk:** Medium — large mechanical extraction. Callback captures must reference `WindowUIContext` members correctly. Review with diff carefully.

**Merge gate:** This PR merges first. Agents B, D, E can begin design work once this lands (they need the `WindowUIContext` type).

---

#### Phase 1 — PR2: Extract Loop Helpers (`update_window` / `render_window`) — Zero Behavior Change

✅ **COMPLETED** — 2026-02-17 (Agent C, Day 0)

**What changed:**
- Added `App::FrameState` struct to `include/spectra/app.hpp` — bundles per-frame mutable state (`active_figure`, `active_figure_id`, `has_animation`, `anim_time`, `imgui_frame_started`).
- Added three private methods to `App`:
  - `update_window(WindowUIContext&, FrameState&, FrameScheduler&, GlfwAdapter*, WindowManager*)` — ~450 lines: timeline advance, mode transition, input handler update, series callbacks, user on_frame, ImGui new_frame + build_ui, figure operations, dock sync, layout computation.
  - `render_window(WindowUIContext&, FrameState&, GlfwAdapter*)` → `bool` — ~100 lines: begin_frame, swapchain retry, render pass, figure content (split/non-split), ImGui render, end_frame.
  - `render_secondary_window(WindowContext*)` — ~50 lines: per-window resize debounce, set_active_window, begin_frame, render, end_frame.
- Main loop body replaced with: `update_window(*ui_ctx, frame_state, scheduler, ...); render_window(*ui_ctx, frame_state, ...); for each secondary: render_secondary_window(wctx);`
- Removed `switch_active_figure` lambda — figure switching now handled inside `update_window()` via `FrameState`.
- Cleaned up redundant direct includes in `app.cpp` (now transitively included via `window_ui_context.hpp`).
- All 69 tests pass. All build targets compile. Zero new warnings.

**Scope:** Move the per-frame logic OUT of the `while(running)` body into helper functions.

**Depends on:** PR1 merged.

**Changes:**
- Create `update_window(WindowUIContext&, FrameScheduler&, ...)` — contains: animation advance, timeline update, mode transition update, input handler update, ImGui new_frame + build_ui, figure operations processing, dock sync, layout computation. (app.cpp lines ~1816-2218)
- Create `render_window(WindowUIContext&, VulkanBackend&, Renderer&)` — contains: begin_frame, render pass, figure rendering (split and non-split paths), ImGui render, end_render_pass, end_frame. (app.cpp lines ~2220-2304)
- The main loop becomes: `for each window: update_window(ui_ctx); render_window(ui_ctx);`
- Secondary window render loop (lines 2306-2367) refactored to also call `render_window()` (without ImGui for now — that comes in Phase 2).
- Global per-frame logic (scheduler, command queue drain, termination checks, GLFW poll) stays in the main loop.

**Acceptance criteria:**
- Bit-for-bit identical behavior to post-PR1 state.
- All existing tests pass.
- `App::run()` is now ~300-400 lines of orchestration + a clear `for each window` structure.
- `update_window()` and `render_window()` have clear function signatures documenting their dependencies.
- Zero new warnings.

**Risk:** Low-Medium — mechanical extraction, but must correctly handle the `frame_ok = false` retry path and `imgui_frame_started` flag across update/render boundary.

**Merge gate:** This PR merges second. After this, Agent B can implement `create_window_with_ui()` that calls the same `create_window_ui()` factory + `render_window()` helper.

### Phase 2: Multiple Windows with Full ImGui (Manual Spawn)

✅ **COMPLETED** — 2026-02-17 (Agent B, Day 0)

**What changed:**
- `WindowManager::create_window_with_ui()` — full implementation: creates GLFW window, Vulkan resources via `init_window_context()`, sets window position, assigns figure, installs full GLFW input callbacks (cursor, mouse button, scroll, key), and initializes the complete UI subsystem bundle via `init_window_ui()`.
- `WindowManager::init_window_ui()` — creates per-window `WindowUIContext` with: ImGuiIntegration (per-window ImGui context + font atlas), FigureManager, DockSystem, InputHandler, TabBar, DataInteraction, BoxZoomOverlay, AxisLinkManager, TimelineEditor, KeyframeInterpolator, AnimationCurveEditor, ModeTransition, CommandRegistry, ShortcutManager, UndoManager, CommandPalette. All callbacks wired (tab change/close/add/duplicate/drag, dock sync, series selection, pane context menus, figure title lookup).
- Full GLFW input callbacks for secondary windows: `glfw_cursor_pos_callback`, `glfw_mouse_button_callback`, `glfw_scroll_callback`, `glfw_key_callback` — all route through per-window `WindowUIContext` with ImGui capture checks, split-pane figure targeting, and DockSystem awareness.
- Main loop handles secondary windows with full UI: `set_active_window` → `SetCurrentContext(imgui_context)` → `update_window()` → `render_window()` → restore primary context.
- `app.new_window` command registered (Ctrl+Shift+N) — duplicates active figure into a new window.
- Primary window's `imgui_context` stored in `WindowContext` after init.
- Per-window ImGui crash fixes (Agent D): per-context font atlas, explicit `SetCurrentContext` after `CreateContext`, context switching in main loop.
- All 69 tests pass. All 107 build targets compile. Zero new warnings.

**Goal:** Create a second window from a debug command (Ctrl+Shift+N) that renders independently with its own ImGui context, chrome, and figure.

**Changes:**
- `WindowManager::create_window_with_ui()` — creates GLFW window + Vulkan resources + `WindowUIContext`.
- Install full GLFW callbacks (mouse, key, scroll, resize, close) on secondary windows.
- Per-window `ImGui::CreateContext()` + `ImGui_ImplGlfw_InitForVulkan()` + `ImGui_ImplVulkan_Init()`.
- Main loop iterates all windows: for each, `ImGui::SetCurrentContext()` → `new_frame()` → `build_ui()` → `begin_frame()` → render → `end_frame()`.
- Register `app.new_window` command.

**Acceptance criteria:**
- Two independent windows, each with full UI (tabs, inspector, menus).
- Independent resize on both windows — no validation errors, no GPU hang.
- Closing one window does not close the other.
- Theme changes apply to both windows.

**Risk:** Medium — ImGui per-context font atlas upload, render pass compatibility.

### Phase 3: Figure Ownership Decoupling

✅ **COMPLETED** — 2026-02-17 (Agent B, Day 0)

**What changed:**
- Added `FigureManager::remove_figure(FigureId)` — removes figure from this manager's `ordered_ids_` and tab bar without unregistering from `FigureRegistry`. Returns the `FigureState` (axis snapshots, inspector state, title, modified flag) for transfer.
- Added `FigureManager::add_figure(FigureId, FigureState)` — adds an existing registry figure to this manager with transferred state. Syncs tab bar and switches to it. Guards against duplicates.
- Updated `WindowManager::move_figure()` to sync per-window FigureManagers: calls `remove_figure()` on source, `close_pane()` on source DockSystem, `add_figure()` on target, and updates target InputHandler.
- Registered `figure.move_to_window` debug command (Ctrl+Shift+M) — moves active figure from primary to next available secondary window, or creates a new window if none exists.
- Added 5 unit tests: `RemoveFigureReturnsState`, `RemoveFigureInvalidId`, `AddFigureFromAnotherManager`, `AddFigureDuplicateIsNoop`, `RemoveLastFigureSetsInvalidActive`.
- All 69 tests pass. All 107 build targets compile. Zero new warnings.

**Files modified:**
- `src/ui/figure_manager.hpp` — added `remove_figure()`, `add_figure()` declarations
- `src/ui/figure_manager.cpp` — added `remove_figure()`, `add_figure()` implementations
- `src/ui/window_manager.cpp` — FigureManager sync in `move_figure()`
- `src/ui/app.cpp` — registered `figure.move_to_window` command
- `tests/unit/test_figure_manager.cpp` — added 5 cross-window transfer tests

**Goal:** Figures can be moved between windows programmatically (no drag UX yet).

**Changes:**
- `WindowManager::move_figure(FigureId, from_window, to_window)` — removes from source `FigureManager`, adds to target `FigureManager`. Updates split panes.
- Handle edge cases: moving the last figure (refuse or close source window), moving to a window that already has the figure (no-op).
- Register `figure.move_to_window` debug command.
- Add logging for every ownership transfer.

**Acceptance criteria:**
- `move_figure()` is stable — no crashes, no dangling FigureIds, state preserved.
- Source window updates its tabs and splits. Target window shows the new figure.
- Axis limits, series data, inspector state all survive the move.

**Risk:** Low — figures already use stable IDs; FigureRegistry is thread-safe.

### Phase 4: Drag + Detach Preview Mode (No Spawning Yet)

✅ **COMPLETED** — 2026-02-17 (Agent D, Day 0)

**What changed:**
- Created `src/ui/tab_drag_controller.hpp` — `TabDragController` class with formal state machine (Idle → DragStartCandidate → DraggingDetached → DropInside/DropOutside/Cancel → Idle).
- Created `src/ui/tab_drag_controller.cpp` — implementation with multi-window outside-detection via `glfwGetWindowPos/Size` on all managed windows.
- Integrated into `ImGuiIntegration::draw_pane_tab_headers()`:
  - Phase 2 (input): `on_mouse_down()` called on tab click, alongside legacy `pane_tab_drag_` sync for rendering compatibility.
  - Phase 3 (drag update): controller's `update()` drives threshold detection and dock-drag transitions; state synced back to `pane_tab_drag_` for ghost tab rendering.
  - Phase 4 (drop/cancel): controller handles drop-inside (dock system end_drag) and drop-outside (detach callback) via state transitions. Legacy fallback preserved when no controller is set.
  - Cancel: ESC **and right-click** both cancel drag (new: right-click cancel added).
- Ghost tab overlay rendering preserved unchanged — reads from `pane_tab_drag_` which is kept in sync with controller state.
- Drop zone indicators preserved unchanged — reuses `DockSystem::update_drag()`.
- `TabDragController` added to `WindowUIContext` as a member.
- Wired in `App::run()`: dock system, window manager, drop-inside/drop-outside callbacks.
- Forward-declared `TabDragController` in `include/spectra/fwd.hpp`.
- Added `src/ui/tab_drag_controller.cpp` to CMakeLists.txt ImGui-dependent sources.
- Fixed pre-existing build issue: `GLFW_PRESS` macro conflict in `shortcut_manager.hpp` (renamed to `kGlfwPress`).
- Fixed pre-existing build issue: wrong include path for `window_ui_context.hpp` in `vk_backend.cpp`.
- All 69 tests pass. All build targets compile. Zero new warnings.

**Goal:** Implement the drag state machine with ghost tab overlay. Drop always results in dock/split (no new window spawn).

**Changes:**
- New class `TabDragController` (src/ui/tab_drag_controller.hpp) implementing the state machine from Section 4.
- Integrate into `ImGuiIntegration::draw_pane_tab_headers()` — detect drag start, transition states.
- Ghost tab overlay: `ImGui::GetForegroundDrawList()->AddRectFilled()` at cursor position with tab title.
- While dragging: suppress menus/toolbars for dragged content (hide via flag).
- Drop zone indicators on existing panes (reuse `DockSystem::update_drag()`).
- ESC cancels drag.

**Acceptance criteria:**
- Dragging a tab shows ghost overlay following cursor.
- No chrome/buttons/menus visible on ghost overlay.
- Dropping inside window triggers normal dock/split behavior.
- No flicker, no broken tab order.
- Existing dock behavior not regressed.

**Risk:** Low — builds on existing `PaneTabDragState` infrastructure.

### Phase 5: Drop-Outside Spawns New Window

**Goal:** Complete R1–R4 implementation.

**Changes:**
- `TabDragController` detects "outside all windows" on mouse-up.
- Calls `WindowManager::create_window_with_ui()` at cursor screen position.
- Transfers figure from source to new window.
- Source window removes figure from its tab bar and split panes.
- New window renders immediately on next frame.
- Logging: drag start, drag end, window spawn, figure transfer, swapchain creation.

**Acceptance criteria:**
- Exact R1–R4 behaviors met:
  - R1: tab detaches on drag, ghost follows cursor, no chrome during drag.
  - R2: drop outside → new identical window with the figure.
  - R3: drop inside → normal dock/split (no regression).
  - R4: each window independently closable; closing detached window destroys its figures.
- No validation errors on rapid detach/close cycles.

**Risk:** Medium — timing of Vulkan resource creation during drag, first-frame rendering.

### Phase 6: Polish and Hardening

**Goal:** Handle edge cases, optimize, add instrumentation.

**Changes:**
- Edge cases: minimize during drag, alt-tab during drag, DPI-aware positioning, multi-monitor.
- Animations continue during and after detach (timeline, keyframe interpolator).
- Fence wait spike logging: measure `vkWaitForFences()` duration, log if >5ms.
- Swapchain recreation logging per window.
- "Torture test" harness: script that rapidly creates/destroys windows, moves figures, resizes.
- Workspace serialization: save/restore window layout (which figures in which windows).

**Acceptance criteria:**
- Torture test passes 1000 iterations without stalls, leaks, or validation errors.
- Fence wait spikes logged and <16ms in steady state.
- Workspace save/load preserves multi-window state.

**Risk:** Low — incremental polish.

---

## 6. MULTI-AGENT JOB SPLIT

### Agent A: WindowContext + Vulkan Multi-Swapchain (Phase 1–2)

✅ **COMPLETED** — 2026-02-17 (Agent A)

**Scope:** `src/render/vulkan/`, `src/render/renderer.hpp/.cpp`

**Deliverables:**
1. PR: Enhance `WindowContext` with multi-figure fields (`vector<FigureId>`, `active_figure_id`).
2. PR: `VulkanBackend::init_window_context_with_imgui()` — creates full Vulkan + ImGui resources. **Must enforce Section 3F render pass compatibility constraints:** `set_active_window(wctx)` before ImGui init, assert format matches primary, use per-window `ImageCount`, fix latent render pass lifetime bug in `on_swapchain_recreated()`.
3. PR: Per-window render loop helper in `VulkanBackend`.

**What changed:**
- **PR1 (WindowContext fields):** Already completed by Agent C — `assigned_figures`, `active_figure_id`, `is_primary`, `title`, `ui_ctx` (unique_ptr\<WindowUIContext\>), `imgui_context` (void* for per-window ImGui context).
- **PR2 (init_window_context_with_imgui):**
  - `VulkanBackend::init_window_context_with_imgui()` — creates Vulkan resources (surface, swapchain, cmd buffers, sync) then initializes a per-window `ImGuiContext` with `ImGui_ImplGlfw_InitForVulkan` + `ImGui_ImplVulkan_Init`.
  - Section 3F constraint 1: `set_active_window(&wctx)` before ImGui init so `render_pass()` returns the correct per-window render pass.
  - Section 3F constraint 2: Asserts swapchain format matches primary; on mismatch, destroys and recreates swapchain (logs warning).
  - Section 3F constraint 3: Uses per-window `ImageCount` from `wctx.swapchain.images.size()`.
  - Section 3F constraint 5 (latent bug fix): `ImGuiIntegration::on_swapchain_recreated()` now caches the render pass handle (`cached_render_pass_` as `uint64_t`) and re-initializes the ImGui Vulkan backend if the handle changes after swapchain recreation.
  - `destroy_window_context()` now tears down per-window ImGui context (SetCurrentContext → Shutdown → DestroyContext) before Vulkan resource cleanup.
  - `WindowContext` given out-of-line destructor (defined in `vk_backend.cpp`) so `unique_ptr<WindowUIContext>` sees the complete type.
- **PR3 (Per-window render loop helpers):**
  - `VulkanBackend::recreate_swapchain_for_with_imgui()` — recreates swapchain then updates the window's ImGui backend with new image count (Section 3F constraint 4). Falls back to plain `recreate_swapchain_for()` if no ImGui context.
  - `App::render_secondary_window()` updated to use `recreate_swapchain_for_with_imgui()` for ImGui-aware swapchain recreation.

**Files modified:**
- `src/render/vulkan/vk_backend.hpp` — added `init_window_context_with_imgui()`, `recreate_swapchain_for_with_imgui()` declarations
- `src/render/vulkan/vk_backend.cpp` — added ImGui includes, `WindowContext::~WindowContext()` definition, `init_window_context_with_imgui()` impl, `recreate_swapchain_for_with_imgui()` impl, ImGui cleanup in `destroy_window_context()`
- `src/render/vulkan/window_context.hpp` — added `imgui_context` field, out-of-line destructor, copy/move semantics
- `src/ui/imgui_integration.hpp` — added `cached_render_pass_` member (uint64_t)
- `src/ui/imgui_integration.cpp` — fixed `on_swapchain_recreated()` to detect render pass changes and re-init ImGui Vulkan backend; cache render pass in `init()`
- `src/ui/app.cpp` — `render_secondary_window()` uses `recreate_swapchain_for_with_imgui()`
- `tests/unit/test_window_manager.cpp` — added `window_ui_context.hpp` include
- `tests/unit/test_multi_window.cpp` — added `window_ui_context.hpp` include

**Verification:** 69/69 tests pass. All 107 build targets compile. Zero new warnings (ignoring pre-existing clangd "unused include" advisory on `vk_pipeline.hpp`).

**Acceptance tests:**
- Two windows rendering independently.
- Independent resize without validation errors (`VK_LAYER_KHRONOS_validation` clean).
- `vkQueueSubmit` per window uses correct command buffer and sync objects.
- ImGui renders correctly in secondary window after resize (render pass not stale).

**Dependencies:** None (foundational).

### Agent B: WindowManager + Window Lifetime (Phase 2–3)

**Scope:** `src/ui/window_manager.hpp/.cpp`

**Deliverables:**
1. PR: `create_window_with_ui()` — full UI stack creation for secondary windows.
2. PR: Full GLFW input callback installation for secondary windows.
3. PR: `move_figure()` implementation with FigureManager integration.
4. PR: Window close policy (destroy figures on close).

**Acceptance tests:**
- New window from debug command works.
- Input (mouse, keyboard, scroll) works in secondary windows.
- Figure move between windows preserves state.
- Closing secondary window cleans up all resources.

**Dependencies:** Agent A (WindowContext enhancements).

### Agent C: App::run() Decomposition + WindowUIContext (Phase 1)

**Scope:** `src/ui/app.cpp`, new file `src/ui/window_ui_context.hpp`

**Deliverables (two PRs, strict merge order):**
1. **PR1 (merge first):** `WindowUIContext` struct + `create_window_ui()` factory. Moves ~800 lines of subsystem creation/wiring out of `App::run()`. Zero behavior change. See Phase 1 — PR1 for exact scope.
2. **PR2 (merge second, depends on PR1):** `update_window()` + `render_window()` loop helpers. Moves ~400 lines of per-frame logic out of `while(running)`. Zero behavior change. See Phase 1 — PR2 for exact scope.

**Acceptance tests (both PRs):**
- All existing functionality preserved (100% regression test pass).
- After PR1: `App::run()` shrinks by ~600 lines; `WindowUIContext` is a standalone reusable type.
- After PR2: `App::run()` is ~300-400 lines; main loop has a clear `for each window` structure.
- Zero new warnings under `-Wall -Wextra`.

**Dependencies:** None (can start immediately, foundational for all other agents).

**Merge gates:** PR1 unblocks Agents B, D, E for design work. PR2 unblocks Agent B for `create_window_with_ui()` implementation.

### Agent D: Drag State Machine + Ghost Tab Overlay (Phase 4)

**Scope:** New `src/ui/tab_drag_controller.hpp/.cpp`, modifications to `src/ui/imgui_integration.cpp`

**Deliverables:**
1. PR: `TabDragController` class with full state machine.
2. PR: Ghost tab overlay rendering.
3. PR: Integration with `ImGuiIntegration::draw_pane_tab_headers()`.
4. PR: ESC cancel, right-click cancel.

**Acceptance tests:**
- State machine transitions are deterministic (unit test with mock input).
- Ghost overlay follows cursor smoothly.
- No chrome visible during drag.
- Existing dock/split behavior not regressed.

**Dependencies:** Agent C (WindowUIContext for context awareness).

### Agent E: Input/Focus Routing + Shortcuts Per Window (Phase 2–3)

**Scope:** `src/ui/input.hpp/.cpp`, `src/ui/glfw_adapter.hpp/.cpp`, `src/ui/shortcut_manager.hpp/.cpp`

**Deliverables:**
1. PR: Per-window `InputHandler` instantiation.
2. PR: GLFW callback routing that dispatches to correct window's InputHandler.
3. PR: `ShortcutManager` focus gating (dispatch to focused window only).
4. PR: Per-window cursor readout, box zoom, data interaction.

**Acceptance tests:**
- Scrolling/panning in window A does not affect window B.
- Keyboard shortcuts work in whichever window is focused.
- Box zoom, measure tool, select tool work per-window.

**Dependencies:** Agent B (secondary window input callbacks), Agent C (WindowUIContext).

### Agent F: Drop-Outside + Figure Transfer (Phase 5)

**Scope:** `src/ui/tab_drag_controller.cpp`, `src/ui/window_manager.cpp`

**Deliverables:**
1. PR: "Outside all windows" detection using `glfwGetWindowPos/Size`.
2. PR: Window spawn on drop-outside with full UI initialization.
3. PR: Figure transfer from source to new window (FigureManager↔FigureManager).
4. PR: Source window tab bar / split pane cleanup after detach.

**Acceptance tests:**
- Drop outside creates window at correct screen position.
- Figure renders in new window on first frame.
- Source window continues working with remaining figures.
- Rapid detach→close→detach cycle stable (100 iterations).

**Dependencies:** Agent B, Agent D, Agent E (all infrastructure must be in place).

### Agent G: Testing/Validation/Perf + Torture Harness (Phase 6)

**Scope:** `tests/`, new `tools/torture_test.cpp`

**Deliverables:**
1. PR: Automated torture test (create/destroy/move/resize 1000 cycles).
2. PR: Fence wait spike instrumentation + logging.
3. PR: Vulkan validation layer CI integration (ensure zero errors).
4. PR: Workspace serialization for multi-window state.
5. PR: Edge case fixes (minimize, alt-tab, DPI, multi-monitor).

**Acceptance tests:**
- Torture test passes clean (zero validation errors, zero leaks, <16ms fence waits).
- CI runs validation layer checks.
- Workspace round-trip preserves multi-window layout.

**Dependencies:** Agent F (complete feature must be working).

### Dependency Graph

```
Agent C PR1 (factory extract) ──────────────────┐
        │                                        │
        ▼                                        │
Agent C PR2 (loop helpers) ─────────┐            │
                                    │            │
Agent A (Vulkan multi-swapchain) ───┤            │
                                    ▼            ▼
                             Agent B (WindowMgr) ──────┐
                                    │                   │
                                    ▼                   ▼
                             Agent E (Input)      Agent D (Drag SM)
                                    │                   │
                                    └─────┬─────────────┘
                                          ▼
                                   Agent F (Drop-outside)
                                          │
                                          ▼
                                   Agent G (Testing)
```

**Critical path:** C-PR1 → C-PR2 → B → F → G

**Early unblocks:**
- Agents B, D, E can begin **design work** after C-PR1 (they need the `WindowUIContext` type).
- Agent A can work **fully in parallel** with C (no dependency on app.cpp decomposition).
- Agent D can begin the `TabDragController` state machine immediately (no dependency on Vulkan changes).

---

## 7. MODIFICATIONS REPORT

### Files Requiring Modification

| # | File | Change Type | What Changes | Why | Risk |
|---|------|-------------|-------------|-----|------|
| M1 | `src/ui/app.cpp` | **Refactor (2 PRs)** | **PR1:** Extract `create_window_ui()` factory (~800 lines out). **PR2:** Extract `update_window()` + `render_window()` loop helpers (~400 lines out). Both zero-behavior-change. Multi-window iteration in main loop. | S1–S6, S8–S11, S16 | **HIGH** — largest change, but split into 2 safe PRs to reduce merge risk. |
| M2 | `src/render/vulkan/window_context.hpp` | **Extend** | Add `vector<FigureId> assigned_figures`, `FigureId active_figure_id`, `bool is_primary`, `string title`. Remove `assigned_figure_index`. | S7 — single figure per window assumption | **LOW** — additive change to data struct |
| M3 | `src/ui/window_manager.hpp/.cpp` | **Extend** | Add `create_window_with_ui()`, full GLFW input callback installation (mouse, key, scroll), `move_figure()` with FigureManager integration, improved close policy. | S4, S5 — secondary windows need full UI | **MED** — extends existing working code |
| M4 | `src/ui/imgui_integration.hpp/.cpp` | **Modify** | Make per-window instantiable. Remove assumption of single global state. Handle `ImGui::SetCurrentContext()` switching. Factor out font atlas creation to share pixel data. | S1, S2, S3, S6, S11 — single ImGui context | **MED** — 160KB file, many internal dependencies |
| M5 | `src/render/vulkan/vk_backend.hpp/.cpp` | **Extend** | Add `init_window_context_with_imgui()`. Ensure `begin_frame()`/`end_frame()` work with ImGui per-context. **Must implement all 5 constraints from Section 3F** (per-window render pass for ImGui init, format consistency assertion, per-window ImageCount, per-window `on_swapchain_recreated`, render pass lifetime fix). | S1 — ImGui render data per window | **MED** — Vulkan correctness critical; Section 3F is binding |
| M6 | `src/ui/figure_manager.hpp/.cpp` | **Modify** | Support multiple instances (one per window). Transfer figures between instances. Decouple from single TabBar assumption. | S8 — one FigureManager assumption | **LOW** — clean existing design |
| M7 | `src/ui/dock_system.hpp/.cpp` | **Minor** | Support multiple instances (one per window). Already stateless w.r.t. window. | S9 — one DockSystem assumption | **LOW** — already self-contained |
| M8 | `src/ui/split_view.hpp/.cpp` | **None** | Already per-instance, owned by DockSystem. No changes needed. | — | **NONE** |
| M9 | `src/ui/input.hpp/.cpp` | **Modify** | Support multiple instances (one per window). Decouple from global callback captures. | S10 — single InputHandler | **LOW** — already parameterized |
| M10 | `src/ui/tab_bar.hpp/.cpp` | **Minor** | Already per-instance. Add drag-to-detach state enhancement for cross-window awareness. Improve `end_drag()` screen coordinate detection to check all windows. | Existing code at lines 564-593 only checks primary window bounds | **LOW** |
| M11 | `src/ui/glfw_adapter.hpp/.cpp` | **Minor** | Add `screen_position()` method. Possibly add static helper for cursor-to-screen conversion. | Needed for outside-window detection | **LOW** |
| M12 | `src/ui/layout_manager.hpp/.cpp` | **None** | Already per-instance, owned by ImGuiIntegration. No changes needed. | — | **NONE** |
| M13 | `src/ui/workspace.hpp/.cpp` | **Extend** | Add per-window figure assignment to `WorkspaceData`. Serialize/deserialize multi-window state. | S18 — no window state in workspace | **LOW** |
| M14 | `src/ui/shortcut_manager.hpp/.cpp` | **Minor** | Add focus gating — dispatch only to focused window's context. | S12 — global shortcuts | **LOW** |
| M15 | `src/ui/theme.hpp/.cpp` | **None** | Global singleton, already shared. Apply to each ImGui context at creation. | — | **NONE** |
| M16 | `include/spectra/fwd.hpp` | **Minor** | Forward-declare `WindowUIContext`, `TabDragController`. | New types | **LOW** |
| M17 | `include/spectra/app.hpp` | **Minor** | No public API change needed. Internal only. | — | **LOW** |

### New Files

| # | File | Purpose |
|---|------|---------|
| N1 | `src/ui/window_ui_context.hpp` | Struct bundling all per-window UI subsystems |
| N2 | `src/ui/tab_drag_controller.hpp/.cpp` | Drag state machine for tab tear-off |
| N3 | `tests/multi_window_test.cpp` | Multi-window integration tests |
| N4 | `tools/torture_test.cpp` | Stress test for window create/destroy/resize cycles |

### Observability Instrumentation

| Event | Log Tag | Log Level | Data |
|-------|---------|-----------|------|
| Drag start | `tab_drag` | INFO | figure_id, source_window_id, mouse(x,y) |
| Drag end (drop inside) | `tab_drag` | INFO | figure_id, target_window_id, drop_zone |
| Drag end (drop outside) | `tab_drag` | INFO | figure_id, screen(x,y) |
| Drag cancel | `tab_drag` | DEBUG | figure_id, reason (ESC/focus_loss/right_click) |
| Window spawn | `window_manager` | INFO | window_id, size, position, figure_count |
| Window close | `window_manager` | INFO | window_id, figures_destroyed |
| Figure ownership transfer | `window_manager` | INFO | figure_id, from_window, to_window |
| Swapchain recreation | `vulkan` | INFO | window_id, old_extent, new_extent |
| Fence wait spike (>5ms) | `vulkan_perf` | WARN | window_id, wait_duration_ms, fence_index |
| ImGui context create/destroy | `imgui` | INFO | window_id |

---

## SUMMARY

The codebase is **well-positioned** for this feature. Key advantages:

1. **`WindowManager` + `WindowContext` already exist** with multi-window Vulkan support (surface, swapchain, command buffers, sync objects, resize handling).
2. **`FigureRegistry`** provides stable IDs with thread-safe ownership — no pointer invalidation on move.
3. **`detach_figure()`** and **`move_figure()`** already exist in `WindowManager`.
4. **Tab drag detection** already exists in `TabBar::end_drag()` (lines 564-593) with outside-window detection.
5. **Per-pane tab drag** already exists in `ImGuiIntegration::PaneTabDragState`.

The main gap is that secondary windows today get **raw figure rendering only** — no ImGui, no input, no tabs. The core work is:

1. **Decompose `App::run()`** from a 2553-line monolith into per-window helpers (biggest risk).
2. **Per-window ImGui context** initialization and frame cycling.
3. **Per-window input routing** with full GLFW callbacks.
4. **Drag state machine** with ghost overlay and outside-detection.

Estimated total effort: **~4-6 weeks** with the 7-agent split, or **~8-10 weeks** single-developer.
