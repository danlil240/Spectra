#include <cmath>
#include <gtest/gtest.h>

#include "../../src/ui/camera.hpp"

using namespace spectra;

constexpr float EPSILON = 1e-5f;

bool vec3_near(vec3 a, vec3 b, float eps = EPSILON)
{
    return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps && std::fabs(a.z - b.z) < eps;
}

bool mat4_near(const mat4& a, const mat4& b, float eps = EPSILON)
{
    for (int i = 0; i < 16; ++i)
    {
        if (std::fabs(a.m[i] - b.m[i]) > eps)
            return false;
    }
    return true;
}

TEST(CameraTest, DefaultConstruction)
{
    Camera cam;
    EXPECT_EQ(cam.position, vec3(0.0f, 0.0f, 5.0f));
    EXPECT_EQ(cam.target, vec3(0.0f, 0.0f, 0.0f));
    EXPECT_EQ(cam.up, vec3(0.0f, 1.0f, 0.0f));
    EXPECT_EQ(cam.projection_mode, Camera::ProjectionMode::Perspective);
    EXPECT_FLOAT_EQ(cam.fov, 45.0f);
    EXPECT_FLOAT_EQ(cam.near_clip, 0.01f);
    EXPECT_FLOAT_EQ(cam.far_clip, 1000.0f);
    EXPECT_FLOAT_EQ(cam.ortho_size, 10.0f);
    EXPECT_FLOAT_EQ(cam.azimuth, 45.0f);
    EXPECT_FLOAT_EQ(cam.elevation, 30.0f);
    EXPECT_FLOAT_EQ(cam.distance, 5.0f);
}

TEST(CameraTest, ViewMatrixIdentity)
{
    Camera cam;
    cam.position = {0, 0, 0};
    cam.target   = {0, 0, -1};
    cam.up       = {0, 1, 0};

    mat4 view = cam.view_matrix();

    vec4 origin = mat4_mul_vec4(view, {0, 0, 0, 1});
    EXPECT_TRUE(vec3_near(origin.xyz(), {0, 0, 0}));
}

TEST(CameraTest, ViewMatrixTranslation)
{
    Camera cam;
    cam.position = {0, 0, 5};
    cam.target   = {0, 0, 0};
    cam.up       = {0, 1, 0};

    mat4 view = cam.view_matrix();

    vec4 world_origin = mat4_mul_vec4(view, {0, 0, 0, 1});
    EXPECT_TRUE(vec3_near(world_origin.xyz(), {0, 0, -5}, 1e-4f));
}

TEST(CameraTest, PerspectiveProjection)
{
    Camera cam;
    cam.projection_mode = Camera::ProjectionMode::Perspective;
    cam.fov             = 90.0f;
    cam.near_clip       = 0.1f;
    cam.far_clip        = 100.0f;

    mat4 proj = cam.projection_matrix(1.0f);

    EXPECT_NE(proj.m[0], 0.0f);
    EXPECT_NE(proj.m[5], 0.0f);
    EXPECT_NE(proj.m[10], 0.0f);
    EXPECT_FLOAT_EQ(proj.m[11], -1.0f);
}

TEST(CameraTest, OrthographicProjection)
{
    Camera cam;
    cam.projection_mode = Camera::ProjectionMode::Orthographic;
    cam.ortho_size      = 10.0f;
    cam.near_clip       = 0.1f;
    cam.far_clip        = 100.0f;

    mat4 proj = cam.projection_matrix(1.0f);

    EXPECT_NE(proj.m[0], 0.0f);
    EXPECT_NE(proj.m[5], 0.0f);
    EXPECT_NE(proj.m[10], 0.0f);
    EXPECT_FLOAT_EQ(proj.m[15], 1.0f);
}

TEST(CameraTest, OrbitAzimuth)
{
    Camera cam;
    cam.target    = {0, 0, 0};
    cam.azimuth   = 0.0f;
    cam.elevation = 0.0f;
    cam.distance  = 5.0f;
    cam.update_position_from_orbit();

    vec3 initial_pos = cam.position;
    EXPECT_TRUE(vec3_near(initial_pos, {5, 0, 0}, 1e-4f));

    cam.orbit(90.0f, 0.0f);
    EXPECT_FLOAT_EQ(cam.azimuth, 90.0f);
    EXPECT_TRUE(vec3_near(cam.position, {0, 0, 5}, 1e-4f));
}

