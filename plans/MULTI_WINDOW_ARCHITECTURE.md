# Spectra Multi-Window Architecture Plan

**Author:** Principal Graphics Architect  
**Date:** 2026-02-16  
**Status:** Planning — NO IMPLEMENTATION

---

## 1. CURRENT STATE ANALYSIS

### 1.1 Architecture Snapshot

```
┌─────────────────────────────────────────────────────────────────┐
│                          App::run()                             │
│  ┌──────────┐  ┌──────────────┐  ┌───────────────────────────┐ │
│  │ GlfwAdapter│  │ VulkanBackend│  │     Renderer              │ │
│  │ (1 window) │  │ (1 swapchain)│  │ (1 backend ref)           │ │
│  │ window_    │  │ surface_     │  │ backend_                  │ │
│  │ callbacks_ │  │ swapchain_   │  │ pipelines (shared)        │ │
│  └─────┬──────┘  │ offscreen_   │  │ series_gpu_data_ (shared) │ │
│        │         │ cmd_buffers_ │  │ axes_gpu_data_ (shared)   │ │
│        │         │ fences_      │  │ frame_ubo_buffer_         │ │
│        │         │ semaphores_  │  └───────────────────────────┘ │
│        │         └──────┬───────┘                                │
│        │                │                                        │
│  ┌─────┴────────────────┴──────────────────────────────────────┐ │
│  │                  Single Frame Loop                          │ │
│  │  poll_events → new_frame → build_ui → begin_frame →         │ │
│  │  begin_render_pass → render_figure_content → render(imgui)→ │ │
│  │  end_render_pass → end_frame                                │ │
│  └─────────────────────────────────────────────────────────────┘ │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ figures_: vector<unique_ptr<Figure>>                       │  │
│  │   └── Figure owns Axes owns Series (data + GPU buffers)   │  │
│  │                                                            │  │
│  │ FigureManager  → TabBar  (dead, not rendered)             │  │
│  │ DockSystem     → SplitViewManager → SplitPane tree        │  │
│  │ ImGuiIntegration → LayoutManager → draw_pane_tab_headers  │  │
│  │ InputHandler   → single active_figure                     │  │
│  └────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

### 1.2 Component Ownership Details

| Component | Owner | Singleton? | Notes |
|-----------|-------|------------|-------|
| `GLFWwindow*` | `GlfwAdapter::window_` | **Yes** | Calls `glfwTerminate()` on shutdown |
| `VkInstance` | `VulkanBackend::ctx_.instance` | **Yes** | One instance per process |
| `VkDevice` | `VulkanBackend::ctx_.device` | **Yes** | Single logical device |
| `VkSurfaceKHR` | `VulkanBackend::surface_` | **Yes** | One surface for one window |
| `SwapchainContext` | `VulkanBackend::swapchain_` | **Yes** | Images, views, framebuffers, depth, MSAA |
| `VkCommandPool` | `VulkanBackend::command_pool_` | **Yes** | One pool for all commands |
| `VkCommandBuffer[]` | `VulkanBackend::command_buffers_` | **Yes** | Per-swapchain-image |
| `VkFence[]` | `VulkanBackend::in_flight_fences_` | **Yes** | `MAX_FRAMES_IN_FLIGHT = 2` |
| `VkSemaphore[]` | `VulkanBackend` | **Yes** | Per-swapchain-image acquire + render |
| `VkDescriptorPool` | `VulkanBackend::descriptor_pool_` | **Yes** | 256-set pool shared |
| `VkPipeline` map | `VulkanBackend::pipelines_` | **Yes** | All pipeline types in one map |
| `VkRenderPass` | `SwapchainContext::render_pass` | **Yes** | One render pass for swapchain format |
| `Renderer` | `App::renderer_` | **Yes** | Holds `backend_` reference, all GPU data maps |
| `Figure[]` | `App::figures_` | **Yes** | Owned by App, indexed by position |
| `ImGuiIntegration` | `App::run()` local | **Yes** | One ImGui context |
| `DockSystem` | `App::run()` local | **Yes** | One split view tree |
| `InputHandler` | `App::run()` local | **Yes** | Routes to one active figure |

### 1.3 Current Window Lifecycle

1. `GlfwAdapter::init()` → `glfwInit()` + `glfwCreateWindow()` (single window)
2. `VulkanBackend::init()` → creates `VkInstance`, `VkDevice`, queues
3. `VulkanBackend::create_surface(native_window)` → `glfwCreateWindowSurface()`
4. `VulkanBackend::create_swapchain(w, h)` → swapchain + depth + MSAA + render pass + framebuffers
5. Main loop runs until `glfw->should_close()`
6. `GlfwAdapter::shutdown()` → `glfwDestroyWindow()` + `glfwTerminate()`

**Critical:** `glfwTerminate()` is called in `GlfwAdapter::shutdown()`, which destroys the GLFW context entirely. Multi-window requires separating `glfwTerminate()` from individual window destruction.

### 1.4 Current Swapchain Ownership

All swapchain resources live inside `VulkanBackend` as flat members:
- `surface_` — single `VkSurfaceKHR`
- `swapchain_` — single `vk::SwapchainContext` (swapchain + images + views + framebuffers + depth + MSAA)
- `command_buffers_` — indexed by swapchain image
- `image_available_semaphores_` — indexed by swapchain image
- `render_finished_semaphores_` — indexed by swapchain image
- `in_flight_fences_` — indexed by flight frame (0..1)
- `current_image_index_` — single active image
- `current_cmd_` — single active command buffer

**No indirection.** Every Vulkan call goes through one swapchain. There is no concept of "which surface are we targeting."

### 1.5 Current Frame Loop (simplified)

```
while (running) {
    scheduler.begin_frame()
    animator.evaluate()
    user_on_frame()
    
    imgui_ui->new_frame()
    imgui_ui->build_ui(*active_figure)
    
    // Resize debounce
    if (needs_resize) backend->recreate_swapchain()
    
    // Layout
    dock_system.update_layout(canvas)
    compute_subplot_layout(...)
    
    // Render
    if (backend->begin_frame()) {          // acquires swapchain image, waits fence
        renderer->flush_pending_deletions()
        renderer->begin_render_pass()      // begins command buffer recording
        renderer->render_figure_content()  // records draw commands
        imgui_ui->render()                 // records ImGui draw commands
        renderer->end_render_pass()        // ends command buffer recording
        backend->end_frame()               // submits command buffer, presents
    }
    
    glfw->poll_events()
}
```

### 1.6 How ImGui is Integrated

- **One ImGui context** created in `ImGuiIntegration::init(VulkanBackend&, GLFWwindow*)`
- Uses `ImGui_ImplGlfw_InitForVulkan()` with the single GLFW window
- Uses `ImGui_ImplVulkan_Init()` with the single render pass
- `new_frame()` → `ImGui_ImplVulkan_NewFrame()` + `ImGui_ImplGlfw_NewFrame()` + `ImGui::NewFrame()`
- `render()` → `ImGui::Render()` + `ImGui_ImplVulkan_RenderDrawData()`
- `on_swapchain_recreated()` handles new render pass after resize
- **ImGui Docking** is enabled but **multi-viewport is NOT** (no `ImGuiConfigFlags_ViewportsEnable`)
- All ImGui windows (`##commandbar`, `##canvas`, `##inspector`, etc.) draw inside the single OS window

