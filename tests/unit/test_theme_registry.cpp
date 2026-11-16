// Theme / palette registry: built-in order, user theme and palette files in the
// config dir, import/export round trips (including the theme's data palette),
// the colour vision deficiency preview and its commands, and Settings search.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "ui/app/commands/command_context.hpp"
#include "ui/app/commands/command_descriptor.hpp"
#include "ui/app/commands/command_groups.hpp"
#include "ui/app/window_ui_context.hpp"
#include "ui/figures/figure_registry.hpp"
#include "ui/settings/settings_search.hpp"
#include "ui/theme/theme.hpp"

namespace ui = spectra::ui;
using spectra::CommandContext;
using spectra::CommandDescriptor;
using spectra::Figure;
using spectra::FigureId;
using spectra::FigureRegistry;
using spectra::INVALID_FIGURE_ID;
using spectra::make_theme_commands;
using spectra::WindowUIContext;
using spectra::ui::CVDType;
using spectra::ui::DataPalette;
using spectra::ui::ScopedExportTheme;
using spectra::ui::Theme;
using spectra::ui::ThemeColors;
using spectra::ui::ThemeManager;
using Color = spectra::ui::Color;

namespace
{

namespace fs = std::filesystem;

class ThemeRegistryTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        tm_.ensure_initialized();
        ThemeManager::set_current(&tm_);
        dir_ = fs::temp_directory_path()
               / ("spectra_theme_registry_"
                  + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "_"
                  + ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(dir_);
        fs::create_directories(dir_);
    }
    void TearDown() override
    {
        ThemeManager::set_current(nullptr);
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    void write(const fs::path& p, const std::string& text)
    {
        fs::create_directories(p.parent_path());
        std::ofstream f(p);
        f << text;
    }

    ThemeManager tm_;
    fs::path     dir_;
};

bool near(const Color& a, const Color& b, float eps = 1e-4f)
{
    return std::abs(a.r - b.r) < eps && std::abs(a.g - b.g) < eps && std::abs(a.b - b.b) < eps
           && std::abs(a.a - b.a) < eps;
}

}   // namespace

// ─── Registry order ─────────────────────────────────────────────────────────

TEST_F(ThemeRegistryTest, BuiltinListsAreTheSingleSourceOfTruth)
{
    const auto& themes = ThemeManager::builtin_theme_ids();
    ASSERT_EQ(themes.size(), 4u);
    EXPECT_EQ(themes.front(), "night");
    for (const auto& id : themes)
    {
        EXPECT_TRUE(ThemeManager::is_builtin_theme(id));
        EXPECT_NE(tm_.find_theme(id), nullptr) << id;
    }
    // available_themes() starts with the built-ins, in that order.
    const auto avail = tm_.available_themes();
    ASSERT_GE(avail.size(), themes.size());
    EXPECT_TRUE(std::equal(themes.begin(), themes.end(), avail.begin()));

    for (const auto& id : ThemeManager::builtin_data_palette_ids())
    {
        EXPECT_TRUE(ThemeManager::is_builtin_data_palette(id));
        const auto names = tm_.available_data_palettes();
        EXPECT_NE(std::find(names.begin(), names.end(), id), names.end()) << id;
    }
    EXPECT_FALSE(ThemeManager::is_builtin_theme("my_theme"));
}

TEST_F(ThemeRegistryTest, OrderedPalettesPutBuiltinsFirstThenUserByName)
{
    DataPalette p;
    p.colors = {Color(1, 0, 0), Color(0, 1, 0)};
    tm_.register_data_palette("zz_user", p);
    tm_.register_data_palette("aa_user", p);

    const auto order    = tm_.ordered_data_palettes();
    const auto builtins = ThemeManager::builtin_data_palette_ids();
    ASSERT_EQ(order.size(), builtins.size() + 2);
    EXPECT_TRUE(std::equal(builtins.begin(), builtins.end(), order.begin()));
    EXPECT_EQ(order[builtins.size()], "aa_user");
    EXPECT_EQ(order[builtins.size() + 1], "zz_user");
    EXPECT_EQ(order.front(), "default");
}

TEST_F(ThemeRegistryTest, PaletteLabels)
{
    EXPECT_EQ(tm_.data_palette_label("default"), "Theme Default");
    EXPECT_EQ(tm_.data_palette_label("colorblind"), "Okabe-Ito (Colorblind)");
    EXPECT_EQ(tm_.data_palette_label("tol_bright"), "Tol Bright");
    EXPECT_EQ(tm_.data_palette_label("my_cool_palette"), "My Cool Palette");
}

// ─── Import / export round trips ────────────────────────────────────────────

