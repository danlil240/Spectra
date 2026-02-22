#include <cstring>
#include <filesystem>
#include <gtest/gtest.h>

#include "ui/recording_export.hpp"

using namespace spectra;

// ─── Helper: dummy render callback ──────────────────────────────────────────

static bool fill_solid_color(uint32_t /*frame*/,
                             float /*time*/,
                             uint8_t* rgba,
                             uint32_t w,
                             uint32_t h)
{
    size_t pixels = static_cast<size_t>(w) * h;
    for (size_t i = 0; i < pixels; ++i)
    {
        rgba[i * 4 + 0] = 128;   // R
        rgba[i * 4 + 1] = 64;    // G
        rgba[i * 4 + 2] = 32;    // B
        rgba[i * 4 + 3] = 255;   // A
    }
    return true;
}

static bool fill_gradient(uint32_t frame, float /*time*/, uint8_t* rgba, uint32_t w, uint32_t h)
{
    for (uint32_t y = 0; y < h; ++y)
    {
        for (uint32_t x = 0; x < w; ++x)
        {
            size_t idx    = (static_cast<size_t>(y) * w + x) * 4;
            rgba[idx + 0] = static_cast<uint8_t>((x * 255) / w);
            rgba[idx + 1] = static_cast<uint8_t>((y * 255) / h);
            rgba[idx + 2] = static_cast<uint8_t>((frame * 10) % 256);
            rgba[idx + 3] = 255;
        }
    }
    return true;
}

static bool fail_render(uint32_t /*frame*/,
                        float /*time*/,
                        uint8_t* /*rgba*/,
                        uint32_t /*w*/,
                        uint32_t /*h*/)
{
    return false;
}

// ─── Construction ────────────────────────────────────────────────────────────

TEST(RecordingSessionConstruction, DefaultState)
{
    RecordingSession rs;
    EXPECT_EQ(rs.state(), RecordingState::Idle);
    EXPECT_FALSE(rs.is_active());
    EXPECT_FALSE(rs.is_finished());
    EXPECT_EQ(rs.total_frames(), 0u);
    EXPECT_EQ(rs.current_frame(), 0u);
    EXPECT_TRUE(rs.error().empty());
}

// ─── Config Validation ───────────────────────────────────────────────────────

TEST(RecordingSessionValidation, EmptyPath)
{
    RecordingSession rs;
    RecordingConfig  cfg;
    cfg.output_path = "";
    cfg.start_time  = 0.0f;
    cfg.end_time    = 1.0f;

    EXPECT_FALSE(rs.begin(cfg, fill_solid_color));
    EXPECT_EQ(rs.state(), RecordingState::Failed);
    EXPECT_FALSE(rs.error().empty());
}

TEST(RecordingSessionValidation, ZeroDimensions)
{
    RecordingSession rs;
    RecordingConfig  cfg;
    cfg.output_path = "/tmp/spectra_test_rec";
    cfg.width       = 0;
    cfg.height      = 0;
    cfg.start_time  = 0.0f;
    cfg.end_time    = 1.0f;

    EXPECT_FALSE(rs.begin(cfg, fill_solid_color));
    EXPECT_EQ(rs.state(), RecordingState::Failed);
}

TEST(RecordingSessionValidation, ZeroFPS)
{
    RecordingSession rs;
    RecordingConfig  cfg;
    cfg.output_path = "/tmp/spectra_test_rec";
    cfg.fps         = 0.0f;
    cfg.start_time  = 0.0f;
    cfg.end_time    = 1.0f;

    EXPECT_FALSE(rs.begin(cfg, fill_solid_color));
    EXPECT_EQ(rs.state(), RecordingState::Failed);
}

TEST(RecordingSessionValidation, InvalidTimeRange)
{
    RecordingSession rs;
    RecordingConfig  cfg;
    cfg.output_path = "/tmp/spectra_test_rec";
    cfg.start_time  = 5.0f;
    cfg.end_time    = 2.0f;

    EXPECT_FALSE(rs.begin(cfg, fill_solid_color));
    EXPECT_EQ(rs.state(), RecordingState::Failed);
}

TEST(RecordingSessionValidation, NullCallback)
{
    RecordingSession rs;
    RecordingConfig  cfg;
    cfg.output_path = "/tmp/spectra_test_rec";
    cfg.start_time  = 0.0f;
    cfg.end_time    = 1.0f;

    EXPECT_FALSE(rs.begin(cfg, nullptr));
    EXPECT_EQ(rs.state(), RecordingState::Failed);
}

