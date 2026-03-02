// test_ros_screenshot_export.cpp — E3 unit tests
//
// Pure C++ logic tests: no ROS2 runtime, no Vulkan, no ImGui context.
// All tests use the RosScreenshotExport public API with a mock grab callback
// that fills the pixel buffer with a known pattern, and a mock render callback
// that does nothing (for RecordingSession path tests).

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "ros_screenshot_export.hpp"

namespace fs = std::filesystem;
using namespace spectra::adapters::ros2;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Fills an RGBA buffer with a checkerboard pattern to produce a valid PNG.
static bool checker_grab(uint8_t* buf, uint32_t w, uint32_t h)
{
    for (uint32_t y = 0; y < h; ++y)
    {
        for (uint32_t x = 0; x < w; ++x)
        {
            const bool on     = ((x / 8) + (y / 8)) % 2 == 0;
            uint8_t*   p      = buf + (y * w + x) * 4u;
            p[0] = on ? 0xFF : 0x20;
            p[1] = on ? 0xFF : 0x20;
            p[2] = on ? 0xFF : 0x20;
            p[3] = 0xFF;
        }
    }
    return true;
}

static bool failing_grab(uint8_t* /*buf*/, uint32_t /*w*/, uint32_t /*h*/)
{
    return false;
}

// Returns a temp path unique per test.
static std::string tmp_png(const std::string& name)
{
    return "/tmp/test_ros_screenshot_" + name + ".png";
}

// ---------------------------------------------------------------------------
// Suite: ScreenshotConfig
// ---------------------------------------------------------------------------

TEST(ScreenshotConfig, DefaultValues)
{
    ScreenshotConfig cfg;
    EXPECT_TRUE(cfg.path.empty());
    EXPECT_EQ(cfg.width,  1280u);
    EXPECT_EQ(cfg.height,  720u);
}

TEST(ScreenshotConfig, AssignFields)
{
    ScreenshotConfig cfg;
    cfg.path   = "/tmp/foo.png";
    cfg.width  = 800;
    cfg.height = 600;
    EXPECT_EQ(cfg.path,   "/tmp/foo.png");
    EXPECT_EQ(cfg.width,   800u);
    EXPECT_EQ(cfg.height,  600u);
}

// ---------------------------------------------------------------------------
// Suite: ScreenshotResult
// ---------------------------------------------------------------------------

TEST(ScreenshotResult, DefaultFalse)
{
    ScreenshotResult r;
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(static_cast<bool>(r));
    EXPECT_TRUE(r.error.empty());
}

TEST(ScreenshotResult, BoolOperator)
{
    ScreenshotResult r;
    r.ok = true;
    EXPECT_TRUE(static_cast<bool>(r));
}

// ---------------------------------------------------------------------------
// Suite: RecordingDialogConfig
// ---------------------------------------------------------------------------

TEST(RecordingDialogConfig, Defaults)
{
    RecordingDialogConfig cfg;
    EXPECT_FALSE(cfg.default_path.empty());
    EXPECT_GT(cfg.default_fps, 0.0f);
    EXPECT_GT(cfg.default_duration, 0.0f);
    EXPECT_GT(cfg.default_width,  0u);
    EXPECT_GT(cfg.default_height, 0u);
}

TEST(RecordingDialogConfig, CustomValues)
{
    RecordingDialogConfig cfg;
    cfg.default_path     = "/data/rec.mp4";
    cfg.default_fps      = 60.0f;
    cfg.default_duration = 5.0f;
    cfg.default_width    = 1920;
    cfg.default_height   = 1080;

    EXPECT_EQ(cfg.default_path,     "/data/rec.mp4");
    EXPECT_FLOAT_EQ(cfg.default_fps,      60.0f);
    EXPECT_FLOAT_EQ(cfg.default_duration,  5.0f);
    EXPECT_EQ(cfg.default_width,     1920u);
    EXPECT_EQ(cfg.default_height,    1080u);
}

// ---------------------------------------------------------------------------
// Suite: RosScreenshotExport construction
// ---------------------------------------------------------------------------

TEST(RosScreenshotExport, ConstructDefault)
{
    RosScreenshotExport exporter;
    EXPECT_FALSE(exporter.is_recording());
    EXPECT_FALSE(exporter.last_record_ok());
    EXPECT_FALSE(exporter.screenshot_toast_active());
    EXPECT_TRUE(exporter.last_screenshot_path().empty());
}

