#include <plotix/plotix.hpp>
#include <plotix/math3d.hpp>
#include <gtest/gtest.h>

#include "ui/camera_animator.hpp"

#include <cmath>
#include <vector>

using namespace plotix;

// ─── Fixture ────────────────────────────────────────────────────────────────

class Integration3DTest : public ::testing::Test {
protected:
    void SetUp() override {
        AppConfig config;
        config.headless = true;
        app_ = std::make_unique<App>(config);
    }

    void TearDown() override {
        app_.reset();
    }

    std::unique_ptr<App> app_;
};

// ─── Mixed 2D + 3D ─────────────────────────────────────────────────────────

TEST_F(Integration3DTest, Mixed2DAnd3DFigure) {
    auto& fig = app_->figure();

    auto& ax2d = fig.subplot(2, 1, 1);
    std::vector<float> x2d = {0.0f, 1.0f, 2.0f, 3.0f};
    std::vector<float> y2d = {0.0f, 1.0f, 0.5f, 1.5f};
    ax2d.line(x2d, y2d).color(colors::blue);
    ax2d.title("2D Line Plot");

    auto& ax3d = fig.subplot3d(2, 1, 2);
    std::vector<float> x3d = {0.0f, 1.0f, 2.0f};
    std::vector<float> y3d = {0.0f, 1.0f, 0.5f};
    std::vector<float> z3d = {0.0f, 0.5f, 1.0f};
    ax3d.scatter3d(x3d, y3d, z3d).color(colors::red);
    ax3d.title("3D Scatter Plot");

    SUCCEED();
}

TEST_F(Integration3DTest, Multiple3DSubplots) {
    auto& fig = app_->figure();

    auto& ax1 = fig.subplot3d(2, 2, 1);
    std::vector<float> x1 = {0.0f, 1.0f};
    std::vector<float> y1 = {0.0f, 1.0f};
    std::vector<float> z1 = {0.0f, 1.0f};
    ax1.scatter3d(x1, y1, z1).color(colors::red);

    auto& ax2 = fig.subplot3d(2, 2, 2);
    std::vector<float> x2 = {0.0f, 1.0f, 2.0f};
    std::vector<float> y2 = {0.0f, 1.0f, 0.0f};
    std::vector<float> z2 = {0.0f, 0.0f, 1.0f};
    ax2.line3d(x2, y2, z2).color(colors::green);

    auto& ax3 = fig.subplot3d(2, 2, 3);
    const int nx = 10, ny = 10;
    std::vector<float> x_grid(nx), y_grid(ny), z_values(nx * ny);
    for (int i = 0; i < nx; ++i) x_grid[i] = static_cast<float>(i);
    for (int j = 0; j < ny; ++j) y_grid[j] = static_cast<float>(j);
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            z_values[j * nx + i] = static_cast<float>(i + j);
        }
    }
    ax3.surface(x_grid, y_grid, z_values).color(colors::blue);

    auto& ax4 = fig.subplot3d(2, 2, 4);
    std::vector<float> x4 = {0.0f, 1.0f, 0.5f};
    std::vector<float> y4 = {0.0f, 0.0f, 1.0f};
    std::vector<float> z4 = {0.0f, 1.0f, 0.5f};
    ax4.scatter3d(x4, y4, z4).color(colors::cyan);

    SUCCEED();
}

// ─── Camera Independence ────────────────────────────────────────────────────

TEST_F(Integration3DTest, CameraIndependenceAcrossSubplots) {
    auto& fig = app_->figure();

    auto& ax1 = fig.subplot3d(1, 2, 1);
    ax1.camera().azimuth = 45.0f;
    ax1.camera().elevation = 30.0f;

    auto& ax2 = fig.subplot3d(1, 2, 2);
    ax2.camera().azimuth = 90.0f;
    ax2.camera().elevation = 60.0f;

    EXPECT_FLOAT_EQ(ax1.camera().azimuth, 45.0f);
    EXPECT_FLOAT_EQ(ax2.camera().azimuth, 90.0f);
}

// ─── Grid Planes ────────────────────────────────────────────────────────────