#ifndef SPECTRA_USE_FFMPEG
TEST(RecordingSessionValidation, MP4WithoutFFmpeg)
{
    RecordingSession rs;
    RecordingConfig  cfg;
    cfg.output_path = "/tmp/spectra_test.mp4";
    cfg.format      = RecordingFormat::MP4;
    cfg.start_time  = 0.0f;
    cfg.end_time    = 1.0f;

    EXPECT_FALSE(rs.begin(cfg, fill_solid_color));
    EXPECT_EQ(rs.state(), RecordingState::Failed);
}
#endif

// ─── Frame Computation ───────────────────────────────────────────────────────

TEST(RecordingSessionFrames, FrameCount)
{
    RecordingSession rs;
    RecordingConfig  cfg;
    cfg.output_path = "/tmp/spectra_test_frames";
    cfg.format      = RecordingFormat::PNG_Sequence;
    cfg.width       = 8;
    cfg.height      = 8;
    cfg.fps         = 10.0f;
    cfg.start_time  = 0.0f;
    cfg.end_time    = 2.0f;

    EXPECT_TRUE(rs.begin(cfg, fill_solid_color));
    EXPECT_EQ(rs.total_frames(), 20u);
}

TEST(RecordingSessionFrames, FrameTime)
{
    RecordingSession rs;
    RecordingConfig  cfg;
    cfg.output_path = "/tmp/spectra_test_ftime";
    cfg.format      = RecordingFormat::PNG_Sequence;
    cfg.width       = 8;
    cfg.height      = 8;
    cfg.fps         = 10.0f;
    cfg.start_time  = 1.0f;
    cfg.end_time    = 3.0f;

    EXPECT_TRUE(rs.begin(cfg, fill_solid_color));
    EXPECT_NEAR(rs.frame_time(0), 1.0f, 0.001f);
    EXPECT_NEAR(rs.frame_time(10), 2.0f, 0.001f);
}

// ─── PNG Sequence Export ─────────────────────────────────────────────────────

TEST(RecordingSessionPNG, BasicExport)
{
    namespace fs    = std::filesystem;
    std::string dir = "/tmp/spectra_test_png_export";

    // Clean up
    fs::remove_all(dir);

    RecordingSession rs;
    RecordingConfig  cfg;
    cfg.output_path = dir;
    cfg.format      = RecordingFormat::PNG_Sequence;
    cfg.width       = 16;
    cfg.height      = 16;
    cfg.fps         = 5.0f;
    cfg.start_time  = 0.0f;
    cfg.end_time    = 1.0f;

    ASSERT_TRUE(rs.begin(cfg, fill_solid_color));
    EXPECT_EQ(rs.state(), RecordingState::Recording);
    EXPECT_EQ(rs.total_frames(), 5u);

    // Advance all frames
    while (rs.advance())
    {
    }

    EXPECT_TRUE(rs.finish());
    EXPECT_EQ(rs.state(), RecordingState::Finished);
    EXPECT_TRUE(rs.is_finished());

    // Verify PNG files exist
    EXPECT_TRUE(fs::exists(dir + "/frame_0000.png"));
    EXPECT_TRUE(fs::exists(dir + "/frame_0004.png"));
    EXPECT_FALSE(fs::exists(dir + "/frame_0005.png"));

    // Clean up
    fs::remove_all(dir);
}

TEST(RecordingSessionPNG, RunAll)
{
    namespace fs    = std::filesystem;
    std::string dir = "/tmp/spectra_test_png_runall";
    fs::remove_all(dir);

    RecordingSession rs;
    RecordingConfig  cfg;
    cfg.output_path = dir;
    cfg.format      = RecordingFormat::PNG_Sequence;
    cfg.width       = 8;
    cfg.height      = 8;
    cfg.fps         = 10.0f;
    cfg.start_time  = 0.0f;
    cfg.end_time    = 0.5f;

    ASSERT_TRUE(rs.begin(cfg, fill_solid_color));
    EXPECT_TRUE(rs.run_all());
    EXPECT_TRUE(rs.is_finished());

    EXPECT_TRUE(fs::exists(dir + "/frame_0000.png"));
    EXPECT_TRUE(fs::exists(dir + "/frame_0004.png"));

    fs::remove_all(dir);
}

// ─── Progress Tracking ───────────────────────────────────────────────────────

