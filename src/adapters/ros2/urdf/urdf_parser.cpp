#include "urdf/urdf_parser.hpp"

#include <cmath>
#include <fstream>
#include <sstream>
#include <utility>

#include <tinyxml2.h>

namespace spectra::adapters::ros2
{

namespace
{
using tinyxml2::XMLElement;
using tinyxml2::XMLDocument;

std::string element_name(const XMLElement* element)
{
    return element && element->Name() ? element->Name() : "<unknown>";
}

bool parse_vec3_attr(const char* text, spectra::vec3& out)
{
    if (!text)
        return false;

    std::istringstream stream(text);
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    if (!(stream >> x >> y >> z))
        return false;

    out = {x, y, z};
    return true;
}

spectra::quat quat_from_rpy(double roll, double pitch, double yaw)
{
    const double cr = std::cos(roll * 0.5);
    const double sr = std::sin(roll * 0.5);
    const double cp = std::cos(pitch * 0.5);
    const double sp = std::sin(pitch * 0.5);
    const double cy = std::cos(yaw * 0.5);
    const double sy = std::sin(yaw * 0.5);

    return spectra::quat_normalize({
        sr * cp * cy - cr * sp * sy,
        cr * sp * cy + sr * cp * sy,
        cr * cp * sy - sr * sp * cy,
        cr * cp * cy + sr * sp * sy,
    });
}

spectra::Transform parse_origin(const XMLElement* parent,
                                std::vector<std::string>& warnings)
{
    spectra::Transform origin{};
    if (!parent)
        return origin;

    const XMLElement* origin_element = parent->FirstChildElement("origin");
    if (!origin_element)
        return origin;

    spectra::vec3 xyz{};
    if (const char* xyz_attr = origin_element->Attribute("xyz"))
    {
        if (!parse_vec3_attr(xyz_attr, xyz))
            warnings.push_back("Invalid xyz attribute on <origin>");
        else
            origin.translation = xyz;
    }

    spectra::vec3 rpy{};
    if (const char* rpy_attr = origin_element->Attribute("rpy"))
    {
        if (!parse_vec3_attr(rpy_attr, rpy))
            warnings.push_back("Invalid rpy attribute on <origin>");
        else
            origin.rotation = quat_from_rpy(rpy.x, rpy.y, rpy.z);
    }

    return origin;
}

UrdfGeometry parse_geometry(const XMLElement* geometry_element,
                            std::vector<std::string>& warnings)
{
    UrdfGeometry geometry;
    if (!geometry_element)
        return geometry;

    if (const XMLElement* box = geometry_element->FirstChildElement("box"))
    {
        spectra::vec3 size{};
        if (!parse_vec3_attr(box->Attribute("size"), size))
            warnings.push_back("Ignoring <box> geometry with invalid size");
        else
        {
            geometry.type = UrdfGeometryType::Box;
            geometry.box_size = size;
        }
        return geometry;
    }

    if (const XMLElement* cylinder = geometry_element->FirstChildElement("cylinder"))
    {
        double radius = 0.0;
        double length = 0.0;
        if (cylinder->QueryDoubleAttribute("radius", &radius) != tinyxml2::XML_SUCCESS
            || cylinder->QueryDoubleAttribute("length", &length) != tinyxml2::XML_SUCCESS)
        {
            warnings.push_back("Ignoring <cylinder> geometry with invalid radius/length");
        }
        else
        {
            geometry.type = UrdfGeometryType::Cylinder;
            geometry.radius = radius;
            geometry.length = length;
        }
        return geometry;
    }

    if (const XMLElement* sphere = geometry_element->FirstChildElement("sphere"))
    {
        double radius = 0.0;
        if (sphere->QueryDoubleAttribute("radius", &radius) != tinyxml2::XML_SUCCESS)
        {
            warnings.push_back("Ignoring <sphere> geometry with invalid radius");
        }
        else
        {
            geometry.type = UrdfGeometryType::Sphere;
            geometry.radius = radius;
        }
        return geometry;
    }

    if (const XMLElement* mesh = geometry_element->FirstChildElement("mesh"))
    {
        geometry.type = UrdfGeometryType::Unsupported;
        geometry.mesh_filename = mesh->Attribute("filename") ? mesh->Attribute("filename") : "";
        warnings.push_back("Ignoring unsupported <mesh> geometry");
        return geometry;
    }

    warnings.push_back("Ignoring <geometry> with no supported shape child");
    return geometry;
}

UrdfJointType parse_joint_type(const char* type_text)
{
    if (!type_text)
        return UrdfJointType::Unknown;
    const std::string type(type_text);
    if (type == "fixed")
        return UrdfJointType::Fixed;
    if (type == "revolute")
        return UrdfJointType::Revolute;
    if (type == "prismatic")
        return UrdfJointType::Prismatic;
    if (type == "continuous")
        return UrdfJointType::Continuous;
    return UrdfJointType::Unknown;
}

bool parse_link(const XMLElement* link_element,
                RobotDescription& robot,
                std::vector<std::string>& warnings,
                std::string& error)
{
    const char* name_attr = link_element->Attribute("name");
    if (!name_attr || name_attr[0] == '\0')
    {
        error = "Encountered <link> without a name";
        return false;
    }

    UrdfLink link;
    link.name = name_attr;

    for (const XMLElement* collision = link_element->FirstChildElement("collision");
         collision != nullptr;
         collision = collision->NextSiblingElement("collision"))
    {
        UrdfCollision parsed_collision;
        if (const char* collision_name = collision->Attribute("name"))
            parsed_collision.name = collision_name;
        parsed_collision.origin = parse_origin(collision, warnings);

        const XMLElement* geometry_element = collision->FirstChildElement("geometry");
        if (!geometry_element)
        {
            warnings.push_back("Ignoring <collision> on link '" + link.name + "' with no <geometry>");
            continue;
        }

        parsed_collision.geometry = parse_geometry(geometry_element, warnings);
        if (parsed_collision.geometry.type == UrdfGeometryType::Unsupported)
            continue;

        link.collisions.push_back(std::move(parsed_collision));
    }

    robot.links.push_back(std::move(link));
    return true;
}

bool parse_joint(const XMLElement* joint_element,
                 RobotDescription& robot,
                 std::vector<std::string>& warnings,
                 std::string& error)
{
    const char* name_attr = joint_element->Attribute("name");
    if (!name_attr || name_attr[0] == '\0')
    {
        error = "Encountered <joint> without a name";
        return false;
    }

    UrdfJoint joint;
    joint.name = name_attr;
    joint.type = parse_joint_type(joint_element->Attribute("type"));
    joint.origin = parse_origin(joint_element, warnings);

    if (joint.type == UrdfJointType::Unknown)
        warnings.push_back("Joint '" + joint.name + "' has unsupported type");

    const XMLElement* parent = joint_element->FirstChildElement("parent");
    const XMLElement* child = joint_element->FirstChildElement("child");
    if (!parent || !child)
    {
        error = "Joint '" + joint.name + "' is missing <parent> or <child>";
        return false;
    }

    const char* parent_link = parent->Attribute("link");
    const char* child_link = child->Attribute("link");
    if (!parent_link || !child_link || parent_link[0] == '\0' || child_link[0] == '\0')
    {
        error = "Joint '" + joint.name + "' has empty parent/child link";
        return false;
    }
    joint.parent_link = parent_link;
    joint.child_link = child_link;

    if (const XMLElement* axis = joint_element->FirstChildElement("axis"))
    {
        spectra::vec3 axis_xyz{};
        if (!parse_vec3_attr(axis->Attribute("xyz"), axis_xyz))
            warnings.push_back("Joint '" + joint.name + "' has invalid axis; using default");
        else
            joint.axis = spectra::vec3_normalize(axis_xyz);
    }

    if (const XMLElement* limit = joint_element->FirstChildElement("limit"))
    {
        if (limit->QueryDoubleAttribute("lower", &joint.limits.lower) == tinyxml2::XML_SUCCESS
            && limit->QueryDoubleAttribute("upper", &joint.limits.upper) == tinyxml2::XML_SUCCESS)
        {
            joint.limits.has_position_limits = true;
        }
        limit->QueryDoubleAttribute("effort", &joint.limits.effort);
        limit->QueryDoubleAttribute("velocity", &joint.limits.velocity);
    }

    robot.joints.push_back(std::move(joint));
    return true;
}

UrdfParseResult parse_document(XMLDocument& document)
{
    UrdfParseResult result;

    const XMLElement* robot_element = document.FirstChildElement("robot");
    if (!robot_element)
    {
        result.error = "URDF is missing a <robot> root element";
        return result;
    }

    const char* robot_name = robot_element->Attribute("name");
    if (!robot_name || robot_name[0] == '\0')
    {
        result.error = "<robot> is missing the required name attribute";
        return result;
    }
    result.robot.name = robot_name;

    for (const XMLElement* child = robot_element->FirstChildElement();
         child != nullptr;
         child = child->NextSiblingElement())
    {
        const std::string tag = element_name(child);
        if (tag == "link")
        {
            if (!parse_link(child, result.robot, result.warnings, result.error))
                return result;
        }
        else if (tag == "joint")
        {
            if (!parse_joint(child, result.robot, result.warnings, result.error))
                return result;
        }
    }

    result.ok = true;
    return result;
}
}   // namespace

const UrdfLink* RobotDescription::find_link(const std::string& link_name) const
{
    for (const auto& link : links)
    {
        if (link.name == link_name)
            return &link;
    }
    return nullptr;
}

const UrdfJoint* RobotDescription::find_joint(const std::string& joint_name) const
{
    for (const auto& joint : joints)
    {
        if (joint.name == joint_name)
            return &joint;
    }
    return nullptr;
}

UrdfParseResult UrdfParser::parse_string(const std::string& xml)
{
    XMLDocument document;
    const tinyxml2::XMLError err = document.Parse(xml.c_str(), xml.size());
    if (err != tinyxml2::XML_SUCCESS)
    {
        UrdfParseResult result;
        result.error = std::string("XML parse failed: ")
            + (document.ErrorStr() ? document.ErrorStr() : "unknown error");
        return result;
    }

    return parse_document(document);
}

UrdfParseResult UrdfParser::parse_file(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        UrdfParseResult result;
        result.error = "Failed to open URDF file: " + path;
        return result;
    }

    std::ostringstream stream;
    stream << file.rdbuf();
    return parse_string(stream.str());
}

}   // namespace spectra::adapters::ros2
