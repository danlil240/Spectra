// test_plugin_export_command.cpp — File > Export <plugin format> wiring.
//
// Regression for the File menus executing "file.export_plugin.<name>" while no
// code registered that command, serialized the figure or called
// ExportFormatRegistry::export_figure().

#include <gtest/gtest.h>

#ifdef SPECTRA_USE_IMGUI

    #include <spectra/axes.hpp>
    #include <spectra/figure.hpp>
    #include <spectra/series.hpp>

    #include <filesystem>
    #include <fstream>
    #include <limits>
    #include <optional>
    #include <sstream>
    #include <string>
    #include <vector>

    #include "io/export_registry.hpp"
    #include "ui/app/commands/plugin_export_commands.hpp"
    #include "ui/commands/command_registry.hpp"
    #include "ui/workspace/plugin_api.hpp"

using namespace spectra;

namespace
{

std::string read_file(const std::filesystem::path& p)
{
    std::ifstream     in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

class PluginExportCommandTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        auto& ax = figure_.subplot(1, 1, 1);
        ax.title("Signals");
        ax.xlabel("t");
        ax.ylabel("v");
        std::vector<float> x  = {0.0f, 1.0f, 2.0f};
        std::vector<float> y  = {0.5f, -1.0f, 2.25f};
        std::vector<float> x2 = {10.0f, 20.0f};
        std::vector<float> y2 = {3.0f, 4.0f};
        ax.line(x, y).label("speed \"m/s\"");
        ax.scatter(x2, y2).label("pts");
        active_ = &figure_;

        out_path_ =
            std::filesystem::temp_directory_path()
            / ("spectra_plugin_export_cmd_"
               + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + ".txt");
        std::filesystem::remove(out_path_);
    }

    void TearDown() override
    {
        cmds_.detach();
        std::filesystem::remove(out_path_);
    }

    void attach(PluginExportPathProvider ask = {})
    {
        PluginExportCommands::Options opts;
        opts.formats       = &formats_;
        opts.commands      = &registry_;
        opts.active_figure = &active_;
        opts.ask_path =
            ask ? std::move(ask) : [this](const ExportFormatInfo&) -> std::optional<std::string>
        { return out_path_.string(); };
        cmds_.attach(std::move(opts));
    }

    // Registers a format whose callback writes the figure JSON it receives.
    void register_echo_format(const std::string& name)
    {
        formats_.register_format(name,
                                 "txt",
                                 [this](const ExportContext& ctx)
                                 {
                                     ++calls_;
                                     std::ofstream out(ctx.output_path, std::ios::binary);
                                     out.write(ctx.figure_json,
                                               static_cast<std::streamsize>(ctx.figure_json_len));
                                     return out.good();
                                 });
    }

    Figure                figure_;
    Figure*               active_ = nullptr;
    ExportFormatRegistry  formats_;
    CommandRegistry       registry_;
    PluginExportCommands  cmds_;
    std::filesystem::path out_path_;
    int                   calls_ = 0;
};

}   // namespace

TEST(PluginExportJson, SerializesLineAndScatterData)
{
    Figure             fig;
    auto&              ax = fig.subplot(1, 1, 1);
    std::vector<float> x  = {0.0f, 0.1f};
    std::vector<float> y  = {1.0f, 2.0f};
    ax.line(x, y).label("a");
    // Raw strings stay out of the gtest macro (MSVC preprocessor).
    const std::string expected =
        R"({"axes":[{"index":0,"title":"","xlabel":"","ylabel":"","series":[)"
        R"({"label":"a","type":"line","x":[0,0.1],"y":[1,2]}]}]})";
    EXPECT_EQ(figure_to_export_json(fig), expected);
}

TEST(PluginExportJson, AppliesOffsetsAndWritesNonFiniteAsNull)
{
    Figure              fig;
    auto&               ax = fig.subplot(1, 1, 1);
    std::vector<double> x  = {1.7e9, 1.7e9 + 1.0};
    std::vector<double> y  = {0.0, std::numeric_limits<double>::quiet_NaN()};
    ax.line(x, y).label("t");
    const std::string json = figure_to_export_json(fig);
    EXPECT_NE(json.find("\"x\":[1.7e+09,1700000001]"), std::string::npos) << json;
    EXPECT_NE(json.find("\"y\":[0,null]"), std::string::npos) << json;
}

