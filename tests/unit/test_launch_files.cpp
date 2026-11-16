// Command-line parsing, file routing (drop / command line / Open File) and the
// empty-axes hint predicate.

#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <string>
#include <vector>

#include "ui/app/launch_files.hpp"
#include "ui/figures/empty_axes_hint.hpp"

using namespace spectra;

namespace
{

LaunchArgs parse(std::vector<const char*> args)
{
    args.insert(args.begin(), "spectra");
    return parse_launch_args(static_cast<int>(args.size()), args.data());
}

}   // namespace

// ─── parse_launch_args ───────────────────────────────────────────────────────

TEST(LaunchArgs, NoArguments)
{
    const LaunchArgs a = parse({});
    EXPECT_TRUE(a.files.empty());
    EXPECT_TRUE(a.layout.empty());
    EXPECT_TRUE(a.warnings.empty());
    EXPECT_TRUE(a.error.empty());
    EXPECT_FALSE(a.show_help);
    EXPECT_FALSE(a.show_version);
}

TEST(LaunchArgs, PositionalFilesKeepOrder)
{
    const LaunchArgs a = parse({"data.csv", "flight.ulg", "dash.spectra"});
    ASSERT_EQ(a.files.size(), 3u);
    EXPECT_EQ(a.files[0], "data.csv");
    EXPECT_EQ(a.files[1], "flight.ulg");
    EXPECT_EQ(a.files[2], "dash.spectra");
    EXPECT_TRUE(a.warnings.empty());
}

TEST(LaunchArgs, LayoutOptionBothForms)
{
    EXPECT_EQ(parse({"--layout", "a.spectra"}).layout, "a.spectra");
    EXPECT_EQ(parse({"--layout=b.spectra"}).layout, "b.spectra");
    const LaunchArgs mixed = parse({"x.csv", "--layout", "a.spectra", "y.mcap"});
    EXPECT_EQ(mixed.layout, "a.spectra");
    ASSERT_EQ(mixed.files.size(), 2u);
    EXPECT_EQ(mixed.files[1], "y.mcap");
}

TEST(LaunchArgs, LayoutWithoutFileIsAnError)
{
    EXPECT_FALSE(parse({"--layout"}).error.empty());
    EXPECT_FALSE(parse({"--layout="}).error.empty());
}

TEST(LaunchArgs, HelpAndVersion)
{
    EXPECT_TRUE(parse({"--help"}).show_help);
    EXPECT_TRUE(parse({"-h"}).show_help);
    EXPECT_TRUE(parse({"--version"}).show_version);
    EXPECT_TRUE(parse({"-v"}).show_version);
    EXPECT_NE(launch_usage().find("[FILE...]"), std::string::npos);
}

TEST(LaunchArgs, UnknownOptionsWarnInsteadOfBeingDropped)
{
    const LaunchArgs a = parse({"--bogus", "data.csv", "-x"});
    ASSERT_EQ(a.warnings.size(), 2u);
    EXPECT_NE(a.warnings[0].find("--bogus"), std::string::npos);
    EXPECT_NE(a.warnings[1].find("-x"), std::string::npos);
    ASSERT_EQ(a.files.size(), 1u);
    EXPECT_EQ(a.files[0], "data.csv");
}

TEST(LaunchArgs, KnownDialogFlagIsAccepted)
{
    EXPECT_TRUE(parse({"--no-native-dialogs"}).warnings.empty());
}

TEST(LaunchArgs, DoubleDashMakesEverythingAFile)
{
    const LaunchArgs a = parse({"--", "--weird-name.csv", "-v"});
    EXPECT_FALSE(a.show_version);
    EXPECT_TRUE(a.warnings.empty());
    ASSERT_EQ(a.files.size(), 2u);
    EXPECT_EQ(a.files[0], "--weird-name.csv");
}

// ─── classify_open_file ──────────────────────────────────────────────────────

TEST(OpenFileRouting, ByExtension)
{
    EXPECT_EQ(classify_open_file("a.csv"), OpenFileKind::Csv);
    EXPECT_EQ(classify_open_file("/x/A.TSV"), OpenFileKind::Csv);
    EXPECT_EQ(classify_open_file("notes.txt"), OpenFileKind::Csv);
    EXPECT_EQ(classify_open_file("flight.ulg"), OpenFileKind::Log);
    EXPECT_EQ(classify_open_file("bag.MCAP"), OpenFileKind::Log);
    EXPECT_EQ(classify_open_file("dash.spectra"), OpenFileKind::Layout);
    EXPECT_EQ(classify_open_file("layout.json"), OpenFileKind::Layout);
    EXPECT_EQ(classify_open_file("image.png"), OpenFileKind::Unsupported);
    EXPECT_EQ(classify_open_file("README"), OpenFileKind::Unsupported);
}

TEST(OpenFileRouting, LogReaderRegistryDecidesLogs)
{
    auto only_bin = [](const std::string& p) { return open_file_extension(p) == "bin"; };
    EXPECT_EQ(classify_open_file("log.bin", only_bin), OpenFileKind::Log);
    EXPECT_EQ(classify_open_file("flight.ulg", only_bin), OpenFileKind::Unsupported);
    // CSV and layouts never go to the log browser.
    EXPECT_EQ(classify_open_file("a.csv", [](const std::string&) { return true; }),
              OpenFileKind::Csv);
}

TEST(OpenFileRouting, KindLabels)
{
    EXPECT_EQ(open_file_kind_label("a.csv"), "CSV");
    EXPECT_EQ(open_file_kind_label("a.tsv"), "TSV");
    EXPECT_EQ(open_file_kind_label("f.ulg"), "ULog");
    EXPECT_EQ(open_file_kind_label("b.mcap"), "MCAP");
    EXPECT_EQ(open_file_kind_label("d.spectra"), "Layout");
}

// ─── Empty-axes hint ─────────────────────────────────────────────────────────

TEST(EmptyAxesHint, ShownOnlyForLargeAxesWithoutSeries)
{
    Figure fig(FigureConfig{800, 600});
    auto&  ax = fig.subplot(1, 1, 1);
    ax.set_viewport({0.0f, 0.0f, 600.0f, 400.0f});
    EXPECT_TRUE(axes_shows_empty_hint(ax));

    ax.set_viewport({0.0f, 0.0f, 120.0f, 90.0f});
    EXPECT_FALSE(axes_shows_empty_hint(ax)) << "tiny subplots stay clean";

    ax.set_viewport({0.0f, 0.0f, 600.0f, 400.0f});
    const std::vector<float> x = {0.0f, 1.0f};
    const std::vector<float> y = {1.0f, 2.0f};
    ax.line(x, y);
    EXPECT_FALSE(axes_shows_empty_hint(ax));
}