TEST(CameraTest, OrbitElevation)
{
    Camera cam;
    cam.target    = {0, 0, 0};
    cam.azimuth   = 0.0f;
    cam.elevation = 0.0f;
    cam.distance  = 5.0f;
    cam.update_position_from_orbit();

    cam.orbit(0.0f, 45.0f);
    EXPECT_FLOAT_EQ(cam.elevation, 45.0f);
    EXPECT_GT(cam.position.y, 0.0f);
    EXPECT_NEAR(vec3_length(cam.position - cam.target), 5.0f, 1e-4f);
}

TEST(CameraTest, OrbitElevationClamping)
{
    Camera cam;
    cam.elevation = 0.0f;

    cam.orbit(0.0f, 100.0f);
    EXPECT_FLOAT_EQ(cam.elevation, 89.0f);

    cam.orbit(0.0f, -200.0f);
    EXPECT_FLOAT_EQ(cam.elevation, -89.0f);
}

TEST(CameraTest, OrbitAzimuthWrapping)
{
    Camera cam;
    cam.azimuth = 350.0f;

    cam.orbit(20.0f, 0.0f);
    EXPECT_FLOAT_EQ(cam.azimuth, 10.0f);

    cam.orbit(-20.0f, 0.0f);
    EXPECT_FLOAT_EQ(cam.azimuth, 350.0f);
}

TEST(CameraTest, Pan)
{
    Camera cam;
    cam.position = {0, 0, 5};
    cam.target   = {0, 0, 0};
    cam.up       = {0, 1, 0};
    cam.distance = 5.0f;

    vec3 initial_pos    = cam.position;
    vec3 initial_target = cam.target;

    cam.pan(100.0f, 0.0f, 800.0f, 600.0f);

    vec3 delta_pos    = cam.position - initial_pos;
    vec3 delta_target = cam.target - initial_target;

    EXPECT_TRUE(vec3_near(delta_pos, delta_target, 1e-4f));
    EXPECT_LT(cam.position.x, initial_pos.x);
}

TEST(CameraTest, ZoomPerspective)
{
    Camera cam;
    cam.projection_mode = Camera::ProjectionMode::Perspective;
    cam.distance        = 10.0f;
    cam.azimuth         = 0.0f;
    cam.elevation       = 0.0f;
    cam.target          = {0, 0, 0};
    cam.update_position_from_orbit();

    float initial_distance = cam.distance;

    cam.zoom(0.5f);
    EXPECT_FLOAT_EQ(cam.distance, initial_distance * 0.5f);
    EXPECT_NEAR(vec3_length(cam.position - cam.target), cam.distance, 1e-4f);
}

TEST(CameraTest, ZoomOrthographic)
{
    Camera cam;
    cam.projection_mode = Camera::ProjectionMode::Orthographic;
    cam.ortho_size      = 10.0f;

    cam.zoom(0.5f);
    EXPECT_FLOAT_EQ(cam.ortho_size, 5.0f);
}

TEST(CameraTest, ZoomClamping)
{
    Camera cam;
    cam.projection_mode = Camera::ProjectionMode::Perspective;
    cam.distance        = 1.0f;

    cam.zoom(0.01f);
    EXPECT_GE(cam.distance, 0.1f);

    cam.distance = 1000.0f;
    cam.zoom(100.0f);
    EXPECT_LE(cam.distance, 10000.0f);
}

TEST(CameraTest, Dolly)
{
    Camera cam;
    cam.position = {0, 0, 10};
    cam.target   = {0, 0, 0};
    cam.up       = {0, 1, 0};

    float initial_distance = vec3_length(cam.position - cam.target);

    cam.dolly(2.0f);

    float new_distance = vec3_length(cam.position - cam.target);
    EXPECT_LT(new_distance, initial_distance);
    EXPECT_FLOAT_EQ(cam.distance, new_distance);
}

TEST(CameraTest, FitToBoundsPerspective)
{
    Camera cam;
    cam.projection_mode = Camera::ProjectionMode::Perspective;
    cam.fov             = 45.0f;

    vec3 min_bound{-1, -1, -1};
    vec3 max_bound{1, 1, 1};

    cam.fit_to_bounds(min_bound, max_bound);

    EXPECT_TRUE(vec3_near(cam.target, {0, 0, 0}, 1e-4f));
    EXPECT_GT(cam.distance, 2.0f);
}

