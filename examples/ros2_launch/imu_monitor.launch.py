"""
imu_monitor.launch.py
=====================
Spectra ROS2 Adapter — IMU live monitoring example.

Launches spectra-ros subscribing four fields from a sensor_msgs/Imu topic:
  - linear_acceleration.x / .y / .z
  - angular_velocity.z

The live plot auto-scrolls over a configurable time window.

Usage
-----
    ros2 launch spectra imu_monitor.launch.py
    ros2 launch spectra imu_monitor.launch.py imu_topic:=/sensors/imu/data
    ros2 launch spectra imu_monitor.launch.py window:=1920x1080

Arguments
---------
    imu_topic  : str  = /imu/data       ROS2 topic publishing sensor_msgs/Imu
    window     : str  = 1280x800        Initial window resolution (WxH)
    node_name  : str  = spectra_imu     ROS2 node name for the adapter
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, ExecuteProcess
from launch.substitutions import LaunchConfiguration


def generate_launch_description():
    imu_topic = LaunchConfiguration("imu_topic")
    window    = LaunchConfiguration("window")
    node_name = LaunchConfiguration("node_name")

    return LaunchDescription([
        DeclareLaunchArgument(
            "imu_topic",
            default_value="/imu/data",
            description="ROS2 topic publishing sensor_msgs/Imu",
        ),
        DeclareLaunchArgument(
            "window",
            default_value="1280x800",
            description="Initial window resolution as WxH",
        ),
        DeclareLaunchArgument(
            "node_name",
            default_value="spectra_imu",
            description="ROS2 node name for the spectra-ros adapter",
        ),

        ExecuteProcess(
            cmd=[
                "spectra-ros",
                "--topics",
                # Four comma-separated field paths from the same Imu message.
                # LaunchConfiguration substitutions are concatenated at launch time.
                [
                    imu_topic, ".linear_acceleration.x,",
                    imu_topic, ".linear_acceleration.y,",
                    imu_topic, ".linear_acceleration.z,",
                    imu_topic, ".angular_velocity.z",
                ],
                "--window",    window,
                "--node-name", node_name,
                "--layout",    "default",
            ],
            output="screen",
            name="spectra_ros_imu",
        ),
    ])