### 1.7 How Figures are Owned

- `App::figures_` — `vector<unique_ptr<Figure>>` — sequential ownership
- Figures are indexed by position (0, 1, 2, ...)
- `FigureManager` manages creation, closing, switching — references `figures_` by index
- Dock system panes store `vector<size_t> figure_indices_` — raw indices into `figures_`
- **Figure has no stable ID.** Closing figure N shifts all indices > N
- Series GPU buffers are keyed by `const Series*` pointer in `Renderer::series_gpu_data_`

### 1.8 How Tabs are Structured

Two overlapping systems exist:
1. **TabBar** (dead code) — `src/ui/tab_bar.hpp/.cpp` — created and wired but never rendered. Main loop sets `tab_bar_visible = false` every frame.
2. **Pane Tab Headers** (active) — rendered by `ImGuiIntegration::draw_pane_tab_headers()` using `GetForegroundDrawList()`. Draws directly from `DockSystem::SplitPane::figure_indices_`. Supports cross-pane drag, close button. Recently added: right-click context menu.

### 1.9 Where Resize is Handled

Two resize paths:
1. **In-callback resize** (`callbacks.on_resize` lambda in `App::run()`) — immediately recreates swapchain + renders a frame inside the GLFW callback. Prevents black flash during drag.
2. **Debounced resize** — `needs_resize` flag + timestamp in main loop. After 50ms of no new resize events, recreates swapchain in the main loop.

Both paths call:
- `backend_->recreate_swapchain(w, h)`
- `imgui_ui->on_swapchain_recreated(*vk)`
- Update `active_figure->config_.width/height`

