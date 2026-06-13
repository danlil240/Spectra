#!/usr/bin/env python3
"""Generate small synthetic rosbags for Spectra ROS CI (Phase B8).

Writes SQLite bags under tests/data/ros_bags/:
  - cmd_vel_snippet/   geometry_msgs/Twist @ 10 Hz, 3 s
  - imu_snippet/       sensor_msgs/Imu linear_acceleration.z @ 10 Hz, 6 s
  - nav_snippet/       cmd_vel + nav_msgs/Odometry @ 5 Hz, 4 s
  - nav2_debug/        TF, scan, path, odom, cmd_vel @ 5 Hz, 5 s (Phase C3)

Requires: sourced ROS 2 (Humble/Jazzy) and rosbag2_py.

Usage:
    source /opt/ros/humble/setup.bash
    python3 scripts/generate_ros_reference_bags.py
"""

from __future__ import annotations

import math
import sys
from pathlib import Path

try:
    import rclpy
    from builtin_interfaces.msg import Time
    from geometry_msgs.msg import PoseStamped, TransformStamped, Twist
    from nav_msgs.msg import Odometry
    from nav_msgs.msg import Path as NavPath
    from rclpy.serialization import serialize_message
    from sensor_msgs.msg import Imu, LaserScan
    from tf2_msgs.msg import TFMessage
    from rosbag2_py import SequentialWriter, StorageOptions, TopicMetadata
except ImportError as exc:  # pragma: no cover
    print(f"ROS 2 Python packages required: {exc}", file=sys.stderr)
    sys.exit(1)

ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "tests" / "data" / "ros_bags"


def make_topic_metadata(topic_id: int, name: str, typ: str) -> TopicMetadata:
    """TopicMetadata gained a required id field in ROS 2 Jazzy."""
    try:
        return TopicMetadata(topic_id, name, typ, "cdr")
    except TypeError:
        return TopicMetadata(name=name, type=typ, serialization_format="cdr")


def write_bag(uri: Path, topics: list[tuple[str, str]], messages: list[tuple[str, int, bytes]]) -> None:
    if uri.exists():
        import shutil

        shutil.rmtree(uri)

    writer = SequentialWriter()
    writer.open(
        StorageOptions(uri=str(uri), storage_id="sqlite3"),
        rosbag2_py_converter_options(),
    )

    for topic_id, (name, typ) in enumerate(topics):
        meta = make_topic_metadata(topic_id, name, typ)
        writer.create_topic(meta)

    for topic, stamp_ns, payload in messages:
        writer.write(topic, payload, stamp_ns)


def rosbag2_py_converter_options():
    from rosbag2_py import ConverterOptions

    return ConverterOptions(
        input_serialization_format="cdr",
        output_serialization_format="cdr",
    )


def gen_cmd_vel_snippet() -> None:
    uri = OUT_DIR / "cmd_vel_snippet"
    start_ns = 1_700_000_000_000_000_000
    hz = 10.0
    duration_s = 3.0
    msgs: list[tuple[str, int, bytes]] = []
    n = int(duration_s * hz)
    for i in range(n):
        msg = Twist()
        t = i / hz
        msg.linear.x = 0.5 * math.sin(t)
        msg.linear.y = 0.25 * math.cos(t)
        stamp = start_ns + int(i * (1e9 / hz))
        msgs.append(("/cmd_vel", stamp, serialize_message(msg)))
    write_bag(uri, [("/cmd_vel", "geometry_msgs/msg/Twist")], msgs)
    print(f"wrote {uri} ({n} messages)")


def gen_imu_snippet() -> None:
    uri = OUT_DIR / "imu_snippet"
    start_ns = 1_700_000_000_000_000_000
    hz = 10.0
    duration_s = 6.0
    msgs: list[tuple[str, int, bytes]] = []
    n = int(duration_s * hz)
    for i in range(n):
        msg = Imu()
        t = i / hz
        msg.linear_acceleration.z = 9.81 + 0.1 * math.sin(t * 2.0)
        stamp = start_ns + int(i * (1e9 / hz))
        msgs.append(("/imu/data", stamp, serialize_message(msg)))
    write_bag(uri, [("/imu/data", "sensor_msgs/msg/Imu")], msgs)
    print(f"wrote {uri} ({n} messages)")


def make_time(stamp_ns: int) -> Time:
    msg = Time()
    msg.sec = stamp_ns // 1_000_000_000
    msg.nanosec = stamp_ns % 1_000_000_000
    return msg


def make_transform(parent: str, child: str, stamp_ns: int, x: float, y: float, yaw: float) -> TransformStamped:
    tf = TransformStamped()
    tf.header.stamp = make_time(stamp_ns)
    tf.header.frame_id = parent
    tf.child_frame_id = child
    tf.transform.translation.x = x
    tf.transform.translation.y = y
    tf.transform.rotation.z = math.sin(yaw * 0.5)
    tf.transform.rotation.w = math.cos(yaw * 0.5)
    return tf


