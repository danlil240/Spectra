#include "ui/scene_viewport.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <spectra/series.hpp>

#include "scene/scene_manager.hpp"

#ifdef SPECTRA_USE_IMGUI
#include <imgui.h>
#endif

namespace spectra::adapters::ros2
{

SceneViewport::SceneViewport()
{
    camera_.reset();
}

void SceneViewport::set_camera(const spectra::Camera& camera)
{
    camera_ = camera;
    camera_initialized_ = true;
}

spectra::Rect SceneViewport::canvas_rect() const
{
    return {canvas_x_, canvas_y_, canvas_w_, canvas_h_};
}

#ifdef SPECTRA_USE_IMGUI
namespace
{
struct SceneBounds
{
    spectra::vec3 min{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
    };
    spectra::vec3 max{
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };
    bool valid{false};

    void include(const spectra::vec3& point)
    {
        min = spectra::vec3_min(min, point);
        max = spectra::vec3_max(max, point);
        valid = true;
    }
};

struct CameraBasis
{
    spectra::vec3 forward;
    spectra::vec3 right;
    spectra::vec3 up;
};

struct SceneCanvasResult
{
    bool clicked{false};
    std::optional<size_t> picked_index;
};

CameraBasis make_camera_basis(const spectra::Camera& camera)
{
    spectra::vec3 forward = spectra::vec3_normalize(camera.target - camera.position);
    if (spectra::vec3_length_sq(forward) < 1e-12)
        forward = {0.0, 0.0, -1.0};

    spectra::vec3 right = spectra::vec3_normalize(spectra::vec3_cross(forward, camera.up));
    if (spectra::vec3_length_sq(right) < 1e-12)
        right = {1.0, 0.0, 0.0};

    spectra::vec3 up = spectra::vec3_normalize(spectra::vec3_cross(right, forward));
    if (spectra::vec3_length_sq(up) < 1e-12)
        up = {0.0, 1.0, 0.0};

    return {forward, right, up};
}

ImU32 color_from_rgba(const std::array<float, 4>& rgba)
{
    return IM_COL32(
        static_cast<int>(std::clamp(rgba[0], 0.0f, 1.0f) * 255.0f),
        static_cast<int>(std::clamp(rgba[1], 0.0f, 1.0f) * 255.0f),
        static_cast<int>(std::clamp(rgba[2], 0.0f, 1.0f) * 255.0f),
        static_cast<int>(std::clamp(rgba[3], 0.0f, 1.0f) * 255.0f));
}

ImU32 entity_color(const SceneEntity& entity, bool selected)
{
    if (selected)
        return IM_COL32(255, 220, 90, 255);
    if (entity.type == "path")
        return IM_COL32(80, 200, 255, 255);
    if (entity.type == "pose")
        return IM_COL32(255, 140, 80, 255);
    if (entity.type == "image")
        return IM_COL32(150, 235, 120, 255);
    if (entity.type == "tf_frame")
        return IM_COL32(210, 210, 210, 255);
    if (entity.type == "grid")
        return IM_COL32(120, 120, 120, 255);
    return IM_COL32(180, 180, 255, 255);
}

void include_entity_bounds(const SceneEntity& entity, SceneBounds& bounds)
{
    bounds.include(entity.transform.translation);

    if (entity.polyline.has_value())
    {
        for (const auto& point : entity.polyline->points)
            bounds.include(entity.transform.transform_point(point));
    }

    if (entity.arrow.has_value())
    {
        const auto& arrow = *entity.arrow;
        const spectra::vec3 origin = entity.transform.transform_point(arrow.origin);
        const spectra::vec3 direction = spectra::vec3_normalize(
            entity.transform.transform_vector(arrow.direction));
        const spectra::vec3 tip = origin + direction * arrow.shaft_length;
        bounds.include(origin);
        bounds.include(tip);
    }

    if (entity.billboard.has_value())
    {
        const double half_w = entity.billboard->width * 0.5;
        const double half_h = entity.billboard->height * 0.5;
        bounds.include(entity.transform.translation + spectra::vec3{-half_w, -half_h, 0.0});
        bounds.include(entity.transform.translation + spectra::vec3{half_w, half_h, 0.0});
    }
}

bool fit_camera_to_bounds(const SceneBounds& bounds, spectra::Camera& camera)
{
    if (!bounds.valid)
        return false;

    camera.fit_to_bounds(bounds.min, bounds.max);
    return true;
}

bool fit_camera_to_scene(const SceneManager& scene, spectra::Camera& camera)
{
    SceneBounds bounds;
    for (const auto& entity : scene.entities())
        include_entity_bounds(entity, bounds);
    return fit_camera_to_bounds(bounds, camera);
}

bool project_point(const spectra::Camera& camera,
                   const spectra::vec3& point,
                   const ImVec2& origin,
                   const ImVec2& size,
                   ImVec2& out,
                   float* depth_out = nullptr)
{
    if (size.x <= 8.0f || size.y <= 8.0f)
        return false;

    const CameraBasis basis = make_camera_basis(camera);
    const spectra::vec3 rel = point - camera.position;
    const double x = spectra::vec3_dot(rel, basis.right);
    const double y = spectra::vec3_dot(rel, basis.up);
    const double z = spectra::vec3_dot(rel, basis.forward);

    if (depth_out != nullptr)
        *depth_out = static_cast<float>(z);

    const float aspect = std::max(1e-3f, size.x / size.y);
    float ndc_x = 0.0f;
    float ndc_y = 0.0f;

    if (camera.projection_mode == spectra::Camera::ProjectionMode::Perspective)
    {
        if (z <= static_cast<double>(camera.near_clip))
            return false;
        const double tan_half_fov = std::tan(spectra::deg_to_rad(camera.fov) * 0.5f);
        const double denom_x = z * tan_half_fov * aspect;
        const double denom_y = z * tan_half_fov;
        if (std::abs(denom_x) < 1e-9 || std::abs(denom_y) < 1e-9)
            return false;
        ndc_x = static_cast<float>(x / denom_x);
        ndc_y = static_cast<float>(y / denom_y);
    }
    else
    {
        const double half_h = std::max(0.1f, camera.ortho_size);
        const double half_w = half_h * aspect;
        ndc_x = static_cast<float>(x / half_w);
        ndc_y = static_cast<float>(y / half_h);
    }

    out = {
        origin.x + (ndc_x * 0.5f + 0.5f) * size.x,
        origin.y + (0.5f - ndc_y * 0.5f) * size.y,
    };
    return true;
}

spectra::Ray build_pick_ray(const spectra::Camera& camera,
                            const ImVec2& mouse,
                            const ImVec2& origin,
                            const ImVec2& size)
{
    const float nx = size.x <= 1e-3f ? 0.0f : ((mouse.x - origin.x) / size.x) * 2.0f - 1.0f;
    const float ny = size.y <= 1e-3f ? 0.0f : 1.0f - ((mouse.y - origin.y) / size.y) * 2.0f;

    const CameraBasis basis = make_camera_basis(camera);
    const float aspect = std::max(1e-3f, size.x / size.y);

    if (camera.projection_mode == spectra::Camera::ProjectionMode::Perspective)
    {
        const float tan_half_fov = std::tan(spectra::deg_to_rad(camera.fov) * 0.5f);
        const spectra::vec3 direction = spectra::vec3_normalize(
            basis.forward
            + basis.right * (nx * tan_half_fov * aspect)
            + basis.up * (ny * tan_half_fov));
        return {camera.position, direction};
    }

    const spectra::vec3 origin_offset =
        basis.right * (nx * camera.ortho_size * aspect)
        + basis.up * (ny * camera.ortho_size);
    return {camera.position + origin_offset, basis.forward};
}

SceneCanvasResult draw_scene_canvas(SceneManager& scene,
                                    spectra::Camera& camera,
                                    const std::array<float, 4>& background_rgba,
                                    bool& camera_initialized,
                                    const ImVec2& size)
{
    SceneCanvasResult result;
    if (size.x <= 8.0f || size.y <= 8.0f)
        return result;

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##scene_canvas", size);
    result.clicked = ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool hovered = ImGui::IsItemHovered();
    const ImGuiIO& io = ImGui::GetIO();

    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        camera.orbit(-io.MouseDelta.x * 0.35f, -io.MouseDelta.y * 0.35f);
        camera_initialized = true;
    }
    if (hovered
        && (ImGui::IsMouseDragging(ImGuiMouseButton_Right)
            || ImGui::IsMouseDragging(ImGuiMouseButton_Middle)))
    {
        camera.pan(io.MouseDelta.x, io.MouseDelta.y, size.x, size.y);
        camera_initialized = true;
    }
    if (hovered && std::abs(io.MouseWheel) > 1e-5f)
    {
        camera.zoom(std::pow(0.9f, io.MouseWheel));
        camera_initialized = true;
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(origin,
                             ImVec2(origin.x + size.x, origin.y + size.y),
                             color_from_rgba(background_rgba),
                             6.0f);
    draw_list->AddRect(origin,
                       ImVec2(origin.x + size.x, origin.y + size.y),
                       IM_COL32(75, 82, 92, 255),
                       6.0f);

    SceneBounds bounds;
    for (const auto& entity : scene.entities())
        include_entity_bounds(entity, bounds);

    if (!camera_initialized && bounds.valid)
        camera_initialized = fit_camera_to_bounds(bounds, camera);

    if (!bounds.valid)
    {
        draw_list->AddText(ImVec2(origin.x + 12.0f, origin.y + 12.0f),
                           IM_COL32(170, 170, 170, 255),
                           "No geometry to preview yet.");
        return result;
    }

    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        fit_camera_to_bounds(bounds, camera);
        camera_initialized = true;
    }