### 1.10 Single-Window Assumptions (Blockers for Multi-Window)

| # | Assumption | Location | Severity |
|---|-----------|----------|----------|
| **S1** | One `GLFWwindow*` + `glfwTerminate()` on shutdown | `glfw_adapter.cpp:65` | **Critical** |
| **S2** | One `VkSurfaceKHR` as flat member | `vk_backend.hpp:108` | **Critical** |
| **S3** | One `SwapchainContext` as flat member | `vk_backend.hpp:109` | **Critical** |
| **S4** | Command buffers indexed by swapchain image count | `vk_backend.hpp:116` | **Critical** |
| **S5** | Semaphores indexed by swapchain image count | `vk_backend.hpp:121-122` | **Critical** |
| **S6** | `begin_frame()` acquires from THE swapchain | `vk_backend.cpp` | **Critical** |
| **S7** | `end_frame()` presents to THE swapchain | `vk_backend.cpp` | **Critical** |
| **S8** | One render pass (from swapchain format) | `vk_backend.hpp: render_pass()` | **High** |
| **S9** | `Renderer::begin_render_pass()` uses backend's single pass | `renderer.cpp` | **High** |
| **S10** | ImGui initialized with single window + render pass | `imgui_integration.cpp` | **High** |
| **S11** | `App::run()` loop: one `active_figure` pointer | `app.cpp` | **Medium** |
| **S12** | `InputHandler` routes to one active figure | `input.hpp` | **Medium** |
| **S13** | Figures indexed by position, no stable ID | `app.hpp:38` | **High** |
| **S14** | `DockSystem` is a single tree for one window | `dock_system.hpp` | **Medium** |
| **S15** | `LayoutManager` computes layout for one window size | `layout_manager.hpp` | **Medium** |
| **S16** | `glfwPollEvents()` is global, not per-window | `glfw_adapter.cpp:71` | **Low** |

### 1.11 Risk Areas

- **Vulkan resource sharing:** Pipelines, descriptor sets, and the descriptor pool are shareable across windows (same `VkDevice`). But framebuffers, swapchains, and command buffers must be per-window.
- **ImGui context:** ImGui supports multi-viewport but the Vulkan backend integration is complex. Alternative: one ImGui context per window (simpler but duplicated state).
- **Resize during drag:** The in-callback resize path assumes a single swapchain. Multi-window must scope resize to the resized window only.
- **GPU buffer ownership:** `Renderer::series_gpu_data_` maps `Series*` → GPU buffers. These buffers are usable from any window's command buffer (same device). No change needed for shared buffers. But `frame_ubo_buffer_` must be per-window (different viewport dimensions).

---

## 2. TARGET ARCHITECTURE

### 2.1 Core Principles

1. **No global window state.** Every window-specific resource lives in a `WindowContext`.
2. **Shared device, shared pipelines.** `VkInstance`, `VkDevice`, `VkPipeline` map, `VkDescriptorPool` remain singletons — they are device-level, not surface-level.
3. **Per-window isolation.** Each OS window owns: surface, swapchain, framebuffers, depth/MSAA, command buffers, sync objects, frame UBO, ImGui viewport.
4. **Figures are logical.** Figures have stable IDs. A figure can be displayed in any window. Moving a figure between windows does not invalidate GPU buffers.
5. **Single render thread.** The main thread iterates over all windows per frame. No multi-threading for rendering.
6. **Resize isolated per window.** Resizing window A does not affect window B's swapchain.

### 2.2 Target Structures

#### WindowContext (new)

```
struct WindowContext {
    // Identity
    uint32_t id;                          // Stable window ID
    
    // GLFW
    GLFWwindow* glfw_window;
    
    // Vulkan surface + swapchain
    VkSurfaceKHR surface;
    vk::SwapchainContext swapchain;
    
    // Per-window command buffers (indexed by swapchain image)
    std::vector<VkCommandBuffer> command_buffers;
    VkCommandBuffer current_cmd;
    uint32_t current_image_index;
    
    // Per-window sync objects
    std::vector<VkSemaphore> image_available_semaphores;
    std::vector<VkSemaphore> render_finished_semaphores;
    std::vector<VkFence> in_flight_fences;
    uint32_t current_flight_frame;
    
    // Per-window frame UBO (different viewport size per window)
    BufferHandle frame_ubo_buffer;
    
    // Per-window UI state
    std::unique_ptr<ImGuiIntegration> imgui;
    std::unique_ptr<LayoutManager> layout;
    std::unique_ptr<DockSystem> dock_system;
    std::unique_ptr<InputHandler> input_handler;
    
    // Figures displayed in this window (by stable ID)
    std::vector<FigureId> figure_ids;
    FigureId active_figure_id;
    
    // Resize state
    bool needs_resize;
    uint32_t pending_width, pending_height;
    std::chrono::steady_clock::time_point resize_time;
    
    // Window state
    bool should_close;
    bool is_focused;
};
```

