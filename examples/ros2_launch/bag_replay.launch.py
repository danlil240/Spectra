"""
bag_replay.launch.py
====================
Spectra ROS2 Adapter — Rosbag replay example.

Opens a specified rosbag file in spectra-ros and starts playback at 1×
speed. All Float64 and numeric topics found in the bag are plotted
automatically via the Bag Info panel's "plot all" feature.

Requires: -DSPECTRA_ROS2_BAG=ON at build time.

Usage
-----
    ros2 launch spectra bag_replay.launch.py bag:=/path/to/recording.db3
    ros2 launch spectra bag_replay.launch.py bag:=/data/run42.mcap rate:=2.0
    ros2 launch spectra bag_replay.launch.py bag:=/data/run.db3 loop:=true

Arguments
---------
    bag        : str   (required)         Path to .db3 or .mcap bag file
    session    : str   = <bag_review preset>  Session layout for bag post-mortem
    rate       : str   = 1.0              Playback rate (0.1 – 10.0)
    loop       : str   = false            Loop playback when bag ends
    window     : str   = 1600x900         Initial window resolution (WxH)
    node_name  : str   = spectra_bag      ROS2 node name for the adapter
"""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, ExecuteProcess
from launch.substitutions import LaunchConfiguration


def _default_session_path():
  try:
    share = get_package_share_directory("spectra")
    return os.path.join(share, "ros", "sessions", "presets", "bag_review.spectra-ros-session")
  except Exception:
    return os.path.abspath(
        os.path.join(
            os.path.dirname(__file__), "../../sessions/presets/bag_review.spectra-ros-session"
        )
    )


def generate_launch_description():
    bag       = LaunchConfiguration("bag")
    session   = LaunchConfiguration("session")
    rate      = LaunchConfiguration("rate")
    loop      = LaunchConfiguration("loop")
    window    = LaunchConfiguration("window")
    node_name = LaunchConfiguration("node_name")

    return LaunchDescription([
        DeclareLaunchArgument(
            "bag",
            description="Path to the rosbag file (.db3 or .mcap) to open",
        ),
        DeclareLaunchArgument(
            "rate",
            default_value="1.0",
            description="Playback rate multiplier (0.1 – 10.0)",
        ),
        DeclareLaunchArgument(
            "loop",
            default_value="false",
            description="Loop playback when the bag reaches the end (true/false)",
        ),
        DeclareLaunchArgument(
            "window",
            default_value="1600x900",
            description="Initial window resolution as WxH",
        ),
        DeclareLaunchArgument(
            "node_name",
            default_value="spectra_bag",
            description="ROS2 node name for the spectra-ros adapter",
        ),
        DeclareLaunchArgument(
            "session",
            default_value=_default_session_path(),
            description="Path to bag_review .spectra-ros-session preset",
        ),

        ExecuteProcess(
            cmd=[
                "spectra-ros",
                "--bag",       bag,
                "--session",   session,
                "--rate",      rate,
                "--loop",      loop,
                "--window",    window,
                "--node-name", node_name,
                "--layout",    "default",
            ],
            output="screen",
            name="spectra_ros_bag",
        ),
    ])