TEST_F(Integration3DTest, GridPlaneConfiguration) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    ax.grid_planes(Axes3D::GridPlane::XY);
    EXPECT_EQ(ax.grid_planes(), Axes3D::GridPlane::XY);

    ax.grid_planes(Axes3D::GridPlane::All);
    EXPECT_EQ(ax.grid_planes(), Axes3D::GridPlane::All);

    ax.grid_planes(Axes3D::GridPlane::None);
    EXPECT_EQ(ax.grid_planes(), Axes3D::GridPlane::None);
}

TEST_F(Integration3DTest, GridPlaneBitwiseOr) {
    auto combined = Axes3D::GridPlane::XY | Axes3D::GridPlane::XZ;
    EXPECT_EQ(combined, (Axes3D::GridPlane::XY | Axes3D::GridPlane::XZ));
    EXPECT_NE(static_cast<int>(combined), 0);
}

// ─── Bounding Box ───────────────────────────────────────────────────────────

TEST_F(Integration3DTest, BoundingBoxToggle) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    EXPECT_TRUE(ax.show_bounding_box());

    ax.show_bounding_box(false);
    EXPECT_FALSE(ax.show_bounding_box());

    ax.show_bounding_box(true);
    EXPECT_TRUE(ax.show_bounding_box());
}

// ─── Axis Limits ────────────────────────────────────────────────────────────

TEST_F(Integration3DTest, AxisLimits3D) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    ax.xlim(-1.0f, 1.0f);
    ax.ylim(-2.0f, 2.0f);
    ax.zlim(-3.0f, 3.0f);

    auto xlim = ax.x_limits();
    auto ylim = ax.y_limits();
    auto zlim = ax.z_limits();

    EXPECT_FLOAT_EQ(xlim.min, -1.0f);
    EXPECT_FLOAT_EQ(xlim.max, 1.0f);
    EXPECT_FLOAT_EQ(ylim.min, -2.0f);
    EXPECT_FLOAT_EQ(ylim.max, 2.0f);
    EXPECT_FLOAT_EQ(zlim.min, -3.0f);
    EXPECT_FLOAT_EQ(zlim.max, 3.0f);
}

TEST_F(Integration3DTest, AxisLabels3D) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    ax.xlabel("X Axis");
    ax.ylabel("Y Axis");
    ax.zlabel("Z Axis");

    EXPECT_EQ(ax.get_xlabel(), "X Axis");
    EXPECT_EQ(ax.get_ylabel(), "Y Axis");
    EXPECT_EQ(ax.get_zlabel(), "Z Axis");
}

// ─── Series Chaining ────────────────────────────────────────────────────────

TEST_F(Integration3DTest, SeriesChaining3D) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    std::vector<float> x = {0.0f, 1.0f, 2.0f};
    std::vector<float> y = {0.0f, 1.0f, 0.5f};
    std::vector<float> z = {0.0f, 0.5f, 1.0f};

    auto& scatter = ax.scatter3d(x, y, z)
        .color(colors::blue)
        .size(5.0f)
        .label("Test Scatter")
        .opacity(0.8f);

    EXPECT_FLOAT_EQ(scatter.size(), 5.0f);

    auto& line = ax.line3d(x, y, z)
        .color(colors::red)
        .width(3.0f)
        .label("Test Line");

    EXPECT_FLOAT_EQ(line.width(), 3.0f);
}

// ─── Camera ─────────────────────────────────────────────────────────────────

TEST_F(Integration3DTest, CameraProjectionModes) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    ax.camera().projection_mode = Camera::ProjectionMode::Perspective;
    EXPECT_EQ(ax.camera().projection_mode, Camera::ProjectionMode::Perspective);

    ax.camera().projection_mode = Camera::ProjectionMode::Orthographic;
    EXPECT_EQ(ax.camera().projection_mode, Camera::ProjectionMode::Orthographic);
}

TEST_F(Integration3DTest, CameraParameters) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    ax.camera().fov = 60.0f;
    ax.camera().near_clip = 0.1f;
    ax.camera().far_clip = 100.0f;
    ax.camera().distance = 10.0f;

    EXPECT_FLOAT_EQ(ax.camera().fov, 60.0f);
    EXPECT_FLOAT_EQ(ax.camera().near_clip, 0.1f);
    EXPECT_FLOAT_EQ(ax.camera().far_clip, 100.0f);
    EXPECT_FLOAT_EQ(ax.camera().distance, 10.0f);
}