#### WindowManager (new)

```
class WindowManager {
    // Creates a new OS window with its own swapchain
    WindowContext* create_window(uint32_t w, uint32_t h, const std::string& title);
    
    // Destroys a window and its resources (waits for GPU idle on that window)
    void destroy_window(uint32_t window_id);
    
    // Moves a figure from one window to another
    void move_figure(FigureId fig, uint32_t from_window, uint32_t to_window);
    
    // Detach: creates a new window and moves a figure into it
    WindowContext* detach_figure(FigureId fig, uint32_t source_window, int screen_x, int screen_y);
    
    // Iteration for render loop
    std::vector<WindowContext*>& windows();
    
    // Input routing
    WindowContext* focused_window();
    
    // Lifecycle
    bool any_window_open() const;
    void poll_events();  // single glfwPollEvents() for all windows
    void close_empty_windows();
    
private:
    std::vector<std::unique_ptr<WindowContext>> windows_;
    uint32_t next_window_id_ = 1;
    
    // Shared Vulkan state (NOT per-window)
    VkDevice device_;          // from VulkanBackend
    VkCommandPool command_pool_; // shared command pool
};
```

#### FigureId (new)

```
using FigureId = uint64_t;

class FigureRegistry {
    FigureId register_figure(std::unique_ptr<Figure> fig);
    void unregister_figure(FigureId id);
    Figure* get(FigureId id);
    
    // Stable iteration
    std::vector<FigureId> all_ids() const;
    
private:
    std::unordered_map<FigureId, std::unique_ptr<Figure>> figures_;
    FigureId next_id_ = 1;
};
```

### 2.3 Target Render Loop

```
while (window_manager.any_window_open()) {
    scheduler.begin_frame()
    animator.evaluate()
    user_on_frame()
    
    window_manager.poll_events()    // single glfwPollEvents()
    window_manager.close_empty_windows()
    
    for (auto* wctx : window_manager.windows()) {
        if (wctx->should_close) continue;
        
        // Handle resize for THIS window
        if (wctx->needs_resize) {
            recreate_window_swapchain(wctx);
        }
        
        // ImGui frame for THIS window
        wctx->imgui->new_frame();
        wctx->imgui->build_ui(wctx->active_figure());
        
        // Layout for THIS window
        wctx->dock_system->update_layout(wctx->layout->canvas_rect());
        compute_layouts_for_window(wctx);
        
        // Render for THIS window
        if (begin_frame(wctx)) {
            renderer->set_target(wctx);            // sets command buffer + render pass
            renderer->begin_render_pass(wctx);
            render_window_content(wctx);            // render all figures in this window
            wctx->imgui->render(wctx);
            renderer->end_render_pass();
            end_frame(wctx);                        // submit + present for this window
        }
    }
    
    scheduler.end_frame()
}
```

### 2.4 What is Shared vs Per-Window

| Resource | Scope | Reason |
|----------|-------|--------|
| `VkInstance` | Global | One per process |
| `VkDevice` | Global | One logical device |
| `VkPhysicalDevice` | Global | One GPU |
| `VkQueue` | Global | One graphics queue (can submit for any surface) |
| `VkCommandPool` | Global | Pool can allocate buffers used with any framebuffer |
| `VkDescriptorPool` | Global | Descriptors are device-level |
| `VkDescriptorSetLayout` | Global | Same for all windows |
| `VkPipelineLayout` | Global | Same for all windows |
| `VkPipeline` map | Global | Pipelines created with compatible render pass |
| `Renderer` | Global | Holds pipeline handles + shared GPU data maps |
| `FigureRegistry` | Global | Figures exist independently of windows |
| Series GPU buffers | Global | Same SSBO usable from any command buffer |
| --- | --- | --- |
| `VkSurfaceKHR` | **Per-window** | Tied to OS window handle |
| `VkSwapchainKHR` | **Per-window** | Tied to surface |
| `VkRenderPass` | **Per-window** | May differ if surface formats differ (unlikely but possible) |
| `VkFramebuffer[]` | **Per-window** | Tied to swapchain images |
| Depth/MSAA images | **Per-window** | Tied to swapchain extent |
| `VkCommandBuffer[]` | **Per-window** | Records into window's framebuffer |
| `VkSemaphore[]` | **Per-window** | Synchronize window's acquire/present |
| `VkFence[]` | **Per-window** | Track window's in-flight frames |
| Frame UBO buffer | **Per-window** | Different viewport dimensions |
| `ImGuiIntegration` | **Per-window** | Each window has its own UI state |
| `LayoutManager` | **Per-window** | Different window sizes |
| `DockSystem` | **Per-window** | Independent split trees |
| `InputHandler` | **Per-window** | Input routed to focused window |

