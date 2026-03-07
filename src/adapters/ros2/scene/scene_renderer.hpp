#pragma once

#include <cstdint>

#include "render/backend.hpp"

namespace spectra
{
class Camera;
class Renderer;
struct Rect;
}   // namespace spectra

namespace spectra::adapters::ros2
{

class SceneManager;

/// GPU scene renderer — dispatches SceneManager entities through Vulkan
/// pipelines (Marker3D, Grid3D, Arrow3D, Line3D).
///
/// Owns cached GPU buffers for primitive meshes (cube, sphere, cylinder,
/// arrow, cone) and grid geometry.  Call render() between
/// Renderer::begin_render_pass() and Renderer::end_render_pass().
class SceneRenderer
{
public:
    /// Render all entities in \p scene using the given camera and viewport.
    /// \p renderer provides the backend for pipeline and buffer operations.
    void render(Renderer& renderer,
                SceneManager& scene,
                const Camera& camera,
                const Rect& viewport);

private:
    void ensure_pipelines(Backend& backend);

    struct GpuData
    {
        bool initialized{false};

        BufferHandle cube_vbo;
        BufferHandle cube_ibo;
        uint32_t     cube_index_count{0};

        BufferHandle sphere_vbo;
        BufferHandle sphere_ibo;
        uint32_t     sphere_index_count{0};

        BufferHandle cylinder_vbo;
        BufferHandle cylinder_ibo;
        uint32_t     cylinder_index_count{0};

        BufferHandle arrow_vbo;
        BufferHandle arrow_ibo;
        uint32_t     arrow_index_count{0};

        BufferHandle cone_vbo;
        BufferHandle cone_ibo;
        uint32_t     cone_index_count{0};

        BufferHandle grid_vbo;
        size_t       grid_capacity{0};
        uint32_t     grid_vertex_count{0};
        float        grid_cell_size{0.0f};
        int          grid_cell_count{0};
    };

    GpuData gpu_;

    PipelineHandle grid3d_pipeline_;
    PipelineHandle marker3d_pipeline_;
    PipelineHandle line3d_pipeline_;
    BufferHandle   frame_ubo_;
    bool           pipelines_ready_{false};
};

}   // namespace spectra::adapters::ros2
