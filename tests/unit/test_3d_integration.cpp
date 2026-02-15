#include <plotix/plotix.hpp>
#include <gtest/gtest.h>

using namespace plotix;

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

TEST_F(Integration3DTest, GridPlaneConfiguration) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);
    
    ax.set_grid_planes(static_cast<int>(Axes3D::GridPlane::XY));
    EXPECT_EQ(ax.grid_planes(), static_cast<int>(Axes3D::GridPlane::XY));
    
    ax.set_grid_planes(static_cast<int>(Axes3D::GridPlane::All));
    EXPECT_EQ(ax.grid_planes(), static_cast<int>(Axes3D::GridPlane::All));
    
    ax.set_grid_planes(static_cast<int>(Axes3D::GridPlane::None));
    EXPECT_EQ(ax.grid_planes(), static_cast<int>(Axes3D::GridPlane::None));
}

TEST_F(Integration3DTest, BoundingBoxToggle) {
    auto& fig = app_->figure();
    auto& ax = fig.subplot3d(1, 1, 1);
    
    EXPECT_TRUE(ax.show_bounding_box());
    
    ax.show_bounding_box(false);
    EXPECT_FALSE(ax.show_bounding_box());
    
    ax.show_bounding_box(true);
    EXPECT_TRUE(ax.show_bounding_box());
}

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
    EXPECT_GT(mesh.vertex_count, 0);
    EXPECT_GT(mesh.triangle_count, 0);
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
    
    EXPECT_EQ(mesh.vertex_count(), 3);
    EXPECT_EQ(mesh.triangle_count(), 1);
}

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

TEST_F(Integration3DTest, No2DRegressions) {
    auto& fig = app_->figure();
    
    auto& ax2d = fig.subplot(1, 1, 1);
    std::vector<float> x = {0.0f, 1.0f, 2.0f, 3.0f};
    std::vector<float> y = {0.0f, 1.0f, 4.0f, 9.0f};
    
    auto& line = ax2d.line(x, y).color(colors::blue).width(2.0f);
    auto& scatter = ax2d.scatter(x, y).color(colors::red).size(5.0f);
    
    EXPECT_EQ(line.point_count(), 4);
    EXPECT_EQ(scatter.point_count(), 4);
    
    SUCCEED();
}