### 2.5 Pipeline Compatibility Note

`VkPipeline` objects are created with a specific `VkRenderPass`. If all windows use the same surface format (very likely: `B8G8R8A8_SRGB`), one set of pipelines works for all. If formats differ, pipelines must be per-render-pass. **Strategy:** Assert all windows get the same format. If not, create a second pipeline set (rare edge case).

---

## 3. EXECUTION PHASES

### Phase 1: Refactor for Multi-Window Readiness (No UI Changes)

**Goal:** Extract `WindowContext` from `VulkanBackend`. No new windows yet.

**Tasks:**
1. Create `WindowContext` struct holding surface, swapchain, command buffers, sync objects, frame UBO
2. Move these members out of `VulkanBackend` into a single `WindowContext` instance
3. Add `set_active_window(WindowContext*)` to `VulkanBackend` — all frame operations target this context
4. `begin_frame()`/`end_frame()` operate on the active `WindowContext`
5. Refactor `GlfwAdapter` — separate `glfwTerminate()` from individual window destruction
6. Introduce `FigureId` (initially `FigureId = size_t index` — migration shim)

**Acceptance Criteria:**
- Single window still works identically
- Resize still stable (both paths)
- All 65/66 tests pass (pre-existing inspector_stats failure unchanged)
- No Vulkan validation errors

### Phase 2: Multi-Window Rendering

**Goal:** Create and render into 2+ windows simultaneously.

**Tasks:**
1. Implement `WindowManager::create_window()` — creates GLFW window + surface + swapchain + sync
2. Implement per-window `begin_frame()`/`end_frame()` via `WindowContext`
3. Main loop iterates over all windows: acquire → record → submit → present
4. Per-window resize handling (scoped to the resized window)
5. Shared command pool allocates separate command buffers per window

**Acceptance Criteria:**
- Two windows render different content simultaneously
- Resizing one window does not affect the other
- No GPU hangs, no validation errors
- Closing one window continues rendering the other
- `glfwPollEvents()` called once per frame (not per window)

### Phase 3: Figure Ownership Decoupling

**Goal:** Figures have stable IDs and can exist in any window.

**Tasks:**
1. Implement `FigureRegistry` with stable `FigureId` (monotonic counter)
2. Replace `vector<unique_ptr<Figure>> figures_` with `FigureRegistry`
3. Replace all `size_t figure_index` with `FigureId` in DockSystem, SplitPane, FigureManager
4. Implement `WindowManager::move_figure(id, from, to)`
5. Update `Renderer::series_gpu_data_` — keyed by `Series*` (unchanged, pointers remain stable)

**Acceptance Criteria:**
- Move figure between windows programmatically — renders correctly in target
- Figure's GPU buffers are not recreated on move
- No dangling pointers after move
- Closing a window with figures → figures destroyed (unless moved first)

### Phase 4: Tab Tear-Off UX

**Goal:** Drag a tab outside the window to spawn a new native window.

**Tasks:**
1. Detect drag-outside-window in `draw_pane_tab_headers()` Phase 4 (already partially implemented)
2. `WindowManager::detach_figure()` — create GLFW window at screen position, move figure into it
3. Handle edge cases: last figure in source window, cross-monitor DPI
4. Smooth transition: ghost tab → new window appears → figure renders

**Acceptance Criteria:**
- Drag tab outside window → new native window spawns at cursor position
- Figure renders immediately in new window
- Source window continues with remaining figures
- No crash, no GPU stall, no Vulkan errors
- Re-docking (drag back) is out of scope for Phase 4

### Phase 5: ImGui Multi-Viewport (Optional Enhancement)

**Goal:** Use ImGui's built-in multi-viewport for popup windows, tooltips crossing window boundaries.

**Tasks:**
1. Enable `ImGuiConfigFlags_ViewportsEnable`
2. Implement Vulkan platform backend for ImGui viewports (surface/swapchain per viewport)
3. Test popup windows (context menus, tooltips) that extend beyond window bounds

