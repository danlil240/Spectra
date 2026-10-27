// Tests for WindowManager::dispatch_file_drop — the shared routing used by
// both the GLFW drop callback and the SDL3 SDL_EVENT_DROP_FILE case.
// Headless: no windows or Vulkan device are created.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "render/vulkan/window_context.hpp"
#include "ui/app/window_ui_context.hpp"
#include "ui/window/window_manager.hpp"

using namespace spectra;

TEST(WindowFileDrop, NoHandlerReturnsFalse)
{
    WindowManager wm;
    EXPECT_FALSE(wm.dispatch_file_drop(1, "/tmp/data.csv"));
}

TEST(WindowFileDrop, ForwardsWindowIdAndPath)
{
    WindowManager                                 wm;
    std::vector<std::pair<uint32_t, std::string>> calls;
    wm.set_file_drop_handler([&calls](uint32_t wid, const std::string& path)
                             { calls.emplace_back(wid, path); });

    EXPECT_TRUE(wm.dispatch_file_drop(7, "/data/run.csv"));
    EXPECT_TRUE(wm.dispatch_file_drop(0, "/data/flight.ulg"));

    ASSERT_EQ(calls.size(), 2u);
    EXPECT_EQ(calls[0].first, 7u);
    EXPECT_EQ(calls[0].second, "/data/run.csv");
    EXPECT_EQ(calls[1].first, 0u);
    EXPECT_EQ(calls[1].second, "/data/flight.ulg");
}

TEST(WindowFileDrop, IgnoresNullAndEmptyPaths)
{
    WindowManager wm;
    int           count = 0;
    wm.set_file_drop_handler([&count](uint32_t, const std::string&) { ++count; });

    EXPECT_FALSE(wm.dispatch_file_drop(1, nullptr));
    EXPECT_FALSE(wm.dispatch_file_drop(1, ""));
    EXPECT_EQ(count, 0);
}

TEST(WindowFileDrop, RequestsRedraw)
{
    WindowManager            wm;
    std::vector<std::string> reasons;
    wm.set_redraw_request_handler([&reasons](const char* r) { reasons.emplace_back(r); });
    wm.set_file_drop_handler([](uint32_t, const std::string&) {});

    ASSERT_TRUE(wm.dispatch_file_drop(1, "/data/a.csv"));
    ASSERT_EQ(reasons.size(), 1u);
    EXPECT_EQ(reasons[0], "file_drop");
}