TEST_F(Integration3DTest, CameraOrbitProducesValidMatrix) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    ax.camera().azimuth = 0.0f;
    ax.camera().elevation = 0.0f;
    ax.camera().distance = 5.0f;
    ax.camera().update_position_from_orbit();

    mat4 view = ax.camera().view_matrix();
    // View matrix should not be all zeros
    bool has_nonzero = false;
    for (int i = 0; i < 16; ++i) {
        if (view.m[i] != 0.0f) { has_nonzero = true; break; }
    }
    EXPECT_TRUE(has_nonzero);
}

TEST_F(Integration3DTest, CameraOrbitChangesPosition) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    ax.camera().azimuth = 0.0f;
    ax.camera().elevation = 0.0f;
    ax.camera().update_position_from_orbit();
    vec3 pos_before = ax.camera().position;

    ax.camera().orbit(90.0f, 0.0f);
    vec3 pos_after = ax.camera().position;

    // Position should have changed
    EXPECT_NE(pos_before.x, pos_after.x);
}

TEST_F(Integration3DTest, CameraSerialization) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    ax.camera().azimuth = 123.0f;
    ax.camera().elevation = 45.0f;
    ax.camera().distance = 7.5f;
    ax.camera().fov = 60.0f;

    std::string json = ax.camera().serialize();
    EXPECT_FALSE(json.empty());

    Camera restored;
    restored.deserialize(json);

    EXPECT_NEAR(restored.azimuth, 123.0f, 0.1f);
    EXPECT_NEAR(restored.elevation, 45.0f, 0.1f);
    EXPECT_NEAR(restored.distance, 7.5f, 0.1f);
    EXPECT_NEAR(restored.fov, 60.0f, 0.1f);
}

TEST_F(Integration3DTest, CameraReset) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    ax.camera().azimuth = 200.0f;
    ax.camera().elevation = 80.0f;
    ax.camera().distance = 50.0f;

    ax.camera().reset();

    EXPECT_FLOAT_EQ(ax.camera().azimuth, 45.0f);
    EXPECT_FLOAT_EQ(ax.camera().elevation, 30.0f);
    EXPECT_FLOAT_EQ(ax.camera().distance, 5.0f);
}

// ─── Surface & Mesh ─────────────────────────────────────────────────────────

TEST_F(Integration3DTest, SurfaceMeshGeneration) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    const int nx = 5, ny = 5;
    std::vector<float> x_grid(nx), y_grid(ny), z_values(nx * ny);

    for (int i = 0; i < nx; ++i) x_grid[i] = static_cast<float>(i);
    for (int j = 0; j < ny; ++j) y_grid[j] = static_cast<float>(j);
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            z_values[j * nx + i] = static_cast<float>(i * j);
        }
    }

    auto& surf = ax.surface(x_grid, y_grid, z_values);

    EXPECT_EQ(surf.rows(), ny);
    EXPECT_EQ(surf.cols(), nx);
    EXPECT_FALSE(surf.is_mesh_generated());

    surf.generate_mesh();
    EXPECT_TRUE(surf.is_mesh_generated());

    const auto& mesh = surf.mesh();
    EXPECT_GT(mesh.vertex_count, 0u);
    EXPECT_GT(mesh.triangle_count, 0u);
}

TEST_F(Integration3DTest, SurfaceMeshTopology) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    const int nx = 4, ny = 3;
    std::vector<float> x_grid(nx), y_grid(ny), z_values(nx * ny);
    for (int i = 0; i < nx; ++i) x_grid[i] = static_cast<float>(i);
    for (int j = 0; j < ny; ++j) y_grid[j] = static_cast<float>(j);
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i)
            z_values[j * nx + i] = 0.0f;

    auto& surf = ax.surface(x_grid, y_grid, z_values);
    surf.generate_mesh();

    // (nx-1)*(ny-1) grid cells, 2 triangles each
    EXPECT_EQ(surf.mesh().triangle_count, static_cast<size_t>((nx - 1) * (ny - 1) * 2));
}