**Acceptance Criteria:**
- ImGui popups can extend outside OS window boundaries
- No rendering artifacts on viewport windows
- This phase is **optional** — can be skipped if Phase 4 works well enough

---

## 4. MULTI-AGENT SPLIT PLAN

### Agent A — Core Window Refactor

**Scope:** Extract `WindowContext`, refactor `VulkanBackend`, decouple GLFW lifecycle.

**Files allowed to modify:**
- `src/render/vulkan/vk_backend.hpp`
- `src/render/vulkan/vk_backend.cpp`
- `src/render/vulkan/vk_swapchain.hpp`
- `src/render/vulkan/vk_swapchain.cpp`
- `src/ui/glfw_adapter.hpp`
- `src/ui/glfw_adapter.cpp`
- `src/render/renderer.hpp`
- `src/render/renderer.cpp`

**Files allowed to create:**
- `src/render/vulkan/window_context.hpp`

**Files CANNOT touch:**
- `src/ui/imgui_integration.*`
- `src/ui/app.cpp` (except minimal wiring of new WindowContext)
- `src/ui/dock_system.*`
- `include/spectra/*`

**Acceptance Criteria:**
- Clean compile, all tests pass
- Single window behavior unchanged
- `WindowContext` struct exists and is populated from `VulkanBackend`
- `VulkanBackend::set_active_window(WindowContext*)` works
- `GlfwAdapter` no longer calls `glfwTerminate()` on individual window destruction

**Testing:**
- Run `multi_figure_demo` — identical behavior
- Run all unit tests — 65/66 pass
- Vulkan validation layers clean

---

### Agent B — Multi-Swapchain Implementation

**Scope:** Multiple GLFW windows rendering independently.

**Files allowed to modify:**
- `src/render/vulkan/vk_backend.hpp`
- `src/render/vulkan/vk_backend.cpp`
- `src/render/vulkan/window_context.hpp` (from Agent A)
- `src/ui/glfw_adapter.hpp`
- `src/ui/glfw_adapter.cpp`
- `src/ui/app.cpp` (render loop refactor)

**Files allowed to create:**
- `src/ui/window_manager.hpp`
- `src/ui/window_manager.cpp`

**Files CANNOT touch:**
- `include/spectra/app.hpp` (public API)
- `src/ui/dock_system.*`
- `src/ui/imgui_integration.*` (except per-window init)
- Test files (Agent E handles testing)

**Acceptance Criteria:**
- `WindowManager::create_window()` creates a second OS window
- Both windows render frames independently
- Resizing window A does not cause validation errors on window B
- Closing one window does not crash the other
- Main loop: single `glfwPollEvents()`, then iterate windows

**Testing:**
- Hardcoded test: open 2 windows, render different solid colors
- Resize each independently — no GPU hang
- Close one — other continues rendering
- Validation layers clean throughout

---

### Agent C — Figure Ownership System

**Scope:** Stable figure IDs, decouple figures from window.

**Files allowed to modify:**
- `include/spectra/app.hpp`
- `src/ui/app.cpp`
- `src/ui/figure_manager.hpp`
- `src/ui/figure_manager.cpp`
- `src/ui/dock_system.hpp`
- `src/ui/dock_system.cpp`
- `src/ui/split_view.hpp`
- `src/ui/split_view.cpp`
- `src/ui/window_manager.hpp` (from Agent B)
- `src/ui/window_manager.cpp`

**Files allowed to create:**
- `src/ui/figure_registry.hpp`
- `src/ui/figure_registry.cpp`

**Files CANNOT touch:**
- `src/render/*` (renderer already works with `Figure&`)
- `src/ui/imgui_integration.*`
- Shader files

**Acceptance Criteria:**
- `FigureId` is a stable 64-bit identifier
- `FigureRegistry` owns all figures
- `DockSystem`/`SplitPane` use `FigureId` instead of `size_t`
- `move_figure(id, window_a, window_b)` works programmatically
- Figure's GPU buffers survive move (not recreated)

**Testing:**
- Create 3 figures, move figure 2 to window 2 via code — renders correctly
- Close source window — target window unaffected
- Verify series GPU data survives the move (`series_gpu_data_` unchanged)

---

### Agent D — Tear-Off Interaction

**Scope:** Mouse-driven tab detach UX.

**Files allowed to modify:**
- `src/ui/imgui_integration.hpp`
- `src/ui/imgui_integration.cpp` (draw_pane_tab_headers Phase 4 + detach callback)
- `src/ui/app.cpp` (wire detach callback to WindowManager)
- `src/ui/window_manager.hpp`
- `src/ui/window_manager.cpp`

