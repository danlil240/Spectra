# Decisions — settings (config isolation)

- One helper, `spectra::user_config_dir()` in `src/core/config_paths.{hpp,cpp}` (spectra-core, so the
  PX4/ROS2 adapters and non-ImGui builds can use it). Resolution: `SPECTRA_CONFIG_DIR` (non-empty, used
  as the Spectra dir itself) -> `XDG_CONFIG_HOME/spectra` (absolute, non-Windows) -> `HOME/.config/spectra`
  -> `USERPROFILE/.config/spectra`. Empty result when nothing is known; each caller keeps its previous
  relative fallback (`./.config/spectra/...`, `workspace.spectra`, `plugins`, `keybindings.json`,
  `/tmp/.config/spectra` for ROS2).
- The `USERPROFILE` fallback now applies to every file (settings.json and autosave previously fell back
  to `.`, the others already used USERPROFILE). On Windows with HOME unset this moves settings.json and
  autosave.spectra from `./.config/spectra/` to `%USERPROFILE%\.config\spectra\`, which is where
  keybindings/workspace/plugins already lived; with HOME set nothing changes.
- Files moved under the helper: settings.json, keybindings.json, workspace.spectra, autosave.spectra
  (WorkspaceAutosave), plugins/, default_theme.json, PX4 recent files (non-ImGui builds no longer duplicate
  the path), ROS2 ros_recent.json / ros_favorites.json / sessions/.
- `default_theme.json` moves from `~/.spectra/` to the config dir. Backward compatibility: when the config
  dir is not redirected and the new file is absent, `load_default()` still reads the legacy
  `~/.spectra/default_theme.json`; saves always write the new location (implicit migration). With
  `SPECTRA_CONFIG_DIR`/`XDG_CONFIG_HOME` set the legacy file is never read (full isolation).
- Not changed: `Workspace::autosave_path()` stays in the temp directory (it is a temp file, isolated via
  `TMPDIR`, and tests assert it); log files keep following `XDG_STATE_HOME` (already separated); native file
  dialogs keep starting in `$HOME` and the PX4 "~" hint keeps shortening `$HOME` (user data, not config).
  imgui.ini persistence is disabled everywhere (`IniFilename = nullptr`), so there is no ini path to move.
  No XDG_DATA_HOME/XDG_CACHE_HOME split: no Spectra file is data/cache-like today.
- `qa_design_review` (`tests/qa/run_design_review.sh`) no longer overrides `HOME`; it exports private
  `SPECTRA_CONFIG_DIR`, `XDG_CONFIG_HOME`, `XDG_STATE_HOME` and `XDG_RUNTIME_DIR`. Unit tests that overrode
  `HOME` to redirect config files (`test_ui_shell_fixes`, `test_ros_session` RecentFixture) now set
  `SPECTRA_CONFIG_DIR` instead.
