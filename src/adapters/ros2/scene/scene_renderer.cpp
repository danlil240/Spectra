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
#include "scene/mesh_primitives.hpp"
#include "scene/scene_manager.hpp"

namespace spectra::adapters::ros2
{

// ──────────────────────────────────────────────────────────────────────
// Internal helpers
// ──────────────────────────────────────────────────────────────────────

namespace
{

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
        ubo.viewport_width  = viewport.w;
        ubo.viewport_height = viewport.h;
        set_camera_pos(ubo);
        std::memcpy(ubo.light_dir, default_light, sizeof(float) * 3);
        backend.upload_buffer(frame_ubo_, &ubo, sizeof(FrameUBO));

        SeriesPushConstants pc{};
        pc.color[0] = 0.45f; pc.color[1] = 0.45f; pc.color[2] = 0.45f; pc.color[3] = 0.6f;
        pc.opacity = 0.6f;

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

        if (entity.type == "marker")
        {
            const std::string prim = entity_property(entity, "primitive", "cube");
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
        pc.opacity = pc.color[3];

        backend.bind_buffer(frame_ubo_, 0);
        backend.bind_buffer(vbo, 0);
        backend.bind_index_buffer(ibo);
        backend.push_constants(pc);
        backend.draw_indexed(idx_count);
    }

    // ── Pass 3: line-strip entities (paths, polylines) ──
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
        BufferHandle line_buf = backend.create_buffer(BufferUsage::Storage, bytes);
        backend.upload_buffer(line_buf, line_data.data(), bytes);

        FrameUBO ubo{};
        std::memcpy(ubo.projection, proj, sizeof(float) * 16);
        std::memcpy(ubo.view, view_mat, sizeof(float) * 16);
        ubo.model[0] = 1.0f; ubo.model[5] = 1.0f; ubo.model[10] = 1.0f; ubo.model[15] = 1.0f;
        ubo.viewport_width  = viewport.w;
        ubo.viewport_height = viewport.h;
        set_camera_pos(ubo);
        std::memcpy(ubo.light_dir, default_light, sizeof(float) * 3);
        backend.upload_buffer(frame_ubo_, &ubo, sizeof(FrameUBO));

        SeriesPushConstants pc{};
        if (is_selected)
        {
            pc.color[0] = 1.0f; pc.color[1] = 0.9f; pc.color[2] = 0.3f;
        }
        else if (entity.type == "path")
        {
            pc.color[0] = 0.31f; pc.color[1] = 0.78f; pc.color[2] = 1.0f;
        }
        else
        {
            pc.color[0] = 0.7f; pc.color[1] = 0.7f; pc.color[2] = 1.0f;
        }
        pc.color[3]   = 1.0f;
        pc.opacity    = 1.0f;
        pc.line_width = 2.5f;

        backend.bind_pipeline(line3d_pipeline_);
        backend.bind_buffer(frame_ubo_, 0);
        backend.bind_buffer(line_buf, 1);
        backend.push_constants(pc);
        backend.draw(static_cast<uint32_t>(points.size()));

        backend.destroy_buffer(line_buf);
    }
}

}   // namespace spectra::adapters::ros2
