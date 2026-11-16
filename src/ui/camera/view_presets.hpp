#pragma once

// Camera moves for the standard 3D views (front / back / left / right / top /
// bottom / isometric) and the perspective <-> orthographic toggle.  Pure
// functions shared by the view commands, the view cube and the unit tests;
// the input handler animates the camera to the result and records undo.

#include <algorithm>
#include <cmath>
#include <spectra/camera.hpp>

namespace spectra
{

struct ViewPresetPlan
{
    Camera target;   // camera at the end of the move (orbit already applied)
    // The six axis views switch to orthographic so ticks of the two visible
    // axes stay evenly spaced (no perspective foreshortening).  auto_ortho
    // records that the switch was automatic: the next orbit drag returns to
    // perspective.  An orthographic projection the user chose is kept.
    bool auto_ortho = false;
};

// Perspective distance that shows the target plane at the size the
// orthographic camera `cam` does (inverse of perspective_matched_ortho_size).
inline float perspective_distance_for_ortho(const Camera& cam)
{
    const float t = std::tan(deg_to_rad(cam.fov) * 0.5f);
    if (!(t > 1e-6f) || !(cam.ortho_size > 0.0f))
        return cam.distance;
    return std::clamp(cam.ortho_size / t, 0.1f, 10000.0f);
}

// Plan the move from `from` to `preset`.  `from_auto_ortho` tells whether an
// orthographic `from` came from an earlier preset (true) or from the user.
inline ViewPresetPlan plan_view_preset(const Camera&      from,
                                       Camera::ViewPreset preset,
                                       bool               from_auto_ortho)
{
    ViewPresetPlan plan;
    plan.target = from;
    plan.target.set_view_preset(preset);

    // Take the shorter way round so an animation never spins past 180 deg.
    const float delta     = std::remainder(plan.target.azimuth - from.azimuth, 360.0f);
    plan.target.azimuth   = from.azimuth + delta;
    const bool from_ortho = from.projection_mode == Camera::ProjectionMode::Orthographic;
    const bool user_ortho = from_ortho && !from_auto_ortho;
    if (!user_ortho)
    {
        if (Camera::is_axis_view(preset))
        {
            plan.target.projection_mode = Camera::ProjectionMode::Orthographic;
            // Keep the zoom of an orthographic camera; otherwise match the
            // perspective framing so the switch does not jump.
            if (!from_ortho)
                plan.target.ortho_size = from.perspective_matched_ortho_size();
            plan.auto_ortho = true;
        }
        else
        {
            plan.target.projection_mode = Camera::ProjectionMode::Perspective;
            // Keep the orthographic zoom (ortho_size) in perspective.
            if (from_ortho)
                plan.target.distance = perspective_distance_for_ortho(from);
        }
    }
    plan.target.update_position_from_orbit();
    return plan;
}

// The camera with its projection switched (perspective <-> orthographic),
// keeping the framing of the plane through the target.
inline Camera toggled_projection(const Camera& cam)
{
    Camera out = cam;
    if (cam.projection_mode == Camera::ProjectionMode::Perspective)
    {
        out.projection_mode = Camera::ProjectionMode::Orthographic;
        out.ortho_size      = cam.perspective_matched_ortho_size();
    }
    else
    {
        // Orthographic zoom changes ortho_size only: move the camera so the
        // perspective view keeps that zoom.
        out.projection_mode = Camera::ProjectionMode::Perspective;
        out.distance        = perspective_distance_for_ortho(cam);
        out.update_position_from_orbit();
    }
    return out;
}

// Command id / palette label / default shortcut of each preset.
struct ViewPresetInfo
{
    Camera::ViewPreset preset;
    const char*        command_id;
    const char*        label;
    const char*        shortcut;
};

inline constexpr ViewPresetInfo VIEW_PRESETS[] = {
    {Camera::ViewPreset::Front, "view.3d_front", "3D View: Front", "Alt+1"},
    {Camera::ViewPreset::Back, "view.3d_back", "3D View: Back", "Alt+2"},
    {Camera::ViewPreset::Left, "view.3d_left", "3D View: Left", "Alt+3"},
    {Camera::ViewPreset::Right, "view.3d_right", "3D View: Right", "Alt+4"},
    {Camera::ViewPreset::Top, "view.3d_top", "3D View: Top", "Alt+5"},
    {Camera::ViewPreset::Bottom, "view.3d_bottom", "3D View: Bottom", "Alt+6"},
    {Camera::ViewPreset::Isometric, "view.3d_iso", "3D View: Isometric", "Alt+7"},
};

}   // namespace spectra