def gen_nav2_debug() -> None:
    uri = OUT_DIR / "nav2_debug"
    start_ns = 1_700_000_000_000_000_000
    hz = 5.0
    duration_s = 5.0
    msgs: list[tuple[str, int, bytes]] = []
    n = int(duration_s * hz)

    static_tf = TFMessage()
    static_tf.transforms.append(make_transform("map", "odom", start_ns, 0.0, 0.0, 0.0))
    msgs.append(("/tf_static", start_ns, serialize_message(static_tf)))

    path = NavPath()
    path.header.frame_id = "map"

    for i in range(n):
        t = i / hz
        stamp = start_ns + int(i * (1e9 / hz))
        x = 1.5 * math.cos(t)
        y = 1.5 * math.sin(t)
        yaw = t + math.pi * 0.5

        twist = Twist()
        twist.linear.x = 0.4 + 0.1 * math.sin(t)
        twist.angular.z = 0.2 * math.cos(t)
        msgs.append(("/cmd_vel", stamp, serialize_message(twist)))

        odom = Odometry()
        odom.header.stamp = make_time(stamp)
        odom.header.frame_id = "odom"
        odom.child_frame_id = "base_link"
        odom.pose.pose.position.x = x
        odom.pose.pose.position.y = y
        odom.pose.pose.orientation.z = math.sin(yaw * 0.5)
        odom.pose.pose.orientation.w = math.cos(yaw * 0.5)
        odom.twist.twist.linear.x = twist.linear.x - 0.03
        odom.twist.twist.angular.z = twist.angular.z
        msgs.append(("/odom", stamp, serialize_message(odom)))

        dynamic_tf = TFMessage()
        dynamic_tf.transforms.append(make_transform("odom", "base_link", stamp, x, y, yaw))
        msgs.append(("/tf", stamp, serialize_message(dynamic_tf)))

        scan = LaserScan()
        scan.header.stamp = make_time(stamp)
        scan.header.frame_id = "base_link"
        scan.angle_min = -math.pi
        scan.angle_max = math.pi
        scan.angle_increment = math.pi / 36.0
        beam_count = 73
        scan.ranges = [
            2.0 + 0.4 * math.sin(t + idx * 0.08) for idx in range(beam_count)
        ]
        scan.range_min = 0.1
        scan.range_max = 10.0
        scan.scan_time = 1.0 / hz
        scan.time_increment = scan.scan_time / beam_count
        msgs.append(("/scan", stamp, serialize_message(scan)))

        pose = PoseStamped()
        pose.header.stamp = make_time(stamp)
        pose.header.frame_id = "map"
        pose.pose.position.x = x
        pose.pose.position.y = y
        pose.pose.orientation.z = math.sin(yaw * 0.5)
        pose.pose.orientation.w = math.cos(yaw * 0.5)
        path.poses.append(pose)
        path.header.stamp = make_time(stamp)
        msgs.append(("/plan", stamp, serialize_message(path)))

    write_bag(
        uri,
        [
            ("/tf_static", "tf2_msgs/msg/TFMessage"),
            ("/tf", "tf2_msgs/msg/TFMessage"),
            ("/scan", "sensor_msgs/msg/LaserScan"),
            ("/plan", "nav_msgs/msg/Path"),
            ("/odom", "nav_msgs/msg/Odometry"),
            ("/cmd_vel", "geometry_msgs/msg/Twist"),
        ],
        msgs,
    )
    print(f"wrote {uri} ({len(msgs)} messages)")


def gen_nav_snippet() -> None:
    uri = OUT_DIR / "nav_snippet"
    start_ns = 1_700_000_000_000_000_000
    hz = 5.0
    duration_s = 4.0
    msgs: list[tuple[str, int, bytes]] = []
    n = int(duration_s * hz)
    for i in range(n):
        t = i / hz
        twist = Twist()
        twist.linear.x = 0.3 + 0.1 * t
        odom = Odometry()
        odom.twist.twist.linear.x = twist.linear.x - 0.02
        stamp = start_ns + int(i * (1e9 / hz))
        msgs.append(("/cmd_vel", stamp, serialize_message(twist)))
        msgs.append(("/odom", stamp, serialize_message(odom)))
    write_bag(
        uri,
        [
            ("/cmd_vel", "geometry_msgs/msg/Twist"),
            ("/odom", "nav_msgs/msg/Odometry"),
        ],
        msgs,
    )
    print(f"wrote {uri} ({n * 2} messages)")


def main() -> int:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    rclpy.init(args=None)
    try:
        gen_cmd_vel_snippet()
        gen_imu_snippet()
        gen_nav_snippet()
        gen_nav2_debug()
    finally:
        rclpy.shutdown()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
