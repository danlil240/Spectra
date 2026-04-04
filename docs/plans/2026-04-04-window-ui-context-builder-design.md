# WindowUIContext Builder Design

> **Date:** 2026-04-04
> **Scope:** First architecture slice for unifying `WindowUIContext` startup across windowed and headless paths

## Goal

Introduce a shared `WindowUIContext` builder so the full app window path and the headless bootstrap path construct the same core UI bundle in the same order.

## Problem

`WindowUIContext` is currently assembled in two different places:

- [src/ui/window/window_lifecycle.cpp](/home/daniel/projects/Spectra/src/ui/window/window_lifecycle.cpp) builds the full per-window stack for real OS windows.
- [src/ui/app/app_step.cpp](/home/daniel/projects/Spectra/src/ui/app/app_step.cpp) builds a thinner headless context and then replays additional command, shortcut, timeline, and plugin wiring.

That split makes startup behavior drift over time and forces new UI dependencies to be wired in multiple places.

## First Slice

This slice introduces a shared builder for the safe, reusable assembly work:

- Create `FigureManager` and prune it to the requested initial figure.
- Create the common per-context state used in both startup modes.
- Wire shared command, shortcut, command-palette, timeline, interpolator, plugin, and figure-close behavior.
- Register standard commands from one shared path.

The builder will not own window-specific responsibilities in this slice:

- Real ImGui backend initialization against a GLFW/Vulkan window
- GLFW callback installation
- Preview/panel window special cases
- Window teardown changes

Those remain in `WindowManager` for now.

## Proposed Shape

Add a builder module under `src/ui/app/` with:

- a dependency bundle for shared services (`FigureRegistry`, theme manager, plugin services, clipboard, optional window manager/session hooks)
- a build request describing the initial figure and whether an ImGui integration object should be created
- a function that returns a fully wired `std::unique_ptr<WindowUIContext>`

Windowed startup will use the builder, then perform ImGui initialization and the remaining window-only wiring.
Headless startup will use the same builder with ImGui creation disabled.

## Safety Constraints

- Headless startup must not create an `ImGuiIntegration` object unless it is also fully initialized with a valid ImGui context.
- Existing command behavior must stay intact, including active-figure pointer semantics and session bindings.
- Panel and preview windows stay on their lighter custom path during this slice.

## Testing

Add a focused unit test for the shared builder that verifies:

- the requested figure is retained in `FigureManager`
- shared services are attached to the resulting `WindowUIContext`
- standard commands and default shortcuts are registered
- the headless-safe configuration leaves `imgui_ui` unset

This gives us a stable regression test before the runtime paths are switched over.
