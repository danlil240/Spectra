// test_px4_file_drop.cpp — Dropping a ULog onto spectra-px4 opens it as data.
//
// Px4AppShell::set_window_manager() routes the WindowManager's file drops
// (the dispatch shared by the GLFW drop callback and SDL_EVENT_DROP_FILE) to
// the shell, which opens .ulg files on its next poll().  Before this, a ULog
// dropped on the PX4 viewer was handed to the main app's CSV-only handler and
// silently ignored.  Uses the committed fixture tests/data/ulog/px4_fixture.ulg.
// Headless: no window, GPU or ImGui context.

#include <gtest/gtest.h>

#include "px4_app_shell.hpp"

#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>

#include "render/vulkan/window_context.hpp"
#include "ui/app/window_ui_context.hpp"
#include "ui/window/window_manager.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef SPECTRA_SOURCE_DIR
    #define SPECTRA_SOURCE_DIR "."
#endif

namespace fs = std::filesystem;
using namespace spectra::adapters::px4;

namespace
{

std::string fixture_path()
{
    return std::string(SPECTRA_SOURCE_DIR) + "/tests/data/ulog/px4_fixture.ulg";
}

// The battery voltage line the auto-plot draws for the fixture, or null.
const spectra::LineSeries* voltage_line(const spectra::Figure& fig)
{
    for (const auto& ax : fig.axes())
    {
        if (!ax)
            continue;
        for (const auto& s : ax->series())
        {
            auto* line = dynamic_cast<const spectra::LineSeries*>(s.get());
            if (line && line->label() == "voltage (V)")
                return line;
        }
    }
    return nullptr;
}

class Px4FileDropTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        ASSERT_TRUE(fs::exists(fixture_path())) << fixture_path();
        ASSERT_TRUE(shell_.init());
        shell_.set_canvas_figure(&fig_);
        shell_.set_window_manager(&wm_);
        tmp_ = fs::temp_directory_path() / "spectra_px4_file_drop";
        fs::create_directories(tmp_);
    }

    void TearDown() override
    {
        shell_.shutdown();
        std::error_code ec;
        fs::remove_all(tmp_, ec);
    }

    // The WindowManager is declared first so it outlives the shell's handler.
    spectra::WindowManager wm_;
    Px4AppShell            shell_{Px4AppConfig{}};
    spectra::Figure        fig_{{.width = 900, .height = 700}};
    fs::path               tmp_;
};

}   // namespace

TEST_F(Px4FileDropTest, DroppedUlogOpensAsPlottedData)
{
    ASSERT_FALSE(shell_.reader().is_open());
    ASSERT_TRUE(shell_.empty_state_visible());

    // Window drop -> WindowManager dispatch -> shell handler (queued).
    EXPECT_TRUE(wm_.dispatch_file_drop(1, fixture_path().c_str()));
    EXPECT_FALSE(shell_.reader().is_open()) << "opened inside the event callback";

    shell_.poll();

    ASSERT_TRUE(shell_.reader().is_open()) << shell_.last_open_error();
    EXPECT_TRUE(shell_.last_open_error().empty());
    EXPECT_EQ(shell_.reader().metadata().path, fixture_path());
    EXPECT_EQ(shell_.reader().topic_count(), 3u);
    EXPECT_EQ(shell_.reader().parameters().size(), 8u);
    EXPECT_FALSE(shell_.empty_state_visible());
    ASSERT_FALSE(shell_.recent_files().entries().empty());
    EXPECT_EQ(fs::path(shell_.recent_files().entries().front()), fs::absolute(fixture_path()));

    // The flight-review auto-plot shows the battery voltage with the file's values.
    const spectra::LineSeries* line = voltage_line(fig_);
    ASSERT_NE(line, nullptr) << "battery voltage not plotted";
    ASSERT_EQ(line->y_data().size(), 10u);
    for (size_t i = 0; i < 10; ++i)
        EXPECT_NEAR(line->y_offset() + line->y_data()[i], 16.0 - 0.1 * static_cast<double>(i), 1e-5)
            << "sample " << i;
    const double t0 = line->x_offset() + line->x_data()[0];
    const double t9 = line->x_offset() + line->x_data()[9];
    EXPECT_NEAR(t9 - t0, 0.9, 1e-5);

    // Nothing queued any more: a second poll does not reopen.
    EXPECT_FALSE(shell_.process_pending_file_drop());
}

TEST_F(Px4FileDropTest, ExtensionIsCaseInsensitive)
{
    const fs::path upper = tmp_ / "FLIGHT.ULG";
    fs::copy_file(fixture_path(), upper, fs::copy_options::overwrite_existing);
    EXPECT_TRUE(Px4AppShell::is_ulog_path(upper.string()));
    EXPECT_TRUE(Px4AppShell::is_ulog_path("/logs/a.ulog"));
    EXPECT_FALSE(Px4AppShell::is_ulog_path("/logs/a.csv"));
    EXPECT_FALSE(Px4AppShell::is_ulog_path("/logs/ulg"));

    ASSERT_TRUE(wm_.dispatch_file_drop(1, upper.string().c_str()));
    shell_.poll();
    ASSERT_TRUE(shell_.reader().is_open()) << shell_.last_open_error();
    EXPECT_EQ(shell_.reader().metadata().path, upper.string());
}

TEST_F(Px4FileDropTest, NonUlogDropIsRejectedAndKeepsOpenLog)
{
    ASSERT_TRUE(shell_.open_ulog(fixture_path()));
    const fs::path csv = tmp_ / "data.csv";
    std::ofstream(csv) << "t,v\n0,1\n1,2\n";

    EXPECT_FALSE(shell_.handle_file_drop(csv.string()));
    EXPECT_NE(shell_.last_open_error().find("not a PX4 ULog"), std::string::npos);
    EXPECT_FALSE(shell_.process_pending_file_drop());
    shell_.poll();
    ASSERT_TRUE(shell_.reader().is_open());
    EXPECT_EQ(shell_.reader().metadata().path, fixture_path());
    EXPECT_NE(voltage_line(fig_), nullptr);
}

TEST_F(Px4FileDropTest, CorruptUlogDropReportsError)
{
    const fs::path bad = tmp_ / "broken.ulg";
    std::ofstream(bad, std::ios::binary) << "not a ulog file at all";

    ASSERT_TRUE(wm_.dispatch_file_drop(1, bad.string().c_str()));
    shell_.poll();
    EXPECT_FALSE(shell_.reader().is_open());
    EXPECT_FALSE(shell_.last_open_error().empty());
    EXPECT_TRUE(shell_.empty_state_visible());
}

TEST_F(Px4FileDropTest, LastOfSeveralDroppedLogsWins)
{
    const fs::path first = tmp_ / "first.ulg";
    fs::copy_file(fixture_path(), first, fs::copy_options::overwrite_existing);

    // One OS drop of two files arrives as two dispatches before the next poll.
    ASSERT_TRUE(wm_.dispatch_file_drop(1, first.string().c_str()));
    ASSERT_TRUE(wm_.dispatch_file_drop(1, fixture_path().c_str()));
    shell_.poll();
    ASSERT_TRUE(shell_.reader().is_open());
    EXPECT_EQ(shell_.reader().metadata().path, fixture_path());
}