**Files CANNOT touch:**
- `src/render/*`
- `src/ui/glfw_adapter.*`
- `src/ui/figure_registry.*`
- Shader files

**Acceptance Criteria:**
- Drag tab outside window → new OS window spawns at cursor
- Figure renders in new window within 1 frame
- Source window tab removed, remaining figures unaffected
- Cannot detach last figure (window must have ≥1)
- No GPU stalls during detach

**Testing:**
- Manual test: run `multi_figure_demo`, drag tab outside → new window
- Stress test: rapidly detach and re-close 5 windows
- Resize new window immediately after detach — stable

---

### Agent E — Stability & Validation

**Scope:** Comprehensive testing, validation, edge cases.

**Files allowed to modify:**
- `tests/*` (all test files)
- `examples/multi_window_demo.cpp` (new)

**Files allowed to create:**
- `tests/unit/test_multi_window.cpp`
- `tests/unit/test_figure_registry.cpp`
- `tests/bench/bench_multi_window.cpp`

**Files CANNOT touch:**
- Any `src/` files

**Acceptance Criteria:**
- Vulkan validation layers produce zero errors across all scenarios
- No GPU hang under any test scenario
- Animation callbacks fire correctly for figures in any window
- Resize torture test: 100 rapid resizes on 3 windows simultaneously
- Window close order test: close windows in every permutation

**Testing Scenarios:**
1. Single window (regression)
2. Two windows, static content
3. Two windows, animated figures
4. Detach tab, resize new window
5. Close source window after detach
6. Close target window (figure destroyed)
7. Rapid resize during detach
8. Minimized window (zero-size swapchain)
9. All windows minimized simultaneously
10. DPI change (monitor switch)

---

## 5. RISK REGISTER

| # | Risk | Probability | Impact | Detection | Mitigation |
|---|------|------------|--------|-----------|------------|
| **R1** | Destroying swapchain resources while in-flight GPU work references them | High | Critical (crash/device lost) | Vulkan validation layer `VUID-vkDestroySwapchainKHR` | Per-window `vkDeviceWaitIdle()` or per-window fence wait before swapchain destroy. Use `vkQueueWaitIdle()` scoped to the window's last submit. |
| **R2** | Fence deadlock: waiting on a fence that was never submitted | Medium | Critical (hang) | Application freeze, no GPU activity | Initialize fences as `VK_FENCE_CREATE_SIGNALED_BIT`. Track submission state per-window: only wait if a submit occurred. |
| **R3** | ImGui context confusion: wrong window's draw data rendered into wrong framebuffer | Medium | High (visual corruption) | Wrong UI in wrong window | One `ImGuiContext` per window (cleanest). OR single context with viewport support. Decision: start with one context per window. |
| **R4** | Focus/input routing: keystrokes go to wrong window | Medium | Medium (UX bug) | Key events affect wrong figure | GLFW callbacks include `GLFWwindow*` — route via `glfwGetWindowUserPointer()` to the correct `WindowContext`. Each `WindowContext` has its own `InputHandler`. |
| **R5** | DPI mismatch between monitors | Low | Medium (UI scaling wrong) | Mismatched text/element sizes | Query `glfwGetWindowContentScale()` per window. Pass scale to ImGui and LayoutManager per-window. |
| **R6** | Shared descriptor pool exhaustion across windows | Medium | High (allocation failure) | `vkAllocateDescriptorSets` returns `VK_ERROR_OUT_OF_POOL_MEMORY` | Increase pool size proportionally with window count. Or use per-window descriptor pools. |
| **R7** | Shared pipeline render pass incompatibility | Low | High (validation error) | Different swapchain formats across monitors | Assert format matches. If different, create duplicate pipeline set for new format. |
| **R8** | Animation running on moved figure: callback references stale window state | Medium | Medium (crash or no-op) | Crash on animation tick after move | Animation callbacks reference `Figure&` directly (not window state). Verify callbacks don't capture window-local references. |
| **R9** | `glfwTerminate()` called while windows still exist | High | Critical (crash) | Crash on second window event | Move `glfwTerminate()` to `WindowManager` destructor, after all windows destroyed. Remove from `GlfwAdapter::shutdown()`. |
| **R10** | Command buffer recorded for wrong framebuffer | Medium | Critical (validation error + corruption) | Wrong image in `vkCmdBeginRenderPass` | `Renderer::begin_render_pass()` must read framebuffer from the **active** `WindowContext`. Never cache framebuffer index across windows. |

