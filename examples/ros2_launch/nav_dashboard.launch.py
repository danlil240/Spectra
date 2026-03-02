"""
nav_dashboard.launch.py
=======================
Spectra ROS2 Adapter — Navigation stack dashboard example.

Launches spectra-ros with a 3-panel live plot layout monitoring common
navigation topics:
  - /cmd_vel   (geometry_msgs/Twist)  — commanded linear.x and angular.z
  - /odom      (nav_msgs/Odometry)    — actual linear.x and angular.z
  - /diagnostics aggregation via topic monitor panel

The node graph and TF tree panels are also visible in the default layout,
giving a full picture of the nav stack at a glance.

Usage
-----
    ros2 launch spectra nav_dashboard.launch.py
    ros2 launch spectra nav_dashboard.launch.py cmd_vel:=/cmd_vel_mux/input/navi
    ros2 launch spectra nav_dashboard.launch.py odom:=/odometry/filtered

Arguments
---------
    cmd_vel    : str  = /cmd_vel          Twist command topic
    odom       : str  = /odom             Odometry topic
    window     : str  = 1600x900          Initial window resolution (WxH)
    node_name  : str  = spectra_nav       ROS2 node name for the adapter
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, ExecuteProcess
from launch.substitutions import LaunchConfiguration


def generate_launch_description():
    cmd_vel   = LaunchConfiguration("cmd_vel")
    odom      = LaunchConfiguration("odom")
    window    = LaunchConfiguration("window")
    node_name = LaunchConfiguration("node_name")

    return LaunchDescription([
        DeclareLaunchArgument(
            "cmd_vel",
            default_value="/cmd_vel",
            description="geometry_msgs/Twist command velocity topic",
        ),
        DeclareLaunchArgument(
            "odom",
            default_value="/odom",
            description="nav_msgs/Odometry odometry topic",
        ),
        DeclareLaunchArgument(
            "window",
            default_value="1600x900",
            description="Initial window resolution as WxH",
        ),
        DeclareLaunchArgument(
            "node_name",
            default_value="spectra_nav",
            description="ROS2 node name for the spectra-ros adapter",
        ),

        ExecuteProcess(
            cmd=[
                "spectra-ros",
                "--topics",
                # cmd_vel fields: commanded linear + angular
                # odom fields:    actual linear + angular
                [
                    cmd_vel, ".linear.x,",
                    cmd_vel, ".angular.z,",
                    odom,    ".twist.twist.linear.x,",
                    odom,    ".twist.twist.angular.z,",
                    odom,    ".pose.pose.position.x,",
                    odom,    ".pose.pose.position.y",
                ],
                "--window",    window,
                "--node-name", node_name,
                "--layout",    "default",
            ],
            output="screen",
            name="spectra_ros_nav",
        ),
    ])
