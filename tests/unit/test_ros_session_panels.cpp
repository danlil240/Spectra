// test_ros_session_panels.cpp — Panel visibility map + legacy migration (Phase 1.3).

#include <gtest/gtest.h>

#include <string>

#include "ros_session.hpp"

namespace ros2 = spectra::adapters::ros2;
using ros2::PanelVisibility;
using ros2::RosSession;
using ros2::RosSessionManager;
using ros2::default_panel_visibility;
using ros2::panel_legacy_key_to_id;

TEST(RosSessionPanels, LegacyKeyMapsToRegistryId)
{
    EXPECT_EQ(panel_legacy_key_to_id("topic_list"), "ros.topic_list");
    EXPECT_EQ(panel_legacy_key_to_id("displays_panel"), "ros.displays");
    EXPECT_EQ(panel_legacy_key_to_id("inspector_panel"), "ros.inspector");
    EXPECT_EQ(panel_legacy_key_to_id("ros.plot_area"), "ros.plot_area");
}

TEST(RosSessionPanels, RoundTripRegistryIds)
{
    RosSession s;
    s.panels.set_visible("ros.topic_list", false);
    s.panels.set_visible("ros.topic_echo", true);
    s.panels.set_visible("ros.plot_area", true);
    s.panels.set_visible("ros.displays", true);
    s.panels.set_visible("ros.inspector", false);
    s.panels.nav_rail = false;

    RosSession  out;
    std::string err;
    const auto  json = RosSessionManager::serialize(s);
    ASSERT_TRUE(RosSessionManager::deserialize(json, out, err)) << err;

    EXPECT_FALSE(out.panels.visible("ros.topic_list"));
    EXPECT_TRUE(out.panels.visible("ros.topic_echo"));
    EXPECT_TRUE(out.panels.visible("ros.plot_area"));
    EXPECT_TRUE(out.panels.visible("ros.displays"));
    EXPECT_FALSE(out.panels.visible("ros.inspector"));
    EXPECT_FALSE(out.panels.nav_rail);
}

TEST(RosSessionPanels, LegacyVersion2PanelsMigrateToRegistryIds)
{
    const std::string json = R"({
  "version": 2,
  "ui": {
    "panels": {
      "topic_list": false,
      "plot_area": true,
      "displays_panel": true,
      "inspector_panel": true,
      "nav_rail": false
    },
    "nav_rail": {
      "visible": false,
      "expanded": false,
      "width": 220.0
    }
  }
})";

    RosSession  out;
    std::string err;
    ASSERT_TRUE(RosSessionManager::deserialize(json, out, err)) << err;

    EXPECT_FALSE(out.panels.visible("ros.topic_list"));
    EXPECT_TRUE(out.panels.visible("ros.plot_area"));
    EXPECT_TRUE(out.panels.visible("ros.displays"));
    EXPECT_TRUE(out.panels.visible("ros.inspector"));
    EXPECT_FALSE(out.panels.nav_rail);
}

TEST(RosSessionPanels, LegacyVersion1PanelsMigrateToRegistryIds)
{
    const std::string json = R"({
  "version": 1,
  "panels": {
    "topic_list": false,
    "bag_info": true,
    "bag_playback": true,
    "nav_rail": false
  }
})";

    RosSession  out;
    std::string err;
    ASSERT_TRUE(RosSessionManager::deserialize(json, out, err)) << err;

    EXPECT_FALSE(out.panels.visible("ros.topic_list"));
    EXPECT_TRUE(out.panels.visible("ros.bag_info"));
    EXPECT_TRUE(out.panels.visible("ros.bag_playback"));
    EXPECT_FALSE(out.panels.nav_rail);
}

TEST(RosSessionPanels, MissingPanelsUseDefaults)
{
    const std::string json = R"({"version": 2, "ui": {}})";

    RosSession  out;
    std::string err;
    ASSERT_TRUE(RosSessionManager::deserialize(json, out, err)) << err;

    const PanelVisibility defaults = default_panel_visibility();
    EXPECT_EQ(out.panels.visible("ros.topic_list"), defaults.visible("ros.topic_list"));
    EXPECT_EQ(out.panels.visible("ros.bag_info"), defaults.visible("ros.bag_info"));
    EXPECT_EQ(out.panels.nav_rail, defaults.nav_rail);
}

TEST(RosSessionPanels, SerializeWritesRegistryIds)
{
    RosSession s;
    s.panels.set_visible("ros.topic_list", true);
    s.panels.set_visible("ros.inspector", true);

    const std::string json = RosSessionManager::serialize(s);
    EXPECT_NE(json.find("\"ros.topic_list\": true"), std::string::npos);
    EXPECT_NE(json.find("\"ros.inspector\": true"), std::string::npos);
    EXPECT_EQ(json.find("\"topic_list\":"), std::string::npos);
    EXPECT_EQ(json.find("\"inspector_panel\":"), std::string::npos);
}