TEST(RecordingSessionProgress, ProgressCallback)
{
    namespace fs    = std::filesystem;
    std::string dir = "/tmp/spectra_test_progress";
    fs::remove_all(dir);

    RecordingSession rs;
    RecordingConfig  cfg;
    cfg.output_path = dir;
    cfg.format      = RecordingFormat::PNG_Sequence;
    cfg.width       = 8;
    cfg.height      = 8;
    cfg.fps         = 5.0f;
    cfg.start_time  = 0.0f;
    cfg.end_time    = 1.0f;

    int   progress_calls = 0;
    float last_percent   = 0.0f;
    rs.set_on_progress(
        [&](const RecordingProgress& p)
        {
            progress_calls++;
            last_percent = p.percent;
            EXPECT_LE(p.current_frame, p.total_frames);
        });

    ASSERT_TRUE(rs.begin(cfg, fill_solid_color));
    rs.run_all();

    EXPECT_EQ(progress_calls, 5);
    EXPECT_NEAR(last_percent, 100.0f, 0.1f);

    fs::remove_all(dir);
}

TEST(RecordingSessionProgress, CompletionCallback)
{
    namespace fs    = std::filesystem;
    std::string dir = "/tmp/spectra_test_complete";
    fs::remove_all(dir);

    RecordingSession rs;
    RecordingConfig  cfg;
    cfg.output_path = dir;
    cfg.format      = RecordingFormat::PNG_Sequence;
    cfg.width       = 8;
    cfg.height      = 8;
    cfg.fps         = 5.0f;
    cfg.start_time  = 0.0f;
    cfg.end_time    = 0.4f;

    bool completed   = false;
    bool success_val = false;
    rs.set_on_complete(
        [&](bool success)
        {
            completed   = true;
            success_val = success;
        });

    ASSERT_TRUE(rs.begin(cfg, fill_solid_color));
    rs.run_all();

    EXPECT_TRUE(completed);
    EXPECT_TRUE(success_val);

    fs::remove_all(dir);
}

TEST(RecordingSessionProgress, ProgressState)
{
    RecordingSession rs;
    RecordingConfig  cfg;
    cfg.output_path = "/tmp/spectra_test_pstate";
    cfg.format      = RecordingFormat::PNG_Sequence;
    cfg.width       = 8;
    cfg.height      = 8;
    cfg.fps         = 10.0f;
    cfg.start_time  = 0.0f;
    cfg.end_time    = 1.0f;

    auto p = rs.progress();
    EXPECT_EQ(p.current_frame, 0u);
    EXPECT_EQ(p.total_frames, 0u);
    EXPECT_FLOAT_EQ(p.percent, 0.0f);
}

// ─── Cancel ──────────────────────────────────────────────────────────────────

TEST(RecordingSessionCancel, CancelDuringRecording)
{
    namespace fs    = std::filesystem;
    std::string dir = "/tmp/spectra_test_cancel";
    fs::remove_all(dir);

    RecordingSession rs;
    RecordingConfig  cfg;
    cfg.output_path = dir;
    cfg.format      = RecordingFormat::PNG_Sequence;
    cfg.width       = 8;
    cfg.height      = 8;
    cfg.fps         = 10.0f;
    cfg.start_time  = 0.0f;
    cfg.end_time    = 5.0f;

    bool completed   = false;
    bool success_val = true;
    rs.set_on_complete(
        [&](bool s)
        {
            completed   = true;
            success_val = s;
        });

    ASSERT_TRUE(rs.begin(cfg, fill_solid_color));

    // Advance a few frames then cancel
    rs.advance();
    rs.advance();
    rs.cancel();

    EXPECT_EQ(rs.state(), RecordingState::Cancelled);
    EXPECT_TRUE(completed);
    EXPECT_FALSE(success_val);

    fs::remove_all(dir);
}

TEST(RecordingSessionCancel, CancelWhileIdle)
{
    RecordingSession rs;
    rs.cancel();   // Should not crash
    EXPECT_EQ(rs.state(), RecordingState::Idle);
}

// ─── Error Handling ──────────────────────────────────────────────────────────

TEST(RecordingSessionErrors, RenderFailure)
{
    namespace fs    = std::filesystem;
    std::string dir = "/tmp/spectra_test_renderfail";
    fs::remove_all(dir);

    RecordingSession rs;
    RecordingConfig  cfg;
    cfg.output_path = dir;
    cfg.format      = RecordingFormat::PNG_Sequence;
    cfg.width       = 8;
    cfg.height      = 8;
    cfg.fps         = 5.0f;
    cfg.start_time  = 0.0f;
    cfg.end_time    = 1.0f;

    ASSERT_TRUE(rs.begin(cfg, fail_render));

    bool more = rs.advance();
    EXPECT_FALSE(more);
    EXPECT_EQ(rs.state(), RecordingState::Failed);
    EXPECT_FALSE(rs.error().empty());

    fs::remove_all(dir);
}