    const double max_extent = std::max({bounds.max.x - bounds.min.x,
                                        bounds.max.y - bounds.min.y,
                                        bounds.max.z - bounds.min.z,
                                        1.0});
    const float axis_len = static_cast<float>(std::max(0.5, max_extent * 0.25));
    ImVec2 axis_origin{};
    ImVec2 axis_x{};
    ImVec2 axis_y{};
    ImVec2 axis_z{};
    if (project_point(camera, {0.0, 0.0, 0.0}, origin, size, axis_origin)
        && project_point(camera, {axis_len, 0.0, 0.0}, origin, size, axis_x)
        && project_point(camera, {0.0, axis_len, 0.0}, origin, size, axis_y)
        && project_point(camera, {0.0, 0.0, axis_len}, origin, size, axis_z))
    {
        draw_list->AddLine(axis_origin, axis_x, IM_COL32(235, 90, 90, 220), 2.0f);
        draw_list->AddLine(axis_origin, axis_y, IM_COL32(90, 220, 120, 220), 2.0f);
        draw_list->AddLine(axis_origin, axis_z, IM_COL32(100, 150, 255, 220), 2.0f);
    }

    const auto selected_index = scene.selected_index();

    for (size_t i = 0; i < scene.entities().size(); ++i)
    {
        const auto& entity = scene.entities()[i];
        const bool selected = selected_index.has_value() && *selected_index == i;
        const ImU32 color = entity_color(entity, selected);
        const float thickness = selected ? 3.0f : 2.0f;

        ImVec2 center{};
        const bool center_visible =
            project_point(camera, entity.transform.translation, origin, size, center);

        if (entity.polyline.has_value() && entity.polyline->points.size() >= 2)
        {
            for (size_t p = 1; p < entity.polyline->points.size(); ++p)
            {
                ImVec2 a{};
                ImVec2 b{};
                const bool a_ok = project_point(
                    camera,
                    entity.transform.transform_point(entity.polyline->points[p - 1]),
                    origin,
                    size,
                    a);
                const bool b_ok = project_point(
                    camera,
                    entity.transform.transform_point(entity.polyline->points[p]),
                    origin,
                    size,
                    b);
                if (!a_ok || !b_ok)
                    continue;
                draw_list->AddLine(a, b, color, thickness);
            }
            if (center_visible)
                draw_list->AddCircleFilled(center, selected ? 5.0f : 4.0f, color);
        }
        else if (entity.arrow.has_value())
        {
            const auto& arrow = *entity.arrow;
            const spectra::vec3 world_origin = entity.transform.transform_point(arrow.origin);
            const spectra::vec3 world_dir = spectra::vec3_normalize(
                entity.transform.transform_vector(arrow.direction));
            const spectra::vec3 world_tip = world_origin + world_dir * arrow.shaft_length;
            ImVec2 shaft_a{};
            ImVec2 shaft_b{};
            if (!project_point(camera, world_origin, origin, size, shaft_a)
                || !project_point(camera, world_tip, origin, size, shaft_b))
            {
                continue;
            }
            draw_list->AddLine(shaft_a, shaft_b, color, thickness);

            const ImVec2 dir2d{shaft_b.x - shaft_a.x, shaft_b.y - shaft_a.y};
            const float len = std::sqrt(dir2d.x * dir2d.x + dir2d.y * dir2d.y);
            if (len > 1e-3f)
            {
                const ImVec2 unit{dir2d.x / len, dir2d.y / len};
                const ImVec2 perp{-unit.y, unit.x};
                const float head_len = static_cast<float>(std::max(8.0, arrow.head_length * 20.0));
                const float head_w = static_cast<float>(std::max(5.0, arrow.head_width * 16.0));
                const ImVec2 left{
                    shaft_b.x - unit.x * head_len + perp.x * head_w,
                    shaft_b.y - unit.y * head_len + perp.y * head_w,
                };
                const ImVec2 right{
                    shaft_b.x - unit.x * head_len - perp.x * head_w,
                    shaft_b.y - unit.y * head_len - perp.y * head_w,
                };
                draw_list->AddLine(shaft_b, left, color, thickness);
                draw_list->AddLine(shaft_b, right, color, thickness);
            }
        }
        else if (entity.billboard.has_value())
        {
            if (!center_visible)
                continue;
            const float half_w = static_cast<float>(entity.billboard->width * 16.0);
            const float half_h = static_cast<float>(entity.billboard->height * 16.0);
            draw_list->AddRect(ImVec2(center.x - half_w, center.y - half_h),
                               ImVec2(center.x + half_w, center.y + half_h),
                               color,
                               2.0f,
                               0,
                               thickness);
            draw_list->AddLine(ImVec2(center.x - half_w, center.y - half_h),
                               ImVec2(center.x + half_w, center.y + half_h),
                               color,
                               1.0f);
        }
        else
        {
            if (center_visible)
                draw_list->AddCircleFilled(center, selected ? 5.0f : 4.0f, color);
        }

        if (center_visible)
            draw_list->AddText(ImVec2(center.x + 6.0f, center.y + 4.0f), color, entity.label.c_str());
    }