TEST_F(ThemeRegistryTest, ImportThemeKeepsDataPalette)
{
    // A user theme with its own palette, exported then re-imported.
    Theme t                        = *tm_.find_theme("dark");
    t.display_name                 = "Ocean";
    t.data_palette.colors          = {Color(0.1f, 0.2f, 0.3f),
                                      Color(0.4f, 0.5f, 0.6f),
                                      Color(0.7f, 0.8f, 0.9f)};
    t.data_palette.colorblind_safe = true;
    tm_.register_theme("ocean", t);

    const fs::path path = dir_ / "ocean.json";
    ASSERT_TRUE(tm_.export_theme_file("ocean", path.string()));

    ThemeManager other;
    other.ensure_initialized();
    ASSERT_TRUE(other.import_theme(path.string()));
    const Theme* back = other.find_theme("ocean");
    ASSERT_NE(back, nullptr);
    EXPECT_EQ(back->display_name, "Ocean");
    ASSERT_EQ(back->data_palette.colors.size(), 3u);
    EXPECT_TRUE(near(back->data_palette.colors[1], Color(0.4f, 0.5f, 0.6f)));
    EXPECT_TRUE(back->data_palette.colorblind_safe);
    EXPECT_TRUE(near(back->colors.bg_canvas, t.colors.bg_canvas));

    // "default" follows the imported theme's palette once it is active.
    other.set_theme("ocean");
    other.set_data_palette("default");
    ASSERT_EQ(other.current_data_palette().colors.size(), 3u);
    EXPECT_TRUE(near(other.current_data_palette().colors[2], Color(0.7f, 0.8f, 0.9f)));
}

TEST_F(ThemeRegistryTest, ExportCurrentThemeRoundTripsPalette)
{
    tm_.set_theme("light");
    const fs::path path = dir_ / "light.json";
    ASSERT_TRUE(tm_.export_theme(path.string()));
    const auto expected = tm_.find_theme("light")->data_palette.colors;

    ThemeManager other;
    other.ensure_initialized();
    ASSERT_TRUE(other.import_theme(path.string()));
    const auto& got = other.find_theme("light")->data_palette.colors;
    ASSERT_EQ(got.size(), expected.size());
    for (size_t i = 0; i < got.size(); ++i)
        EXPECT_TRUE(near(got[i], expected[i])) << i;
}

