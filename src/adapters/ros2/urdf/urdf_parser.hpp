#pragma once

#include <string>
#include <vector>

#include <spectra/math3d.hpp>

namespace spectra::adapters::ros2
{

enum class UrdfGeometryType
{
    Box,
    Cylinder,
    Sphere,
    Unsupported,
};

struct UrdfGeometry
{
    UrdfGeometryType type{UrdfGeometryType::Unsupported};
    spectra::vec3    box_size{0.0, 0.0, 0.0};
    double           radius{0.0};
    double           length{0.0};
    std::string      mesh_filename;
};

struct UrdfCollision
{
    std::string        name;
    spectra::Transform origin{};
    UrdfGeometry       geometry;
};

struct UrdfLink
{
    std::string                name;
    std::vector<UrdfCollision> collisions;
};

enum class UrdfJointType
{
    Fixed,
    Revolute,
    Prismatic,
    Continuous,
    Unknown,
};

struct UrdfJointLimit
{
    bool   has_position_limits{false};
    double lower{0.0};
    double upper{0.0};
    double effort{0.0};
    double velocity{0.0};
};

struct UrdfJoint
{
    std::string        name;
    UrdfJointType      type{UrdfJointType::Unknown};
    std::string        parent_link;
    std::string        child_link;
    spectra::Transform origin{};
    spectra::vec3      axis{1.0, 0.0, 0.0};
    UrdfJointLimit     limits;
};

struct RobotDescription
{
    std::string            name;
    std::vector<UrdfLink>  links;
    std::vector<UrdfJoint> joints;

    const UrdfLink*  find_link(const std::string& link_name) const;
    const UrdfJoint* find_joint(const std::string& joint_name) const;
};

struct UrdfParseResult
{
    bool                     ok{false};
    RobotDescription         robot;
    std::string              error;
    std::vector<std::string> warnings;

    explicit operator bool() const { return ok; }
};

class UrdfParser
{
   public:
    static UrdfParseResult parse_string(const std::string& xml);
    static UrdfParseResult parse_file(const std::string& path);
};

}   // namespace spectra::adapters::ros2
