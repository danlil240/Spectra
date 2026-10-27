// test_plugin_unload_cleanup.cpp — PluginManager owns the lifetime of everything a
// plugin registers.
//
// mock_leaky_plugin registers one extension of every kind (command, undo action,
// transform, overlay, export format, data source, custom series type) and never
// unregisters any of them.  Unloading must remove all of them before the
// library is closed (no registry may keep a callback into unmapped code), and
// disabling must deactivate all of them, not just the commands.

#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "adapters/data_source_registry.hpp"
#include "io/export_registry.hpp"
#include "math/data_transform.hpp"
#include "render/series_type_registry.hpp"
#include "ui/commands/command_registry.hpp"
#include "ui/commands/shortcut_manager.hpp"
#include "ui/commands/undo_manager.hpp"
#include "ui/overlay/overlay_registry.hpp"
#include "ui/workspace/plugin_api.hpp"

#ifdef _WIN32
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

#ifndef SPECTRA_MOCK_LEAKY_PLUGIN_PATH
    #define SPECTRA_MOCK_LEAKY_PLUGIN_PATH ""
#endif

using namespace spectra;

namespace
{

constexpr const char* kPluginName = "MockLeakyPlugin";

bool contains(const std::vector<std::string>& values, const std::string& name)
{
    for (const auto& v : values)
    {
        if (v == name)
            return true;
    }
    return false;
}

bool has_format(const ExportFormatRegistry& reg, const std::string& name)
{
    for (const auto& f : reg.available_formats())
    {
        if (f.name == name)
            return true;
    }
    return false;
}

// Names the step in a failure (and in an SEH crash report on Windows, where
// gtest prints the active traces).
#define LEAKY_STEP(name) SCOPED_TRACE("step: " name)

class PluginUnloadCleanupTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        path_ = SPECTRA_MOCK_LEAKY_PLUGIN_PATH;
        if (path_.empty() || !std::filesystem::exists(path_))
            GTEST_SKIP() << "mock_leaky_plugin not found at: " << path_;

        mgr_.set_command_registry(&commands_);
        mgr_.set_shortcut_manager(&shortcuts_);
        mgr_.set_undo_manager(&undo_);
        mgr_.set_transform_registry(&transforms_);
        mgr_.set_overlay_registry(&overlays_);
        mgr_.set_export_format_registry(&exports_);
        mgr_.set_data_source_registry(&sources_);
        mgr_.set_series_type_registry(&series_types_);
    }

    void TearDown() override { mgr_.unload_all(); }

    const void* owner() const
    {
        const PluginEntry* p = mgr_.find_plugin(kPluginName);
        return p ? p->owner : nullptr;
    }

    // Reads one of the plug-in's exported int counters (valid while loaded).
    int counter(const char* symbol) const
    {
        const PluginEntry* p = mgr_.find_plugin(kPluginName);
        if (!p || !p->handle)
            return -1;
        using Fn = int (*)();
#ifdef _WIN32
        auto fn = reinterpret_cast<Fn>(GetProcAddress(static_cast<HMODULE>(p->handle), symbol));
#else
        auto fn = reinterpret_cast<Fn>(dlsym(p->handle, symbol));
#endif
        return fn ? fn() : -1;
    }

    void draw_overlays() const
    {
        OverlayDrawContext ctx{};
        ctx.draw_list = nullptr;   // draw helpers are no-ops without a draw list
        overlays_.draw_all(ctx);
    }

    bool export_leaky() const
    {
        return exports_.export_figure("LeakyExport", "{}", nullptr, 0, 0, "unused.lk");
    }

    // Asserts that every registration of the plug-in is present and active.
    void expect_all_active()
    {
        ASSERT_NE(owner(), nullptr);
        EXPECT_NE(commands_.find("leaky.ping"), nullptr);
        EXPECT_EQ(commands_.ids_owned_by(owner()).size(), 2u);
        EXPECT_EQ(transforms_.count_owned(owner()), 1u);
        EXPECT_EQ(overlays_.count_owned(owner()), 1u);
        EXPECT_EQ(exports_.count_owned(owner()), 1u);
        EXPECT_EQ(sources_.count_owned(owner()), 1u);
        EXPECT_EQ(series_types_.count_owned(owner()), 1u);
        EXPECT_EQ(undo_.undo_count(), 1u);

        {
            LEAKY_STEP("command");
            const int runs = counter("mock_leaky_command_runs");
            EXPECT_TRUE(commands_.execute("leaky.ping"));
            EXPECT_EQ(counter("mock_leaky_command_runs"), runs + 1);
        }
        {
            LEAKY_STEP("transform");
            DataTransform t;
            EXPECT_TRUE(contains(transforms_.available_transforms(), "LeakyShift"));
            EXPECT_TRUE(transforms_.get_transform("LeakyShift", t));
            EXPECT_FLOAT_EQ(t.apply_scalar(1.0f), 2.0f);
        }
        {
            LEAKY_STEP("overlay");
            const int draws = counter("mock_leaky_overlay_draws");
            draw_overlays();
            EXPECT_EQ(counter("mock_leaky_overlay_draws"), draws + 1);
        }
        {
            LEAKY_STEP("export");
            EXPECT_TRUE(has_format(exports_, "LeakyExport"));
            EXPECT_TRUE(export_leaky());
        }
        LEAKY_STEP("data source / series type");
        EXPECT_TRUE(contains(sources_.source_names(), "LeakySource"));
        EXPECT_NE(sources_.find("LeakySource"), nullptr);

        auto st = series_types_.find_shared("leaky_series");
        ASSERT_NE(st, nullptr);
        EXPECT_TRUE(st->enabled.load());
    }

    std::string          path_;
    CommandRegistry      commands_;
    ShortcutManager      shortcuts_;
    UndoManager          undo_;
    TransformRegistry    transforms_;
    OverlayRegistry      overlays_;
    ExportFormatRegistry exports_;
    DataSourceRegistry   sources_;
    SeriesTypeRegistry   series_types_;
    PluginManager        mgr_;   // Last: destroyed (and unloads) first
};

}   // namespace

