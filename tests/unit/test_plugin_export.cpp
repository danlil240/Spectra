// test_plugin_export.cpp — Unit tests for ExportFormatRegistry and Plugin C ABI (C1 / C3)

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "io/export_registry.hpp"
#include "ui/workspace/plugin_api.hpp"

using namespace spectra;

// ─── ExportFormatRegistry tests ──────────────────────────────────────────────

TEST(ExportFormatRegistry, RegisterAndList)
{
    ExportFormatRegistry reg;
    EXPECT_EQ(reg.count(), 0u);

    reg.register_format("CSV Data", "csv", [](const ExportContext&) { return true; });
    EXPECT_EQ(reg.count(), 1u);

    auto formats = reg.available_formats();
    ASSERT_EQ(formats.size(), 1u);
    EXPECT_EQ(formats[0].name, "CSV Data");
    EXPECT_EQ(formats[0].extension, "csv");
}

TEST(ExportFormatRegistry, RegisterDuplicateIsIgnored)
{
    ExportFormatRegistry reg;
    reg.register_format("CSV Data", "csv", [](const ExportContext&) { return true; });
    reg.register_format("CSV Data", "csv", [](const ExportContext&) { return false; });
    EXPECT_EQ(reg.count(), 1u);
}

TEST(ExportFormatRegistry, UnregisterRemovesFormat)
{
    ExportFormatRegistry reg;
    reg.register_format("CSV Data", "csv", [](const ExportContext&) { return true; });
    reg.register_format("JSON Data", "json", [](const ExportContext&) { return true; });
    EXPECT_EQ(reg.count(), 2u);

    reg.unregister_format("CSV Data");
    EXPECT_EQ(reg.count(), 1u);

    auto formats = reg.available_formats();
    EXPECT_EQ(formats[0].name, "JSON Data");
}

TEST(ExportFormatRegistry, UnregisterNonexistentIsNoop)
{
    ExportFormatRegistry reg;
    reg.unregister_format("DoesNotExist");   // must not crash
    EXPECT_EQ(reg.count(), 0u);
}

TEST(ExportFormatRegistry, ExportFigureCallsCallback)
{
    ExportFormatRegistry reg;

    bool called = false;
    reg.register_format("Test Format",
                        "tst",
                        [&called](const ExportContext& ctx) -> bool
                        {
                            called = true;
                            EXPECT_STREQ(ctx.output_path, "/tmp/out.tst");
                            EXPECT_NE(ctx.figure_json, nullptr);
                            return true;
                        });

    bool ok = reg.export_figure("Test Format", "{}", nullptr, 0, 0, "/tmp/out.tst");
    EXPECT_TRUE(ok);
    EXPECT_TRUE(called);
}

TEST(ExportFormatRegistry, ExportFigureReturnsFalseForUnknownFormat)
{
    ExportFormatRegistry reg;
    bool                 ok = reg.export_figure("NoSuchFormat", "{}", nullptr, 0, 0, "/tmp/x");
    EXPECT_FALSE(ok);
}

TEST(ExportFormatRegistry, ExportFigureForwardsPixelBuffer)
{
    ExportFormatRegistry       reg;
    const std::vector<uint8_t> pixels = {0xFF, 0x00, 0x00, 0xFF};   // 1×1 red RGBA

    uint32_t captured_w = 0, captured_h = 0;
    bool     captured_pixels_ok = false;

    reg.register_format("Pixel Test",
                        "px",
                        [&](const ExportContext& ctx) -> bool
                        {
                            captured_w = ctx.pixel_width;
                            captured_h = ctx.pixel_height;
                            captured_pixels_ok =
                                (ctx.rgba_pixels != nullptr && ctx.rgba_pixels[0] == 0xFF
                                 && ctx.rgba_pixels[1] == 0x00);
                            return true;
                        });

    reg.export_figure("Pixel Test", "{}", pixels.data(), 1, 1, "/tmp/pixel.px");
    EXPECT_EQ(captured_w, 1u);
    EXPECT_EQ(captured_h, 1u);
    EXPECT_TRUE(captured_pixels_ok);
}

TEST(ExportFormatRegistry, ExportCallbackReturningFalsePropagatesToCaller)
{
    ExportFormatRegistry reg;
    reg.register_format("Fail", "fail", [](const ExportContext&) { return false; });
    EXPECT_FALSE(reg.export_figure("Fail", "{}", nullptr, 0, 0, "/tmp/fail.fail"));
}

// ─── C ABI export registration tests ─────────────────────────────────────────