TEST_F(Integration3DTest, MeshSeriesCustomGeometry) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    std::vector<float> vertices = {
        0.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,
        0.5f, 1.0f, 0.0f,  0.0f, 0.0f, 1.0f,
    };

    std::vector<uint32_t> indices = {0, 1, 2};

    auto& mesh = ax.mesh(vertices, indices);

    EXPECT_EQ(mesh.vertex_count(), 3u);
    EXPECT_EQ(mesh.triangle_count(), 1u);
}

// ─── Bounds & Centroid ──────────────────────────────────────────────────────

TEST_F(Integration3DTest, SeriesBoundsComputation) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    std::vector<float> x = {-1.0f, 2.0f, 0.0f};
    std::vector<float> y = {-2.0f, 1.0f, 0.0f};
    std::vector<float> z = {-3.0f, 3.0f, 0.0f};

    auto& scatter = ax.scatter3d(x, y, z);

    vec3 min_bounds, max_bounds;
    scatter.get_bounds(min_bounds, max_bounds);

    EXPECT_FLOAT_EQ(min_bounds.x, -1.0f);
    EXPECT_FLOAT_EQ(max_bounds.x, 2.0f);
    EXPECT_FLOAT_EQ(min_bounds.y, -2.0f);
    EXPECT_FLOAT_EQ(max_bounds.y, 1.0f);
    EXPECT_FLOAT_EQ(min_bounds.z, -3.0f);
    EXPECT_FLOAT_EQ(max_bounds.z, 3.0f);
}

TEST_F(Integration3DTest, SeriesCentroidComputation) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    std::vector<float> x = {0.0f, 2.0f, 4.0f};
    std::vector<float> y = {0.0f, 0.0f, 0.0f};
    std::vector<float> z = {0.0f, 0.0f, 0.0f};

    auto& line = ax.line3d(x, y, z);

    vec3 centroid = line.compute_centroid();

    EXPECT_FLOAT_EQ(centroid.x, 2.0f);
    EXPECT_FLOAT_EQ(centroid.y, 0.0f);
    EXPECT_FLOAT_EQ(centroid.z, 0.0f);
}

TEST_F(Integration3DTest, ScatterBoundsSymmetric) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    std::vector<float> x = {-5.0f, 5.0f};
    std::vector<float> y = {-5.0f, 5.0f};
    std::vector<float> z = {-5.0f, 5.0f};

    auto& scatter = ax.scatter3d(x, y, z);
    vec3 min_b, max_b;
    scatter.get_bounds(min_b, max_b);

    EXPECT_FLOAT_EQ(min_b.x, -5.0f);
    EXPECT_FLOAT_EQ(max_b.x, 5.0f);
    EXPECT_FLOAT_EQ(min_b.y, -5.0f);
    EXPECT_FLOAT_EQ(max_b.y, 5.0f);
    EXPECT_FLOAT_EQ(min_b.z, -5.0f);
    EXPECT_FLOAT_EQ(max_b.z, 5.0f);
}

// ─── Auto-Fit ───────────────────────────────────────────────────────────────

TEST_F(Integration3DTest, AutoFit3D) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    std::vector<float> x = {-5.0f, 5.0f};
    std::vector<float> y = {-10.0f, 10.0f};
    std::vector<float> z = {-2.0f, 2.0f};

    ax.scatter3d(x, y, z);
    ax.auto_fit();

    auto xlim = ax.x_limits();
    auto ylim = ax.y_limits();
    auto zlim = ax.z_limits();

    EXPECT_LE(xlim.min, -5.0f);
    EXPECT_GE(xlim.max, 5.0f);
    EXPECT_LE(ylim.min, -10.0f);
    EXPECT_GE(ylim.max, 10.0f);
    EXPECT_LE(zlim.min, -2.0f);
    EXPECT_GE(zlim.max, 2.0f);
}