---

## 6. TWO-WEEK EXECUTION BREAKDOWN

### Week 1: Window Context Extraction (Agent A)

| Day | Goal | Files | Test | Checkpoint |
|-----|------|-------|------|------------|
| **D1** | Create `WindowContext` struct | `window_context.hpp` | Compiles | Struct committed |
| **D2** | Move surface + swapchain into `WindowContext` | `vk_backend.hpp/cpp` | Single window renders | Backend refactored |
| **D3** | Move command buffers + sync objects into `WindowContext` | `vk_backend.hpp/cpp` | `begin_frame`/`end_frame` work | Sync objects per-context |
| **D4** | Add `set_active_window()` + move frame UBO | `vk_backend.hpp/cpp`, `renderer.hpp/cpp` | Render still works | Renderer targets context |
| **D5** | Refactor `GlfwAdapter`: separate init/shutdown/terminate | `glfw_adapter.hpp/cpp` | Window lifecycle clean | GLFW lifecycle fixed |
| **D6** | Introduce `FigureId` type alias (= `size_t` initially) | `figure_manager.hpp`, `split_view.hpp` | All tests pass | ID type ready |
| **D7** | Integration testing + validation | All Phase 1 files | Full test suite + validation layers | **Phase 1 MERGE** |

### Week 2: Multi-Window Rendering (Agent B)

| Day | Goal | Files | Test | Checkpoint |
|-----|------|-------|------|------------|
| **D8** | Create `WindowManager` class skeleton | `window_manager.hpp/cpp` | Compiles | Manager exists |
| **D9** | Implement `create_window()`: GLFW + surface + swapchain | `window_manager.cpp`, `vk_backend.cpp` | Second window appears | Window creation works |
| **D10** | Per-window `begin_frame`/`end_frame` in render loop | `app.cpp`, `vk_backend.cpp` | Both windows render solid color | Multi-present works |
| **D11** | Per-window ImGui context (one ImGuiIntegration per window) | `imgui_integration.hpp/cpp`, `app.cpp` | Both windows show UI | Multi-ImGui works |
| **D12** | Per-window resize handling | `app.cpp`, `window_manager.cpp` | Resize window A, B unaffected | Resize isolation |
| **D13** | Per-window input routing (GLFW callbacks → WindowContext) | `glfw_adapter.cpp`, `window_manager.cpp` | Mouse/keyboard per window | Input routing correct |
| **D14** | Validation + stress test + merge | All Phase 2 files | Validation clean, resize torture | **Phase 2 MERGE** |

### Beyond Week 2 (Weeks 3-4)

| Week | Phase | Agent | Goal |
|------|-------|-------|------|
| **W3** | Phase 3 | Agent C | Figure registry + move between windows |
| **W3** | Phase 5 | Agent E | Multi-window test suite |
| **W4** | Phase 4 | Agent D | Tab tear-off UX |
| **W4** | Phase 5 | Agent E | Stability validation + benchmarks |

---

## 7. DEFINITION OF DONE

The multi-window system is complete when ALL of the following are true:

### Functional
- [ ] Multiple OS windows can be created and destroyed at runtime
- [ ] Each window has its own swapchain, depth buffer, MSAA, framebuffers
- [ ] Each window has independent UI (command bar, inspector, tab headers)
- [ ] Figures have stable IDs that survive window transitions
- [ ] A figure can be moved from window A to window B (programmatically + via tab drag)
- [ ] Dragging a tab outside a window spawns a new OS window with that figure
- [ ] Closing a window destroys its figures (unless moved first)
- [ ] The last window closing exits the application
- [ ] Animations continue correctly for figures in any window

### Stability
- [ ] Zero Vulkan validation errors under all test scenarios
- [ ] No GPU hang (device lost) under any test scenario
- [ ] Resize is stable per-window (independent debouncing)
- [ ] Minimized windows (zero-size) handled gracefully (skip render, no crash)
- [ ] Rapid window create/destroy (10 in 1 second) does not crash

### Performance
- [ ] Frame time for N windows scales linearly (no quadratic overhead)
- [ ] Shared GPU buffers (series data) are not duplicated across windows
- [ ] Pipeline objects are shared across all windows
- [ ] Descriptor pool sized for worst-case window count

### Regression
- [ ] All existing 66 unit tests pass
- [ ] All golden image tests pass
- [ ] All benchmarks show no regression (±5%)
- [ ] Single-window mode is the default and works identically to pre-refactor

---

*End of plan. No code was written or modified.*
