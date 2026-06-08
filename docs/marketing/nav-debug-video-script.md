# Nav debug launch video script (~3 minutes)

**Product:** Spectra ROS `nav2_debug` preset  
**Audience:** Navigation engineers evaluating bag post-mortem tools  
**Capture:** 1920×1080, dark theme, `nav2_debug` bag + session

## 0:00 — Hook (15 s)

> "What was the robot doing when cmd_vel spiked? In most tools you scrub a plot in
> one app and guess at the pose in another. Spectra syncs plots, echo, laser, path,
> and TF on one bag timeline."

**Visual:** Split screen — PlotJuggler + RViz (static) → cut to Spectra rviz-plot layout.

## 0:15 — Open bag (30 s)

```bash
./build/spectra-ros \
  --bag tests/data/ros_bags/nav2_debug \
  --session sessions/presets/nav2_debug.spectra-ros-session \
  --layout rviz-plot
```

**Visual:** Transport bar appears; plots populate; displays panel shows grid, scan, path, pose.

**VO:** "One command loads the nav debug session — plots and 3D displays, no manual setup."

## 0:45 — Scrub to event (45 s)

**Visual:** Pause playback. Drag playhead to ~75%. Laser scan and path update. TF tree shows
`map → odom → base_link`. Subplot 1 cmd_vel and subplot 3 tracking error visible.

**VO:** "Scrub the bag once. Echo, plots, and 3D views share the playhead — not wall clock."

## 1:30 — Correlate signals (30 s)

**Visual:** Zoom subplot 1. Step forward on transport. Inspector open on right.

**VO:** "Compare commanded vs measured velocity and the tracking-error expression on the same clock."

## 2:00 — Pick robot link (30 s)

**Visual:** Click a robot collision shape in the viewport. Inspector shows link name, joint,
joint position, parent frame.

**VO:** "Click a link — inspector shows joint state and frame without leaving the session."

## 2:30 — Export & CI (30 s)

```bash
./build/spectra-ros-analyze \
  --bag tests/data/ros_bags/nav2_debug \
  --fields /cmd_vel.linear.x,/odom.twist.twist.linear.x \
  --csv /tmp/nav.csv
```

**VO:** "Export CSV for reports or gate regressions in CI. Workshop script: thirty minutes,
link in docs."

## 2:55 — Close (5 s)

**Visual:** Spectra logo + `spectra-ros` CLI + link to GitHub.

**VO:** "Spectra ROS — spatiotemporal debug in one native session."

## Asset checklist

| Asset | Path / command |
|-------|----------------|
| Reference bag | `tests/data/ros_bags/nav2_debug` |
| Session preset | `sessions/presets/nav2_debug.spectra-ros-session` |
| Workshop doc | `docs/workshops/nav-debug-30min.md` |
| QA scenario | `spectra_ros_qa_agent --scenario nav_spatiotemporal` |
| B-roll terminal | `docs/marketing/nav-debug-video-script.md` (this file) |
