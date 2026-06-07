# Spectra ROS reference bags (Phase B8)

Small synthetic bags for CI, `spectra-ros-analyze`, and bag-replay tutorials.

| Bag | Topics | Duration | Rate |
|-----|--------|----------|------|
| `cmd_vel_snippet/` | `/cmd_vel` | 3 s | 10 Hz |
| `imu_snippet/` | `/imu/data` | 6 s | 10 Hz |
| `nav_snippet/` | `/cmd_vel`, `/odom` | 4 s | 5 Hz |
| `nav2_debug/` | `/tf`, `/tf_static`, `/scan`, `/plan`, `/odom`, `/cmd_vel` | 5 s | 5 Hz |

Workshop: [`docs/workshops/nav-debug-30min.md`](../../docs/workshops/nav-debug-30min.md)

## Regenerate

```bash
source /opt/ros/humble/setup.bash
python3 scripts/generate_ros_reference_bags.py
```

## Verify headless export

```bash
./build/spectra-ros-analyze \
  --bag tests/data/ros_bags/imu_snippet \
  --fields /imu/data.linear_acceleration.z \
  --csv /tmp/imu_z.csv
wc -l /tmp/imu_z.csv
```