TEST(RecordingSessionErrors, AdvanceAfterFinish)
{
    namespace fs    = std::filesystem;
    std::string dir = "/tmp/spectra_test_advfinish";
    fs::remove_all(dir);

    RecordingSession rs;
    RecordingConfig  cfg;
    cfg.output_path = dir;
    cfg.format      = RecordingFormat::PNG_Sequence;
    cfg.width       = 8;
    cfg.height      = 8;
    cfg.fps         = 5.0f;
    cfg.start_time  = 0.0f;
    cfg.end_time    = 0.2f;

    ASSERT_TRUE(rs.begin(cfg, fill_solid_color));
    rs.run_all();

    bool more = rs.advance();
    EXPECT_FALSE(more);

    fs::remove_all(dir);
}

TEST(RecordingSessionErrors, DoubleFinish)
{
    namespace fs    = std::filesystem;
    std::string dir = "/tmp/spectra_test_dblfinish";
    fs::remove_all(dir);

    RecordingSession rs;
    RecordingConfig  cfg;
    cfg.output_path = dir;
    cfg.format      = RecordingFormat::PNG_Sequence;
    cfg.width       = 8;
    cfg.height      = 8;
    cfg.fps         = 5.0f;
    cfg.start_time  = 0.0f;
    cfg.end_time    = 0.2f;

    ASSERT_TRUE(rs.begin(cfg, fill_solid_color));
    rs.run_all();

    // Second finish should return true (already finished)
    EXPECT_TRUE(rs.finish());

    fs::remove_all(dir);
}

// ─── GIF Utilities ───────────────────────────────────────────────────────────

TEST(RecordingGifUtils, MedianCutBasic)
{
    // 4 pixels: red, green, blue, white
    uint8_t rgba[] = {
        255,
        0,
        0,
        255,
        0,
        255,
        0,
        255,
        0,
        0,
        255,
        255,
        255,
        255,
        255,
        255,
    };

    auto palette = RecordingSession::median_cut(rgba, 4, 4);
    EXPECT_EQ(palette.size(), 4u);
}

TEST(RecordingGifUtils, MedianCutReduces)
{
    // 4 pixels but request only 2 colors
    uint8_t rgba[] = {
        255,
        0,
        0,
        255,
        250,
        5,
        5,
        255,
        0,
        0,
        255,
        255,
        5,
        5,
        250,
        255,
    };

    auto palette = RecordingSession::median_cut(rgba, 4, 2);
    EXPECT_LE(palette.size(), 2u);
}

TEST(RecordingGifUtils, MedianCutEmpty)
{
    auto palette = RecordingSession::median_cut(nullptr, 0, 256);
    EXPECT_TRUE(palette.empty());
}

TEST(RecordingGifUtils, NearestPaletteIndex)
{
    std::vector<Color> palette = {
        Color{1.0f, 0.0f, 0.0f},   // Red
        Color{0.0f, 1.0f, 0.0f},   // Green
        Color{0.0f, 0.0f, 1.0f},   // Blue
    };

    EXPECT_EQ(RecordingSession::nearest_palette_index(palette, 255, 0, 0), 0);
    EXPECT_EQ(RecordingSession::nearest_palette_index(palette, 0, 255, 0), 1);
    EXPECT_EQ(RecordingSession::nearest_palette_index(palette, 0, 0, 255), 2);
    EXPECT_EQ(RecordingSession::nearest_palette_index(palette, 200, 30, 30), 0);
}

TEST(RecordingGifUtils, NearestPaletteIndexEmpty)
{
    std::vector<Color> empty;
    EXPECT_EQ(RecordingSession::nearest_palette_index(empty, 128, 128, 128), 0);
}

TEST(RecordingGifUtils, QuantizeFrame)
{
    // 4x2 gradient image
    uint8_t rgba[4 * 2 * 4];
    for (int i = 0; i < 8; ++i)
    {
        rgba[i * 4 + 0] = static_cast<uint8_t>(i * 32);
        rgba[i * 4 + 1] = static_cast<uint8_t>(i * 16);
        rgba[i * 4 + 2] = static_cast<uint8_t>(i * 8);
        rgba[i * 4 + 3] = 255;
    }

    std::vector<uint8_t> palette, indexed;
    RecordingSession::quantize_frame(rgba, 4, 2, 4, palette, indexed);

    EXPECT_EQ(indexed.size(), 8u);
    EXPECT_LE(palette.size(), 4u * 3);
    // Each index should be valid
    uint32_t num_colors = static_cast<uint32_t>(palette.size() / 3);
    for (uint8_t idx : indexed)
    {
        EXPECT_LT(idx, num_colors);
    }
}

