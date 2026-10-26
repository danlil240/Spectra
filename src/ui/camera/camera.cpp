#include "camera.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
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

    float half_w = ortho_size * aspect_ratio;
    float half_h = ortho_size;
    return mat4_ortho(-half_w, half_w, -half_h, half_h, near_clip, far_clip);
}

void Camera::orbit(float d_azimuth, float d_elevation)
{
    vec3  offset = position - target;
    float dist   = vec3_length(offset);
    if (dist < 1e-6f)
        return;

    const vec3 world_up = (up_axis == UpAxis::Z) ? vec3{0.0f, 0.0f, 1.0f} : vec3{0.0f, 1.0f, 0.0f};

    if (std::abs(d_azimuth) > 1e-6f)
    {
        // Sign matches legacy turntable: +azimuth moves +X toward +Z (Y-up).
        const quat q_az =
            quat_from_axis_angle(world_up, static_cast<double>(deg_to_rad(-d_azimuth)));
        offset = quat_rotate(q_az, offset);
    }

    if (std::abs(d_elevation) > 1e-6f)
    {
        vec3 right = vec3_cross(world_up, offset);
        if (vec3_length(right) < 1e-4f)
        {
            const vec3 fallback =
                (std::abs(world_up.y) < 0.9f) ? vec3{1.0f, 0.0f, 0.0f} : vec3{0.0f, 1.0f, 0.0f};
            right = vec3_cross(world_up, fallback);
        }
        right = vec3_normalize(right);

        const quat q_el =
            quat_from_axis_angle(right, static_cast<double>(deg_to_rad(-d_elevation)));
        offset = quat_rotate(q_el, offset);
    }

    position = target + offset;
    distance = dist;
    up       = world_up;
    sync_orbit_from_position();

    if (elevation > 89.0f)
    {
        elevation = 89.0f;
        update_position_from_orbit();
    }
    else if (elevation < -89.0f)
    {
        elevation = -89.0f;
        update_position_from_orbit();
    }
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

void Camera::sync_orbit_from_position()
{
    vec3 offset = position - target;
    distance    = static_cast<float>(vec3_length(offset));
    if (distance < 1e-6f)
        return;

    const vec3 dir = offset * (1.0f / distance);

    if (up_axis == UpAxis::Z)
    {
        elevation = rad_to_deg(std::asin(clampf(dir.z, -1.0f, 1.0f)));
        azimuth   = rad_to_deg(std::atan2(dir.y, dir.x));
    }
    else
    {
        elevation = rad_to_deg(std::asin(clampf(dir.y, -1.0f, 1.0f)));
        azimuth   = rad_to_deg(std::atan2(dir.z, dir.x));
    }

    while (azimuth < 0.0f)
        azimuth += 360.0f;
    while (azimuth >= 360.0f)
        azimuth -= 360.0f;

    elevation = clampf(elevation, -89.0f, 89.0f);
}

Camera& Camera::align_view_to_axis(AxisView view)
{
    // Slightly off-pole so horizontal orbit still moves the camera after a snap.
    constexpr float kPoleElevation = 75.0f;

    if (up_axis == UpAxis::Z)
    {
        switch (view)
        {
            case AxisView::PositiveX:
                azimuth   = 0.0f;
                elevation = 0.0f;
                break;
            case AxisView::PositiveY:
                azimuth   = 90.0f;
                elevation = 0.0f;
                break;
            case AxisView::PositiveZ:
                azimuth   = 0.0f;
                elevation = kPoleElevation;
                break;
        }
    }
    else
    {
        switch (view)
        {
            case AxisView::PositiveX:
                azimuth   = 0.0f;
                elevation = 0.0f;
                break;
            case AxisView::PositiveY:
                azimuth   = 0.0f;
                elevation = kPoleElevation;
                break;
            case AxisView::PositiveZ:
                azimuth   = 90.0f;
                elevation = 0.0f;
                break;
        }
    }

    update_position_from_orbit();
    return *this;
}

void Camera::reset()
{
    if (up_axis == UpAxis::Z)
    {
        position  = {5.0f, 5.0f, 3.0f};
        target    = {0.0f, 0.0f, 0.0f};
        up        = {0.0f, 0.0f, 1.0f};
        azimuth   = 45.0f;
        elevation = 30.0f;
    }
    else
    {
        position  = {0.0f, 0.0f, 5.0f};
        target    = {0.0f, 0.0f, 0.0f};
        up        = {0.0f, 1.0f, 0.0f};
        azimuth   = 45.0f;
        elevation = 30.0f;
    }
    distance        = 5.0f;
    fov             = 45.0f;
    ortho_size      = 10.0f;
    projection_mode = ProjectionMode::Perspective;
    update_position_from_orbit();
}

void Camera::update_position_from_orbit()
{
    float az_rad = deg_to_rad(azimuth);
    float el_rad = deg_to_rad(elevation);

    float cos_el = std::cos(el_rad);

    vec3 offset;
    if (up_axis == UpAxis::Z)
    {
        // Z-up: azimuth rotates in XY plane, elevation lifts toward +Z
        offset = {distance * cos_el * std::cos(az_rad),
                  distance * cos_el * std::sin(az_rad),
                  distance * std::sin(el_rad)};
        up     = {0.0f, 0.0f, 1.0f};
    }
    else
    {
        // Y-up (default): azimuth rotates in XZ plane, elevation lifts toward +Y
        offset = {distance * cos_el * std::cos(az_rad),
                  distance * std::sin(el_rad),
                  distance * cos_el * std::sin(az_rad)};
        up     = {0.0f, 1.0f, 0.0f};
    }

    position = target + offset;
}

std::string Camera::serialize() const
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6);
    oss << "{" << "\"position\":[" << position.x << "," << position.y << "," << position.z << "],"
        << "\"target\":[" << target.x << "," << target.y << "," << target.z << "]," << "\"up\":["
        << up.x << "," << up.y << "," << up.z << "],"
        << "\"projection_mode\":" << (projection_mode == ProjectionMode::Perspective ? 0 : 1) << ","
        << "\"up_axis\":" << (up_axis == UpAxis::Z ? 1 : 0) << "," << "\"fov\":" << fov << ","
        << "\"near_clip\":" << near_clip << "," << "\"far_clip\":" << far_clip << ","
        << "\"ortho_size\":" << ortho_size << "," << "\"azimuth\":" << azimuth << ","
        << "\"elevation\":" << elevation << "," << "\"distance\":" << distance << "}";
    return oss.str();
}