TEST(PluginExportCAPI, RegisterExportFormat)
{
    ExportFormatRegistry reg;

    bool called = false;
    auto cb     = [](const SpectraExportContext* ctx, void* ud) -> int
    {
        *static_cast<bool*>(ud) = true;
        EXPECT_STREQ(ctx->output_path, "/tmp/cabi.csv");
        return 0;
    };

    int result = spectra_register_export_format(&reg, "C ABI CSV", "csv", cb, &called);
    EXPECT_EQ(result, 0);
    EXPECT_EQ(reg.count(), 1u);

    auto formats = reg.available_formats();
    ASSERT_EQ(formats.size(), 1u);
    EXPECT_EQ(formats[0].name, "C ABI CSV");
    EXPECT_EQ(formats[0].extension, "csv");

    bool ok = reg.export_figure("C ABI CSV", "{}", nullptr, 0, 0, "/tmp/cabi.csv");
    EXPECT_TRUE(ok);
    EXPECT_TRUE(called);
}

TEST(PluginExportCAPI, RegisterExportFormatNullRegistry)
{
    auto cb = [](const SpectraExportContext*, void*) -> int { return 0; };
    EXPECT_EQ(spectra_register_export_format(nullptr, "X", "x", cb, nullptr), -1);
}

TEST(PluginExportCAPI, RegisterExportFormatNullName)
{
    ExportFormatRegistry reg;
    auto                 cb = [](const SpectraExportContext*, void*) -> int { return 0; };
    EXPECT_EQ(spectra_register_export_format(&reg, nullptr, "x", cb, nullptr), -1);
}

TEST(PluginExportCAPI, RegisterExportFormatNullExtension)
{
    ExportFormatRegistry reg;
    auto                 cb = [](const SpectraExportContext*, void*) -> int { return 0; };
    EXPECT_EQ(spectra_register_export_format(&reg, "X", nullptr, cb, nullptr), -1);
}

TEST(PluginExportCAPI, RegisterExportFormatNullCallback)
{
    ExportFormatRegistry reg;
    EXPECT_EQ(spectra_register_export_format(&reg, "X", "x", nullptr, nullptr), -1);
}

TEST(PluginExportCAPI, UnregisterExportFormat)
{
    ExportFormatRegistry reg;
    auto                 cb = [](const SpectraExportContext*, void*) -> int { return 0; };
    spectra_register_export_format(&reg, "ToRemove", "tr", cb, nullptr);
    ASSERT_EQ(reg.count(), 1u);

    int result = spectra_unregister_export_format(&reg, "ToRemove");
    EXPECT_EQ(result, 0);
    EXPECT_EQ(reg.count(), 0u);
}

TEST(PluginExportCAPI, UnregisterNullRegistryReturnsError)
{
    EXPECT_EQ(spectra_unregister_export_format(nullptr, "X"), -1);
}

TEST(PluginExportCAPI, UnregisterNullNameReturnsError)
{
    ExportFormatRegistry reg;
    EXPECT_EQ(spectra_unregister_export_format(&reg, nullptr), -1);
}

TEST(PluginExportCAPI, CallbackNonZeroReturnMeansFailed)
{
    ExportFormatRegistry reg;
    auto                 cb = [](const SpectraExportContext*, void*) -> int { return 1; };
    spectra_register_export_format(&reg, "Fail", "f", cb, nullptr);
    EXPECT_FALSE(reg.export_figure("Fail", "{}", nullptr, 0, 0, "/tmp/f.f"));
}

// ─── CSV output integration test ─────────────────────────────────────────────
//
// Demonstrates end-to-end: register a minimal CSV writer, export, read back.

static int write_csv_export(const SpectraExportContext* ctx, void* /*user_data*/)
{
    std::ofstream f(ctx->output_path);
    if (!f.is_open())
        return 1;

    // Write a single-line CSV using the figure JSON as the "data" row.
    f << "figure_json\n";
    f << std::string(ctx->figure_json, ctx->figure_json_len) << "\n";
    return f.good() ? 0 : 1;
}

TEST(PluginExportCAPI, WriteAndVerifyCsvOutput)
{
    ExportFormatRegistry reg;
    spectra_register_export_format(&reg, "CSV Test", "csv", write_csv_export, nullptr);

    namespace fs         = std::filesystem;
    auto        tmp_path = (fs::temp_directory_path() / "spectra_export_test.csv").string();
    std::string json     = R"({"axes":[{"title":"My Axes"}]})";

    bool ok = reg.export_figure("CSV Test", json, nullptr, 0, 0, tmp_path);
    ASSERT_TRUE(ok);

    std::ifstream f(tmp_path);
    ASSERT_TRUE(f.is_open());

    std::string header, content;
    std::getline(f, header);
    std::getline(f, content);

    EXPECT_EQ(header, "figure_json");
    EXPECT_EQ(content, json);

    fs::remove(tmp_path);
}