TEST(CameraTest, FitToBoundsOrthographic)
{
    Camera cam;
    cam.projection_mode = Camera::ProjectionMode::Orthographic;

    vec3 min_bound{-5, -5, -5};
    vec3 max_bound{5, 5, 5};

    cam.fit_to_bounds(min_bound, max_bound);

    EXPECT_TRUE(vec3_near(cam.target, {0, 0, 0}, 1e-4f));
    EXPECT_GT(cam.ortho_size, 0.0f);
}

TEST(CameraTest, FitToBoundsDegenerateBox)
{
    Camera cam;

    vec3 min_bound{0, 0, 0};
    vec3 max_bound{0, 0, 0};

    cam.fit_to_bounds(min_bound, max_bound);

    EXPECT_GT(cam.distance, 0.0f);
}

TEST(CameraTest, Reset)
{
    Camera cam;
    cam.position  = {10, 20, 30};
    cam.target    = {5, 5, 5};
    cam.azimuth   = 180.0f;
    cam.elevation = 60.0f;
    cam.distance  = 100.0f;
    cam.fov       = 90.0f;

    cam.reset();

    EXPECT_EQ(cam.position, vec3(0.0f, 0.0f, 5.0f));
    EXPECT_EQ(cam.target, vec3(0.0f, 0.0f, 0.0f));
    EXPECT_EQ(cam.up, vec3(0.0f, 1.0f, 0.0f));
    EXPECT_FLOAT_EQ(cam.azimuth, 45.0f);
    EXPECT_FLOAT_EQ(cam.elevation, 30.0f);
    EXPECT_FLOAT_EQ(cam.distance, 5.0f);
    EXPECT_FLOAT_EQ(cam.fov, 45.0f);
}

TEST(CameraTest, UpdatePositionFromOrbit)
{
    Camera cam;
    cam.target    = {0, 0, 0};
    cam.azimuth   = 0.0f;
    cam.elevation = 0.0f;
    cam.distance  = 10.0f;

    cam.update_position_from_orbit();
    EXPECT_TRUE(vec3_near(cam.position, {10, 0, 0}, 1e-4f));

    cam.azimuth = 90.0f;
    cam.update_position_from_orbit();
    EXPECT_TRUE(vec3_near(cam.position, {0, 0, 10}, 1e-4f));

    cam.azimuth = 180.0f;
    cam.update_position_from_orbit();
    EXPECT_TRUE(vec3_near(cam.position, {-10, 0, 0}, 1e-4f));

    cam.azimuth = 270.0f;
    cam.update_position_from_orbit();
    EXPECT_TRUE(vec3_near(cam.position, {0, 0, -10}, 1e-4f));
}

TEST(CameraTest, UpdatePositionFromOrbitElevation)
{
    Camera cam;
    cam.target    = {0, 0, 0};
    cam.azimuth   = 0.0f;
    cam.elevation = 90.0f;
    cam.distance  = 10.0f;

    cam.update_position_from_orbit();
    EXPECT_TRUE(vec3_near(cam.position, {0, 10, 0}, 1e-4f));

    cam.elevation = -90.0f;
    cam.update_position_from_orbit();
    EXPECT_TRUE(vec3_near(cam.position, {0, -10, 0}, 1e-4f));
}