TEST_F(PluginUnloadCleanupTest, UnloadRemovesEveryRegistrationKindAndReloads)
{
    // Host registrations must survive the plug-in's unload.
    overlays_.register_overlay("HostOverlay", [](const OverlayDrawContext&) {});
    exports_.register_format("HostFormat", "hf", [](const ExportContext&) { return true; });
    commands_.register_command("host.cmd", "Host", [] {});

    for (int round = 0; round < 2; ++round)
    {
        SCOPED_TRACE(round);
        ASSERT_TRUE(mgr_.load_plugin(path_));
        {
            LEAKY_STEP("load");
            expect_all_active();
        }

        LEAKY_STEP("late registration and unload");
        // A registration made after init (from a plug-in command) is tracked too.
        ASSERT_TRUE(commands_.execute("leaky.register_late"));
        EXPECT_EQ(overlays_.count_owned(owner()), 2u);

        // Unload stops a running data source while the plug-in is still loaded.
        DataSourceAdapter* src = sources_.find("LeakySource");
        ASSERT_NE(src, nullptr);
        src->start();
        EXPECT_EQ(counter("mock_leaky_source_running"), 1);

        const void* token = owner();
        ASSERT_TRUE(mgr_.unload_plugin(kPluginName));
        EXPECT_EQ(mgr_.plugin_count(), 0u);

        EXPECT_EQ(commands_.find("leaky.ping"), nullptr);
        EXPECT_EQ(commands_.find("leaky.register_late"), nullptr);
        EXPECT_TRUE(commands_.ids_owned_by(token).empty());
        EXPECT_EQ(undo_.undo_count(), 0u);
        EXPECT_EQ(transforms_.count_owned(token), 0u);
        EXPECT_FALSE(contains(transforms_.available_transforms(), "LeakyShift"));
        EXPECT_EQ(overlays_.count_owned(token), 0u);
        EXPECT_EQ(overlays_.overlay_names(), std::vector<std::string>{"HostOverlay"});
        EXPECT_EQ(exports_.count_owned(token), 0u);
        ASSERT_EQ(exports_.available_formats().size(), 1u);
        EXPECT_EQ(exports_.available_formats()[0].name, "HostFormat");
        EXPECT_EQ(sources_.count(), 0u);
        EXPECT_EQ(series_types_.count(), 0u);
        EXPECT_NE(commands_.find("host.cmd"), nullptr);

        // Nothing left that could call into the unloaded library.
        draw_overlays();
        EXPECT_FALSE(export_leaky());
    }
}