void Camera::deserialize(const std::string& json)
{
    // Non-throwing parsers: malformed input (hand-edited or truncated workspace
    // files) leaves the corresponding camera field unchanged.
    auto parse_number = [](const std::string& s, size_t& pos, float& out) -> bool
    {
        if (pos >= s.size())
            return false;
        const char* begin = s.c_str() + pos;
        char*       end   = nullptr;
        errno             = 0;
        const float v     = std::strtof(begin, &end);
        if (end == begin || errno == ERANGE || !std::isfinite(v))
            return false;
        pos += static_cast<size_t>(end - begin);
        out = v;
        return true;
    };

    auto skip_ws = [](const std::string& s, size_t& pos)
    {
        while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos])))
            ++pos;
    };

    auto expect_char = [&skip_ws](const std::string& s, size_t& pos, char c) -> bool
    {
        skip_ws(s, pos);
        if (pos >= s.size() || s[pos] != c)
            return false;
        ++pos;
        return true;
    };

    auto parse_vec3 = [&](const std::string& s, size_t pos, vec3& out) -> bool
    {
        pos = s.find(':', pos);
        if (pos == std::string::npos)
            return false;
        ++pos;
        if (!expect_char(s, pos, '['))
            return false;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        if (!parse_number(s, pos, x) || !expect_char(s, pos, ',') || !parse_number(s, pos, y)
            || !expect_char(s, pos, ',') || !parse_number(s, pos, z) || !expect_char(s, pos, ']'))
            return false;
        out = vec3{x, y, z};
        return true;
    };

    auto parse_float = [&](const std::string& s, size_t pos, float& out) -> bool
    {
        pos = s.find(':', pos);
        if (pos == std::string::npos)
            return false;
        ++pos;
        return parse_number(s, pos, out);
    };

    auto parse_int = [](const std::string& s, size_t pos, int& out) -> bool
    {
        pos = s.find(':', pos);
        if (pos == std::string::npos)
            return false;
        ++pos;
        const char* begin = s.c_str() + pos;
        char*       end   = nullptr;
        errno             = 0;
        const long v      = std::strtol(begin, &end, 10);
        if (end == begin || errno == ERANGE || v < std::numeric_limits<int>::min()
            || v > std::numeric_limits<int>::max())
            return false;
        out = static_cast<int>(v);
        return true;
    };

    auto find_key = [&json](const char* key) { return json.find(key); };

    size_t pos = find_key("\"position\"");
    if (pos != std::string::npos)
        parse_vec3(json, pos, position);

    pos = find_key("\"target\"");
    if (pos != std::string::npos)
        parse_vec3(json, pos, target);

    pos = find_key("\"up\"");
    if (pos != std::string::npos)
        parse_vec3(json, pos, up);

    int ival = 0;
    pos      = find_key("\"projection_mode\"");
    if (pos != std::string::npos && parse_int(json, pos, ival))
        projection_mode = ival == 0 ? ProjectionMode::Perspective : ProjectionMode::Orthographic;

    pos = find_key("\"up_axis\"");
    if (pos != std::string::npos && parse_int(json, pos, ival))
        up_axis = ival == 1 ? UpAxis::Z : UpAxis::Y;

    pos = find_key("\"fov\"");
    if (pos != std::string::npos)
        parse_float(json, pos, fov);

    pos = find_key("\"near_clip\"");
    if (pos != std::string::npos)
        parse_float(json, pos, near_clip);

    pos = find_key("\"far_clip\"");
    if (pos != std::string::npos)
        parse_float(json, pos, far_clip);

    pos = find_key("\"ortho_size\"");
    if (pos != std::string::npos)
        parse_float(json, pos, ortho_size);

    pos = find_key("\"azimuth\"");
    if (pos != std::string::npos)
        parse_float(json, pos, azimuth);

    pos = find_key("\"elevation\"");
    if (pos != std::string::npos)
        parse_float(json, pos, elevation);

    pos = find_key("\"distance\"");
    if (pos != std::string::npos)
        parse_float(json, pos, distance);
}

}   // namespace spectra
