// test_plugin_app_shutdown.cpp — Ordinary App teardown with plugins loaded.
//
// QA S3-P03: AppRuntime destroyed the host registries, the headless UI context
// and the windows (owners of the CommandRegistry / UndoManager the plugin
// manager points to) before ~PluginManager unloaded the plugins, so a normal
// App shutdown with a plugin still loaded walked freed registries (windowed:
// SIGSEGV in CommandRegistry::ids_owned_by; headless: std::bad_alloc).
//
// Every test leaves the shipped example plugins (and the mock plugin that
// registers commands and undo actions) loaded and never unloads them
// explicitly: the App's own teardown must do it while its hosts are alive.

#include <gtest/gtest.h>

#include <spectra/app.hpp>
#include <spectra/axes.hpp>
#include <spectra/custom_series.hpp>
#include <spectra/figure.hpp>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "math/data_transform.hpp"
#include "render/vulkan/window_context.hpp"
#include "ui/app/window_ui_context.hpp"
#include "ui/window/window_manager.hpp"
#include "ui/workspace/plugin_api.hpp"

namespace fs = std::filesystem;
using namespace spectra;

#if defined(SPECTRA_HEATMAP_PLUGIN_PATH) && defined(SPECTRA_TRANSFORM_SMOOTH_PLUGIN_PATH)        \
    && defined(SPECTRA_OVERLAY_CROSSHAIR_PLUGIN_PATH) && defined(SPECTRA_EXPORT_CSV_PLUGIN_PATH) \
    && defined(SPECTRA_MOCK_LEAKY_PLUGIN_PATH)
    #define SPECTRA_HAVE_SHUTDOWN_PLUGINS 1
#endif

#ifdef SPECTRA_HAVE_SHUTDOWN_PLUGINS
namespace
{

constexpr const char* kSmoothTransform = "Moving Average (5-pt)";
constexpr const char* kLeakyTransform  = "LeakyShift";

// Every plugin the tests leave loaded.
const std::vector<std::string>& plugin_paths()
{
    static const std::vector<std::string> paths = {SPECTRA_HEATMAP_PLUGIN_PATH,
                                                   SPECTRA_TRANSFORM_SMOOTH_PLUGIN_PATH,
                                                   SPECTRA_OVERLAY_CROSSHAIR_PLUGIN_PATH,
                                                   SPECTRA_EXPORT_CSV_PLUGIN_PATH,
                                                   SPECTRA_MOCK_LEAKY_PLUGIN_PATH};
    return paths;
}

bool transform_registered(const char* name)
{
    DataTransform t;
    return TransformRegistry::instance().get_transform(name, t);
}

void add_heatmap(Axes& ax)
{
    constexpr uint32_t cols = 4, rows = 3;
    std::vector<float> blob(4 + cols * rows);
    std::memcpy(&blob[0], &cols, sizeof(cols));
    std::memcpy(&blob[1], &rows, sizeof(rows));
    blob[2] = 0.0f;
    blob[3] = static_cast<float>(cols * rows - 1);
    for (uint32_t i = 0; i < cols * rows; ++i)
        blob[4 + i] = static_cast<float>(i);
    auto hm = std::make_unique<CustomSeries>("heatmap");
    hm->set_data(blob.data(), blob.size() * sizeof(float), blob.size());
    ax.series_mut().push_back(std::move(hm));
    ax.xlim(0.0, cols);
    ax.ylim(0.0, rows);
}

// Loads every plugin into the App's own manager; checks they registered into
// the App's host services (commands and undo of the host UI context).
void load_plugins(WindowUIContext& ui)
{
    ASSERT_NE(ui.plugin_manager, nullptr);
    for (const auto& path : plugin_paths())
        ASSERT_TRUE(ui.plugin_manager->load_plugin(path)) << path;
    ASSERT_EQ(ui.plugin_manager->plugin_count(), plugin_paths().size());
    EXPECT_NE(ui.cmd_registry.find("leaky.ping"), nullptr);
    EXPECT_GT(ui.undo_mgr.undo_count(), 0u);
    EXPECT_TRUE(transform_registered(kSmoothTransform));
    EXPECT_TRUE(transform_registered(kLeakyTransform));
}

// The transform registry is process-wide, so it outlives the App: the plugins'
// entries being gone afterwards shows teardown unloaded them.
void expect_plugins_released()
{
    EXPECT_FALSE(transform_registered(kSmoothTransform));
    EXPECT_FALSE(transform_registered(kLeakyTransform));
}

// Opens a windowed App's runtime; false (with the test skipped) when there is
// no Vulkan device or display.
bool init_windowed(App& app)
{
    if (!app.ok())
        return false;
    app.init_runtime();
    return app.window_manager() != nullptr && app.ui_context() != nullptr;
}

}   // namespace
#endif