TEST_F(PluginUnloadCleanupTest, DisableGatesEveryExtensionKind)
{
    ASSERT_TRUE(mgr_.load_plugin(path_));
    expect_all_active();

    DataSourceAdapter* src = sources_.find("LeakySource");
    ASSERT_NE(src, nullptr);
    src->start();
    ASSERT_EQ(counter("mock_leaky_source_running"), 1);

    mgr_.set_plugin_enabled(kPluginName, false);
    ASSERT_FALSE(mgr_.find_plugin(kPluginName)->enabled);

    // Commands.
    const int runs = counter("mock_leaky_command_runs");
    EXPECT_FALSE(commands_.execute("leaky.ping"));
    EXPECT_EQ(counter("mock_leaky_command_runs"), runs);

    // Transforms: neither listed nor callable.
    DataTransform t;
    EXPECT_FALSE(contains(transforms_.available_transforms(), "LeakyShift"));
    EXPECT_FALSE(transforms_.get_transform("LeakyShift", t));

    // Overlays: not drawn.
    const int draws = counter("mock_leaky_overlay_draws");
    draw_overlays();
    EXPECT_EQ(counter("mock_leaky_overlay_draws"), draws);

    // Export formats: neither listed nor callable.
    const int exports = counter("mock_leaky_export_calls");
    EXPECT_FALSE(has_format(exports_, "LeakyExport"));
    EXPECT_FALSE(export_leaky());
    EXPECT_EQ(counter("mock_leaky_export_calls"), exports);

    // Data sources: stopped, hidden, not polled.
    EXPECT_EQ(counter("mock_leaky_source_running"), 0);
    EXPECT_FALSE(contains(sources_.source_names(), "LeakySource"));
    EXPECT_EQ(sources_.find("LeakySource"), nullptr);
    EXPECT_TRUE(sources_.poll_all().empty());

    // Custom series types: entry flagged so the Renderer skips upload/draw.
    auto st = series_types_.find_shared("leaky_series");
    ASSERT_NE(st, nullptr);
    EXPECT_FALSE(st->enabled.load());

    // Everything is still registered (and removed on unload).
    EXPECT_EQ(transforms_.count_owned(owner()), 1u);
    EXPECT_EQ(overlays_.count_owned(owner()), 1u);
    EXPECT_EQ(exports_.count_owned(owner()), 1u);
    EXPECT_EQ(sources_.count_owned(owner()), 1u);

    mgr_.set_plugin_enabled(kPluginName, true);
    expect_all_active();

    // A re-enabled source polls again once restarted.
    src = sources_.find("LeakySource");
    ASSERT_NE(src, nullptr);
    src->start();
    EXPECT_EQ(sources_.poll_all().size(), 1u);
}

TEST_F(PluginUnloadCleanupTest, DisabledPluginStillUnloadsCleanly)
{
    ASSERT_TRUE(mgr_.load_plugin(path_));
    mgr_.set_plugin_enabled(kPluginName, false);
    ASSERT_TRUE(mgr_.unload_plugin(kPluginName));

    EXPECT_TRUE(commands_.all_commands().empty());
    EXPECT_EQ(transforms_.available_transforms(), TransformRegistry{}.available_transforms());
    EXPECT_EQ(overlays_.count(), 0u);
    EXPECT_EQ(exports_.count(), 0u);
    EXPECT_EQ(sources_.count(), 0u);
    EXPECT_EQ(series_types_.count(), 0u);
}

TEST(PluginRegistryOwnership, NullOwnerNeverMatchesHostEntries)
{
    OverlayRegistry overlays;
    overlays.register_overlay("Host", [](const OverlayDrawContext&) {});
    EXPECT_EQ(overlays.unregister_owner(nullptr), 0u);
    overlays.set_owner_enabled(nullptr, false);
    EXPECT_EQ(overlays.count(), 1u);

    TransformRegistry transforms;
    const auto        before = transforms.available_transforms();
    EXPECT_EQ(transforms.unregister_owner(nullptr), 0u);
    transforms.set_owner_enabled(nullptr, false);
    EXPECT_EQ(transforms.available_transforms(), before);

    CommandRegistry commands;
    commands.register_command("a", "A", [] {});
    EXPECT_TRUE(commands.ids_owned_by(nullptr).empty());
}