TEST_F(Integration3DTest, AutoFitWithMultipleSeries) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    std::vector<float> x1 = {0.0f, 1.0f};
    std::vector<float> y1 = {0.0f, 1.0f};
    std::vector<float> z1 = {0.0f, 1.0f};
    ax.scatter3d(x1, y1, z1);

    std::vector<float> x2 = {-10.0f, 10.0f};
    std::vector<float> y2 = {-10.0f, 10.0f};
    std::vector<float> z2 = {-10.0f, 10.0f};
    ax.line3d(x2, y2, z2);

    ax.auto_fit();

    auto xlim = ax.x_limits();
    EXPECT_LE(xlim.min, -10.0f);
    EXPECT_GE(xlim.max, 10.0f);
}

// ─── Zoom Limits ────────────────────────────────────────────────────────────

TEST_F(Integration3DTest, ZoomLimitsScalesAxes) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    ax.xlim(-1.0f, 1.0f);
    ax.ylim(-1.0f, 1.0f);
    ax.zlim(-1.0f, 1.0f);

    // Zoom in by factor 0.5 — limits should shrink
    ax.zoom_limits(0.5f);

    auto xlim = ax.x_limits();
    EXPECT_GT(xlim.min, -1.0f);
    EXPECT_LT(xlim.max, 1.0f);
}

TEST_F(Integration3DTest, ZoomLimitsOutExpandsAxes) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    ax.xlim(-1.0f, 1.0f);
    ax.ylim(-1.0f, 1.0f);
    ax.zlim(-1.0f, 1.0f);

    // Zoom out by factor 2.0 — limits should expand
    ax.zoom_limits(2.0f);

    auto xlim = ax.x_limits();
    EXPECT_LT(xlim.min, -1.0f);
    EXPECT_GT(xlim.max, 1.0f);
}

// ─── Data-to-Normalized Matrix ──────────────────────────────────────────────

TEST_F(Integration3DTest, DataToNormalizedMatrixProducesValidTransform) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    ax.xlim(-5.0f, 5.0f);
    ax.ylim(-5.0f, 5.0f);
    ax.zlim(-5.0f, 5.0f);

    mat4 model = ax.data_to_normalized_matrix();

    // The model matrix should not be identity (it maps data to normalized box)
    mat4 identity = mat4_identity();
    bool is_identity = true;
    for (int i = 0; i < 16; ++i) {
        if (std::abs(model.m[i] - identity.m[i]) > 1e-6f) {
            is_identity = false;
            break;
        }
    }
    // For non-unit data range, model should differ from identity
    // (it scales data into [-box_half_size, +box_half_size])
    EXPECT_FALSE(is_identity);
}

TEST_F(Integration3DTest, BoxHalfSizeConstant) {
    EXPECT_FLOAT_EQ(Axes3D::box_half_size(), 3.0f);
}

// ─── Colormap ───────────────────────────────────────────────────────────────

TEST_F(Integration3DTest, SurfaceColormapSetting) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    const int nx = 5, ny = 5;
    std::vector<float> x_grid(nx), y_grid(ny), z_values(nx * ny, 0.0f);
    for (int i = 0; i < nx; ++i) x_grid[i] = static_cast<float>(i);
    for (int j = 0; j < ny; ++j) y_grid[j] = static_cast<float>(j);

    auto& surf = ax.surface(x_grid, y_grid, z_values);
    surf.colormap(ColormapType::Viridis);
    EXPECT_EQ(surf.colormap_type(), ColormapType::Viridis);

    surf.colormap(ColormapType::Jet);
    EXPECT_EQ(surf.colormap_type(), ColormapType::Jet);
}

TEST_F(Integration3DTest, ColormapSampling) {
    // Sample at t=0 and t=1 for each colormap — should produce valid colors
    for (int cm = 0; cm <= static_cast<int>(ColormapType::Grayscale); ++cm) {
        auto type = static_cast<ColormapType>(cm);
        Color c0 = SurfaceSeries::sample_colormap(type, 0.0f);
        Color c1 = SurfaceSeries::sample_colormap(type, 1.0f);

        EXPECT_GE(c0.r, 0.0f); EXPECT_LE(c0.r, 1.0f);
        EXPECT_GE(c0.g, 0.0f); EXPECT_LE(c0.g, 1.0f);
        EXPECT_GE(c0.b, 0.0f); EXPECT_LE(c0.b, 1.0f);
        EXPECT_GE(c1.r, 0.0f); EXPECT_LE(c1.r, 1.0f);
        EXPECT_GE(c1.g, 0.0f); EXPECT_LE(c1.g, 1.0f);
        EXPECT_GE(c1.b, 0.0f); EXPECT_LE(c1.b, 1.0f);
    }
}

