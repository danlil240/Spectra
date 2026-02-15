#include <plotix/series3d.hpp>
#include <plotix/math3d.hpp>

#include <gtest/gtest.h>
#include <cmath>
#include <vector>

using namespace plotix;

// ─── LineSeries3D Tests ──────────────────────────────────────────────────────

TEST(LineSeries3D, DefaultConstruction) {
    LineSeries3D series;
    EXPECT_EQ(series.point_count(), 0);
    EXPECT_TRUE(series.x_data().empty());
    EXPECT_TRUE(series.y_data().empty());
    EXPECT_TRUE(series.z_data().empty());
}

TEST(LineSeries3D, ConstructionWithData) {
    std::vector<float> x = {1.0f, 2.0f, 3.0f};
    std::vector<float> y = {4.0f, 5.0f, 6.0f};
    std::vector<float> z = {7.0f, 8.0f, 9.0f};
    
    LineSeries3D series(x, y, z);
    EXPECT_EQ(series.point_count(), 3);
    EXPECT_EQ(series.x_data().size(), 3);
    EXPECT_EQ(series.y_data().size(), 3);
    EXPECT_EQ(series.z_data().size(), 3);
}

TEST(LineSeries3D, SetData) {
    LineSeries3D series;
    std::vector<float> x = {1.0f, 2.0f};
    std::vector<float> y = {3.0f, 4.0f};
    std::vector<float> z = {5.0f, 6.0f};
    
    series.set_x(x).set_y(y).set_z(z);
    EXPECT_EQ(series.point_count(), 2);
    EXPECT_FLOAT_EQ(series.x_data()[0], 1.0f);
    EXPECT_FLOAT_EQ(series.y_data()[1], 4.0f);
    EXPECT_FLOAT_EQ(series.z_data()[0], 5.0f);
}

TEST(LineSeries3D, AppendPoint) {
    LineSeries3D series;
    series.append(1.0f, 2.0f, 3.0f);
    series.append(4.0f, 5.0f, 6.0f);
    
    EXPECT_EQ(series.point_count(), 2);
    EXPECT_FLOAT_EQ(series.x_data()[1], 4.0f);
    EXPECT_FLOAT_EQ(series.y_data()[1], 5.0f);
    EXPECT_FLOAT_EQ(series.z_data()[1], 6.0f);
}

TEST(LineSeries3D, ComputeCentroid) {
    std::vector<float> x = {0.0f, 2.0f, 4.0f};
    std::vector<float> y = {0.0f, 3.0f, 6.0f};
    std::vector<float> z = {0.0f, 1.0f, 2.0f};
    
    LineSeries3D series(x, y, z);
    vec3 centroid = series.compute_centroid();
    
    EXPECT_FLOAT_EQ(centroid.x, 2.0f);
    EXPECT_FLOAT_EQ(centroid.y, 3.0f);
    EXPECT_FLOAT_EQ(centroid.z, 1.0f);
}

TEST(LineSeries3D, ComputeCentroidEmpty) {
    LineSeries3D series;
    vec3 centroid = series.compute_centroid();
    
    EXPECT_FLOAT_EQ(centroid.x, 0.0f);
    EXPECT_FLOAT_EQ(centroid.y, 0.0f);
    EXPECT_FLOAT_EQ(centroid.z, 0.0f);
}

TEST(LineSeries3D, GetBounds) {
    std::vector<float> x = {-1.0f, 2.0f, 5.0f};
    std::vector<float> y = {-3.0f, 0.0f, 4.0f};
    std::vector<float> z = {-2.0f, 1.0f, 3.0f};
    
    LineSeries3D series(x, y, z);
    vec3 min_bound, max_bound;
    series.get_bounds(min_bound, max_bound);
    
    EXPECT_FLOAT_EQ(min_bound.x, -1.0f);
    EXPECT_FLOAT_EQ(min_bound.y, -3.0f);
    EXPECT_FLOAT_EQ(min_bound.z, -2.0f);
    EXPECT_FLOAT_EQ(max_bound.x, 5.0f);
    EXPECT_FLOAT_EQ(max_bound.y, 4.0f);
    EXPECT_FLOAT_EQ(max_bound.z, 3.0f);
}

