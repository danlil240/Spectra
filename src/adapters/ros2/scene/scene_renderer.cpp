#include "scene/scene_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <spectra/camera.hpp>
#include <spectra/math3d.hpp>
#include <spectra/series.hpp>

#include "render/renderer.hpp"
#include "render/text_renderer.hpp"
#include "scene/mesh_primitives.hpp"
#include "scene/scene_manager.hpp"

namespace spectra::adapters::ros2
{

// ──────────────────────────────────────────────────────────────────────
// Internal helpers
// ──────────────────────────────────────────────────────────────────────

namespace
{
struct GpuPointData
{
    float x;
    float y;
    float z;
    uint32_t rgba;
};

void upload_primitive(Backend& backend,
                      const PrimitiveMesh& mesh,
                      BufferHandle& vbo,
                      BufferHandle& ibo,
                      uint32_t& index_count)
{
    if (!vbo)
        vbo = backend.create_buffer(BufferUsage::Vertex, mesh.vertex_bytes());
    backend.upload_buffer(vbo, mesh.vertices.data(), mesh.vertex_bytes());

    if (!ibo)
        ibo = backend.create_buffer(BufferUsage::Index, mesh.index_bytes());
    backend.upload_buffer(ibo, mesh.indices.data(), mesh.index_bytes());
    index_count = static_cast<uint32_t>(mesh.index_count());
}

void build_model_matrix(const Transform& transform,
                        const vec3& scale,
                        float* out_mat4)
{
    mat4 m = transform.to_mat4();

    // Apply scale to each column
    m.m[0] *= static_cast<float>(scale.x);
    m.m[1] *= static_cast<float>(scale.x);
    m.m[2] *= static_cast<float>(scale.x);
    m.m[4] *= static_cast<float>(scale.y);
    m.m[5] *= static_cast<float>(scale.y);
    m.m[6] *= static_cast<float>(scale.y);
    m.m[8]  *= static_cast<float>(scale.z);
    m.m[9]  *= static_cast<float>(scale.z);
    m.m[10] *= static_cast<float>(scale.z);

    std::memcpy(out_mat4, m.m, sizeof(float) * 16);
}

std::string entity_property(const SceneEntity& entity,
                            const std::string& key,
                            const std::string& fallback = {})
{
    for (const auto& prop : entity.properties)
    {
        if (prop.key == key)
            return prop.value;
    }
    return fallback;
}

float entity_property_float(const SceneEntity& entity,
                            const std::string& key,
                            float              fallback)
{
    const std::string value = entity_property(entity, key, "");
    if (value.empty())
        return fallback;

    float parsed = fallback;
    if (std::sscanf(value.c_str(), "%f", &parsed) == 1)
        return parsed;
    return fallback;
}

void parse_color_string(const std::string& color_str,
                        float*             rgba_out)
{
    if (color_str.empty())
        return;
    std::sscanf(color_str.c_str(), "%f, %f, %f, %f",
                &rgba_out[0], &rgba_out[1], &rgba_out[2], &rgba_out[3]);
}

void unpack_rgba(uint32_t rgba, float* rgba_out)
{
    rgba_out[0] = static_cast<float>((rgba >> 0) & 0xFFu) / 255.0f;
    rgba_out[1] = static_cast<float>((rgba >> 8) & 0xFFu) / 255.0f;
    rgba_out[2] = static_cast<float>((rgba >> 16) & 0xFFu) / 255.0f;
    rgba_out[3] = static_cast<float>((rgba >> 24) & 0xFFu) / 255.0f;
}

void ensure_storage_capacity(Backend&     backend,
                             BufferHandle& buffer,
                             size_t&       capacity,
                             size_t        required_bytes)
{
    if (required_bytes <= capacity && buffer)
        return;
    if (buffer)
        backend.destroy_buffer(buffer);
    buffer = backend.create_buffer(BufferUsage::Storage, required_bytes);
    capacity = required_bytes;
}

}   // namespace

// ──────────────────────────────────────────────────────────────────────
// Pipeline / buffer lazy init
// ──────────────────────────────────────────────────────────────────────

void SceneRenderer::ensure_pipelines(Backend& backend)
{
    if (pipelines_ready_)
        return;

    grid3d_pipeline_   = backend.create_pipeline(PipelineType::Grid3D);
    marker3d_pipeline_ = backend.create_pipeline(PipelineType::Marker3D);
    line3d_pipeline_   = backend.create_pipeline(PipelineType::Line3D);
    line3d_transparent_pipeline_ = backend.create_pipeline(PipelineType::Line3D_Transparent);
    pointcloud_pipeline_ = backend.create_pipeline(PipelineType::PointCloud);
    pointcloud_transparent_pipeline_ = backend.create_pipeline(PipelineType::PointCloud_Transparent);
    image3d_pipeline_  = backend.create_pipeline(PipelineType::Image3D);
    frame_ubo_         = backend.create_buffer(BufferUsage::Uniform, sizeof(FrameUBO));
    pipelines_ready_   = true;
}

// ──────────────────────────────────────────────────────────────────────
// Main render dispatch
// ──────────────────────────────────────────────────────────────────────

void SceneRenderer::render(Renderer& renderer,
                           SceneManager& scene,
                           const Camera& camera,
                           const Rect& viewport)
{
    auto& backend = renderer.backend();

    const auto& entities = scene.entities();
    if (entities.empty())
        return;

    ensure_pipelines(backend);

    // Lazily upload primitive mesh buffers
    if (!gpu_.initialized)
    {
        auto cube     = generate_cube();
        auto sphere   = generate_sphere(16, 24);
        auto cylinder = generate_cylinder(24);
        auto arrow    = generate_arrow(16);
        auto cone     = generate_cone(24);

        upload_primitive(backend, cube,     gpu_.cube_vbo,     gpu_.cube_ibo,     gpu_.cube_index_count);
        upload_primitive(backend, sphere,   gpu_.sphere_vbo,   gpu_.sphere_ibo,   gpu_.sphere_index_count);
        upload_primitive(backend, cylinder, gpu_.cylinder_vbo, gpu_.cylinder_ibo, gpu_.cylinder_index_count);
        upload_primitive(backend, arrow,    gpu_.arrow_vbo,    gpu_.arrow_ibo,    gpu_.arrow_index_count);
        upload_primitive(backend, cone,     gpu_.cone_vbo,     gpu_.cone_ibo,     gpu_.cone_index_count);
        gpu_.initialized = true;
    }

    // Set viewport and scissor
    backend.set_viewport(viewport.x, viewport.y, viewport.w, viewport.h);
    backend.set_scissor(static_cast<int32_t>(viewport.x),
                        static_cast<int32_t>(viewport.y),
                        static_cast<uint32_t>(viewport.w),
                        static_cast<uint32_t>(viewport.h));

    // Camera matrices
    mat4 proj_mat = camera.projection_matrix(viewport.w / std::max(1.0f, viewport.h));
    mat4 view_m   = camera.view_matrix();
    const float* proj     = proj_mat.m;
    const float* view_mat = view_m.m;

    auto set_camera_pos = [&](FrameUBO& ubo)
    {
        ubo.camera_pos[0] = static_cast<float>(camera.position.x);
        ubo.camera_pos[1] = static_cast<float>(camera.position.y);
        ubo.camera_pos[2] = static_cast<float>(camera.position.z);
    };

    float default_light[3] = {0.5f, 0.7f, 1.0f};

    // ── Pass 1: grid entities ──
    for (const auto& entity : entities)
    {
        if (entity.type != "grid")
            continue;

        const float cell_size = static_cast<float>(std::max(0.05, std::abs(entity.scale.x)));
        const int cell_count  = static_cast<int>(std::max(1.0, std::abs(entity.scale.z)));
        const std::string plane = entity_property(entity, "plane", "xz");

        if (cell_size != gpu_.grid_cell_size || cell_count != gpu_.grid_cell_count)
        {
            const float half = cell_size * static_cast<float>(cell_count) * 0.5f;
            std::vector<float> verts;
            verts.reserve(static_cast<size_t>((cell_count + 1) * 2 * 2 * 3));

            for (int i = 0; i <= cell_count; ++i)
            {
                float t = -half + cell_size * static_cast<float>(i);
                if (plane == "xy")
                {
                    verts.push_back(-half); verts.push_back(t);     verts.push_back(0.0f);
                    verts.push_back(half);  verts.push_back(t);     verts.push_back(0.0f);
                    verts.push_back(t);     verts.push_back(-half); verts.push_back(0.0f);
                    verts.push_back(t);     verts.push_back(half);  verts.push_back(0.0f);
                }
                else if (plane == "yz")
                {
                    verts.push_back(0.0f); verts.push_back(-half); verts.push_back(t);
                    verts.push_back(0.0f); verts.push_back(half);  verts.push_back(t);
                    verts.push_back(0.0f); verts.push_back(t);     verts.push_back(-half);
                    verts.push_back(0.0f); verts.push_back(t);     verts.push_back(half);
                }
                else   // xz
                {
                    verts.push_back(-half); verts.push_back(0.0f); verts.push_back(t);
                    verts.push_back(half);  verts.push_back(0.0f); verts.push_back(t);
                    verts.push_back(t);     verts.push_back(0.0f); verts.push_back(-half);
                    verts.push_back(t);     verts.push_back(0.0f); verts.push_back(half);
                }
            }

            const size_t bytes = verts.size() * sizeof(float);
            if (bytes > gpu_.grid_capacity)
            {
                if (gpu_.grid_vbo)
                    backend.destroy_buffer(gpu_.grid_vbo);
                gpu_.grid_vbo      = backend.create_buffer(BufferUsage::Vertex, bytes);
                gpu_.grid_capacity = bytes;
            }
            backend.upload_buffer(gpu_.grid_vbo, verts.data(), bytes);
            gpu_.grid_vertex_count = static_cast<uint32_t>(verts.size() / 3);
            gpu_.grid_cell_size    = cell_size;
            gpu_.grid_cell_count   = cell_count;
        }

        if (!gpu_.grid_vbo || gpu_.grid_vertex_count == 0)
            continue;

        FrameUBO ubo{};
        std::memcpy(ubo.projection, proj, sizeof(float) * 16);
        std::memcpy(ubo.view, view_mat, sizeof(float) * 16);
        ubo.model[0] = 1.0f; ubo.model[5] = 1.0f; ubo.model[10] = 1.0f; ubo.model[15] = 1.0f;
        ubo.model[12] = static_cast<float>(entity.transform.translation.x);
        ubo.model[13] = static_cast<float>(entity.transform.translation.y);
        ubo.model[14] = static_cast<float>(entity.transform.translation.z);
        ubo.viewport_width  = viewport.w;
        ubo.viewport_height = viewport.h;
        set_camera_pos(ubo);
        std::memcpy(ubo.light_dir, default_light, sizeof(float) * 3);
        backend.upload_buffer(frame_ubo_, &ubo, sizeof(FrameUBO));

        SeriesPushConstants pc{};
        const std::string grid_color_str = entity_property(entity, "color", "");
        if (!grid_color_str.empty())
        {
            std::sscanf(grid_color_str.c_str(), "%f, %f, %f, %f",
                        &pc.color[0], &pc.color[1], &pc.color[2], &pc.color[3]);
        }
        else
        {
            pc.color[0] = 0.45f; pc.color[1] = 0.45f; pc.color[2] = 0.45f; pc.color[3] = 0.6f;
        }
        pc.opacity = pc.color[3];

        backend.bind_pipeline(grid3d_pipeline_);
        backend.bind_buffer(frame_ubo_, 0);
        backend.bind_buffer(gpu_.grid_vbo, 0);
        backend.push_constants(pc);
        backend.draw(gpu_.grid_vertex_count);
    }

    // ── Pass 2: opaque marker / mesh entities ──
    backend.bind_pipeline(marker3d_pipeline_);

    for (size_t i = 0; i < entities.size(); ++i)
    {
        const auto& entity = entities[i];
        bool is_selected = scene.selected_index().has_value() && *scene.selected_index() == i;

        BufferHandle vbo;
        BufferHandle ibo;
        uint32_t     idx_count = 0;

        // Skip entities rendered by later passes (points, polylines, text, images).
        if (entity.polyline.has_value() || entity.point_set.has_value()
            || entity.image.has_value())
            continue;

        if (entity.type == "marker")
        {
            const std::string prim = entity_property(entity, "primitive", "cube");

            // Skip marker types handled elsewhere (text in pass 5).
            if (prim == "text_view_facing")
                continue;

            if (prim == "cube")
            {
                vbo = gpu_.cube_vbo; ibo = gpu_.cube_ibo; idx_count = gpu_.cube_index_count;
            }
            else if (prim == "sphere")
            {
                vbo = gpu_.sphere_vbo; ibo = gpu_.sphere_ibo; idx_count = gpu_.sphere_index_count;
            }
            else if (prim == "cylinder")
            {
                vbo = gpu_.cylinder_vbo; ibo = gpu_.cylinder_ibo; idx_count = gpu_.cylinder_index_count;
            }
            else if (prim == "arrow")
            {
                vbo = gpu_.arrow_vbo; ibo = gpu_.arrow_ibo; idx_count = gpu_.arrow_index_count;
            }
            else if (prim == "cone")
            {
                vbo = gpu_.cone_vbo; ibo = gpu_.cone_ibo; idx_count = gpu_.cone_index_count;
            }
            else
            {
                vbo = gpu_.cube_vbo; ibo = gpu_.cube_ibo; idx_count = gpu_.cube_index_count;
            }
        }
        else if (entity.type == "tf_frame")
        {
            const float axis_len = static_cast<float>(std::max(0.05, entity.scale.x));

            struct AxisSpec { float dx, dy, dz, r, g, b; };
            const AxisSpec axes_spec[] = {
                {axis_len, 0.0f, 0.0f, 0.9f, 0.2f, 0.2f},   // X
                {0.0f, axis_len, 0.0f, 0.2f, 0.8f, 0.3f},   // Y
                {0.0f, 0.0f, axis_len, 0.3f, 0.5f, 0.9f},   // Z
            };

            for (const auto& axis : axes_spec)
            {
                Transform arrow_t = entity.transform;

                if (std::abs(axis.dy) > 0.5f)
                {
                    quat rot = {0.0f, 0.0f, 0.7071068f, 0.7071068f};
                    arrow_t.rotation = quat_mul(arrow_t.rotation, rot);
                }
                else if (std::abs(axis.dz) > 0.5f)
                {
                    quat rot = {0.0f, -0.7071068f, 0.0f, 0.7071068f};
                    arrow_t.rotation = quat_mul(arrow_t.rotation, rot);
                }

                vec3 arrow_scale = {axis_len, axis_len, axis_len};

                FrameUBO ubo{};
                std::memcpy(ubo.projection, proj, sizeof(float) * 16);
                std::memcpy(ubo.view, view_mat, sizeof(float) * 16);
                build_model_matrix(arrow_t, arrow_scale, ubo.model);
                ubo.viewport_width  = viewport.w;
                ubo.viewport_height = viewport.h;
                set_camera_pos(ubo);
                std::memcpy(ubo.light_dir, default_light, sizeof(float) * 3);
                backend.upload_buffer(frame_ubo_, &ubo, sizeof(FrameUBO));

                SeriesPushConstants pc{};
                pc.color[0] = is_selected ? 1.0f : axis.r;
                pc.color[1] = is_selected ? 0.9f : axis.g;
                pc.color[2] = is_selected ? 0.3f : axis.b;
                pc.color[3] = 1.0f;
                pc.opacity  = 1.0f;

                backend.bind_buffer(frame_ubo_, 0);
                backend.bind_buffer(gpu_.arrow_vbo, 0);
                backend.bind_index_buffer(gpu_.arrow_ibo);
                backend.push_constants(pc);
                backend.draw_indexed(gpu_.arrow_index_count);
            }
            continue;
        }
        else if (entity.type == "pose")
        {
            vbo = gpu_.arrow_vbo; ibo = gpu_.arrow_ibo; idx_count = gpu_.arrow_index_count;
        }
        else if (entity.type == "robot_box")
        {
            vbo = gpu_.cube_vbo; ibo = gpu_.cube_ibo; idx_count = gpu_.cube_index_count;
        }
        else if (entity.type == "robot_cylinder")
        {
            vbo = gpu_.cylinder_vbo; ibo = gpu_.cylinder_ibo; idx_count = gpu_.cylinder_index_count;
        }
        else if (entity.type == "robot_sphere")
        {
            vbo = gpu_.sphere_vbo; ibo = gpu_.sphere_ibo; idx_count = gpu_.sphere_index_count;
        }
        else
        {
            continue;
        }

        if (!vbo || !ibo || idx_count == 0)
            continue;

        FrameUBO ubo{};
        std::memcpy(ubo.projection, proj, sizeof(float) * 16);
        std::memcpy(ubo.view, view_mat, sizeof(float) * 16);
        build_model_matrix(entity.transform, entity.scale, ubo.model);
        ubo.viewport_width  = viewport.w;
        ubo.viewport_height = viewport.h;
        set_camera_pos(ubo);
        std::memcpy(ubo.light_dir, default_light, sizeof(float) * 3);
        backend.upload_buffer(frame_ubo_, &ubo, sizeof(FrameUBO));

        SeriesPushConstants pc{};
        const std::string color_str = entity_property(entity, "color", "");
        if (!color_str.empty())
        {
            std::sscanf(color_str.c_str(), "%f, %f, %f, %f",
                        &pc.color[0], &pc.color[1], &pc.color[2], &pc.color[3]);
        }
        else
        {
            pc.color[0] = 0.6f; pc.color[1] = 0.6f; pc.color[2] = 0.8f; pc.color[3] = 1.0f;
        }
        if (is_selected)
        {
            pc.color[0] = std::min(1.0f, pc.color[0] + 0.3f);
            pc.color[1] = std::min(1.0f, pc.color[1] + 0.3f);
            pc.color[2] = std::min(1.0f, pc.color[2] + 0.1f);
        }
        pc.opacity = 1.0f;

        backend.bind_buffer(frame_ubo_, 0);
        backend.bind_buffer(vbo, 0);
        backend.bind_index_buffer(ibo);
        backend.push_constants(pc);
        backend.draw_indexed(idx_count);
    }

    // ── Pass 3: point-set entities (point clouds, laser scans in point mode) ──
    for (size_t i = 0; i < entities.size(); ++i)
    {
        const auto& entity = entities[i];
        if (!entity.point_set.has_value() || entity.point_set->points.empty())
            continue;

        const bool is_selected = scene.selected_index().has_value() && *scene.selected_index() == i;
        const auto& point_set = *entity.point_set;

        std::vector<GpuPointData> gpu_points;
        gpu_points.reserve(point_set.points.size());
        for (const auto& point : point_set.points)
        {
            gpu_points.push_back({
                static_cast<float>(point.position.x),
                static_cast<float>(point.position.y),
                static_cast<float>(point.position.z),
                point.rgba,
            });
        }

        const size_t bytes = gpu_points.size() * sizeof(GpuPointData);
        ensure_storage_capacity(backend, gpu_.point_ssbo, gpu_.point_capacity, bytes);
        if (!gpu_.point_ssbo)
            continue;
        backend.upload_buffer(gpu_.point_ssbo, gpu_points.data(), bytes);

        FrameUBO ubo{};
        std::memcpy(ubo.projection, proj, sizeof(float) * 16);
        std::memcpy(ubo.view, view_mat, sizeof(float) * 16);
        build_model_matrix(entity.transform, vec3{1.0, 1.0, 1.0}, ubo.model);
        ubo.viewport_width  = viewport.w;
        ubo.viewport_height = viewport.h;
        set_camera_pos(ubo);
        std::memcpy(ubo.light_dir, default_light, sizeof(float) * 3);
        backend.upload_buffer(frame_ubo_, &ubo, sizeof(FrameUBO));

        SeriesPushConstants pc{};
        unpack_rgba(point_set.default_rgba, pc.color);
        if (is_selected)
        {
            pc.color[0] = std::min(1.0f, pc.color[0] + 0.2f);
            pc.color[1] = std::min(1.0f, pc.color[1] + 0.2f);
            pc.color[2] = std::min(1.0f, pc.color[2] + 0.05f);
        }
        pc.opacity = 1.0f;
        pc.point_size = point_set.point_size + (is_selected ? 1.0f : 0.0f);
        pc.marker_type = point_set.use_per_point_color ? 1u : 0u;

        backend.bind_pipeline(point_set.transparent
                                  ? pointcloud_transparent_pipeline_
                                  : pointcloud_pipeline_);
        backend.bind_buffer(frame_ubo_, 0);
        backend.bind_buffer(gpu_.point_ssbo, 1);
        backend.push_constants(pc);
        backend.draw(static_cast<uint32_t>(gpu_points.size()));
    }

    // ── Pass 4: line-strip entities (paths, polylines) ──
    for (size_t i = 0; i < entities.size(); ++i)
    {
        const auto& entity = entities[i];
        if (!entity.polyline.has_value() || entity.polyline->points.size() < 2)
            continue;

        bool is_selected = scene.selected_index().has_value() && *scene.selected_index() == i;

        const auto& points = entity.polyline->points;
        std::vector<float> line_data;
        line_data.reserve(points.size() * 4);
        for (const auto& pt : points)
        {
            line_data.push_back(static_cast<float>(pt.x));
            line_data.push_back(static_cast<float>(pt.y));
            line_data.push_back(static_cast<float>(pt.z));
            line_data.push_back(0.0f);
        }

        const size_t bytes = line_data.size() * sizeof(float);
        ensure_storage_capacity(backend, gpu_.line_ssbo, gpu_.line_capacity, bytes);
        if (!gpu_.line_ssbo)
            continue;
        backend.upload_buffer(gpu_.line_ssbo, line_data.data(), bytes);

        FrameUBO ubo{};
        std::memcpy(ubo.projection, proj, sizeof(float) * 16);
        std::memcpy(ubo.view, view_mat, sizeof(float) * 16);
        build_model_matrix(entity.transform, vec3{1.0, 1.0, 1.0}, ubo.model);
        ubo.viewport_width  = viewport.w;
        ubo.viewport_height = viewport.h;
        set_camera_pos(ubo);
        std::memcpy(ubo.light_dir, default_light, sizeof(float) * 3);
        backend.upload_buffer(frame_ubo_, &ubo, sizeof(FrameUBO));

        SeriesPushConstants pc{};
        const std::string color_str = entity_property(entity, "color", "");
        if (is_selected)
        {
            pc.color[0] = 1.0f; pc.color[1] = 0.9f; pc.color[2] = 0.3f;
            pc.color[3] = 1.0f;
            pc.opacity = 1.0f;
        }
        else if (entity.type == "path")
        {
            pc.color[0] = 0.31f; pc.color[1] = 0.78f; pc.color[2] = 1.0f;
            pc.color[3] = entity_property_float(entity, "alpha", 1.0f);
            pc.opacity = 1.0f;
        }
        else
        {
            pc.color[0] = 0.7f; pc.color[1] = 0.7f; pc.color[2] = 1.0f;
            pc.color[3] = 1.0f;
            pc.opacity = 1.0f;
        }
        if (!color_str.empty())
        {
            parse_color_string(color_str, pc.color);
            pc.opacity = 1.0f;
        }
        pc.line_width = entity_property_float(entity, "line_width", 2.5f);

        const uint32_t segments = static_cast<uint32_t>(points.size()) - 1;
        backend.bind_pipeline(pc.color[3] < 0.99f
                                  ? line3d_transparent_pipeline_
                                  : line3d_pipeline_);
        backend.bind_buffer(frame_ubo_, 0);
        backend.bind_buffer(gpu_.line_ssbo, 1);
        backend.push_constants(pc);
        backend.draw(segments * 6);
    }

    // ── Pass 5: textured image billboards ──
    for (size_t i = 0; i < entities.size(); ++i)
    {
        const auto& entity = entities[i];
        if (entity.type != "image" || !entity.image.has_value())
            continue;

        const auto& img = *entity.image;
        if (img.width == 0 || img.height == 0)
            continue;

        // Create or update GPU texture.
        const uint64_t tex_key = img.texture_id;
        auto tex_it = image_textures_.find(tex_key);
        if (tex_it == image_textures_.end() || tex_it->second.width != img.width
            || tex_it->second.height != img.height || img.needs_upload)
        {
            if (tex_it != image_textures_.end() && tex_it->second.handle)
                backend.destroy_texture(tex_it->second.handle);

            if (!img.rgba_data.empty())
            {
                TextureHandle th = backend.create_texture(img.width, img.height, img.rgba_data.data());
                image_textures_[tex_key] = {th, img.width, img.height};
                tex_it = image_textures_.find(tex_key);
            }
            else
            {
                continue;
            }
        }

        if (!tex_it->second.handle)
            continue;

        // Build a unit quad in the XY plane: [-0.5, 0.5] with UVs.
        const float aspect =
            img.height > 0
                ? static_cast<float>(img.width) / static_cast<float>(img.height)
                : 1.0f;
        const float hw = aspect * 0.5f;
        const float hh = 0.5f;

        // 6 vertices: 2 triangles, each vertex = {x, y, z, u, v}
        // clang-format off
        const float quad_verts[] = {
            -hw, -hh, 0.0f,  0.0f, 1.0f,   // bottom-left
             hw, -hh, 0.0f,  1.0f, 1.0f,   // bottom-right
             hw,  hh, 0.0f,  1.0f, 0.0f,   // top-right
            -hw, -hh, 0.0f,  0.0f, 1.0f,   // bottom-left
             hw,  hh, 0.0f,  1.0f, 0.0f,   // top-right
            -hw,  hh, 0.0f,  0.0f, 0.0f,   // top-left
        };
        // clang-format on

        if (!gpu_.image_quad_vbo)
            gpu_.image_quad_vbo = backend.create_buffer(BufferUsage::Vertex, sizeof(quad_verts));
        backend.upload_buffer(gpu_.image_quad_vbo, quad_verts, sizeof(quad_verts));

        FrameUBO ubo{};
        std::memcpy(ubo.projection, proj, sizeof(float) * 16);
        std::memcpy(ubo.view, view_mat, sizeof(float) * 16);
        build_model_matrix(entity.transform, entity.scale, ubo.model);
        ubo.viewport_width  = viewport.w;
        ubo.viewport_height = viewport.h;
        set_camera_pos(ubo);
        std::memcpy(ubo.light_dir, default_light, sizeof(float) * 3);
        backend.upload_buffer(frame_ubo_, &ubo, sizeof(FrameUBO));

        SeriesPushConstants pc{};
        pc.color[0] = 1.0f;
        pc.color[1] = 1.0f;
        pc.color[2] = 1.0f;
        pc.color[3] = 1.0f;
        pc.opacity  = 1.0f;

        backend.bind_pipeline(image3d_pipeline_);
        backend.bind_buffer(frame_ubo_, 0);
        backend.bind_buffer(gpu_.image_quad_vbo, 0);
        backend.bind_texture(tex_it->second.handle, 0);
        backend.push_constants(pc);
        backend.draw(6);
    }

    // ── Pass 6: depth-tested text labels (TF frame names, TEXT_VIEW_FACING markers) ──
    auto& text_renderer = renderer.text_renderer();
    if (text_renderer.is_initialized())
    {
        const mat4 vp = mat4_mul(proj_mat, view_m);

        for (const auto& entity : entities)
        {
            std::string label_str;
            uint32_t label_color = 0xFFDDDDDDu;   // default: light grey

            if (entity.type == "tf_frame")
            {
                label_str   = entity.label;
                label_color = 0xFFDDDDDDu;
            }
            else if (entity.type == "marker"
                     && entity_property(entity, "primitive") == "text_view_facing")
            {
                label_str = entity_property(entity, "text");
                if (label_str.empty())
                    continue;
                // Use marker color if specified, otherwise white.
                const std::string color_str = entity_property(entity, "color");
                if (!color_str.empty())
                {
                    float rgba[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                    parse_color_string(color_str, rgba);
                    label_color = (static_cast<uint32_t>(rgba[0] * 255.0f)) |
                                  (static_cast<uint32_t>(rgba[1] * 255.0f) << 8) |
                                  (static_cast<uint32_t>(rgba[2] * 255.0f) << 16) |
                                  (static_cast<uint32_t>(rgba[3] * 255.0f) << 24);
                }
            }
            else
            {
                continue;
            }

            if (label_str.empty())
                continue;

            // Project entity position to clip space.
            const vec3& world_pos = entity.transform.translation;
            const vec4 clip = mat4_mul_vec4(vp, {static_cast<float>(world_pos.x),
                                                 static_cast<float>(world_pos.y),
                                                 static_cast<float>(world_pos.z),
                                                 1.0f});
            if (clip.w <= 0.0f)
                continue;   // behind camera

            const float inv_w = 1.0f / clip.w;
            const float ndc_x = clip.x * inv_w;
            const float ndc_y = clip.y * inv_w;
            const float ndc_z = clip.z * inv_w;

            // NDC → screen pixel coordinates within the viewport.
            const float screen_x = viewport.x + (ndc_x * 0.5f + 0.5f) * viewport.w;
            const float screen_y = viewport.y + (0.5f - ndc_y * 0.5f) * viewport.h;

            // Vulkan depth is already in [0,1] range.
            const float depth = std::clamp(ndc_z, 0.0f, 1.0f);

            // Offset label slightly above the entity position.
            text_renderer.draw_text_depth(
                label_str,
                screen_x,
                screen_y - 14.0f,
                depth,
                FontSize::Tick,
                label_color,
                TextAlign::Center,
                TextVAlign::Bottom);
        }
    }
}

}   // namespace spectra::adapters::ros2
