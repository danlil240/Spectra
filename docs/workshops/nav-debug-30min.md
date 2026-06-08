# Nav debug workshop (30 minutes)

Hands-on walkthrough: open a Nav2-style bag, scrub the timeline, and correlate
cmd_vel spikes with laser, path, TF, and odometry on one clock.

**Prerequisites:** Spectra built with `SPECTRA_USE_ROS2=ON` and bag support,
reference bags generated, and a display (X11 or Xvfb for headless CI).

## 0. Setup (5 min)

```bash
source /opt/ros/humble/setup.bash
cmake --build build -j$(nproc)
python3 scripts/generate_ros_reference_bags.py   # if tests/data/ros_bags/nav2_debug/ is missing
```

Optional lavapipe / Xvfb for software rendering — see `BUILD_ENVIRONMENT.md`.

## 1. Open the nav2_debug preset (5 min)

```bash
./build/spectra-ros \
  --bag tests/data/ros_bags/nav2_debug \
  --session sessions/presets/nav2_debug.spectra-ros-session \
  --layout rviz-plot
```

Or via launch file:

```bash
ros2 launch spectra nav2_debug.launch.py
```

You should see:

- **Left:** topic list + displays panel (grid, laser scan, path, pose, robot model)
- **Center:** 3D viewport + plot grid (cmd_vel, odom, tracking error)
- **Right:** TF tree + inspector
- **Bottom:** unified bag transport bar

## 2. Scrub the playhead (10 min)

1. Pause playback if it started automatically.
2. Drag the transport slider to **25%**, then **75%** of the bag.
3. Watch the **laser scan** and **path** update with the playhead (not live wall clock).
4. Open **TF tree** — `map` → `odom` → `base_link` should stay consistent.
5. Select **Topic echo**, pick `/cmd_vel` — message content should match the scrub time.
6. On subplot 3, note the **linear tracking error** expression (`cmd − actual`).

**Pass check:** At ~3.75 s into the 5 s bag, the robot pose in 3D and odom plots
reflect the same moment (no multi-second drift between panels).

## 3. Compare command vs odometry (5 min)

1. Scrub to a point where cmd_vel `linear.x` rises (visible on subplot 1).
2. Confirm odom `linear.x` on subplot 2 lags slightly (synthetic bag models tracking error).
3. Use **step forward** on the transport bar one message at a time.

## 4. Export and analyze (5 min)

Headless CSV for a field:

```bash
./build/spectra-ros-analyze \
  --bag tests/data/ros_bags/nav2_debug \
  --fields /cmd_vel.linear.x,/odom.twist.twist.linear.x \
  --csv /tmp/nav_compare.csv
```

## 5. Automated regression

```bash
xvfb-run -a ./build/tests/spectra_ros_qa_agent \
  --scenario nav_spatiotemporal \
  --seed 42 \
  --output-dir /tmp/ros_qa_nav
```

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| Empty 3D view | Check fixed frame is `map` (session default); TF panel shows frames |
| Plots move but laser frozen | Rebuild with bag support; confirm `nav2_debug` bag includes `/scan` |
| Robot model empty | Expected in bag-only mode without `robot_description` param — pose display shows odom |
| Bag won't open | Regenerate bags with `scripts/generate_ros_reference_bags.py` |

## Next steps

- Record your own Nav2 bag and **Import Session (merge)** extra plots from `bag_review`.
- Live stack: `examples/ros2_launch/nav_dashboard.launch.py` with the same display types.
