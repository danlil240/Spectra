#pragma once

// ROS2 adapter for Spectra — top-level public header.
//
// This header is the entry point for consumers of the spectra_ros2_adapter
// library.  It deliberately includes only the stable public surface; internal
// implementation headers live alongside their .cpp files.
//
// Build gate: this file (and the whole adapter) is compiled only when
// -DSPECTRA_USE_ROS2=ON is passed to CMake.

namespace spectra::adapters::ros2
{

// Returns the version string of the adapter (matches Spectra project version).
const char* adapter_version();

}   // namespace spectra::adapters::ros2