TEST(LineSeries3D, GetBoundsEmpty) {
    LineSeries3D series;
    vec3 min_bound, max_bound;
    series.get_bounds(min_bound, max_bound);
    
    EXPECT_FLOAT_EQ(min_bound.x, 0.0f);
    EXPECT_FLOAT_EQ(max_bound.x, 0.0f);
}

TEST(LineSeries3D, WidthProperty) {
    LineSeries3D series;
    series.width(3.5f);
    EXPECT_FLOAT_EQ(series.width(), 3.5f);
}

TEST(LineSeries3D, FluentInterface) {
    LineSeries3D series;
    auto& result = series.width(2.0f).color(colors::red).opacity(0.8f);
    EXPECT_EQ(&result, &series);
}

// ─── ScatterSeries3D Tests ───────────────────────────────────────────────────

TEST(ScatterSeries3D, DefaultConstruction) {
    ScatterSeries3D series;
    EXPECT_EQ(series.point_count(), 0);
    EXPECT_FLOAT_EQ(series.size(), 4.0f);
}

TEST(ScatterSeries3D, ConstructionWithData) {
    std::vector<float> x = {1.0f, 2.0f};
    std::vector<float> y = {3.0f, 4.0f};
    std::vector<float> z = {5.0f, 6.0f};
    
    ScatterSeries3D series(x, y, z);
    EXPECT_EQ(series.point_count(), 2);
}

TEST(ScatterSeries3D, SetData) {
    ScatterSeries3D series;
    std::vector<float> x = {1.0f};
    std::vector<float> y = {2.0f};
    std::vector<float> z = {3.0f};
    
    series.set_x(x).set_y(y).set_z(z);
    EXPECT_EQ(series.point_count(), 1);
    EXPECT_FLOAT_EQ(series.x_data()[0], 1.0f);
}

TEST(ScatterSeries3D, AppendPoint) {
    ScatterSeries3D series;
    series.append(1.0f, 2.0f, 3.0f);
    
    EXPECT_EQ(series.point_count(), 1);
    EXPECT_FLOAT_EQ(series.z_data()[0], 3.0f);
}

TEST(ScatterSeries3D, ComputeCentroid) {
    std::vector<float> x = {1.0f, 3.0f, 5.0f};
    std::vector<float> y = {2.0f, 4.0f, 6.0f};
    std::vector<float> z = {0.0f, 2.0f, 4.0f};
    
    ScatterSeries3D series(x, y, z);
    vec3 centroid = series.compute_centroid();
    
    EXPECT_FLOAT_EQ(centroid.x, 3.0f);
    EXPECT_FLOAT_EQ(centroid.y, 4.0f);
    EXPECT_FLOAT_EQ(centroid.z, 2.0f);
}

TEST(ScatterSeries3D, GetBounds) {
    std::vector<float> x = {0.0f, 10.0f};
    std::vector<float> y = {-5.0f, 5.0f};
    std::vector<float> z = {-1.0f, 1.0f};
    
    ScatterSeries3D series(x, y, z);
    vec3 min_bound, max_bound;
    series.get_bounds(min_bound, max_bound);
    
    EXPECT_FLOAT_EQ(min_bound.x, 0.0f);
    EXPECT_FLOAT_EQ(max_bound.x, 10.0f);
    EXPECT_FLOAT_EQ(min_bound.y, -5.0f);
    EXPECT_FLOAT_EQ(max_bound.y, 5.0f);
}

TEST(ScatterSeries3D, SizeProperty) {
    ScatterSeries3D series;
    series.size(8.0f);
    EXPECT_FLOAT_EQ(series.size(), 8.0f);
}

TEST(ScatterSeries3D, LargeDataset) {
    std::vector<float> x(10000), y(10000), z(10000);
    for (int i = 0; i < 10000; ++i) {
        x[i] = static_cast<float>(i);
        y[i] = static_cast<float>(i * 2);
        z[i] = static_cast<float>(i * 3);
    }
    
    ScatterSeries3D series(x, y, z);
    EXPECT_EQ(series.point_count(), 10000);
}

// ─── SurfaceSeries Tests ─────────────────────────────────────────────────────

TEST(SurfaceSeries, DefaultConstruction) {
    SurfaceSeries series;
    EXPECT_EQ(series.rows(), 0);
    EXPECT_EQ(series.cols(), 0);
    EXPECT_FALSE(series.is_mesh_generated());
}

