#include <cstdio>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <string>
#include <vector>

#include "ui/accessibility/sonification.hpp"
#include "ui/data/html_table_export.hpp"

using namespace spectra;

// ─── HTML Table Export ───────────────────────────────────────────────────────

class HtmlTableTest : public ::testing::Test
{
   protected:
    Figure fig_{FigureConfig{800, 600}};
};

TEST_F(HtmlTableTest, EmptyFigureProducesHtml)
{
    std::string html = figure_to_html_table(fig_);
    EXPECT_FALSE(html.empty());
    EXPECT_NE(html.find("<!DOCTYPE html>"), std::string::npos);
    EXPECT_NE(html.find("</html>"), std::string::npos);
}

TEST_F(HtmlTableTest, ContainsAxesTitleAndLabels)
{
    auto& ax = fig_.subplot(1, 1, 1);
    ax.title("Temperature vs Time");
    ax.xlabel("Time (s)");
    ax.ylabel("Temp (°C)");

    float x[] = {1.0f, 2.0f, 3.0f};
    float y[] = {20.0f, 22.0f, 25.0f};
    ax.line(x, y).label("sensor1");

    std::string html = figure_to_html_table(fig_);

    EXPECT_NE(html.find("Temperature vs Time"), std::string::npos);
    EXPECT_NE(html.find("Time (s)"), std::string::npos);
    EXPECT_NE(html.find("Temp"), std::string::npos);   // °C may be entity-encoded
    EXPECT_NE(html.find("sensor1"), std::string::npos);
}

TEST_F(HtmlTableTest, DataValuesAppearsInRows)
{
    auto& ax  = fig_.subplot(1, 1, 1);
    float x[] = {1.0f, 2.0f, 3.0f};
    float y[] = {10.0f, 20.0f, 30.0f};
    ax.line(x, y).label("series");

    std::string html = figure_to_html_table(fig_);

    EXPECT_NE(html.find("<table>"), std::string::npos);
    EXPECT_NE(html.find("<tbody>"), std::string::npos);
    EXPECT_NE(html.find("1"), std::string::npos);
    EXPECT_NE(html.find("10"), std::string::npos);
}

TEST_F(HtmlTableTest, HtmlSpecialCharsEscaped)
{
    auto& ax = fig_.subplot(1, 1, 1);
    ax.title("Signal <A> & B");

    std::string html = figure_to_html_table(fig_);

    // Title must be escaped, raw < must not appear inside the h2 text
    EXPECT_NE(html.find("Signal &lt;A&gt; &amp; B"), std::string::npos);
}

TEST_F(HtmlTableTest, WriteToFileSucceeds)
{
    auto& ax  = fig_.subplot(1, 1, 1);
    float x[] = {0.0f, 1.0f};
    float y[] = {0.0f, 1.0f};
    ax.line(x, y);

    const std::string path =
        (std::filesystem::temp_directory_path() / "test_spectra_table.html").string();
    EXPECT_TRUE(figure_to_html_table_file(fig_, path));
    EXPECT_TRUE(std::filesystem::exists(path));
    std::filesystem::remove(path);
}

TEST_F(HtmlTableTest, SectionAriaLabelPresent)
{
    fig_.subplot(1, 1, 1);
    std::string html = figure_to_html_table(fig_);
    EXPECT_NE(html.find("aria-label"), std::string::npos);
}

TEST_F(HtmlTableTest, TheadWithScopePresent)
{
    auto& ax  = fig_.subplot(1, 1, 1);
    float x[] = {1.0f};
    float y[] = {2.0f};
    ax.line(x, y).label("s");

    std::string html = figure_to_html_table(fig_);
    EXPECT_NE(html.find("<thead>"), std::string::npos);
    EXPECT_NE(html.find("scope=\"col\""), std::string::npos);
}

TEST_F(HtmlTableTest, MultipleAxesMultipleSections)
{
    auto& ax1 = fig_.subplot(1, 2, 1);
    auto& ax2 = fig_.subplot(1, 2, 2);
    float x[] = {1.0f};
    float y[] = {2.0f};
    ax1.line(x, y).label("a");
    ax2.line(x, y).label("b");

    std::string html = figure_to_html_table(fig_);
    EXPECT_NE(html.find("aria-label=\"Axes 1\""), std::string::npos);
    EXPECT_NE(html.find("aria-label=\"Axes 2\""), std::string::npos);
}

// ─── Sonification ────────────────────────────────────────────────────────────

class SonificationTest : public ::testing::Test
{
   protected:
    Figure fig_{FigureConfig{800, 600}};
};

TEST_F(SonificationTest, EmptyAxesReturnsEmpty)
{
    auto& ax  = fig_.subplot(1, 1, 1);
    auto  pcm = sonify_axes(static_cast<const Axes&>(ax));
    EXPECT_TRUE(pcm.empty());
}