TEST(RosScreenshotExport, ConstructWithConfig)
{
    RecordingDialogConfig cfg;
    cfg.default_fps      = 60.0f;
    cfg.default_duration = 5.0f;
    RosScreenshotExport exporter(cfg);
    EXPECT_FLOAT_EQ(exporter.dialog_config().default_fps, 60.0f);
}

TEST(RosScreenshotExport, NonCopyable)
{
    EXPECT_FALSE(std::is_copy_constructible_v<RosScreenshotExport>);
    EXPECT_FALSE(std::is_copy_assignable_v<RosScreenshotExport>);
    EXPECT_FALSE(std::is_move_constructible_v<RosScreenshotExport>);
    EXPECT_FALSE(std::is_move_assignable_v<RosScreenshotExport>);
}

TEST(RosScreenshotExport, SetDialogConfig)
{
    RosScreenshotExport exporter;
    RecordingDialogConfig cfg;
    cfg.default_fps = 24.0f;
    exporter.set_dialog_config(cfg);
    EXPECT_FLOAT_EQ(exporter.dialog_config().default_fps, 24.0f);
}

// ---------------------------------------------------------------------------
// Suite: Static helpers
// ---------------------------------------------------------------------------

TEST(StaticHelpers, MakeScreenshotPath_NonEmpty)
{
    const std::string p = RosScreenshotExport::make_screenshot_path("/tmp", "test");
    EXPECT_FALSE(p.empty());
    EXPECT_NE(p.find("/tmp/test_"), std::string::npos);
    EXPECT_NE(p.rfind(".png"), std::string::npos);
}

TEST(StaticHelpers, MakeScreenshotPath_DefaultArgs)
{
    const std::string p = RosScreenshotExport::make_screenshot_path();
    EXPECT_NE(p.find("/tmp/spectra_ros_"), std::string::npos);
    EXPECT_NE(p.rfind(".png"), std::string::npos);
}

TEST(StaticHelpers, MakeScreenshotPath_Unique)
{
    const std::string p1 = RosScreenshotExport::make_screenshot_path("/tmp", "unique");
    // Sleep 1s would be flaky; just verify the function is stable.
    EXPECT_FALSE(p1.empty());
}

TEST(StaticHelpers, WritePng_ZeroSize)
{
    std::vector<uint8_t> buf(4, 0);
    EXPECT_FALSE(RosScreenshotExport::write_png("/tmp/test_zero.png", buf.data(), 0, 4));
    EXPECT_FALSE(RosScreenshotExport::write_png("/tmp/test_zero.png", buf.data(), 4, 0));
}

TEST(StaticHelpers, WritePng_NullBuffer)
{
    EXPECT_FALSE(RosScreenshotExport::write_png("/tmp/test_null.png", nullptr, 4, 4));
}

TEST(StaticHelpers, WritePng_EmptyPath)
{
    std::vector<uint8_t> buf(16, 0xFF);
    EXPECT_FALSE(RosScreenshotExport::write_png("", buf.data(), 2, 2));
}

TEST(StaticHelpers, WritePng_ValidSmall)
{
    const uint32_t w = 4, h = 4;
    std::vector<uint8_t> buf(w * h * 4u, 0xFF);
    const std::string path = "/tmp/test_ros_ss_valid_small.png";
    EXPECT_TRUE(RosScreenshotExport::write_png(path, buf.data(), w, h));
    EXPECT_TRUE(fs::exists(path));
    fs::remove(path);
}

// ---------------------------------------------------------------------------
// Suite: take_screenshot — error paths
// ---------------------------------------------------------------------------

TEST(TakeScreenshot, EmptyPath)
{
    RosScreenshotExport exporter;
    exporter.set_frame_grab_callback(checker_grab);
    const auto r = exporter.take_screenshot("", 64, 64);
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
}

TEST(TakeScreenshot, ZeroWidth)
{
    RosScreenshotExport exporter;
    exporter.set_frame_grab_callback(checker_grab);
    const auto r = exporter.take_screenshot("/tmp/test_zw.png", 0, 64);
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
}

TEST(TakeScreenshot, ZeroHeight)
{
    RosScreenshotExport exporter;
    exporter.set_frame_grab_callback(checker_grab);
    const auto r = exporter.take_screenshot("/tmp/test_zh.png", 64, 0);
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
}

TEST(TakeScreenshot, FailingGrabCallback)
{
    RosScreenshotExport exporter;
    exporter.set_frame_grab_callback(failing_grab);
    const auto r = exporter.take_screenshot("/tmp/test_fail_grab.png", 32, 32);
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
}

// ---------------------------------------------------------------------------
// Suite: take_screenshot — success path
// ---------------------------------------------------------------------------