TEST_F(Integration3DTest, ColormapRange) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    const int nx = 5, ny = 5;
    std::vector<float> x_grid(nx), y_grid(ny), z_values(nx * ny, 0.0f);
    for (int i = 0; i < nx; ++i) x_grid[i] = static_cast<float>(i);
    for (int j = 0; j < ny; ++j) y_grid[j] = static_cast<float>(j);

    auto& surf = ax.surface(x_grid, y_grid, z_values);
    surf.set_colormap_range(-5.0f, 5.0f);

    EXPECT_FLOAT_EQ(surf.colormap_min(), -5.0f);
    EXPECT_FLOAT_EQ(surf.colormap_max(), 5.0f);
}

// ─── Camera Animator Integration ────────────────────────────────────────────

TEST_F(Integration3DTest, CameraAnimatorOrbitPath) {
    CameraAnimator animator;
    animator.set_path_mode(CameraPathMode::Orbit);

    Camera cam1;
    cam1.azimuth = 0.0f;
    cam1.elevation = 30.0f;
    cam1.distance = 5.0f;

    Camera cam2;
    cam2.azimuth = 180.0f;
    cam2.elevation = 30.0f;
    cam2.distance = 5.0f;

    animator.add_keyframe(0.0f, cam1);
    animator.add_keyframe(2.0f, cam2);

    Camera mid = animator.evaluate(1.0f);
    EXPECT_NEAR(mid.azimuth, 90.0f, 1.0f);
    EXPECT_NEAR(mid.elevation, 30.0f, 1.0f);
}

TEST_F(Integration3DTest, CameraAnimatorTargetBinding) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    CameraAnimator animator;
    animator.set_path_mode(CameraPathMode::Orbit);

    Camera cam1;
    cam1.azimuth = 0.0f;
    cam1.elevation = 30.0f;
    cam1.distance = 5.0f;

    Camera cam2;
    cam2.azimuth = 360.0f;
    cam2.elevation = 30.0f;
    cam2.distance = 5.0f;

    animator.add_keyframe(0.0f, cam1);
    animator.add_keyframe(4.0f, cam2);

    animator.set_target_camera(&ax.camera());
    EXPECT_EQ(animator.target_camera(), &ax.camera());

    animator.evaluate_at(2.0f);
    EXPECT_NEAR(ax.camera().azimuth, 180.0f, 1.0f);
}

TEST_F(Integration3DTest, CameraAnimatorTurntable) {
    CameraAnimator animator;

    Camera base;
    base.azimuth = 0.0f;
    base.elevation = 30.0f;
    base.distance = 5.0f;

    animator.create_turntable(base, 4.0f);

    EXPECT_EQ(animator.keyframe_count(), 2u);
    EXPECT_NEAR(animator.duration(), 4.0f, 0.01f);

    // At t=2 (halfway), azimuth should be ~180
    Camera mid = animator.evaluate(2.0f);
    EXPECT_NEAR(mid.azimuth, 180.0f, 1.0f);
}

TEST_F(Integration3DTest, CameraAnimatorSerialization) {
    CameraAnimator animator;
    animator.set_path_mode(CameraPathMode::Orbit);

    Camera cam;
    cam.azimuth = 45.0f;
    cam.elevation = 30.0f;
    cam.distance = 7.0f;
    animator.add_keyframe(0.0f, cam);

    cam.azimuth = 135.0f;
    animator.add_keyframe(1.0f, cam);

    std::string json = animator.serialize();
    EXPECT_FALSE(json.empty());

    CameraAnimator restored;
    EXPECT_TRUE(restored.deserialize(json));
    EXPECT_EQ(restored.keyframe_count(), 2u);
}

// ─── Tick Computation ───────────────────────────────────────────────────────

