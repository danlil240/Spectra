// Regression tests for the io-misc bug-fix group:
//   - Camera::deserialize must not throw on malformed input (bug #66)
//   - SVG export must skip hidden series (bug #87)
//   - ffmpeg command quoting / pipe mode helpers (bug #39)

#include <gtest/gtest.h>

#include <cmath>
#include <spectra/camera.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <string>
#include <vector>

#include "io/ffmpeg_command.hpp"

namespace spectra
{
namespace
{

// ─── Camera::deserialize (bug #66) ──────────────────────────────────────────

TEST(BugfixCameraDeserialize, RoundTripStillWorks)
{
    Camera a;
    a.position  = {1.0f, 2.0f, 3.0f};
    a.target    = {-1.0f, 0.5f, 0.25f};
    a.fov       = 60.0f;
    a.azimuth   = 12.5f;
    a.elevation = -7.0f;
    a.distance  = 9.0f;
    a.up_axis   = Camera::UpAxis::Z;

    Camera b;
    b.deserialize(a.serialize());
    EXPECT_FLOAT_EQ(b.position.x, 1.0f);
    EXPECT_FLOAT_EQ(b.position.y, 2.0f);
    EXPECT_FLOAT_EQ(b.position.z, 3.0f);
    EXPECT_FLOAT_EQ(b.target.x, -1.0f);
    EXPECT_FLOAT_EQ(b.fov, 60.0f);
    EXPECT_FLOAT_EQ(b.azimuth, 12.5f);
    EXPECT_FLOAT_EQ(b.elevation, -7.0f);
    EXPECT_FLOAT_EQ(b.distance, 9.0f);
    EXPECT_EQ(b.up_axis, Camera::UpAxis::Z);
}

TEST(BugfixCameraDeserialize, NullValueDoesNotThrowAndKeepsField)
{
    Camera c;
    c.fov = 33.0f;
    EXPECT_NO_THROW(c.deserialize("{\"fov\":null}"));
    EXPECT_FLOAT_EQ(c.fov, 33.0f);
}

TEST(BugfixCameraDeserialize, TruncatedVectorDoesNotThrowAndKeepsField)
{
    Camera c;
    c.position = {4.0f, 5.0f, 6.0f};
    EXPECT_NO_THROW(c.deserialize("{\"position\":[1.0"));
    EXPECT_FLOAT_EQ(c.position.x, 4.0f);
    EXPECT_FLOAT_EQ(c.position.y, 5.0f);
    EXPECT_FLOAT_EQ(c.position.z, 6.0f);
}

TEST(BugfixCameraDeserialize, GarbageInputDoesNotThrow)
{
    Camera      c;
    const float fov = c.fov;
    EXPECT_NO_THROW(c.deserialize("{\"position\":[a,b,c],\"up_axis\":\"x\",\"distance\":}"));
    EXPECT_NO_THROW(c.deserialize("{\"far_clip\":1e999999,\"projection_mode\":99999999999999}"));
    EXPECT_NO_THROW(c.deserialize("{\"fov\":"));
    EXPECT_NO_THROW(c.deserialize(""));
    EXPECT_FLOAT_EQ(c.fov, fov);
    EXPECT_TRUE(std::isfinite(c.far_clip));
}

TEST(BugfixCameraDeserialize, ValidFieldsAppliedAlongsideInvalidOnes)
{
    Camera c;
    c.deserialize("{\"position\":[1,2],\"fov\":50.0,\"distance\":null}");
    EXPECT_FLOAT_EQ(c.fov, 50.0f);
}

// ─── SVG export hidden series (bug #87) ─────────────────────────────────────

TEST(BugfixSvgExport, HiddenSeriesOmittedFromPlotAndLegend)
{
    Figure fig({.width = 640, .height = 480});
    auto&  ax = fig.subplot(1, 1, 1);

    std::vector<float> x = {0.0f, 1.0f, 2.0f};
    std::vector<float> y = {0.0f, 1.0f, 0.5f};
    ax.line(x, y).label("shown-line").color(rgb(1.0f, 0.0f, 0.0f));
    ax.line(x, y).label("hidden-line").color(rgb(0.0f, 1.0f, 0.0f)).visible(false);
    ax.scatter(x, y).label("hidden-points").color(rgb(0.0f, 0.0f, 1.0f)).visible(false);
    fig.compute_layout();

    const std::string svg = SvgExporter::to_string(fig);
    EXPECT_NE(svg.find("shown-line"), std::string::npos);
    EXPECT_EQ(svg.find("hidden-line"), std::string::npos);
    EXPECT_EQ(svg.find("hidden-points"), std::string::npos);
    EXPECT_EQ(svg.find("<circle"), std::string::npos);
}

// ─── ffmpeg command helpers (bug #39) ───────────────────────────────────────

TEST(BugfixFfmpegCommand, WindowsQuoteWrapsInDoubleQuotes)
{
    EXPECT_EQ(detail::windows_quote("libx264"), "\"libx264\"");
    EXPECT_EQ(detail::windows_quote("C:\\My Videos\\out.mp4"), "\"C:\\My Videos\\out.mp4\"");
}

TEST(BugfixFfmpegCommand, WindowsQuoteEscapesQuotesAndTrailingBackslashes)
{
    EXPECT_EQ(detail::windows_quote("a\"b"), "\"a\\\"b\"");
    EXPECT_EQ(detail::windows_quote("a\\\"b"), "\"a\\\\\\\"b\"");
    EXPECT_EQ(detail::windows_quote("dir\\"), "\"dir\\\\\"");
}

TEST(BugfixFfmpegCommand, PlatformQuoteAndPipeMode)
{
#if defined(_WIN32)
    EXPECT_EQ(detail::platform_quote("x y"), "\"x y\"");
    EXPECT_STREQ(detail::ffmpeg_pipe_mode(), "wb");
#else
    EXPECT_EQ(detail::platform_quote("x y"), "'x y'");
    EXPECT_STREQ(detail::ffmpeg_pipe_mode(), "w");
#endif
}

}   // namespace
}   // namespace spectra