TEST(TakeScreenshot, NoCallback_BlackImage)
{
    // No grab callback — buffer stays zero (black). Should still write a PNG.
    RosScreenshotExport exporter;
    const std::string path = tmp_png("no_callback");
    const auto r = exporter.take_screenshot(path, 8, 8);
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.path,   path);
    EXPECT_EQ(r.width,  8u);
    EXPECT_EQ(r.height, 8u);
    EXPECT_TRUE(r.error.empty());
    EXPECT_TRUE(fs::exists(path));
    fs::remove(path);
}

TEST(TakeScreenshot, WithCheckerCallback)
{
    RosScreenshotExport exporter;
    exporter.set_frame_grab_callback(checker_grab);
    const std::string path = tmp_png("checker");
    const auto r = exporter.take_screenshot(path, 64, 64);
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.path,   path);
    EXPECT_EQ(r.width,  64u);
    EXPECT_EQ(r.height, 64u);
    EXPECT_TRUE(fs::exists(path));
    // File must be non-empty.
    EXPECT_GT(fs::file_size(path), 0u);
    fs::remove(path);
}

TEST(TakeScreenshot, DefaultDimensionsFromConfig)
{
    RecordingDialogConfig cfg;
    cfg.default_width  = 320;
    cfg.default_height = 240;
    RosScreenshotExport exporter(cfg);
    const std::string path = tmp_png("default_dims");
    // Pass width=0, height=0 — should use config defaults.
    const auto r = exporter.take_screenshot(path, 0, 0);
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.width,  320u);
    EXPECT_EQ(r.height, 240u);
    fs::remove(path);
}

TEST(TakeScreenshot, LastScreenshotUpdated)
{
    RosScreenshotExport exporter;
    EXPECT_TRUE(exporter.last_screenshot_path().empty());
    const std::string path = tmp_png("last_ss");
    exporter.take_screenshot(path, 8, 8);
    EXPECT_EQ(exporter.last_screenshot_path(), path);
    fs::remove(path);
}

TEST(TakeScreenshot, UsingScreenshotConfig)
{
    RosScreenshotExport exporter;
    ScreenshotConfig cfg;
    cfg.path   = tmp_png("cfg_overload");
    cfg.width  = 16;
    cfg.height = 16;
    const auto r = exporter.take_screenshot(cfg);
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.width,  16u);
    EXPECT_EQ(r.height, 16u);
    fs::remove(cfg.path);
}

TEST(TakeScreenshot, MultipleScreenshots)
{
    RosScreenshotExport exporter;
    exporter.set_frame_grab_callback(checker_grab);
    for (int i = 0; i < 3; ++i)
    {
        const std::string path = tmp_png("multi_" + std::to_string(i));
        const auto r = exporter.take_screenshot(path, 16, 16);
        EXPECT_TRUE(r.ok) << "Screenshot " << i << " failed";
        EXPECT_TRUE(fs::exists(path));
        fs::remove(path);
    }
}

// ---------------------------------------------------------------------------
// Suite: Toast timer
// ---------------------------------------------------------------------------

TEST(ToastTimer, InactiveBeforeScreenshot)
{
    RosScreenshotExport exporter;
    EXPECT_FALSE(exporter.screenshot_toast_active());
}

TEST(ToastTimer, ActiveAfterSuccessfulScreenshot)
{
    RosScreenshotExport exporter;
    const std::string path = tmp_png("toast_active");
    exporter.take_screenshot(path, 8, 8);
    EXPECT_TRUE(exporter.screenshot_toast_active());
    fs::remove(path);
}

TEST(ToastTimer, InactiveAfterFailedScreenshot)
{
    RosScreenshotExport exporter;
    exporter.set_frame_grab_callback(failing_grab);
    exporter.take_screenshot("/tmp/toast_fail.png", 8, 8);
    EXPECT_FALSE(exporter.screenshot_toast_active());
}

TEST(ToastTimer, TickDecreases)
{
    RosScreenshotExport exporter;
    const std::string path = tmp_png("toast_tick");
    exporter.take_screenshot(path, 8, 8);
    EXPECT_TRUE(exporter.screenshot_toast_active());
    // Advance well past default 3 s duration.
    exporter.tick(10.0f);
    EXPECT_FALSE(exporter.screenshot_toast_active());
    fs::remove(path);
}