TEST(CameraTest, SerializeDeserialize)
{
    Camera cam1;
    cam1.position        = {1, 2, 3};
    cam1.target          = {4, 5, 6};
    cam1.up              = {0, 1, 0};
    cam1.projection_mode = Camera::ProjectionMode::Orthographic;
    cam1.fov             = 60.0f;
    cam1.near_clip       = 0.5f;
    cam1.far_clip        = 500.0f;
    cam1.ortho_size      = 15.0f;
    cam1.azimuth         = 120.0f;
    cam1.elevation       = 45.0f;
    cam1.distance        = 7.5f;

    std::string json = cam1.serialize();

    Camera cam2;
    cam2.deserialize(json);

    EXPECT_TRUE(vec3_near(cam2.position, cam1.position));
    EXPECT_TRUE(vec3_near(cam2.target, cam1.target));
    EXPECT_TRUE(vec3_near(cam2.up, cam1.up));
    EXPECT_EQ(cam2.projection_mode, cam1.projection_mode);
    EXPECT_NEAR(cam2.fov, cam1.fov, 1e-4f);
    EXPECT_NEAR(cam2.near_clip, cam1.near_clip, 1e-4f);
    EXPECT_NEAR(cam2.far_clip, cam1.far_clip, 1e-4f);
    EXPECT_NEAR(cam2.ortho_size, cam1.ortho_size, 1e-4f);
    EXPECT_NEAR(cam2.azimuth, cam1.azimuth, 1e-4f);
    EXPECT_NEAR(cam2.elevation, cam1.elevation, 1e-4f);
    EXPECT_NEAR(cam2.distance, cam1.distance, 1e-4f);
}

TEST(CameraTest, SerializePerspective)
{
    Camera cam;
    cam.projection_mode = Camera::ProjectionMode::Perspective;

    std::string json = cam.serialize();
    EXPECT_NE(json.find("\"projection_mode\":0"), std::string::npos);
}

TEST(CameraTest, ViewProjectionComposition)
{
    Camera cam;
    cam.position        = {0, 0, 5};
    cam.target          = {0, 0, 0};
    cam.up              = {0, 1, 0};
    cam.projection_mode = Camera::ProjectionMode::Perspective;
    cam.fov             = 45.0f;
    cam.near_clip       = 0.1f;
    cam.far_clip        = 100.0f;

    mat4 view = cam.view_matrix();
    mat4 proj = cam.projection_matrix(1.0f);
    mat4 vp   = mat4_mul(proj, view);

    vec4 world_point = {0, 0, 0, 1};
    vec4 clip_point  = mat4_mul_vec4(vp, world_point);

    EXPECT_NE(clip_point.w, 0.0f);
}

TEST(CameraTest, DistancePreservation)
{
    Camera cam;
    cam.target    = {0, 0, 0};
    cam.azimuth   = 45.0f;
    cam.elevation = 30.0f;
    cam.distance  = 10.0f;

    cam.update_position_from_orbit();
    float actual_distance = vec3_length(cam.position - cam.target);
    EXPECT_NEAR(actual_distance, cam.distance, 1e-4f);

    cam.orbit(90.0f, 20.0f);
    actual_distance = vec3_length(cam.position - cam.target);
    EXPECT_NEAR(actual_distance, cam.distance, 1e-4f);
}

TEST(CameraTest, PanPreservesDistance)
{
    Camera cam;
    cam.position = {0, 0, 10};
    cam.target   = {0, 0, 0};
    cam.distance = 10.0f;

    float initial_distance = vec3_length(cam.position - cam.target);

    cam.pan(50.0f, 50.0f, 800.0f, 600.0f);

    float new_distance = vec3_length(cam.position - cam.target);
    EXPECT_NEAR(new_distance, initial_distance, 1e-3f);
}

TEST(CameraTest, MultipleOrbits)
{
    Camera cam;
    cam.target    = {0, 0, 0};
    cam.azimuth   = 0.0f;
    cam.elevation = 0.0f;
    cam.distance  = 5.0f;
    cam.update_position_from_orbit();

    for (int i = 0; i < 10; ++i)
    {
        cam.orbit(36.0f, 0.0f);
    }

    EXPECT_NEAR(cam.azimuth, 0.0f, 1e-3f);
    EXPECT_TRUE(vec3_near(cam.position, {5, 0, 0}, 1e-3f));
}

TEST(CameraTest, AspectRatioEffect)
{
    Camera cam;
    cam.projection_mode = Camera::ProjectionMode::Perspective;
    cam.fov             = 45.0f;

    mat4 proj1 = cam.projection_matrix(1.0f);
    mat4 proj2 = cam.projection_matrix(2.0f);

    EXPECT_NE(proj1.m[0], proj2.m[0]);
}

TEST(CameraTest, NearFarClipping)
{
    Camera cam;
    cam.near_clip = 1.0f;
    cam.far_clip  = 100.0f;

    mat4 proj = cam.projection_matrix(1.0f);

    EXPECT_NE(proj.m[10], 0.0f);
    EXPECT_NE(proj.m[14], 0.0f);
}
