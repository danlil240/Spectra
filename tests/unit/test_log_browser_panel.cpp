// Tests for the Log Browser panel (src/ui/logs/): opening dropped log files
// through the registry, plotting a field into axes with double-precision
// timestamps, the axes-provider fallback, and an ImGui draw smoke test.

#include <gtest/gtest.h>
#include <imgui.h>

#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "io/logs/log_reader_registry.hpp"
#include "ui/logs/log_browser_panel.hpp"
#include "ui/theme/theme.hpp"

#ifdef SPECTRA_HAS_MCAP
    #include <mcap/writer.hpp>
#endif

using spectra::Axes;
using spectra::Figure;
using spectra::LineSeries;
using spectra::ui::logs::LogBrowserPanel;
using spectra::ui::logs::LogFieldDragPayload;

namespace
{

std::string fixture_ulog()
{
    return std::string(SPECTRA_SOURCE_DIR) + "/tests/data/ulog/px4_fixture.ulg";
}

std::string temp_path(const std::string& name)
{
    return (std::filesystem::temp_directory_path() / ("spectra_log_panel_" + name)).string();
}

double x_at(const LineSeries& s, size_t i)
{
    return s.x_offset() + static_cast<double>(s.x_data()[i]);
}

}   // namespace

TEST(LogBrowserPanel, DroppedULogOpensAndPlotsIntoAxes)
{
    spectra::logs::LogReaderRegistry reg;
    spectra::logs::register_builtin_log_readers(reg);
    LogBrowserPanel panel;
    panel.set_registry(&reg);

    // What the app's file-drop handler checks before opening.
    EXPECT_TRUE(panel.can_open(fixture_ulog()));
    EXPECT_FALSE(panel.can_open(temp_path("data.csv")));

    EXPECT_FALSE(panel.is_visible());
    ASSERT_TRUE(panel.open_file(fixture_ulog())) << panel.status();
    EXPECT_TRUE(panel.is_visible());   // opening shows the panel
    EXPECT_FALSE(panel.status_is_error());
    ASSERT_EQ(panel.files().size(), 1u);

    const auto& file = panel.files()[0];
    EXPECT_EQ(file.name, "px4_fixture.ulg");
    ASSERT_EQ(file.tree.children.size(), 3u);   // three topics
    EXPECT_EQ(file.tree.children[1].name, "sensor_combined");

    const auto idx = file.reader->find_field("sensor_combined", "gyro_rad[2]");
    ASSERT_TRUE(idx.has_value());

    Figure fig({.width = 640, .height = 480});
    Axes&  ax = fig.subplot(1, 1, 1);

    // A drag payload dropped on the axes.
    LogFieldDragPayload payload{file.id, static_cast<uint32_t>(*idx)};
    LineSeries*         line = panel.plot_payload(payload, &ax);
    ASSERT_NE(line, nullptr) << panel.status();
    ASSERT_EQ(ax.series().size(), 1u);
    EXPECT_EQ(line->label(), "sensor_combined/gyro_rad[2]");
    EXPECT_EQ(ax.xlabel(), "Time (s)");
    ASSERT_EQ(line->x_data().size(), 10u);
    for (size_t i = 0; i < 10; ++i)
    {
        EXPECT_NEAR(x_at(*line, i), 1.0 + 0.1 * static_cast<double>(i), 1e-6);
        EXPECT_FLOAT_EQ(line->y_data()[i], 0.5f * static_cast<float>(i));
    }

    // A second field adds a second series to the same axes.
    ASSERT_NE(panel.plot_field(file.id, 0, &ax), nullptr);
    EXPECT_EQ(ax.series().size(), 2u);
}

TEST(LogBrowserPanel, UsesAxesProviderWithoutTarget)
{
    LogBrowserPanel panel;   // no registry wired: built-in readers
    ASSERT_TRUE(panel.open_file(fixture_ulog())) << panel.status();
    const uint32_t id = panel.files()[0].id;

    // No provider and no target: nothing to plot into.
    EXPECT_FALSE(panel.has_axes_provider());
    EXPECT_EQ(panel.plot_field(id, 0), nullptr);
    EXPECT_TRUE(panel.status_is_error());

    Figure fig({.width = 640, .height = 480});
    int    calls = 0;
    panel.set_axes_provider(
        [&]() -> Axes*
        {
            ++calls;
            return &fig.subplot(1, 1, 1);
        });
    ASSERT_NE(panel.plot_field(id, 0), nullptr);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(fig.axes().front()->series().size(), 1u);

    // Bad indices fail without touching the axes.
    EXPECT_EQ(panel.plot_field(id, 999), nullptr);
    EXPECT_EQ(panel.plot_field(id + 100, 0), nullptr);
    EXPECT_EQ(fig.axes().front()->series().size(), 1u);
}

