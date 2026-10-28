# Decisions — soak (S4-M01)

- `--repeat N` already existed (endpoint-difference heuristic, INFO/WARNING only). It now samples RSS and the
  in-use heap after every iteration, fits a least-squares slope (MB/iteration) over the iterations after
  `--repeat-warmup` (default 1) and fails (ERROR, exit 1) when it exceeds `--max-rss-slope` (default 0.5).
  Fewer than 3 measured iterations are reported as NOT_JUDGED, so a gated run needs `--repeat 4` or more.
- In repeat mode every iteration ends with `reset_to_single_window_lightweight_state()` before sampling.
  Most scenarios leave their figures open; without the reset, figure accumulation read as ~1 MB/iteration
  "growth" (stress_docking) that is not a product leak. Single runs (`--repeat 1`) are unchanged.
- The in-use heap (glibc `mallinfo2`, uordblks + hblkhd) is sampled next to RSS because RSS after
  `malloc_trim` still moves by 0.3-0.8 MB/iteration from fragmentation and driver mappings (series_clipboard
  measured 0.16-0.81 MB/iter in repeated runs with a flat heap). The heap gate is opt-in
  (`--max-heap-slope`, off by default) and uses a Theil-Sen slope, since single transient +1.5 MB heap
  samples would move a least-squares fit past a tight threshold.
- ctest `qa_soak` (labels gpu;qa): stress_docking, series_mixing, figure_serialization, 12 iterations each,
  2 warm-up iterations, RSS gate 0.5 MB/iter and heap gate 0.05 MB/iter (~25 s under lavapipe). Two
  warm-up iterations because RSS settles over the first 2-3 iterations (figure_serialization: 186 -> 188 MB,
  then flat for 30 iterations); 12 iterations because RSS moves in ~1 MB steps, and one step at the end of
  only 6 measured samples already reads as +0.3-0.4 MB/iter. stress_docking is the regression scenario for
  the ImGui window leak fixed here (heap slope +0.093..0.196 -> +0.006..0.011 MB/iter).
- ImGui never frees a window, so per-object window names leak. Pane tab header windows are now named by
  header slot (they are input-less and placed every frame, no per-pane state) and the interactive legend
  uses a pooled `##legend_<slot>` name per (figure, axes), released when the figure is closed
  (`DataInteraction::clear_figure_cache` -> `LegendInteraction::forget_figure`, which also drops that
  figure's drag offset / Best-corner / overflow state). A reused slot resets the window scroll.
  `LegendInteraction::window_name()` replaces the old name-format assumption in tests and the design-review
  ghost-legend check.
- Pane tab animation state (`pane_tab_anims_`) is pruned to the (pane, figure) keys drawn this frame.
- Demo animation channels (inproc App start-up figure) captured raw `Series*`; closing that figure and
  pressing play was a heap-use-after-free (found by the soak: animation_stress crashed at iteration 2-6;
  ASan confirmed). Callbacks now re-resolve the series through the FigureRegistry
  (`make_series_anim_callback`) and are no-ops once the figure or series is gone, instead of adding
  unbind hooks to every close/remove path.
- Second third-party plugin for the concurrent test: `mock_leaky_plugin.cpp` is built a second time
  (`mock_leaky_plugin_b`, `MOCK_LEAKY_TAG="B"`, transform +10) so both plugins register every extension kind
  with non-colliding names. A worker thread applies retained transform copies concurrently with
  enable/disable/unload/reload (supported: copies are revoked under a lock); custom-series use is serialised
  with lifecycle steps like the render thread, since unregistering a series type is a render-thread operation.
- RSS slopes are meaningless under ASan (quarantine keeps freed memory, mallinfo2 reports 0); the ASan soak
  run was used only to look for memory errors, not to judge the gate.