TEST_F(ThemeRegistryTest, PaletteFileRoundTripAndHexColours)
{
    DataPalette p;
    p.description     = "Two greens";
    p.colors          = {Color(0.0f, 0.5f, 0.0f), Color(0.2f, 0.8f, 0.2f, 0.5f)};
    p.colorblind_safe = true;
    p.sequential      = true;
    tm_.register_data_palette("greens", p);
    const fs::path path = dir_ / "greens.json";
    ASSERT_TRUE(tm_.export_data_palette_file("greens", path.string()));

    ThemeManager other;
    other.ensure_initialized();
    std::string err;
    EXPECT_EQ(other.load_data_palette_file(path.string(), &err), "greens") << err;
    const DataPalette& back = other.get_data_palette("greens");
    ASSERT_EQ(back.colors.size(), 2u);
    EXPECT_TRUE(near(back.colors[1], Color(0.2f, 0.8f, 0.2f, 0.5f)));
    EXPECT_EQ(back.description, "Two greens");
    EXPECT_TRUE(back.colorblind_safe);
    EXPECT_TRUE(back.sequential);

    // Hand-written file with hex colours.
    const fs::path hex = dir_ / "hex.json";
    write(hex, R"({"name": "hexy", "colors": ["#ff0000", "#00FF0080", "#00f"]})");
    EXPECT_EQ(other.load_data_palette_file(hex.string(), &err), "hexy") << err;
    const DataPalette& h = other.get_data_palette("hexy");
    ASSERT_EQ(h.colors.size(), 3u);
    EXPECT_TRUE(near(h.colors[0], Color(1, 0, 0, 1)));
    EXPECT_NEAR(h.colors[1].a, 128.0f / 255.0f, 1e-4f);
    EXPECT_TRUE(near(h.colors[2], Color(0, 0, 1, 1)));
}

TEST_F(ThemeRegistryTest, MalformedFilesAreRejectedWithReasons)
{
    std::string    err;
    const fs::path bad_hex = dir_ / "bad.json";
    write(bad_hex, R"({"name": "bad", "colors": ["#12"]})");
    EXPECT_EQ(tm_.load_data_palette_file(bad_hex.string(), &err), "");
    EXPECT_FALSE(err.empty());

    const fs::path no_colors = dir_ / "none.json";
    write(no_colors, R"({"name": "none"})");
    EXPECT_EQ(tm_.load_data_palette_file(no_colors.string(), &err), "");
    EXPECT_EQ(tm_.load_theme_file(no_colors.string(), &err), "");

    const fs::path builtin = dir_ / "viridis.json";
    write(builtin, R"({"name": "viridis", "colors": [[1,0,0]]})");
    EXPECT_EQ(tm_.load_data_palette_file(builtin.string(), &err), "");
    EXPECT_NE(err.find("built-in"), std::string::npos);

    EXPECT_EQ(tm_.load_theme_file((dir_ / "missing.json").string(), &err), "");
}

// ─── User files in the config dir ───────────────────────────────────────────

TEST_F(ThemeRegistryTest, LoadUserFilesRegistersThemesAndPalettes)
{
    // A theme written by export, renamed, in <config>/themes.
    Theme t = *tm_.find_theme("night");
    t.name  = "midnight";
    tm_.register_theme("midnight", t);
    fs::create_directories(ThemeManager::user_themes_dir(dir_));
    ASSERT_TRUE(tm_.export_theme_file("midnight",
                                      (ThemeManager::user_themes_dir(dir_) / "m.json").string()));
    write(ThemeManager::user_palettes_dir(dir_) / "warm.json",
          R"({"name": "warm", "colors": ["#ff8800", "#cc4400"]})");
    write(ThemeManager::user_palettes_dir(dir_) / "broken.json", "{");
    write(ThemeManager::user_themes_dir(dir_) / "readme.txt", "ignored");

    ThemeManager fresh;
    fresh.ensure_initialized();
    const auto res = fresh.load_user_files(dir_);
    EXPECT_EQ(res.themes, std::vector<std::string>{"midnight"});
    EXPECT_EQ(res.palettes, std::vector<std::string>{"warm"});
    ASSERT_EQ(res.errors.size(), 1u);
    EXPECT_NE(res.errors[0].find("broken.json"), std::string::npos);

    EXPECT_TRUE(fresh.is_user_theme("midnight"));
    EXPECT_FALSE(fresh.is_user_theme("night"));
    EXPECT_TRUE(fresh.is_user_data_palette("warm"));
    // Listed after the built-ins.
    EXPECT_EQ(fresh.available_themes().back(), "midnight");
    EXPECT_EQ(fresh.ordered_data_palettes().back(), "warm");
    // A user theme can be made active.
    fresh.set_theme("midnight");
    EXPECT_EQ(fresh.current_theme_name(), "midnight");

    // A missing config dir is not an error.
    EXPECT_TRUE(fresh.load_user_files(dir_ / "nope").errors.empty());
}

TEST_F(ThemeRegistryTest, ImportUserFileCopiesIntoConfigDirAndRenamesBuiltins)
{
    // Exporting a built-in and importing it creates "<id>_custom".
    tm_.set_theme("dark");
    const fs::path exported = dir_ / "dark_export.json";
    ASSERT_TRUE(tm_.export_theme(exported.string()));
    const fs::path cfg = dir_ / "cfg";

    const auto res = tm_.import_user_file(exported.string(), cfg);
    ASSERT_TRUE(res.ok) << res.error;
    EXPECT_FALSE(res.is_palette);
    EXPECT_EQ(res.id, "dark_custom");
    EXPECT_TRUE(fs::exists(ThemeManager::user_themes_dir(cfg) / "dark_custom.json"));
    EXPECT_TRUE(tm_.is_user_theme("dark_custom"));
    EXPECT_EQ(tm_.display_name("dark_custom"), "Dark (Custom)");
    // The built-in is untouched and still active.
    EXPECT_EQ(tm_.current_theme_name(), "dark");

    // A palette file is detected from its colours array.
    const fs::path pal = dir_ / "pal.json";
    write(pal, R"({"name": "my pal", "colors": [[0.1, 0.2, 0.3], [0.4, 0.5, 0.6, 1.0]]})");
    const auto pres = tm_.import_user_file(pal.string(), cfg);
    ASSERT_TRUE(pres.ok) << pres.error;
    EXPECT_TRUE(pres.is_palette);
    EXPECT_EQ(pres.id, "my_pal");   // sanitized id
    EXPECT_TRUE(fs::exists(ThemeManager::user_palettes_dir(cfg) / "my_pal.json"));

    // Both come back on the next start.
    ThemeManager next;
    next.ensure_initialized();
    const auto loaded = next.load_user_files(cfg);
    EXPECT_EQ(loaded.themes, std::vector<std::string>{"dark_custom"});
    EXPECT_EQ(loaded.palettes, std::vector<std::string>{"my_pal"});
    EXPECT_TRUE(loaded.errors.empty());
}

// ─── Colour vision deficiency preview ───────────────────────────────────────

TEST_F(ThemeRegistryTest, CvdPreviewIsViewOnlyAndSuspendedForExports)
{
    EXPECT_EQ(tm_.cvd_preview(), CVDType::None);
    const Color red(1.0f, 0.0f, 0.0f);
    EXPECT_TRUE(near(tm_.cvd_view_color(red), red));

    tm_.set_cvd_preview(CVDType::Deuteranopia);
    EXPECT_TRUE(near(tm_.cvd_view_color(red), ui::simulate_cvd(red, CVDType::Deuteranopia)));
    EXPECT_FALSE(near(tm_.cvd_view_color(red), red));
    // Theme colours themselves are never changed by the preview.
    EXPECT_TRUE(near(tm_.colors().bg_canvas, tm_.current().colors.bg_canvas));

    {
        ScopedExportTheme guard(&tm_, "");
        EXPECT_EQ(tm_.cvd_preview(), CVDType::None);
    }
    EXPECT_EQ(tm_.cvd_preview(), CVDType::Deuteranopia);
    {
        ScopedExportTheme guard(&tm_, "light");
        EXPECT_TRUE(guard.active());
        EXPECT_EQ(tm_.cvd_preview(), CVDType::None);
    }
    EXPECT_EQ(tm_.cvd_preview(), CVDType::Deuteranopia);
}

TEST_F(ThemeRegistryTest, CvdIdsAndThemeColourSimulation)
{
    for (CVDType t : {CVDType::None,
                      CVDType::Protanopia,
                      CVDType::Deuteranopia,
                      CVDType::Tritanopia,
                      CVDType::Achromatopsia})
    {
        CVDType back = CVDType::None;
        ASSERT_TRUE(ui::cvd_type_from_id(ui::cvd_type_id(t), back));
        EXPECT_EQ(back, t);
    }
    CVDType out = CVDType::Protanopia;
    EXPECT_FALSE(ui::cvd_type_from_id("bogus", out));

    const ThemeColors& base = tm_.find_theme("light")->colors;
    const ThemeColors  same = ui::simulate_cvd(base, CVDType::None);
    EXPECT_TRUE(near(same.accent, base.accent));
    const ThemeColors sim = ui::simulate_cvd(base, CVDType::Achromatopsia);
    // Greyscale: equal channels, alpha and glow kept.
    EXPECT_NEAR(sim.accent.r, sim.accent.g, 1e-5f);
    EXPECT_NEAR(sim.accent.g, sim.accent.b, 1e-5f);
    EXPECT_FLOAT_EQ(sim.accent_muted.a, base.accent_muted.a);
    EXPECT_FLOAT_EQ(sim.glow_intensity, base.glow_intensity);
}

TEST_F(ThemeRegistryTest, CvdCommandsSetThePreview)
{
    WindowUIContext ui;
    ui.theme_mgr = &tm_;
    FigureRegistry registry;
    Figure*        active_fig = nullptr;
    FigureId       active_id  = INVALID_FIGURE_ID;
    CommandContext ctx{ui, registry, &active_fig, &active_id};
    const auto     cmds = make_theme_commands(ctx);

    auto run = [&](const std::string& id)
    {
        auto it = std::find_if(cmds.begin(),
                               cmds.end(),
                               [&](const CommandDescriptor& c) { return c.id == id; });
        ASSERT_NE(it, cmds.end()) << id;
        it->action();
    };
    run("view.cvd.protanopia");
    EXPECT_EQ(tm_.cvd_preview(), CVDType::Protanopia);
    run("view.cvd.tritanopia");
    EXPECT_EQ(tm_.cvd_preview(), CVDType::Tritanopia);
    run("view.cvd.none");
    EXPECT_EQ(tm_.cvd_preview(), CVDType::None);
}

// ─── Settings search ────────────────────────────────────────────────────────

TEST(SettingsSearch, MatchesEveryTermInTitleOrKeywords)
{
    using spectra::ui::settings::settings_query_active;
    using spectra::ui::settings::settings_query_matches;
    EXPECT_TRUE(settings_query_matches("", "Theme", "dark light"));
    EXPECT_TRUE(settings_query_matches("   ", "Theme", ""));
    EXPECT_TRUE(settings_query_matches("theme", "Theme", ""));
    EXPECT_TRUE(settings_query_matches("DARK", "Theme", "dark light"));
    EXPECT_TRUE(settings_query_matches("vision deut", "Color Vision Preview", "deuteranopia"));
    EXPECT_FALSE(settings_query_matches("vision glass", "Color Vision Preview", "deuteranopia"));
    EXPECT_FALSE(settings_query_matches("shortcut", "Theme", "dark"));
    EXPECT_FALSE(settings_query_active(" \t"));
    EXPECT_TRUE(settings_query_active(" a"));
}
