#include <gtest/gtest.h>

#include "scene/scene_manager.hpp"

using namespace spectra::adapters::ros2;

TEST(SceneManager, PickReturnsNearestHitAlongRay)
{
    SceneManager scene;
    scene.add_entity(SceneEntity{
        .type = "marker",
        .label = "Near",
        .transform = spectra::Transform{{0.0, 0.0, -2.0}, spectra::quat_identity()},
        .scale = {1.0, 1.0, 1.0},
    });
    scene.add_entity(SceneEntity{
        .type = "marker",
        .label = "Far",
        .transform = spectra::Transform{{0.0, 0.0, -5.0}, spectra::quat_identity()},
        .scale = {1.0, 1.0, 1.0},
    });

    const auto picked = scene.pick(spectra::Ray{{0.0, 0.0, 0.0}, {0.0, 0.0, -1.0}});
    ASSERT_TRUE(picked.has_value());
    EXPECT_EQ(*picked, 0u);
}

TEST(SceneManager, PickUsesPolylineBounds)
{
    SceneManager scene;
    SceneEntity path;
    path.type = "path";
    path.label = "/plan";
    path.transform.translation = {0.0, 0.0, -3.0};
    path.polyline = ScenePolyline{{spectra::vec3{-1.0, 0.0, 0.0}, spectra::vec3{1.0, 0.0, 0.0}}};
    scene.add_entity(std::move(path));

    const auto picked = scene.pick(spectra::Ray{{0.5, 0.0, 0.0}, {0.0, 0.0, -1.0}});
    ASSERT_TRUE(picked.has_value());
    EXPECT_EQ(*picked, 0u);
}

TEST(SceneManager, PickMissReturnsNullopt)
{
    SceneManager scene;
    scene.add_entity(SceneEntity{
        .type = "marker",
        .label = "OffAxis",
        .transform = spectra::Transform{{3.0, 0.0, -2.0}, spectra::quat_identity()},
        .scale = {0.5, 0.5, 0.5},
    });

    EXPECT_FALSE(scene.pick(spectra::Ray{{0.0, 0.0, 0.0}, {0.0, 0.0, -1.0}}).has_value());
}

TEST(SceneManager, SelectionPersistsByStableIdentityAcrossRebuild)
{
    SceneManager scene;
    scene.add_entity(SceneEntity{
        .type = "marker",
        .label = "Marker",
        .display_name = "Markers",
        .topic = "/markers_a",
    });
    scene.add_entity(SceneEntity{
        .type = "marker",
        .label = "Marker",
        .display_name = "Markers",
        .topic = "/markers_b",
    });
    scene.set_selected_index(1u);

    scene.clear();
    scene.add_entity(SceneEntity{
        .type = "marker",
        .label = "Marker",
        .display_name = "Markers",
        .topic = "/markers_a",
    });
    scene.add_entity(SceneEntity{
        .type = "marker",
        .label = "Marker",
        .display_name = "Markers",
        .topic = "/markers_b",
    });

    ASSERT_TRUE(scene.selected_index().has_value());
    EXPECT_EQ(*scene.selected_index(), 1u);
    ASSERT_NE(scene.selected_entity(), nullptr);
    EXPECT_EQ(scene.selected_entity()->topic, "/markers_b");
}
