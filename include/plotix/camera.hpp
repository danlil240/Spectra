#pragma once

#include <plotix/math3d.hpp>
#include <string>

namespace plotix {

class Camera {
public:
    enum class ProjectionMode {
        Perspective,
        Orthographic
    };

    vec3 position{0.0f, 0.0f, 5.0f};
    vec3 target{0.0f, 0.0f, 0.0f};
    vec3 up{0.0f, 1.0f, 0.0f};

    ProjectionMode projection_mode = ProjectionMode::Perspective;
    float fov = 45.0f;
    float near_clip = 0.01f;
    float far_clip = 1000.0f;
    float ortho_size = 10.0f;

    float azimuth = 45.0f;
    float elevation = 30.0f;
    float distance = 5.0f;

    Camera() = default;

    mat4 view_matrix() const;
    mat4 projection_matrix(float aspect_ratio) const;

    void orbit(float d_azimuth, float d_elevation);
    void pan(float dx, float dy, float viewport_width, float viewport_height);
    void zoom(float factor);
    void dolly(float amount);

    void fit_to_bounds(vec3 min_bound, vec3 max_bound);
    void reset();

    void update_position_from_orbit();

    std::string serialize() const;
    void deserialize(const std::string& json);
};

} // namespace plotix