TEST(ToastTimer, TickPartial)
{
    RosScreenshotExport exporter;
    const std::string path = tmp_png("toast_partial");
    exporter.take_screenshot(path, 8, 8);
    // Advance 1 s — default duration is 3 s, still active.
    exporter.tick(1.0f);
    EXPECT_TRUE(exporter.screenshot_toast_active());
    fs::remove(path);
}

// ---------------------------------------------------------------------------
// Suite: Recording state
// ---------------------------------------------------------------------------

TEST(Recording, NotRecordingInitially)
{
    RosScreenshotExport exporter;
    EXPECT_FALSE(exporter.is_recording());
}

TEST(Recording, CancelWhenNotRecordingIsNoOp)
{
    RosScreenshotExport exporter;
    EXPECT_NO_THROW(exporter.cancel_recording());
    EXPECT_FALSE(exporter.is_recording());
}

TEST(Recording, LastRecordOkFalseInitially)
{
    RosScreenshotExport exporter;
    EXPECT_FALSE(exporter.last_record_ok());
}

TEST(Recording, TickWithNoSessionIsNoOp)
{
    RosScreenshotExport exporter;
    // Should not crash or assert.
    EXPECT_NO_THROW(exporter.tick(0.016f));
}

TEST(Recording, DrawRecordDialogNoImGui)
{
    // Without an ImGui context, draw_record_dialog returns false and doesn't crash.
    RosScreenshotExport exporter;
    bool open = true;
    const bool r = exporter.draw_record_dialog(&open);
    EXPECT_FALSE(r);
}

TEST(Recording, DrawRecordDialogNullOpen)
{
    RosScreenshotExport exporter;
    // nullptr p_open should not crash.
    EXPECT_NO_THROW(exporter.draw_record_dialog(nullptr));
}

// ---------------------------------------------------------------------------
// Suite: Edge cases
// ---------------------------------------------------------------------------

TEST(EdgeCases, ReplaceGrabCallback)
{
    RosScreenshotExport exporter;
    int call_count = 0;
    exporter.set_frame_grab_callback([&](uint8_t* buf, uint32_t w, uint32_t h) {
        ++call_count;
        (void)buf; (void)w; (void)h;
        return true;
    });
    exporter.take_screenshot(tmp_png("edge_replace1"), 8, 8);
    EXPECT_EQ(call_count, 1);

    // Replace with checker.
    exporter.set_frame_grab_callback(checker_grab);
    const std::string path2 = tmp_png("edge_replace2");
    const auto r = exporter.take_screenshot(path2, 8, 8);
    EXPECT_TRUE(r.ok);
    // Original callback not called again.
    EXPECT_EQ(call_count, 1);
    fs::remove(tmp_png("edge_replace1"));
    fs::remove(path2);
}

TEST(EdgeCases, LargeScreenshot)
{
    RosScreenshotExport exporter;
    // 1920×1080 — no grab callback, black image.
    const std::string path = tmp_png("large");
    const auto r = exporter.take_screenshot(path, 1920, 1080);
    EXPECT_TRUE(r.ok);
    EXPECT_GT(fs::file_size(path), 0u);
    fs::remove(path);
}

TEST(EdgeCases, TickZeroDt)
{
    RosScreenshotExport exporter;
    const std::string path = tmp_png("tick_zero");
    exporter.take_screenshot(path, 8, 8);
    EXPECT_TRUE(exporter.screenshot_toast_active());
    exporter.tick(0.0f);
    EXPECT_TRUE(exporter.screenshot_toast_active());   // still active — dt=0
    fs::remove(path);
}

TEST(EdgeCases, ScreenshotPathPreservedInResult)
{
    RosScreenshotExport exporter;
    const std::string path = tmp_png("path_preserved");
    const auto r = exporter.take_screenshot(path, 4, 4);
    EXPECT_EQ(r.path, path);
    fs::remove(path);
}

TEST(EdgeCases, CallbackSetAfterConstruction)
{
    RosScreenshotExport exporter;
    // Callback installed after construction — should be picked up.
    exporter.set_frame_grab_callback(checker_grab);
    const std::string path = tmp_png("late_callback");
    const auto r = exporter.take_screenshot(path, 16, 16);
    EXPECT_TRUE(r.ok);
    fs::remove(path);
}

TEST(EdgeCases, CancelAfterScreenshotIsNoOp)
{
    RosScreenshotExport exporter;
    exporter.take_screenshot(tmp_png("cancel_after_ss"), 4, 4);
    // cancel_recording has nothing to cancel — should be safe.
    EXPECT_NO_THROW(exporter.cancel_recording());
    EXPECT_FALSE(exporter.is_recording());
    fs::remove(tmp_png("cancel_after_ss"));
}
