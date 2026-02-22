#pragma once

#include <spectra/fwd.hpp>
#include <spectra/math3d.hpp>
#include <string>
#include <vector>

namespace spectra
{

class Axes3D;

struct GridLineVertex
{
    vec3  position;
    float padding;
};

class Axes3DRenderer
{
   public:
    Axes3DRenderer() = default;

    void render(Axes3D& axes, Renderer& renderer);

    struct BoundingBoxData
    {
        std::vector<vec3> edge_vertices;
        void              generate(vec3 min_corner, vec3 max_corner);
    };

    struct GridPlaneData
    {
        std::vector<vec3> vertices;
        void generate_xy_plane(vec3 min_corner, vec3 max_corner, float z_pos, int grid_divisions);
        void generate_xz_plane(vec3 min_corner, vec3 max_corner, float y_pos, int grid_divisions);
        void generate_yz_plane(vec3 min_corner, vec3 max_corner, float x_pos, int grid_divisions);
    };

    struct TickMarkData
    {
        std::vector<vec3>        positions;
        std::vector<std::string> labels;
        void generate_x_ticks(const class Axes3D& axes, vec3 min_corner, vec3 max_corner);
        void generate_y_ticks(const class Axes3D& axes, vec3 min_corner, vec3 max_corner);
        void generate_z_ticks(const class Axes3D& axes, vec3 min_corner, vec3 max_corner);
    };

   private:
    BoundingBoxData bbox_data_;
    GridPlaneData   grid_data_;
    TickMarkData    tick_data_;
};

}   // namespace spectra
