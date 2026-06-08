"""
tuning.launch.py
================
Spectra ROS2 — live IMU / PID tuning preset.

Loads the checked-in tuning session (four IMU axes on a 2×2 grid).

Usage
-----
    ros2 launch spectra tuning.launch.py
    ros2 launch spectra tuning.launch.py session:=/path/to/custom.spectra-ros-session

Arguments
---------
    session    : str  = <install>/share/spectra/ros/sessions/presets/tuning.spectra-ros-session
    window     : str  = 1600x900
    node_name  : str  = spectra_tuning
"""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, ExecuteProcess
from launch.substitutions import LaunchConfiguration


def _default_session_path():
  try:
    share = get_package_share_directory("spectra")
    return os.path.join(share, "ros", "sessions", "presets", "tuning.spectra-ros-session")
  except Exception:
    return os.path.abspath(
        os.path.join(os.path.dirname(__file__), "../../sessions/presets/tuning.spectra-ros-session")
    )


def generate_launch_description():
    session   = LaunchConfiguration("session")
    window    = LaunchConfiguration("window")
    node_name = LaunchConfiguration("node_name")

    return LaunchDescription([
        DeclareLaunchArgument(
            "session",
            default_value=_default_session_path(),
            description="Path to tuning .spectra-ros-session preset",
        ),
        DeclareLaunchArgument(
            "window",
            default_value="1600x900",
            description="Initial window resolution as WxH",
        ),
        DeclareLaunchArgument(
            "node_name",
            default_value="spectra_tuning",
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
            name="spectra_ros_tuning",
        ),
    ])