TEST(SurfaceSeries, ConstructionWithData) {
    std::vector<float> x = {0.0f, 1.0f, 2.0f};
    std::vector<float> y = {0.0f, 1.0f};
    std::vector<float> z = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};  // 2 rows × 3 cols
    
    SurfaceSeries series(x, y, z);
    EXPECT_EQ(series.cols(), 3);
    EXPECT_EQ(series.rows(), 2);
}

TEST(SurfaceSeries, SetData) {
    SurfaceSeries series;
    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {0.0f, 1.0f};
    std::vector<float> z = {1.0f, 2.0f, 3.0f, 4.0f};
    
    series.set_data(x, y, z);
    EXPECT_EQ(series.cols(), 2);
    EXPECT_EQ(series.rows(), 2);
}

TEST(SurfaceSeries, GenerateMeshSimple) {
    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {0.0f, 1.0f};
    std::vector<float> z = {0.0f, 1.0f, 2.0f, 3.0f};
    
    SurfaceSeries series(x, y, z);
    series.generate_mesh();
    
    EXPECT_TRUE(series.is_mesh_generated());
    const auto& mesh = series.mesh();
    EXPECT_EQ(mesh.vertex_count, 4);  // 2×2 grid
    EXPECT_EQ(mesh.triangle_count, 2);  // 1 quad = 2 triangles
}

TEST(SurfaceSeries, GenerateMeshLarger) {
    std::vector<float> x = {0.0f, 1.0f, 2.0f};
    std::vector<float> y = {0.0f, 1.0f, 2.0f};
    std::vector<float> z(9, 0.0f);  // 3×3 grid
    
    SurfaceSeries series(x, y, z);
    series.generate_mesh();
    
    EXPECT_TRUE(series.is_mesh_generated());
    const auto& mesh = series.mesh();
    EXPECT_EQ(mesh.vertex_count, 9);
    EXPECT_EQ(mesh.triangle_count, 8);  // 4 quads = 8 triangles
}

TEST(SurfaceSeries, MeshVertexFormat) {
    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {0.0f, 1.0f};
    std::vector<float> z = {0.0f, 1.0f, 2.0f, 3.0f};
    
    SurfaceSeries series(x, y, z);
    series.generate_mesh();
    
    const auto& mesh = series.mesh();
    EXPECT_EQ(mesh.vertices.size(), 24);  // 4 vertices × 6 floats (x,y,z,nx,ny,nz)
}

TEST(SurfaceSeries, MeshIndicesFormat) {
    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {0.0f, 1.0f};
    std::vector<float> z = {0.0f, 1.0f, 2.0f, 3.0f};
    
    SurfaceSeries series(x, y, z);
    series.generate_mesh();
    
    const auto& mesh = series.mesh();
    EXPECT_EQ(mesh.indices.size(), 6);  // 2 triangles × 3 indices
}

TEST(SurfaceSeries, GenerateMeshInvalidSize) {
    std::vector<float> x = {0.0f};
    std::vector<float> y = {0.0f};
    std::vector<float> z = {0.0f};
    
    SurfaceSeries series(x, y, z);
    series.generate_mesh();
    
    EXPECT_FALSE(series.is_mesh_generated());
}

TEST(SurfaceSeries, GenerateMeshMismatchedSize) {
    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {0.0f, 1.0f};
    std::vector<float> z = {0.0f, 1.0f, 2.0f};  // Wrong size: should be 4
    
    SurfaceSeries series(x, y, z);
    series.generate_mesh();
    
    EXPECT_FALSE(series.is_mesh_generated());
}

TEST(SurfaceSeries, ComputeCentroid) {
    std::vector<float> x = {0.0f, 2.0f};
    std::vector<float> y = {0.0f, 4.0f};
    std::vector<float> z = {0.0f, 2.0f, 4.0f, 6.0f};
    
    SurfaceSeries series(x, y, z);
    vec3 centroid = series.compute_centroid();
    
    EXPECT_FLOAT_EQ(centroid.x, 1.0f);
    EXPECT_FLOAT_EQ(centroid.y, 2.0f);
    EXPECT_FLOAT_EQ(centroid.z, 3.0f);
}

