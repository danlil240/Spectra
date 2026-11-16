#pragma once

#include <spectra/math3d.hpp>
#include <string>

namespace spectra
{

class Camera
{
   public:
    enum class ProjectionMode
    {
        Perspective,
        Orthographic
    };

    enum class UpAxis
    {
        Y,
        Z
    };

    // World-space +X / +Y / +Z axis-aligned views (2D projection of the other two axes).
    enum class AxisView
    {
        PositiveX,
        PositiveY,
        PositiveZ
    };

    // Standard named views.  With the default Z-up axis, Front looks from -Y
    // toward +Y (X to the right, Z up), Right looks from +X, Top looks down
    // from +Z (X to the right, Y up).  With Y-up, Front looks from +Z and Top
    // from +Y.  Isometric looks from the (+X, +Y, +Z) octant (Z-up) at
    // equal angles to the three axes.
    enum class ViewPreset
    {
        Front,
        Back,
        Left,
        Right,
        Top,
        Bottom,
        Isometric
    };

    // Z-up by default (MATLAB convention): surf/plot3/scatter3 put the third
    // coordinate on the vertical axis.  Use set_up_axis(UpAxis::Y) for Y-up.
    // The default position is the orbit position for azimuth/elevation/distance.
    vec3   position{3.0618622f, 3.0618622f, 2.5f};
    vec3   target{0.0f, 0.0f, 0.0f};
    vec3   up{0.0f, 0.0f, 1.0f};
    UpAxis up_axis = UpAxis::Z;

    ProjectionMode projection_mode = ProjectionMode::Perspective;
    float          fov             = 45.0f;
    float          near_clip       = 0.01f;
    float          far_clip        = 1000.0f;
    float          ortho_size      = 10.0f;

    float azimuth   = 45.0f;
    float elevation = 30.0f;
    float distance  = 5.0f;

    Camera() = default;

    // Fluent setters — automatically update position from orbit parameters
    Camera& set_azimuth(float a)
    {
        azimuth = a;
        update_position_from_orbit();
        return *this;
    }
    Camera& set_elevation(float e)
    {
        elevation = e;
        update_position_from_orbit();
        return *this;
    }
    Camera& set_distance(float d)
    {
        distance = d;
        update_position_from_orbit();
        return *this;
    }
    Camera& set_fov(float f)
    {
        fov = f;
        return *this;
    }
    Camera& set_ortho_size(float s)
    {
        ortho_size = s;
        return *this;
    }
    Camera& set_target(vec3 t)
    {
        target = t;
        update_position_from_orbit();
        return *this;
    }
    Camera& set_projection(ProjectionMode m)
    {
        projection_mode = m;
        return *this;
    }
    Camera& set_up_axis(UpAxis axis)
    {
        up_axis = axis;
        up      = (axis == UpAxis::Z) ? vec3{0.0f, 0.0f, 1.0f} : vec3{0.0f, 1.0f, 0.0f};
        update_position_from_orbit();
        return *this;
    }

    mat4 view_matrix() const;
    mat4 projection_matrix(float aspect_ratio) const;

    void orbit(float d_azimuth, float d_elevation);
    void pan(float dx, float dy, float viewport_width, float viewport_height);
    void zoom(float factor);
    void dolly(float amount);

    void fit_to_bounds(vec3 min_bound, vec3 max_bound);
    void reset();

    // Snap orbit angles so the camera looks along +X, +Y, or +Z (perpendicular 2D view).
    Camera& align_view_to_axis(AxisView view);

    // Orbit to a standard view (sets azimuth / elevation, keeps distance,
    // target and projection).  Top / Bottom stop a hair short of the pole so
    // a following orbit keeps a stable screen orientation.
    Camera& set_view_preset(ViewPreset preset);

    // True for the six axis-aligned presets (all but Isometric).
    static constexpr bool is_axis_view(ViewPreset preset)
    {
        return preset != ViewPreset::Isometric;
    }

    // Orthographic half-height that shows the plane through `target` at the
    // same size as the perspective projection does (distance * tan(fov / 2)),
    // so switching projection keeps the framing.
    float perspective_matched_ortho_size() const;

    void update_position_from_orbit();

    // The orbit fields (azimuth, elevation, distance, target, up_axis) may be
    // assigned directly.  This applies such assignments: when any of them changed
    // since the camera last placed itself, and `position` was not assigned
    // directly in the meantime, the position is recomputed from the orbit.
    // Renderers call it once per frame; returns true if the position moved.
    bool apply_orbit_changes();

    // Recompute azimuth/elevation/distance from the current position and target.
    void sync_orbit_from_position();

    std::string serialize() const;
    void        deserialize(const std::string& json);

   private:
    // Orbit state as of the last time the camera derived one from the other.
    struct OrbitSnapshot
    {
        vec3   position{3.0618622f, 3.0618622f, 2.5f};
        vec3   target{0.0f, 0.0f, 0.0f};
        float  azimuth   = 45.0f;
        float  elevation = 30.0f;
        float  distance  = 5.0f;
        UpAxis up_axis   = UpAxis::Z;
    };
    OrbitSnapshot applied_;
    void          remember_orbit();
};

}   // namespace spectra