    if (!result.clicked)
        return result;

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    result.picked_index = scene.pick(build_pick_ray(camera, mouse, origin, size));
    return result;
}
}   // namespace
#endif

void SceneViewport::draw(bool* p_open,
                         const std::string& fixed_frame,
                         size_t display_count,
                         SceneManager& scene)
{
#ifdef SPECTRA_USE_IMGUI
    if (!ImGui::GetCurrentContext())
        return;
    if (!ImGui::Begin(title_.c_str(), p_open))
    {
        ImGui::End();
        return;
    }

    ImGui::Text("Fixed frame: %s", fixed_frame.empty() ? "(unset)" : fixed_frame.c_str());
    ImGui::Text("Displays: %zu | Scene entities: %zu", display_count, scene.entity_count());
    if (ImGui::Button("Reset View"))
    {
        camera_.reset();
        camera_initialized_ = fit_camera_to_scene(scene, camera_);
    }
    ImGui::SameLine();
    if (ImGui::Button("Fit View"))
        camera_initialized_ = fit_camera_to_scene(scene, camera_);
    ImGui::SameLine();
    int projection = camera_.projection_mode == spectra::Camera::ProjectionMode::Perspective ? 0 : 1;
    if (ImGui::RadioButton("Perspective", projection == 0))
        camera_.set_projection(spectra::Camera::ProjectionMode::Perspective);
    ImGui::SameLine();
    if (ImGui::RadioButton("Ortho", projection == 1))
        camera_.set_projection(spectra::Camera::ProjectionMode::Orthographic);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    ImGui::ColorEdit4("Background",
                      background_rgba_.data(),
                      ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreviewHalf);
    ImGui::Text("Camera: az %.1f | el %.1f | dist %.2f",
                camera_.azimuth,
                camera_.elevation,
                camera_.distance);
    ImGui::TextDisabled("Drag LMB to orbit, RMB/MMB to pan, wheel to zoom, double-click to fit.");
    ImGui::Separator();

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float canvas_height = std::max(140.0f, avail.y * 0.45f);

    // Record screen-space canvas rect for GPU scene rendering.
    {
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        canvas_x_ = cursor.x;
        canvas_y_ = cursor.y;
        canvas_w_ = avail.x;
        canvas_h_ = canvas_height;
    }

    const SceneCanvasResult interaction = draw_scene_canvas(scene,
                                                            camera_,
                                                            background_rgba_,
                                                            camera_initialized_,
                                                            ImVec2(avail.x, canvas_height));
    if (interaction.picked_index.has_value())
        scene.set_selected_index(*interaction.picked_index);
    else if (interaction.clicked)
        scene.clear_selection();

    ImGui::Separator();
    const float list_height = std::max(90.0f, avail.y * 0.20f);
    if (ImGui::BeginChild("##scene_entities", ImVec2(0.0f, list_height), true))
    {
        const auto selected_index = scene.selected_index();
        if (scene.entities().empty())
        {
            ImGui::TextDisabled("No scene entities submitted.");
        }
        else
        {
            for (size_t i = 0; i < scene.entities().size(); ++i)
            {
                const auto& entity = scene.entities()[i];
                const bool selected = selected_index.has_value() && *selected_index == i;
                std::string label = entity.label.empty() ? entity.type : entity.label;
                label += "##entity_" + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), selected))
                    scene.set_selected_index(i);
            }
        }
    }
    ImGui::EndChild();

    ImGui::Separator();
    if (const SceneEntity* entity = scene.selected_entity())
    {
        ImGui::Text("Selected: %s",
                    entity->label.empty() ? entity->type.c_str() : entity->label.c_str());
        if (ImGui::Button("Clear Selection##scene_viewport"))
            scene.clear_selection();
        ImGui::SameLine();
        ImGui::TextDisabled("Open Inspector for details.");
    }
    else
    {
        ImGui::TextDisabled("Select an entity, then open Inspector for details.");
    }
    ImGui::End();
#else
    (void)p_open;
    (void)fixed_frame;
    (void)display_count;
    (void)scene;
#endif
}

}   // namespace spectra::adapters::ros2