TEST(SurfaceSeries, GetBounds) {
    std::vector<float> x = {-1.0f, 1.0f};
    std::vector<float> y = {-2.0f, 2.0f};
    std::vector<float> z = {-3.0f, 0.0f, 0.0f, 3.0f};
    
    SurfaceSeries series(x, y, z);
    vec3 min_bound, max_bound;
    series.get_bounds(min_bound, max_bound);
    
    EXPECT_FLOAT_EQ(min_bound.x, -1.0f);
    EXPECT_FLOAT_EQ(max_bound.x, 1.0f);
    EXPECT_FLOAT_EQ(min_bound.y, -2.0f);
    EXPECT_FLOAT_EQ(max_bound.y, 2.0f);
    EXPECT_FLOAT_EQ(min_bound.z, -3.0f);
    EXPECT_FLOAT_EQ(max_bound.z, 3.0f);
}

TEST(SurfaceSeries, NormalComputation) {
    std::vector<float> x = {0.0f, 1.0f, 2.0f};
    std::vector<float> y = {0.0f, 1.0f, 2.0f};
    std::vector<float> z(9, 0.0f);  // Flat surface
    
    SurfaceSeries series(x, y, z);
    series.generate_mesh();
    
    const auto& mesh = series.mesh();
    // Check that normals exist (every 6th float starting from index 3)
    for (size_t i = 0; i < mesh.vertex_count; ++i) {
        float nx = mesh.vertices[i * 6 + 3];
        float ny = mesh.vertices[i * 6 + 4];
        float nz = mesh.vertices[i * 6 + 5];
        float len = std::sqrt(nx*nx + ny*ny + nz*nz);
        EXPECT_NEAR(len, 1.0f, 1e-5f);  // Normals should be normalized
    }
}

// ─── MeshSeries Tests ────────────────────────────────────────────────────────

TEST(MeshSeries, DefaultConstruction) {
    MeshSeries series;
    EXPECT_EQ(series.vertex_count(), 0);
    EXPECT_EQ(series.triangle_count(), 0);
}

TEST(MeshSeries, ConstructionWithData) {
    std::vector<float> verts = {
        0.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,  // v0: pos + normal
        1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,  // v1
        0.0f, 1.0f, 0.0f,  0.0f, 0.0f, 1.0f   // v2
    };
    std::vector<uint32_t> indices = {0, 1, 2};
    
    MeshSeries series(verts, indices);
    EXPECT_EQ(series.vertex_count(), 3);
    EXPECT_EQ(series.triangle_count(), 1);
}

TEST(MeshSeries, SetVertices) {
    MeshSeries series;
    std::vector<float> verts = {
        0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f
    };
    
    series.set_vertices(verts);
    EXPECT_EQ(series.vertex_count(), 1);
}

TEST(MeshSeries, SetIndices) {
    MeshSeries series;
    std::vector<uint32_t> indices = {0, 1, 2, 3, 4, 5};
    
    series.set_indices(indices);
    EXPECT_EQ(series.triangle_count(), 2);
}

TEST(MeshSeries, ComputeCentroid) {
    std::vector<float> verts = {
        0.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,
        3.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,
        0.0f, 6.0f, 0.0f,  0.0f, 0.0f, 1.0f
    };
    std::vector<uint32_t> indices = {0, 1, 2};
    
    MeshSeries series(verts, indices);
    vec3 centroid = series.compute_centroid();
    
    EXPECT_FLOAT_EQ(centroid.x, 1.0f);
    EXPECT_FLOAT_EQ(centroid.y, 2.0f);
    EXPECT_FLOAT_EQ(centroid.z, 0.0f);
}

TEST(MeshSeries, GetBounds) {
    std::vector<float> verts = {
        -1.0f, -2.0f, -3.0f,  0.0f, 0.0f, 1.0f,
         1.0f,  2.0f,  3.0f,  0.0f, 0.0f, 1.0f
    };
    std::vector<uint32_t> indices = {0, 1, 0};
    
    MeshSeries series(verts, indices);
    vec3 min_bound, max_bound;
    series.get_bounds(min_bound, max_bound);
    
    EXPECT_FLOAT_EQ(min_bound.x, -1.0f);
    EXPECT_FLOAT_EQ(max_bound.x, 1.0f);
    EXPECT_FLOAT_EQ(min_bound.y, -2.0f);
    EXPECT_FLOAT_EQ(max_bound.y, 2.0f);
    EXPECT_FLOAT_EQ(min_bound.z, -3.0f);
    EXPECT_FLOAT_EQ(max_bound.z, 3.0f);
}

