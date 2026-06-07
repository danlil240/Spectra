"""
nav2_debug.launch.py
====================
Spectra ROS2 Adapter — Nav2 spatiotemporal debug workshop.

Opens a reference nav bag with the nav2_debug session preset and rviz-plot
layout so plots, TF tree, laser scan, path, and pose displays share one
bag playhead.

Usage
-----
    ros2 launch spectra nav2_debug.launch.py
    ros2 launch spectra nav2_debug.launch.py bag:=/path/to/recording.db3
    ros2 launch spectra nav2_debug.launch.py rate:=2.0 loop:=true

Arguments
---------
    bag        : str   = <nav2_debug reference bag>
    session    : str   = <nav2_debug preset>
    rate       : str   = 1.0
    loop       : str   = false
    window     : str   = 1600x900
    node_name  : str   = spectra_nav2_debug
"""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, ExecuteProcess
from launch.substitutions import LaunchConfiguration


def _repo_path(*parts):
    return os.path.abspath(os.path.join(os.path.dirname(__file__), "../..", *parts))


def _default_bag_path():
    return _repo_path("tests", "data", "ros_bags", "nav2_debug")


def _default_session_path():
    try:
        share = get_package_share_directory("spectra")
        return os.path.join(share, "ros", "sessions", "presets", "nav2_debug.spectra-ros-session")
    except Exception:
        return _repo_path("sessions", "presets", "nav2_debug.spectra-ros-session")


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
            default_value=_default_bag_path(),
            description="Path to nav debug rosbag (.db3 directory or .mcap)",
        ),
        DeclareLaunchArgument(
            "session",
            default_value=_default_session_path(),
            description="Path to nav2_debug .spectra-ros-session preset",
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
            default_value="spectra_nav2_debug",
            description="ROS2 node name for the spectra-ros adapter",
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
                "--layout",    "rviz-plot",
            ],
            output="screen",
            name="spectra_ros_nav2_debug",
        ),
    ])