TEST_F(Integration3DTest, TickComputation3D) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    ax.xlim(0.0f, 10.0f);
    ax.ylim(0.0f, 10.0f);
    ax.zlim(0.0f, 10.0f);

    auto x_ticks = ax.compute_x_ticks();
    auto y_ticks = ax.compute_y_ticks();
    auto z_ticks = ax.compute_z_ticks();

    EXPECT_GT(x_ticks.positions.size(), 0u);
    EXPECT_GT(y_ticks.positions.size(), 0u);
    EXPECT_GT(z_ticks.positions.size(), 0u);
    EXPECT_EQ(x_ticks.positions.size(), x_ticks.labels.size());
}

// ─── Multiple Series Types in One Axes ──────────────────────────────────────

TEST_F(Integration3DTest, MixedSeriesTypesIn3DAxes) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    std::vector<float> x = {0.0f, 1.0f, 2.0f};
    std::vector<float> y = {0.0f, 1.0f, 0.5f};
    std::vector<float> z = {0.0f, 0.5f, 1.0f};

    ax.scatter3d(x, y, z).color(colors::red).size(5.0f);
    ax.line3d(x, y, z).color(colors::blue).width(2.0f);

    EXPECT_EQ(ax.series().size(), 2u);
}

// ─── Clear Series 3D ────────────────────────────────────────────────────────

TEST_F(Integration3DTest, ClearSeries3D) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {0.0f, 1.0f};
    std::vector<float> z = {0.0f, 1.0f};

    ax.scatter3d(x, y, z);
    ax.line3d(x, y, z);
    EXPECT_EQ(ax.series().size(), 2u);

    ax.clear_series();
    EXPECT_EQ(ax.series().size(), 0u);
}

TEST_F(Integration3DTest, RemoveSingleSeries3D) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {0.0f, 1.0f};
    std::vector<float> z = {0.0f, 1.0f};

    ax.scatter3d(x, y, z).label("first");
    ax.line3d(x, y, z).label("second");
    EXPECT_EQ(ax.series().size(), 2u);

    bool removed = ax.remove_series(0);
    EXPECT_TRUE(removed);
    EXPECT_EQ(ax.series().size(), 1u);
}

// ─── 2D Regression ──────────────────────────────────────────────────────────

TEST_F(Integration3DTest, No2DRegressions) {
    auto& fig = app_->figure();

    auto& ax2d = fig.subplot(1, 1, 1);
    std::vector<float> x = {0.0f, 1.0f, 2.0f, 3.0f};
    std::vector<float> y = {0.0f, 1.0f, 4.0f, 9.0f};

    auto& line = ax2d.line(x, y).color(colors::blue).width(2.0f);
    auto& scatter = ax2d.scatter(x, y).color(colors::red).size(5.0f);

    EXPECT_EQ(line.point_count(), 4u);
    EXPECT_EQ(scatter.point_count(), 4u);

    SUCCEED();
}

// ─── Render Smoke Tests ─────────────────────────────────────────────────────

TEST_F(Integration3DTest, RenderScatter3DSmoke) {
    auto& fig = app_->figure({.width = 128, .height = 128});
    auto& ax = fig.subplot3d(1, 1, 1);

    std::vector<float> x, y, z;
    for (int i = 0; i < 100; ++i) {
        float t = static_cast<float>(i) * 0.1f;
        x.push_back(std::cos(t));
        y.push_back(std::sin(t));
        z.push_back(t * 0.1f);
    }
    ax.scatter3d(x, y, z).color(colors::blue).size(4.0f);

    EXPECT_NO_FATAL_FAILURE(app_->run());
}

TEST_F(Integration3DTest, RenderLine3DSmoke) {
    auto& fig = app_->figure({.width = 128, .height = 128});
    auto& ax = fig.subplot3d(1, 1, 1);

    std::vector<float> x, y, z;
    for (int i = 0; i < 50; ++i) {
        float t = static_cast<float>(i) * 0.2f;
        x.push_back(std::cos(t));
        y.push_back(std::sin(t));
        z.push_back(t * 0.1f);
    }
    ax.line3d(x, y, z).color(colors::green).width(2.0f);

    EXPECT_NO_FATAL_FAILURE(app_->run());
}