TEST_F(PluginExportCommandTest, ExistingFormatGetsCommandThatExportsActiveFigure)
{
    register_echo_format("Echo");
    attach();

    const Command* cmd = registry_.find("file.export_plugin.Echo");
    ASSERT_NE(cmd, nullptr);
    EXPECT_EQ(cmd->label, "Export Echo (.txt)");
    EXPECT_EQ(cmd->category, "File");

    ASSERT_TRUE(registry_.execute("file.export_plugin.Echo"));
    EXPECT_EQ(calls_, 1);
    const std::string expected =
        R"({"axes":[{"index":0,"title":"Signals","xlabel":"t","ylabel":"v","series":[)"
        R"({"label":"speed \"m/s\"","type":"line","x":[0,1,2],"y":[0.5,-1,2.25]},)"
        R"({"label":"pts","type":"scatter","x":[10,20],"y":[3,4]}]}]})";
    EXPECT_EQ(read_file(out_path_), expected);
}

// Registers through the C ABI the shipped plugins use, the way a plugin loading
// (and later unregistering) after the window was created would.
TEST_F(PluginExportCommandTest, FollowsRegistrationAndUnregistration)
{
    attach();
    EXPECT_EQ(registry_.find("file.export_plugin.Late Format"), nullptr);

    auto* c_reg = static_cast<SpectraExportFormatRegistry>(&formats_);
    auto  cb    = [](const SpectraExportContext* ctx, void* user) -> int
    {
        ++*static_cast<int*>(user);
        std::ofstream out(ctx->output_path, std::ios::binary);
        out.write(ctx->figure_json, static_cast<std::streamsize>(ctx->figure_json_len));
        return out.good() ? 0 : 1;
    };
    ASSERT_EQ(spectra_register_export_format(c_reg, "Late Format", "dat", cb, &calls_), 0);
    ASSERT_NE(registry_.find("file.export_plugin.Late Format"), nullptr);
    ASSERT_TRUE(registry_.execute("file.export_plugin.Late Format"));
    EXPECT_EQ(calls_, 1);
    EXPECT_NE(read_file(out_path_).find("\"label\":\"pts\""), std::string::npos);

    ASSERT_EQ(spectra_unregister_export_format(c_reg, "Late Format"), 0);
    EXPECT_EQ(registry_.find("file.export_plugin.Late Format"), nullptr);
    EXPECT_FALSE(registry_.execute("file.export_plugin.Late Format"));
    EXPECT_EQ(calls_, 1);
}

TEST_F(PluginExportCommandTest, CancelledPathOrNoFigureSkipsExport)
{
    register_echo_format("Echo");
    attach([](const ExportFormatInfo&) -> std::optional<std::string> { return std::nullopt; });
    ASSERT_TRUE(registry_.execute("file.export_plugin.Echo"));
    EXPECT_EQ(calls_, 0);

    attach();
    active_ = nullptr;
    ASSERT_TRUE(registry_.execute("file.export_plugin.Echo"));
    EXPECT_EQ(calls_, 0);
    EXPECT_FALSE(std::filesystem::exists(out_path_));
}

TEST_F(PluginExportCommandTest, DetachRemovesCommandsAndStopsListening)
{
    register_echo_format("Echo");
    attach();
    ASSERT_NE(registry_.find("file.export_plugin.Echo"), nullptr);

    cmds_.detach();
    EXPECT_EQ(registry_.find("file.export_plugin.Echo"), nullptr);

    register_echo_format("Other");
    EXPECT_EQ(registry_.find("file.export_plugin.Other"), nullptr);
}

TEST(ExportFormatRegistryListener, NotifiesOnceAndNotForDuplicates)
{
    ExportFormatRegistry     reg;
    std::vector<std::string> events;
    auto                     id = reg.add_change_listener(
        [&](const ExportFormatInfo& info, ExportFormatRegistry::ChangeKind kind)
        {
            events.push_back((kind == ExportFormatRegistry::ChangeKind::Registered ? "+" : "-")
                             + info.name + "." + info.extension);
        });
    reg.register_format("A", "a", [](const ExportContext&) { return true; });
    reg.register_format("A", "a", [](const ExportContext&) { return true; });   // duplicate
    reg.unregister_format("A");
    reg.unregister_format("A");   // already gone
    reg.remove_change_listener(id);
    reg.register_format("B", "b", [](const ExportContext&) { return true; });
    EXPECT_EQ(events, (std::vector<std::string>{"+A.a", "-A.a"}));
}

#endif   // SPECTRA_USE_IMGUI