TEST(MeshSeries, EmptyMesh) {
    MeshSeries series;
    vec3 centroid = series.compute_centroid();
    EXPECT_FLOAT_EQ(centroid.x, 0.0f);
    
    vec3 min_bound, max_bound;
    series.get_bounds(min_bound, max_bound);
    EXPECT_FLOAT_EQ(min_bound.x, 0.0f);
}

TEST(MeshSeries, ComplexMesh) {
    // Cube vertices (8 vertices × 6 floats)
    std::vector<float> verts;
    for (int i = 0; i < 8; ++i) {
        verts.push_back((i & 1) ? 1.0f : 0.0f);  // x
        verts.push_back((i & 2) ? 1.0f : 0.0f);  // y
        verts.push_back((i & 4) ? 1.0f : 0.0f);  // z
        verts.push_back(0.0f);  // nx
        verts.push_back(0.0f);  // ny
        verts.push_back(1.0f);  // nz
    }
    
    std::vector<uint32_t> indices = {
        0, 1, 2,  1, 3, 2,  // 2 triangles
        4, 5, 6,  5, 7, 6   // 2 more triangles
    };
    
    MeshSeries series(verts, indices);
    EXPECT_EQ(series.vertex_count(), 8);
    EXPECT_EQ(series.triangle_count(), 4);
}

// ─── Edge Cases and Stress Tests ─────────────────────────────────────────────

TEST(Series3D, MismatchedArraySizes) {
    std::vector<float> x = {1.0f, 2.0f, 3.0f};
    std::vector<float> y = {1.0f, 2.0f};
    std::vector<float> z = {1.0f};
    
    LineSeries3D series(x, y, z);
    // Should handle gracefully - point_count is min of all sizes
    EXPECT_EQ(series.point_count(), 3);
    
    vec3 min_bound, max_bound;
    series.get_bounds(min_bound, max_bound);
    // Should only process the minimum count
    EXPECT_EQ(series.point_count(), 3);
}

TEST(Series3D, SinglePoint) {
    std::vector<float> x = {5.0f};
    std::vector<float> y = {10.0f};
    std::vector<float> z = {15.0f};
    
    ScatterSeries3D series(x, y, z);
    vec3 centroid = series.compute_centroid();
    
    EXPECT_FLOAT_EQ(centroid.x, 5.0f);
    EXPECT_FLOAT_EQ(centroid.y, 10.0f);
    EXPECT_FLOAT_EQ(centroid.z, 15.0f);
}

TEST(Series3D, NegativeCoordinates) {
    std::vector<float> x = {-10.0f, -5.0f, -1.0f};
    std::vector<float> y = {-20.0f, -10.0f, -5.0f};
    std::vector<float> z = {-30.0f, -15.0f, -7.5f};
    
    LineSeries3D series(x, y, z);
    vec3 min_bound, max_bound;
    series.get_bounds(min_bound, max_bound);
    
    EXPECT_FLOAT_EQ(min_bound.x, -10.0f);
    EXPECT_FLOAT_EQ(max_bound.x, -1.0f);
}

TEST(Series3D, ZeroSizedSurface) {
    std::vector<float> x = {0.0f};
    std::vector<float> y = {0.0f};
    std::vector<float> z = {0.0f};
    
    SurfaceSeries series(x, y, z);
    series.generate_mesh();
    
    EXPECT_FALSE(series.is_mesh_generated());
}

TEST(Series3D, VeryLargeSurface) {
    int size = 100;
    std::vector<float> x(size), y(size), z(size * size);
    
    for (int i = 0; i < size; ++i) {
        x[i] = static_cast<float>(i);
        y[i] = static_cast<float>(i);
    }
    for (int i = 0; i < size * size; ++i) {
        z[i] = std::sin(static_cast<float>(i) * 0.1f);
    }
    
    SurfaceSeries series(x, y, z);
    series.generate_mesh();
    
    EXPECT_TRUE(series.is_mesh_generated());
    EXPECT_EQ(series.mesh().vertex_count, size * size);
    EXPECT_EQ(series.mesh().triangle_count, (size - 1) * (size - 1) * 2);
}
