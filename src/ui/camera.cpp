#include "camera.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace spectra
{

mat4 Camera::view_matrix() const
{
    return mat4_look_at(position, target, up);
}

mat4 Camera::projection_matrix(float aspect_ratio) const
{
    if (projection_mode == ProjectionMode::Perspective)
    {
        return mat4_perspective(deg_to_rad(fov), aspect_ratio, near_clip, far_clip);
    }
    else
    {
        float half_w = ortho_size * aspect_ratio;
        float half_h = ortho_size;
        return mat4_ortho(-half_w, half_w, -half_h, half_h, near_clip, far_clip);
    }
}

void Camera::orbit(float d_azimuth, float d_elevation)
{
    azimuth += d_azimuth;
    elevation += d_elevation;

    while (azimuth < 0.0f)
        azimuth += 360.0f;
    while (azimuth >= 360.0f)
        azimuth -= 360.0f;

    elevation = clampf(elevation, -89.0f, 89.0f);

    update_position_from_orbit();
}

void Camera::pan(float dx, float dy, float /*viewport_width*/, float /*viewport_height*/)
{
    vec3 forward = vec3_normalize(target - position);
    vec3 right   = vec3_normalize(vec3_cross(forward, up));
    vec3 cam_up  = vec3_cross(right, forward);

    float scale = distance * 0.002f;
    if (projection_mode == ProjectionMode::Orthographic)
    {
        scale = ortho_size * 0.002f;
    }

    vec3 offset = right * (-dx * scale) + cam_up * (dy * scale);
    position += offset;
    target += offset;
}

void Camera::zoom(float factor)
{
    if (projection_mode == ProjectionMode::Perspective)
    {
        distance *= factor;
        distance = clampf(distance, 0.1f, 10000.0f);
        update_position_from_orbit();
    }
    else
    {
        ortho_size *= factor;
        ortho_size = clampf(ortho_size, 0.1f, 10000.0f);
    }
}

void Camera::dolly(float amount)
{
    vec3  forward  = vec3_normalize(target - position);
    vec3  new_pos  = position + forward * amount;
    float new_dist = vec3_length(new_pos - target);

    if (new_dist >= 0.1f && new_dist <= 10000.0f)
    {
        position = new_pos;
        distance = new_dist;
    }
}

void Camera::fit_to_bounds(vec3 min_bound, vec3 max_bound)
{
    vec3  center     = (min_bound + max_bound) * 0.5f;
    vec3  extent     = max_bound - min_bound;
    float max_extent = std::max({extent.x, extent.y, extent.z});

    if (max_extent < 1e-6f)
    {
        max_extent = 1.0f;
    }

    target = center;

    if (projection_mode == ProjectionMode::Perspective)
    {
        float fov_rad = deg_to_rad(fov);
        distance      = max_extent / (2.0f * std::tan(fov_rad * 0.5f)) * 1.5f;
    }
    else
    {
        ortho_size = max_extent * 0.6f;
        distance   = max_extent * 2.0f;
    }

    update_position_from_orbit();
}

void Camera::reset()
{
    position        = {0.0f, 0.0f, 5.0f};
    target          = {0.0f, 0.0f, 0.0f};
    up              = {0.0f, 1.0f, 0.0f};
    azimuth         = 45.0f;
    elevation       = 30.0f;
    distance        = 5.0f;
    fov             = 45.0f;
    ortho_size      = 10.0f;
    projection_mode = ProjectionMode::Perspective;
}

void Camera::update_position_from_orbit()
{
    float az_rad = deg_to_rad(azimuth);
    float el_rad = deg_to_rad(elevation);

    float cos_el = std::cos(el_rad);
    vec3  offset{distance * cos_el * std::cos(az_rad),
                distance * std::sin(el_rad),
                distance * cos_el * std::sin(az_rad)};

    position = target + offset;
}

std::string Camera::serialize() const
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6);
    oss << "{"
        << "\"position\":[" << position.x << "," << position.y << "," << position.z << "],"
        << "\"target\":[" << target.x << "," << target.y << "," << target.z << "],"
        << "\"up\":[" << up.x << "," << up.y << "," << up.z << "],"
        << "\"projection_mode\":" << (projection_mode == ProjectionMode::Perspective ? 0 : 1) << ","
        << "\"fov\":" << fov << ","
        << "\"near_clip\":" << near_clip << ","
        << "\"far_clip\":" << far_clip << ","
        << "\"ortho_size\":" << ortho_size << ","
        << "\"azimuth\":" << azimuth << ","
        << "\"elevation\":" << elevation << ","
        << "\"distance\":" << distance << "}";
    return oss.str();
}

void Camera::deserialize(const std::string& json)
{
    auto parse_vec3 = [](const std::string& s, size_t& pos) -> vec3
    {
        pos = s.find('[', pos);
        if (pos == std::string::npos)
            return {0, 0, 0};
        pos++;

        float x = std::stof(s.substr(pos));
        pos     = s.find(',', pos) + 1;
        float y = std::stof(s.substr(pos));
        pos     = s.find(',', pos) + 1;
        float z = std::stof(s.substr(pos));
        pos     = s.find(']', pos) + 1;

        return {x, y, z};
    };

    auto parse_float = [](const std::string& s, size_t& pos) -> float
    {
        pos = s.find(':', pos);
        if (pos == std::string::npos)
            return 0.0f;
        pos++;
        return std::stof(s.substr(pos));
    };

    auto parse_int = [](const std::string& s, size_t& pos) -> int
    {
        pos = s.find(':', pos);
        if (pos == std::string::npos)
            return 0;
        pos++;
        return std::stoi(s.substr(pos));
    };

    size_t pos = 0;

    pos = json.find("\"position\"");
    if (pos != std::string::npos)
        position = parse_vec3(json, pos);

    pos = json.find("\"target\"");
    if (pos != std::string::npos)
        target = parse_vec3(json, pos);

    pos = json.find("\"up\"");
    if (pos != std::string::npos)
        up = parse_vec3(json, pos);

    pos = json.find("\"projection_mode\"");
    if (pos != std::string::npos)
    {
        int mode        = parse_int(json, pos);
        projection_mode = mode == 0 ? ProjectionMode::Perspective : ProjectionMode::Orthographic;
    }

    pos = json.find("\"fov\"");
    if (pos != std::string::npos)
        fov = parse_float(json, pos);

    pos = json.find("\"near_clip\"");
    if (pos != std::string::npos)
        near_clip = parse_float(json, pos);

    pos = json.find("\"far_clip\"");
    if (pos != std::string::npos)
        far_clip = parse_float(json, pos);

    pos = json.find("\"ortho_size\"");
    if (pos != std::string::npos)
        ortho_size = parse_float(json, pos);

    pos = json.find("\"azimuth\"");
    if (pos != std::string::npos)
        azimuth = parse_float(json, pos);

    pos = json.find("\"elevation\"");
    if (pos != std::string::npos)
        elevation = parse_float(json, pos);

    pos = json.find("\"distance\"");
    if (pos != std::string::npos)
        distance = parse_float(json, pos);
}

}   // namespace spectra