TEST_F(Integration3DTest, RenderSurfaceSmoke) {
    auto& fig = app_->figure({.width = 128, .height = 128});
    auto& ax = fig.subplot3d(1, 1, 1);

    const int nx = 10, ny = 10;
    std::vector<float> x_grid(nx), y_grid(ny), z_values(nx * ny);
    for (int i = 0; i < nx; ++i) x_grid[i] = static_cast<float>(i) - 5.0f;
    for (int j = 0; j < ny; ++j) y_grid[j] = static_cast<float>(j) - 5.0f;
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i)
            z_values[j * nx + i] = std::sin(x_grid[i]) * std::cos(y_grid[j]);

    ax.surface(x_grid, y_grid, z_values).color(colors::orange);

    EXPECT_NO_FATAL_FAILURE(app_->run());
}

TEST_F(Integration3DTest, RenderMeshSmoke) {
    auto& fig = app_->figure({.width = 128, .height = 128});
    auto& ax = fig.subplot3d(1, 1, 1);

    // Simple quad (2 triangles)
    std::vector<float> vertices = {
        0.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,
        1.0f, 1.0f, 0.0f,  0.0f, 0.0f, 1.0f,
        0.0f, 1.0f, 0.0f,  0.0f, 0.0f, 1.0f,
    };
    std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};

    ax.mesh(vertices, indices).color(colors::cyan);

    EXPECT_NO_FATAL_FAILURE(app_->run());
}

TEST_F(Integration3DTest, RenderMixed2DAnd3DSmoke) {
    auto& fig = app_->figure({.width = 256, .height = 512});

    auto& ax2d = fig.subplot(2, 1, 1);
    std::vector<float> x2d = {0.0f, 1.0f, 2.0f, 3.0f};
    std::vector<float> y2d = {0.0f, 1.0f, 0.5f, 1.5f};
    ax2d.line(x2d, y2d).color(colors::blue);

    auto& ax3d = fig.subplot3d(2, 1, 2);
    std::vector<float> x3d = {0.0f, 1.0f, 2.0f};
    std::vector<float> y3d = {0.0f, 1.0f, 0.5f};
    std::vector<float> z3d = {0.0f, 0.5f, 1.0f};
    ax3d.scatter3d(x3d, y3d, z3d).color(colors::red);

    EXPECT_NO_FATAL_FAILURE(app_->run());
}

// ─── Edge Cases ─────────────────────────────────────────────────────────────

TEST_F(Integration3DTest, SinglePoint3D) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    std::vector<float> x = {1.0f};
    std::vector<float> y = {2.0f};
    std::vector<float> z = {3.0f};

    auto& scatter = ax.scatter3d(x, y, z);
    EXPECT_EQ(scatter.point_count(), 1u);

    vec3 centroid = scatter.compute_centroid();
    EXPECT_FLOAT_EQ(centroid.x, 1.0f);
    EXPECT_FLOAT_EQ(centroid.y, 2.0f);
    EXPECT_FLOAT_EQ(centroid.z, 3.0f);
}

TEST_F(Integration3DTest, EmptyAxes3DAutoFit) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    // auto_fit on empty axes should not crash
    EXPECT_NO_FATAL_FAILURE(ax.auto_fit());
}

TEST_F(Integration3DTest, LargeDataset3D) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    const size_t N = 10000;
    std::vector<float> x(N), y(N), z(N);
    for (size_t i = 0; i < N; ++i) {
        float t = static_cast<float>(i) * 0.001f;
        x[i] = std::cos(t) * t;
        y[i] = std::sin(t) * t;
        z[i] = t;
    }

    auto& scatter = ax.scatter3d(x, y, z);
    EXPECT_EQ(scatter.point_count(), N);
}

TEST_F(Integration3DTest, NegativeAxisLimits) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);

    ax.xlim(-100.0f, -50.0f);
    ax.ylim(-200.0f, -100.0f);
    ax.zlim(-300.0f, -200.0f);

    auto xlim = ax.x_limits();
    EXPECT_FLOAT_EQ(xlim.min, -100.0f);
    EXPECT_FLOAT_EQ(xlim.max, -50.0f);
}
