# Decisions — adapters (S4-A01)

- ROS2 middleware / rosbag / GPU viewport coverage is skipped: there is no ROS2 runtime in this
  environment (`SPECTRA_USE_ROS2=OFF`). Nothing in `src/adapters/ros2/` was changed.
- There was no real PX4 log in the repo, so a small deterministic ULog fixture is committed at
  `tests/data/ulog/px4_fixture.ulg` together with its generator `make_px4_fixture.py`. It is a
  valid ULog v1 file (info, 8 float/int32 parameters with value ties, 3 topics × 10 samples, log
  messages). The PX4 sorting and drop tests parse it with the real `ULogReader`.
- PX4 "click-to-sort" is tested on the only sortable table in the viewer, the Parameters table.
  The test clicks the real headers of `ULogFilePanel` with synthesized Dear ImGui mouse events, with
  the panel docked into a dockspace as spectra-px4 does. The topic tree is not click-sortable; the
  test checks its fixed order instead (topics alphabetical, fields in format order).
- Fixed: the Parameters order cache keyed on (vector pointer, size, path) went stale when the same
  path was re-opened after the file changed, because `clear()` keeps the buffer. `ULogReader` now
  has a `generation()` counter, and the panel uses that as the key.
- spectra-px4 file drop: `Px4AppShell::set_window_manager()` now installs the WindowManager file
  drop handler. This replaces the main app's CSV-only handler, which ignored `.ulg`. `.ulg` and
  `.ulog` (any case) are queued and opened on the next `poll()`, outside the event callback. When
  several logs are dropped together, the last one wins (only one log can be open). A non-ULog drop
  is rejected; the message is shown in the status bar and the open log stays.
- Main app CSV drop: a drop of several files now queues them, and each opens in turn once the
  previous column picker closes. Before, only the last file was kept. A file that fails to parse now
  opens the picker with its error and a Close button. Before, the drop or open failed silently.
- The "Plot" button body moved to `ImGuiIntegration::plot_csv_selection()`, so tests and
  automation can commit the picker without synthesizing clicks in the real window.
- GLFW drop is tested end to end in a real windowed App under xvfb + lavapipe (label `gpu`). The
  test fetches the callback GLFW actually holds for the window (`glfwSetDropCallback`
  swap/restore) and calls it as GLFW does. GLFW cannot synthesize an OS drop itself.
- SDL3 is mutually exclusive with GLFW. Its test (`test_window_file_drop_sdl3`, pushes a real
  `SDL_EVENT_DROP_FILE`) is only built in an SDL3 configuration. It was run here in a separate SDL3
  build (see the final report). The default CI build does not build it.
- Plugin lifecycle is tested with a new tiny plug-in, `mock_lifecycle_plugin`, using a fake backend
  and the real Renderer/registries/PluginManager. The test needs no GPU, so it runs in the Windows
  CI job (`ctest -LE gpu`) as a real `.dll`. Counters are host-owned (bound through an exported
  `mock_lifecycle_bind`), so they can be read after unload.
- Fixed: a custom (plug-in) series drawn before its plug-in was loaded was marked clean and never
  uploaded or drawn afterwards. An example is a workspace restored ahead of its plug-ins. The upload
  now stays pending until a type of that name is registered, the same way it already stayed pending
  while the plug-in was disabled.
- Python IPC: the new `python/tests/test_ipc_roundtrip.py` starts its own `spectra-backend`
  (`SPECTRA_BACKEND_PATH` or `build/spectra-backend`) and checks the decoded `REQ_GET_SNAPSHOT`
  values. It found two defects, both fixed:
  - `Axes.remove_series(i)` sent `i` as a figure-wide index, despite its "from this axes" docstring.
    On a second subplot it removed a series from the first. It now maps the axes position to the
    figure-wide index and raises `IndexError` when the position is out of range.
  - Values sent from Python were cast to float32, so epoch-second timestamps collapsed to a
    resolution of about 128 s. `Series.set_data()` and `append()` now send float32 offsets from a
    double origin. The origin is chosen with the same rules as the C++ `choose_data_origin` and
    `choose_stream_origin`. It travels as a new `"data_origin"` property (f1/f2, which are doubles
    in the schema), handled by the daemon's new `FigureModel::set_series_origin()`. The daemon
    broadcasts it in the existing `SET_SERIES_DATA` op, whose origins the window agent already
    applies. No wire schema changed. `apply_property_update()` now takes its f1–f4 as double, so
    the origins are not narrowed. `test_phase5`'s source-inspection test now looks at
    `Series._send_set_data`, where the chunking path moved.
