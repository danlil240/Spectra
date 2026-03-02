#include "ros2_adapter.hpp"

#include <cstdio>

// spectra-ros — standalone ROS2 visualization executable.
//
// This is a placeholder entry point.  Full implementation arrives in G1.
// For now it validates that the adapter library links and the ROS2
// dependency chain compiles correctly.

int main(int /*argc*/, char** /*argv*/)
{
    std::printf("spectra-ros %s (ROS2 adapter)\n", spectra::adapters::ros2::adapter_version());
    std::printf("Full implementation coming in Phase G.\n");
    return 0;
}