TEST(PluginAppShutdown, HeadlessAppDestroyedWithPluginsLoaded)
{
#ifndef SPECTRA_HAVE_SHUTDOWN_PLUGINS
    GTEST_SKIP() << "example plugins not built";
#else
    const fs::path dir = fs::temp_directory_path() / "spectra_test_plugin_app_shutdown";
    fs::create_directories(dir);
    const fs::path png = dir / "headless_heatmap.png";
    fs::remove(png);
    {
        App app({.headless = true, .socket_path = ""});
        if (!app.ok())
            GTEST_SKIP() << "No Vulkan device";
        auto& fig = app.figure({.width = 320, .height = 240});
        add_heatmap(fig.subplot(1, 1, 1));
        fig.save_png(png.string());

        app.init_runtime();
        auto* ui = app.ui_context();
        ASSERT_NE(ui, nullptr);
        load_plugins(*ui);
        app.step();
        // No unload and no shutdown_runtime(): ~App performs the export with
        // the heatmap plugin still loaded, then tears everything down.
    }
    EXPECT_TRUE(fs::exists(png));
    expect_plugins_released();
    fs::remove_all(dir);
#endif
}

TEST(PluginAppShutdown, HeadlessShutdownRuntimeWithPluginsLoaded)
{
#ifndef SPECTRA_HAVE_SHUTDOWN_PLUGINS
    GTEST_SKIP() << "example plugins not built";
#else
    App app({.headless = true, .socket_path = ""});
    if (!app.ok())
        GTEST_SKIP() << "No Vulkan device";
    add_heatmap(app.figure({.width = 320, .height = 240}).subplot(1, 1, 1));
    app.init_runtime();
    auto* ui = app.ui_context();
    ASSERT_NE(ui, nullptr);
    load_plugins(*ui);
    app.step();
    app.shutdown_runtime();
    expect_plugins_released();
#endif
}

TEST(PluginAppShutdown, WindowedAppDestroyedWithPluginsLoaded)
{
#ifndef SPECTRA_HAVE_SHUTDOWN_PLUGINS
    GTEST_SKIP() << "example plugins not built";
#else
    {
        App app({.headless = false, .socket_path = ""});
        add_heatmap(app.figure({.width = 320, .height = 240}).subplot(1, 1, 1));
        if (!init_windowed(app))
            GTEST_SKIP() << "No Vulkan device or display";
        load_plugins(*app.ui_context());
        app.step();
        // Windows are destroyed by ordinary shutdown with plugins loaded.
    }
    expect_plugins_released();
#endif
}

TEST(PluginAppShutdown, ClosingLastWindowUnloadsPluginsWhileHostAlive)
{
#ifndef SPECTRA_HAVE_SHUTDOWN_PLUGINS
    GTEST_SKIP() << "example plugins not built";
#else
    {
        App app({.headless = false, .socket_path = ""});
        add_heatmap(app.figure({.width = 320, .height = 240}).subplot(1, 1, 1));
        if (!init_windowed(app))
            GTEST_SKIP() << "No Vulkan device or display";
        auto* ui = app.ui_context();
        load_plugins(*ui);
        PluginManager* pm = ui->plugin_manager;
        app.step();

        // The user closes the only window: its UI context (the plugins' command
        // registry and undo stack) is destroyed before shutdown_runtime runs.
        auto* wm = app.window_manager();
        ASSERT_FALSE(wm->windows().empty());
        wm->request_close(wm->windows().front()->id);
        wm->process_pending_closes();
        EXPECT_EQ(pm->plugin_count(), 0u);
        expect_plugins_released();
    }
    expect_plugins_released();
#endif
}

TEST(PluginAppShutdown, ClosingHostWindowMovesPluginsToSurvivingWindow)
{
#ifndef SPECTRA_HAVE_SHUTDOWN_PLUGINS
    GTEST_SKIP() << "example plugins not built";
#else
    {
        App app({.headless = false, .socket_path = ""});
        add_heatmap(app.figure({.width = 320, .height = 240}).subplot(1, 1, 1));
        add_heatmap(app.figure({.width = 320, .height = 240}).subplot(1, 1, 1));
        if (!init_windowed(app))
            GTEST_SKIP() << "No Vulkan device or display";
        auto* wm = app.window_manager();
        if (wm->windows().size() < 2)
            GTEST_SKIP() << "second window could not be created";

        auto* host = app.ui_context();
        load_plugins(*host);
        PluginManager* pm = host->plugin_manager;
        app.step();

        WindowContext* host_wctx = nullptr;
        WindowContext* other     = nullptr;
        for (auto* wctx : wm->windows())
        {
            if (wctx->ui_ctx.get() == host)
                host_wctx = wctx;
            else if (wctx->ui_ctx && !wctx->is_preview && !other)
                other = wctx;
        }
        ASSERT_NE(host_wctx, nullptr);
        ASSERT_NE(other, nullptr);
        auto* survivor = other->ui_ctx.get();
        EXPECT_EQ(survivor->cmd_registry.find("leaky.ping"), nullptr);

        wm->request_close(host_wctx->id);
        wm->process_pending_closes();

        // The plugins stay available, registered into the surviving window.
        EXPECT_EQ(pm->plugin_count(), plugin_paths().size());
        EXPECT_NE(survivor->cmd_registry.find("leaky.ping"), nullptr);
        EXPECT_GT(survivor->undo_mgr.undo_count(), 0u);
        EXPECT_TRUE(transform_registered(kSmoothTransform));
        app.step();
        // Ordinary shutdown with the re-hosted plugins still loaded.
    }
    expect_plugins_released();
#endif
}
