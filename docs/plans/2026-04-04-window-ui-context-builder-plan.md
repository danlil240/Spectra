# WindowUIContext Builder Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Introduce a shared `WindowUIContext` builder and move both windowed and headless startup onto it without changing teardown behavior.

**Architecture:** Extract the common `WindowUIContext` assembly logic into a builder module under `src/ui/app/`. Keep GLFW/ImGui window initialization inside `WindowManager`, but move shared figure-manager, command, shortcut, plugin, and timeline setup into the builder so both startup paths reuse one construction flow.

**Tech Stack:** C++20, GoogleTest, GLFW/Vulkan-backed UI runtime, Dear ImGui integration, existing `CommandBindings` command registration

---

### Task 1: Add a failing builder test

**Files:**
- Create: `tests/unit/test_window_ui_context_builder.cpp`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write the failing test**

Add a unit test that constructs a shared-services bundle, builds a headless-safe `WindowUIContext`, and asserts:

- `fig_mgr` exists
- only the requested initial figure remains in `fig_mgr`
- `plugin_manager` and `overlay_registry` are wired
- `cmd_registry.find("view.reset")` is non-null
- `shortcut_mgr.count()` is non-zero
- `imgui_ui` is null in the headless-safe configuration

**Step 2: Run test to verify it fails**

Run: `cmake --build build --target unit_test_window_ui_context_builder`

Expected: FAIL because the builder module and test target wiring do not exist yet.

**Step 3: Commit**

```bash
git add tests/CMakeLists.txt tests/unit/test_window_ui_context_builder.cpp
git commit -m "test: add window ui context builder coverage"
```

### Task 2: Implement the shared builder

**Files:**
- Create: `src/ui/app/window_ui_context_builder.hpp`
- Create: `src/ui/app/window_ui_context_builder.cpp`
- Modify: `CMakeLists.txt`
- Modify: `src/ui/app/window_ui_context.hpp`

**Step 1: Write the minimal implementation**

Add:

- a dependency/request struct for building a `WindowUIContext`
- a shared builder function that creates the common context members
- headless-safe handling so `ImGuiIntegration` is only created when requested
- common command registration and shared-service wiring

Keep window-only responsibilities out of this module.

**Step 2: Run the new test**

Run: `cmake --build build --target unit_test_window_ui_context_builder`

Expected: PASS

**Step 3: Commit**

```bash
git add CMakeLists.txt src/ui/app/window_ui_context.hpp src/ui/app/window_ui_context_builder.hpp src/ui/app/window_ui_context_builder.cpp
git commit -m "refactor: add shared window ui context builder"
```

### Task 3: Move startup paths onto the builder

**Files:**
- Modify: `src/ui/window/window_lifecycle.cpp`
- Modify: `src/ui/app/app_step.cpp`
- Modify: `tests/unit/test_window_manager.cpp`

**Step 1: Update windowed startup**

Refactor `WindowManager::init_window_ui(...)` to call the builder for shared assembly first, then keep its existing window-only ImGui initialization and callback wiring.

**Step 2: Update headless startup**

Replace the manual `WindowUIContext` construction in `App::init_runtime()` with a builder call, keeping the runtime-specific active-figure/session bindings intact.

**Step 3: Add/adjust regression coverage**

Extend the existing window-manager tests if needed so the adopted headless window path still has a usable `WindowUIContext` after the refactor.

**Step 4: Run targeted tests**

Run:

```bash
cmake --build build --target unit_test_window_ui_context_builder unit_test_window_manager
ctest --test-dir build --output-on-failure -R "unit_test_window_ui_context_builder|unit_test_window_manager"
```

Expected: PASS

**Step 5: Commit**

```bash
git add src/ui/window/window_lifecycle.cpp src/ui/app/app_step.cpp tests/unit/test_window_manager.cpp
git commit -m "refactor: share window ui context startup assembly"
```
