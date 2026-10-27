// Tests for the CSV conversion used by examples/plugins/export_csv.cpp.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "export_csv_writer.hpp"

#ifdef SPECTRA_EXPORT_CSV_PLUGIN_PATH
    #include <dlfcn.h>

    #include "io/export_registry.hpp"
    #include "ui/workspace/plugin_api.hpp"
#endif

using spectra_examples::csv::figure_json_to_csv;

namespace
{

std::string to_csv(const std::string& json)
{
    std::string csv;
    EXPECT_TRUE(figure_json_to_csv(json, csv)) << json;
    return csv;
}

}   // namespace

TEST(ExportCsvPlugin, WritesNumericRowsPerSeries)
{
    const std::string json = R"({"axes":[{"series":[
        {"label":"temp","x":[0,1,2],"y":[1.5,2.5,-3]}]}]})";
    EXPECT_EQ(to_csv(json),
              "temp x,temp y\r\n"
              "0,1.5\r\n"
              "1,2.5\r\n"
              "2,-3\r\n");
}

TEST(ExportCsvPlugin, QuotesLabelsWithCommasQuotesAndNewlines)
{
    const std::string json = R"({"series":[
        {"label":"a,b","x":[1],"y":[2]},
        {"label":"say \"hi\"","x":[3],"y":[4]},
        {"label":"two\nlines","x":[5],"y":[6]}]})";
    EXPECT_EQ(to_csv(json),
              "\"a,b x\",\"a,b y\","
              "\"say \"\"hi\"\" x\",\"say \"\"hi\"\" y\","
              "\"two\nlines x\",\"two\nlines y\"\r\n"
              "1,2,3,4,5,6\r\n");
}

TEST(ExportCsvPlugin, EmptyLabelDoesNotStopLaterSeries)
{
    const std::string json = R"({"series":[
        {"label":"","x":[1],"y":[10]},
        {"label":"second","x":[2],"y":[20]}]})";
    EXPECT_EQ(to_csv(json),
              "Series 1 x,Series 1 y,second x,second y\r\n"
              "1,10,2,20\r\n");
}

TEST(ExportCsvPlugin, PadsRaggedSeriesWithEmptyCells)
{
    const std::string json = R"({"series":[
        {"label":"long","x":[0,1,2],"y":[5,6,7]},
        {"label":"short","x":[0],"y":[9]}]})";
    EXPECT_EQ(to_csv(json),
              "long x,long y,short x,short y\r\n"
              "0,5,0,9\r\n"
              "1,6,,\r\n"
              "2,7,,\r\n");
}

TEST(ExportCsvPlugin, ToleratesWhitespaceKeyOrderAndUnknownKeys)
{
    const std::string json = "{ \"title\" : \"t\",\n  \"series\" : [ {\n    \"y\" : [ 3 , 4 ],\n"
                             "    \"color\" : { \"r\" : 1 },\n    \"label\" : \"s\\u00e9\",\n"
                             "    \"x\" : [ 1e0 , 2.0 ]\n  } ]\n}\n";
    EXPECT_EQ(to_csv(json),
              "s\xC3\xA9 x,s\xC3\xA9 y\r\n"
              "1,3\r\n"
              "2,4\r\n");
}

TEST(ExportCsvPlugin, MissingXUsesSampleIndexAndNullIsEmpty)
{
    const std::string json = R"({"series":[{"label":"y only","y":[7,null,9]}]})";
    EXPECT_EQ(to_csv(json),
              "y only x,y only y\r\n"
              "0,7\r\n"
              "1,\r\n"
              "2,9\r\n");
}

TEST(ExportCsvPlugin, NoSeriesProducesEmptyOutput)
{
    EXPECT_EQ(to_csv(R"({"axes":[{"title":"My Axes"}]})"), "");
}

TEST(ExportCsvPlugin, MalformedJsonIsRejected)
{
    std::string csv;
    EXPECT_FALSE(figure_json_to_csv(R"({"series":[{"label":"x")", csv));
    EXPECT_FALSE(figure_json_to_csv("", csv));
}

#ifdef SPECTRA_EXPORT_CSV_PLUGIN_PATH
// End-to-end: load the shipped example plug-in, register its format into a real
// ExportFormatRegistry and export through the registry.  The context is built
// by hand (minor version 3) so this test does not depend on the host's
// version-guard behaviour.
TEST(ExportCsvPlugin, ShippedPluginWritesNumericCsv)
{
    void* handle = dlopen(SPECTRA_EXPORT_CSV_PLUGIN_PATH, RTLD_NOW | RTLD_LOCAL);
    ASSERT_NE(handle, nullptr) << dlerror();
    auto init =
        reinterpret_cast<spectra::SpectraPluginInitFn>(dlsym(handle, "spectra_plugin_init"));
    ASSERT_NE(init, nullptr);

    spectra::ExportFormatRegistry reg;
    spectra::SpectraPluginContext ctx{};
    spectra::SpectraPluginInfo    info{};
    ctx.api_version_major      = 2;
    ctx.api_version_minor      = 3;
    ctx.export_format_registry = &reg;
    ASSERT_EQ(init(&ctx, &info), 0);
    ASSERT_EQ(reg.available_formats().size(), 1u);

    namespace fs    = std::filesystem;
    const auto path = (fs::temp_directory_path() / "spectra_export_csv_plugin_test.csv").string();
    const std::string json = R"({"series":[{"label":"","x":[0,1],"y":[2,3]},
        {"label":"q\"c,d","x":[5],"y":[6]}]})";
    ASSERT_TRUE(reg.export_figure("CSV Data", json, nullptr, 0, 0, path));

    std::ifstream     f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    f.close();
    fs::remove(path);
    EXPECT_EQ(ss.str(),
              "Series 1 x,Series 1 y,\"q\"\"c,d x\",\"q\"\"c,d y\"\r\n"
              "0,2,5,6\r\n"
              "1,3,,\r\n");

    // C ABI unregister (also links plugin_api/plugin_guard from libspectra).
    EXPECT_EQ(spectra::spectra_unregister_export_format(&reg, "CSV Data"), 0);
    dlclose(handle);
}
#endif
