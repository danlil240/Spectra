# Headless bag analysis (CI / reports)

Spectra ships `spectra-ros-analyze` for extracting numeric ROS 2 bag fields to CSV
without launching the GUI — suitable for GitHub Actions and offline log regression.

## Build

```bash
cmake -B build -DSPECTRA_USE_ROS2=ON -DSPECTRA_ROS2_BAG=ON
cmake --build build --target spectra-ros-analyze
```

## Usage

```bash
spectra-ros-analyze --bag PATH \
  --fields /imu/data.linear_acceleration.z \
  --fields /cmd_vel:linear.x \
  --csv out.csv
```

Field spec formats:

- `/topic:field.path` — explicit separator
- `/topic.field.path` — topic resolved against bag metadata (longest prefix match)

Output columns: `timestamp_sec`, `timestamp_nsec`, `bag_time_sec`, then one column per field.

## Example: threshold check in CI

```bash
source /opt/ros/humble/setup.bash
python3 scripts/generate_ros_reference_bags.py

./build/spectra-ros-analyze \
  --bag tests/data/ros_bags/imu_snippet \
  --fields /imu/data.linear_acceleration.z \
  --csv /tmp/imu_z.csv

# Header + 60 rows @ 10 Hz for 6 s
test "$(wc -l < /tmp/imu_z.csv)" -eq 61

python3 - <<'PY'
import csv, sys
with open("/tmp/imu_z.csv") as f:
    rows = list(csv.DictReader(f))
z = [float(r["/imu/data/linear_acceleration.z"]) for r in rows]
if max(z) > 10.0:
    sys.exit("IMU z spike")
PY
```

## GUI workflow

For interactive scrub + echo + plots, use the bag post-mortem preset:

```bash
./build/spectra-ros \
  --bag tests/data/ros_bags/nav_snippet \
  --session sessions/presets/bag_review.spectra-ros-session
```

Or:

```bash
ros2 launch spectra bag_replay.launch.py bag:=/path/to/recording.db3
```

See also [`examples/ros2_launch/bag_replay.launch.py`](../../examples/ros2_launch/bag_replay.launch.py).
