"""
bringup.launch.py
=================
Spectra ROS2 — robot bringup debug preset.

Loads cmd_vel and odom plots from the bringup session.

Usage
-----
    ros2 launch spectra bringup.launch.py
    ros2 launch spectra bringup.launch.py session:=/path/to/custom.spectra-ros-session

Arguments
---------
    session    : str  = <install>/share/spectra/ros/sessions/presets/bringup.spectra-ros-session
    window     : str  = 1600x900
    node_name  : str  = spectra_bringup
"""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, ExecuteProcess
from launch.substitutions import LaunchConfiguration


def _default_session_path():
  try:
    share = get_package_share_directory("spectra")
    return os.path.join(share, "ros", "sessions", "presets", "bringup.spectra-ros-session")
  except Exception:
    return os.path.abspath(
        os.path.join(os.path.dirname(__file__), "../../sessions/presets/bringup.spectra-ros-session")
    )


def generate_launch_description():
    session   = LaunchConfiguration("session")
    window    = LaunchConfiguration("window")
    node_name = LaunchConfiguration("node_name")

    return LaunchDescription([
        DeclareLaunchArgument(
            "session",
            default_value=_default_session_path(),
            description="Path to bringup .spectra-ros-session preset",
        ),
        DeclareLaunchArgument(
            "window",
            default_value="1600x900",
            description="Initial window resolution as WxH",
        ),
        DeclareLaunchArgument(
            "node_name",
            default_value="spectra_bringup",
            description="ROS2 node name for spectra-ros",
        ),
        ExecuteProcess(
            cmd=[
                "spectra-ros",
                "--session", session,
                "--window", window,
                "--node-name", node_name,
                "--layout", "default",
            ],
            output="screen",
            name="spectra_ros_bringup",
        ),
    ])
