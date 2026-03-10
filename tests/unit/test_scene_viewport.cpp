#include <cmath>

#include <gtest/gtest.h>

#include <spectra/camera.hpp>

#include "adapters/ros2/ui/scene_viewport.hpp"

namespace ros2 = spectra::adapters::ros2;

TEST(SceneViewportTest, DefaultCameraUsesZUpAxis)
{
    ros2::SceneViewport viewport;

    EXPECT_EQ(viewport.camera().up_axis, spectra::Camera::UpAxis::Z);
    EXPECT_FLOAT_EQ(viewport.camera().up.x, 0.0f);
    EXPECT_FLOAT_EQ(viewport.camera().up.y, 0.0f);
    EXPECT_FLOAT_EQ(viewport.camera().up.z, 1.0f);
}

TEST(SceneViewportTest, SetCameraNormalizesRestoredCameraToZUp)
{
    ros2::SceneViewport viewport;

    spectra::Camera restored;
    restored.reset();
    restored.set_azimuth(90.0f);
    restored.set_elevation(30.0f);
    restored.set_distance(12.0f);
    restored.set_target({1.0f, 2.0f, 3.0f});
    restored.set_projection(spectra::Camera::ProjectionMode::Orthographic);
    restored.set_fov(55.0f);

    ASSERT_EQ(restored.up_axis, spectra::Camera::UpAxis::Y);

    viewport.set_camera(restored);

    const spectra::Camera& camera = viewport.camera();
    EXPECT_EQ(camera.up_axis, spectra::Camera::UpAxis::Z);
    EXPECT_FLOAT_EQ(camera.azimuth, 90.0f);
    EXPECT_FLOAT_EQ(camera.elevation, 30.0f);
    EXPECT_FLOAT_EQ(camera.distance, 12.0f);
    EXPECT_FLOAT_EQ(camera.target.x, 1.0f);
    EXPECT_FLOAT_EQ(camera.target.y, 2.0f);
    EXPECT_FLOAT_EQ(camera.target.z, 3.0f);
    EXPECT_EQ(camera.projection_mode, spectra::Camera::ProjectionMode::Orthographic);
    EXPECT_FLOAT_EQ(camera.fov, 55.0f);
    EXPECT_FLOAT_EQ(camera.up.x, 0.0f);
    EXPECT_FLOAT_EQ(camera.up.y, 0.0f);
    EXPECT_FLOAT_EQ(camera.up.z, 1.0f);

    EXPECT_NEAR(camera.position.x, 1.0f, 1e-4f);
    EXPECT_NEAR(camera.position.y, 2.0f + 12.0f * std::cos(spectra::deg_to_rad(30.0f)), 1e-4f);
    EXPECT_NEAR(camera.position.z, 3.0f + 12.0f * std::sin(spectra::deg_to_rad(30.0f)), 1e-4f);
}