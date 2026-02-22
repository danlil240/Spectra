#include "axes3d_renderer.hpp"

#include "core/axes3d.hpp"
#include "render/renderer.hpp"

namespace spectra
{

void Axes3DRenderer::BoundingBoxData::generate(vec3 min_corner, vec3 max_corner)
{
    edge_vertices.clear();

    vec3 corners[8] = {
        {min_corner.x, min_corner.y, min_corner.z},
        {max_corner.x, min_corner.y, min_corner.z},
        {max_corner.x, max_corner.y, min_corner.z},
        {min_corner.x, max_corner.y, min_corner.z},
        {min_corner.x, min_corner.y, max_corner.z},
        {max_corner.x, min_corner.y, max_corner.z},
        {max_corner.x, max_corner.y, max_corner.z},
        {min_corner.x, max_corner.y, max_corner.z},
    };

    int edges[12][2] = {{0, 1},
                        {1, 2},
                        {2, 3},
                        {3, 0},
                        {4, 5},
                        {5, 6},
                        {6, 7},
                        {7, 4},
                        {0, 4},
                        {1, 5},
                        {2, 6},
                        {3, 7}};

    for (int i = 0; i < 12; ++i)
    {
        edge_vertices.push_back(corners[edges[i][0]]);
        edge_vertices.push_back(corners[edges[i][1]]);
    }
}

void Axes3DRenderer::GridPlaneData::generate_xy_plane(vec3  min_corner,
                                                      vec3  max_corner,
                                                      float z_pos,
                                                      int   grid_divisions)
{
    float x_step = (max_corner.x - min_corner.x) / grid_divisions;
    float y_step = (max_corner.y - min_corner.y) / grid_divisions;

    for (int i = 0; i <= grid_divisions; ++i)
    {
        float x = min_corner.x + i * x_step;
        vertices.push_back({x, min_corner.y, z_pos});
        vertices.push_back({x, max_corner.y, z_pos});
    }

    for (int i = 0; i <= grid_divisions; ++i)
    {
        float y = min_corner.y + i * y_step;
        vertices.push_back({min_corner.x, y, z_pos});
        vertices.push_back({max_corner.x, y, z_pos});
    }
}

void Axes3DRenderer::GridPlaneData::generate_xz_plane(vec3  min_corner,
                                                      vec3  max_corner,
                                                      float y_pos,
                                                      int   grid_divisions)
{
    float x_step = (max_corner.x - min_corner.x) / grid_divisions;
    float z_step = (max_corner.z - min_corner.z) / grid_divisions;

    for (int i = 0; i <= grid_divisions; ++i)
    {
        float x = min_corner.x + i * x_step;
        vertices.push_back({x, y_pos, min_corner.z});
        vertices.push_back({x, y_pos, max_corner.z});
    }

    for (int i = 0; i <= grid_divisions; ++i)
    {
        float z = min_corner.z + i * z_step;
        vertices.push_back({min_corner.x, y_pos, z});
        vertices.push_back({max_corner.x, y_pos, z});
    }
}

void Axes3DRenderer::GridPlaneData::generate_yz_plane(vec3  min_corner,
                                                      vec3  max_corner,
                                                      float x_pos,
                                                      int   grid_divisions)
{
    float y_step = (max_corner.y - min_corner.y) / grid_divisions;
    float z_step = (max_corner.z - min_corner.z) / grid_divisions;

    for (int i = 0; i <= grid_divisions; ++i)
    {
        float y = min_corner.y + i * y_step;
        vertices.push_back({x_pos, y, min_corner.z});
        vertices.push_back({x_pos, y, max_corner.z});
    }

    for (int i = 0; i <= grid_divisions; ++i)
    {
        float z = min_corner.z + i * z_step;
        vertices.push_back({x_pos, min_corner.y, z});
        vertices.push_back({x_pos, max_corner.y, z});
    }
}

void Axes3DRenderer::TickMarkData::generate_x_ticks(const Axes3D& axes,
                                                    vec3          min_corner,
                                                    vec3 /*max_corner*/)
{
    positions.clear();
    labels.clear();

    auto tick_result = axes.compute_x_ticks();

    for (size_t i = 0; i < tick_result.positions.size(); ++i)
    {
        float x = tick_result.positions[i];
        positions.push_back({x, min_corner.y, min_corner.z});
        labels.push_back(tick_result.labels[i]);
    }
}

void Axes3DRenderer::TickMarkData::generate_y_ticks(const Axes3D& axes,
                                                    vec3          min_corner,
                                                    vec3 /*max_corner*/)
{
    positions.clear();
    labels.clear();

    auto tick_result = axes.compute_y_ticks();

    for (size_t i = 0; i < tick_result.positions.size(); ++i)
    {
        float y = tick_result.positions[i];
        positions.push_back({min_corner.x, y, min_corner.z});
        labels.push_back(tick_result.labels[i]);
    }
}

void Axes3DRenderer::TickMarkData::generate_z_ticks(const Axes3D& axes,
                                                    vec3          min_corner,
                                                    vec3 /*max_corner*/)
{
    positions.clear();
    labels.clear();

    auto tick_result = axes.compute_z_ticks();

    for (size_t i = 0; i < tick_result.positions.size(); ++i)
    {
        float z = tick_result.positions[i];
        positions.push_back({min_corner.x, min_corner.y, z});
        labels.push_back(tick_result.labels[i]);
    }
}

void Axes3DRenderer::render(Axes3D& axes, Renderer& /*renderer*/)
{
    auto xlim = axes.x_limits();
    auto ylim = axes.y_limits();
    auto zlim = axes.z_limits();

    vec3 min_corner = {xlim.min, ylim.min, zlim.min};
    vec3 max_corner = {xlim.max, ylim.max, zlim.max};

    if (axes.show_bounding_box())
    {
        bbox_data_.generate(min_corner, max_corner);
    }

    if (axes.grid_enabled())
    {
        grid_data_.vertices.clear();
        auto gp = axes.grid_planes();

        if (static_cast<int>(gp & Axes3D::GridPlane::XY))
        {
            grid_data_.generate_xy_plane(min_corner, max_corner, zlim.min, 10);
        }

        if (static_cast<int>(gp & Axes3D::GridPlane::XZ))
        {
            grid_data_.generate_xz_plane(min_corner, max_corner, ylim.min, 10);
        }

        if (static_cast<int>(gp & Axes3D::GridPlane::YZ))
        {
            grid_data_.generate_yz_plane(min_corner, max_corner, xlim.min, 10);
        }
    }

    tick_data_.generate_x_ticks(axes, min_corner, max_corner);
    tick_data_.generate_y_ticks(axes, min_corner, max_corner);
    tick_data_.generate_z_ticks(axes, min_corner, max_corner);
}

}   // namespace spectra