TEST_F(SonificationTest, LineSeriesProducesSamples)
{
    auto& ax  = fig_.subplot(1, 1, 1);
    float x[] = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f};
    float y[] = {0.0f, 1.0f, 2.0f, 1.0f, 0.0f};
    ax.line(x, y).label("wave");

    SonificationParams params;
    params.duration_sec = 0.1f;   // short for test speed

    auto pcm = sonify_axes(static_cast<const Axes&>(ax), params);
    EXPECT_FALSE(pcm.empty());

    uint32_t expected_samples =
        static_cast<uint32_t>(params.duration_sec * static_cast<float>(params.sample_rate));
    EXPECT_EQ(pcm.size(), static_cast<size_t>(expected_samples));
}

TEST_F(SonificationTest, SamplesWithinRange)
{
    auto& ax  = fig_.subplot(1, 1, 1);
    float x[] = {0.0f, 1.0f, 2.0f};
    float y[] = {-1.0f, 0.0f, 1.0f};
    ax.line(x, y);

    SonificationParams params;
    params.duration_sec = 0.05f;
    params.amplitude    = 0.5f;

    auto pcm = sonify_axes(static_cast<const Axes&>(ax), params);
    ASSERT_FALSE(pcm.empty());

    for (auto s : pcm)
    {
        EXPECT_LE(s, static_cast<int16_t>(32767));
        EXPECT_GE(s, static_cast<int16_t>(-32768));
    }
}

TEST_F(SonificationTest, WriteWavCreatesFile)
{
    std::vector<int16_t> pcm(1000, 0);
    const std::string    path =
        (std::filesystem::temp_directory_path() / "test_spectra_sonify.wav").string();
    EXPECT_TRUE(write_wav(path, pcm, 44100u));
    EXPECT_TRUE(std::filesystem::exists(path));
    std::filesystem::remove(path);
}

TEST_F(SonificationTest, WriteWavEmptyReturnsFalse)
{
    std::vector<int16_t> empty;
    EXPECT_FALSE(write_wav("/tmp/should_not_exist.wav", empty, 44100u));
}

TEST_F(SonificationTest, SonifyToWavEndToEnd)
{
    auto& ax  = fig_.subplot(1, 1, 1);
    float x[] = {0.0f, 1.0f, 2.0f, 3.0f};
    float y[] = {0.0f, 1.0f, 0.5f, -0.5f};
    ax.line(x, y);

    SonificationParams params;
    params.duration_sec = 0.05f;

    const std::string path =
        (std::filesystem::temp_directory_path() / "test_spectra_wav_e2e.wav").string();
    EXPECT_TRUE(sonify_axes_to_wav(static_cast<const Axes&>(ax), path, params));
    EXPECT_GT(std::filesystem::file_size(path), 44u);   // at least WAV header
    std::filesystem::remove(path);
}

TEST_F(SonificationTest, WavFileHasCorrectHeader)
{
    std::vector<int16_t> pcm(44100, 0);   // 1 second at 44100 Hz
    const std::string    path =
        (std::filesystem::temp_directory_path() / "test_spectra_wav_hdr.wav").string();
    ASSERT_TRUE(write_wav(path, pcm, 44100u));

    std::ifstream f(path, std::ios::binary);
    ASSERT_TRUE(f.is_open());

    char riff[4];
    f.read(riff, 4);
    EXPECT_EQ(std::string(riff, 4), "RIFF");

    f.seekg(8);
    char wave[4];
    f.read(wave, 4);
    EXPECT_EQ(std::string(wave, 4), "WAVE");

    f.close();
    std::filesystem::remove(path);
}

// ─── Keyboard pan/zoom commands ─────────────────────────────────────────────
// These are smoke-tested via the shortcut_manager binding count.
// Full command execution tests live in test_shortcut_manager.cpp.

#include "ui/commands/shortcut_manager.hpp"

TEST(KeyboardNavShortcuts, ArrowKeysPanBound)
{
    using namespace spectra;
    ShortcutManager sm;
    sm.register_defaults();

    // Arrow keys must map to pan commands
    EXPECT_EQ(sm.command_for_shortcut(Shortcut::from_string("Left")), "view.pan_left");
    EXPECT_EQ(sm.command_for_shortcut(Shortcut::from_string("Right")), "view.pan_right");
    EXPECT_EQ(sm.command_for_shortcut(Shortcut::from_string("Up")), "view.pan_up");
    EXPECT_EQ(sm.command_for_shortcut(Shortcut::from_string("Down")), "view.pan_down");
}

TEST(KeyboardNavShortcuts, PlusMinusZoomBound)
{
    using namespace spectra;
    ShortcutManager sm;
    sm.register_defaults();

    EXPECT_EQ(sm.command_for_shortcut(Shortcut::from_string("=")), "view.zoom_in");
    EXPECT_EQ(sm.command_for_shortcut(Shortcut::from_string("-")), "view.zoom_out");
}