// ─── GIF Export ──────────────────────────────────────────────────────────────

TEST(RecordingSessionGIF, BasicGifExport)
{
    namespace fs     = std::filesystem;
    std::string path = "/tmp/spectra_test_export.gif";
    fs::remove(path);

    RecordingSession rs;
    RecordingConfig  cfg;
    cfg.output_path      = path;
    cfg.format           = RecordingFormat::GIF;
    cfg.width            = 16;
    cfg.height           = 16;
    cfg.fps              = 5.0f;
    cfg.start_time       = 0.0f;
    cfg.end_time         = 0.6f;
    cfg.gif_palette_size = 16;

    ASSERT_TRUE(rs.begin(cfg, fill_gradient));
    EXPECT_TRUE(rs.run_all());
    EXPECT_TRUE(rs.is_finished());

    // Verify GIF file exists and has content
    EXPECT_TRUE(fs::exists(path));
    EXPECT_GT(fs::file_size(path), 0u);

    // Verify GIF header
    FILE* fp = std::fopen(path.c_str(), "rb");
    ASSERT_NE(fp, nullptr);
    char header[6];
    std::fread(header, 1, 6, fp);
    std::fclose(fp);
    EXPECT_EQ(std::string(header, 6), "GIF89a");

    fs::remove(path);
}

// ─── Edge Cases ──────────────────────────────────────────────────────────────

TEST(RecordingSessionEdgeCases, SingleFrame)
{
    namespace fs    = std::filesystem;
    std::string dir = "/tmp/spectra_test_single";
    fs::remove_all(dir);

    RecordingSession rs;
    RecordingConfig  cfg;
    cfg.output_path = dir;
    cfg.format      = RecordingFormat::PNG_Sequence;
    cfg.width       = 8;
    cfg.height      = 8;
    cfg.fps         = 10.0f;
    cfg.start_time  = 0.0f;
    cfg.end_time    = 0.05f;   // ~0.5 frames, rounds up to 1

    ASSERT_TRUE(rs.begin(cfg, fill_solid_color));
    EXPECT_GE(rs.total_frames(), 1u);

    rs.run_all();
    EXPECT_TRUE(rs.is_finished());

    fs::remove_all(dir);
}

TEST(RecordingSessionEdgeCases, SmallDimensions)
{
    namespace fs    = std::filesystem;
    std::string dir = "/tmp/spectra_test_small";
    fs::remove_all(dir);

    RecordingSession rs;
    RecordingConfig  cfg;
    cfg.output_path = dir;
    cfg.format      = RecordingFormat::PNG_Sequence;
    cfg.width       = 1;
    cfg.height      = 1;
    cfg.fps         = 5.0f;
    cfg.start_time  = 0.0f;
    cfg.end_time    = 0.2f;

    ASSERT_TRUE(rs.begin(cfg, fill_solid_color));
    rs.run_all();
    EXPECT_TRUE(rs.is_finished());

    fs::remove_all(dir);
}

TEST(RecordingSessionEdgeCases, HighFPS)
{
    RecordingSession rs;
    RecordingConfig  cfg;
    cfg.output_path = "/tmp/spectra_test_highfps";
    cfg.format      = RecordingFormat::PNG_Sequence;
    cfg.width       = 4;
    cfg.height      = 4;
    cfg.fps         = 240.0f;
    cfg.start_time  = 0.0f;
    cfg.end_time    = 0.1f;

    ASSERT_TRUE(rs.begin(cfg, fill_solid_color));
    EXPECT_EQ(rs.total_frames(), 24u);

    // Don't actually run all — just verify frame count
    rs.cancel();

    std::filesystem::remove_all("/tmp/spectra_test_highfps");
}

TEST(RecordingSessionEdgeCases, NonZeroStartTime)
{
    RecordingSession rs;
    RecordingConfig  cfg;
    cfg.output_path = "/tmp/spectra_test_offset";
    cfg.format      = RecordingFormat::PNG_Sequence;
    cfg.width       = 4;
    cfg.height      = 4;
    cfg.fps         = 10.0f;
    cfg.start_time  = 5.0f;
    cfg.end_time    = 6.0f;

    ASSERT_TRUE(rs.begin(cfg, fill_solid_color));
    EXPECT_EQ(rs.total_frames(), 10u);
    EXPECT_NEAR(rs.frame_time(0), 5.0f, 0.001f);
    EXPECT_NEAR(rs.frame_time(5), 5.5f, 0.001f);

    rs.cancel();
    std::filesystem::remove_all("/tmp/spectra_test_offset");
}