TEST(LogBrowserPanel, ReopenReplacesAndCloseRemoves)
{
    LogBrowserPanel panel;
    ASSERT_TRUE(panel.open_file(fixture_ulog()));
    const uint32_t first = panel.files()[0].id;
    ASSERT_TRUE(panel.open_file(fixture_ulog()));
    ASSERT_EQ(panel.files().size(), 1u);
    EXPECT_NE(panel.files()[0].id, first);   // stale drag payloads cannot resolve
    EXPECT_EQ(panel.find_file(first), nullptr);

    panel.close_file(panel.files()[0].id);
    EXPECT_TRUE(panel.files().empty());
}

TEST(LogBrowserPanel, OpenFailureIsReported)
{
    const std::string bad = temp_path("broken.ulg");
    {
        std::ofstream out(bad, std::ios::binary);
        out << "definitely not a ulog";
    }
    LogBrowserPanel panel;
    EXPECT_FALSE(panel.open_file(bad));
    EXPECT_TRUE(panel.status_is_error());
    EXPECT_FALSE(panel.status().empty());
    EXPECT_TRUE(panel.files().empty());
    EXPECT_TRUE(panel.is_visible());   // so the error is seen
    std::filesystem::remove(bad);
}

#ifdef SPECTRA_HAS_MCAP
TEST(LogBrowserPanel, McapEpochTimestampsKeepPrecision)
{
    const std::string path = temp_path("epoch.mcap");
    {
        mcap::McapWriter        writer;
        mcap::McapWriterOptions opts("spectra-test");
        opts.compression = mcap::Compression::None;
        ASSERT_TRUE(writer.open(path, opts).ok());
        mcap::Schema schema("Value", "jsonschema", "{}");
        writer.addSchema(schema);
        mcap::Channel ch("/value", "json", schema.id);
        writer.addChannel(ch);
        for (int i = 0; i < 4; ++i)
        {
            const std::string json = "{\"v\": " + std::to_string(i) + "}";
            mcap::Message     m;
            m.channelId   = ch.id;
            m.sequence    = 0;
            m.logTime     = 1'700'000'000'123'000'000ull + static_cast<uint64_t>(i) * 1'000'000ull;
            m.publishTime = m.logTime;
            m.data        = reinterpret_cast<const std::byte*>(json.data());
            m.dataSize    = json.size();
            ASSERT_TRUE(writer.write(m).ok());
        }
        writer.close();
    }

    LogBrowserPanel panel;
    ASSERT_TRUE(panel.open_file(path)) << panel.status();
    Figure      fig({.width = 640, .height = 480});
    LineSeries* line = panel.plot_field(panel.files()[0].id, 0, &fig.subplot(1, 1, 1));
    ASSERT_NE(line, nullptr) << panel.status();
    EXPECT_NE(line->x_offset(), 0.0);
    // 1 ms steps on top of ~1.7e9 s survive (float alone would round to 128 s).
    for (size_t i = 0; i < 4; ++i)
        EXPECT_NEAR(x_at(*line, i), 1'700'000'000.123 + 0.001 * static_cast<double>(i), 1e-6);
    std::filesystem::remove(path);
}
#endif

TEST(LogBrowserPanel, DrawsWithImGui)
{
    spectra::ui::ThemeManager theme;   // the panel takes its colours from the theme
    spectra::ui::ThemeManager::set_current(&theme);
    ImGui::CreateContext();
    ImGuiIO& io    = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 800.0f);
    io.DeltaTime   = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    unsigned char* px = nullptr;
    int            tw = 0, th = 0;
    io.Fonts->GetTexDataAsRGBA32(&px, &tw, &th);

    LogBrowserPanel panel;
    panel.set_visible(true);
    for (int frame = 0; frame < 2; ++frame)   // empty state
    {
        ImGui::NewFrame();
        panel.draw();
        ImGui::Render();
    }
    ASSERT_TRUE(panel.open_file(fixture_ulog()));
    for (const char* filter : {"", "gyro", "no-such-field"})   // tree, filtered, no match
    {
        panel.set_filter(filter);
        ImGui::NewFrame();
        panel.draw();
        ImGui::Render();
    }
    EXPECT_STREQ(panel.filter(), "no-such-field");
    ImGui::DestroyContext();
    spectra::ui::ThemeManager::set_current(nullptr);
    EXPECT_EQ(panel.files().size(), 1u);
}
