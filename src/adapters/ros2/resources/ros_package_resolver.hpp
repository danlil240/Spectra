#pragma once

#include <string>

namespace spectra::adapters::ros2
{

struct PackageResolveResult
{
    bool        ok{false};
    std::string path;
    std::string error;
};

// Resolve package://pkg/share/path and file:// absolute paths.
PackageResolveResult resolve_ros_resource_uri(const std::string& uri);

}   // namespace spectra::adapters::ros2
