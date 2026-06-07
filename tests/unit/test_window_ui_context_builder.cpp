#include <gtest/gtest.h>
#include <memory>
#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>

#include "io/export_registry.hpp"
#include "ui/app/window_ui_context_builder.hpp"
#include "ui/app/window_ui_context_runtime.hpp"
#include "ui/app/window_ui_context.hpp"
#include "ui/commands/shortcut_manager.hpp"
#include "ui/commands/series_clipboard.hpp"
#include "ui/overlay/overlay_registry.hpp"
#include "ui/theme/theme.hpp"
#include "ui/workspace/plugin_api.hpp"

using namespace spectra;

#ifdef SPECTRA_USE_IMGUI

class WindowUIContextBuilderTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        ui::ThemeManager::set_current(&theme_mgr_);
        theme_mgr_.ensure_initialized();

        auto fig1 = std::make_unique<Figure>(FigureConfig{.width = 640, .height = 480});
        fig1->subplot(1, 1, 1);
        primary_id_ = registry_.register_figure(std::move(fig1));

        auto fig2 = std::make_unique<Figure>(FigureConfig{.width = 800, .height = 600});
        fig2->subplot(1, 1, 1);
        secondary_id_ = registry_.register_figure(std::move(fig2));
    }

    void TearDown() override { ui::ThemeManager::set_current(nullptr); }

    FigureRegistry         registry_;
    ui::ThemeManager       theme_mgr_;
    PluginManager          plugin_manager_;
    ExportFormatRegistry   export_registry_;
    OverlayRegistry        overlay_registry_;
    SeriesClipboard        shared_clipboard_;
    Figure*                active_figure_    = nullptr;
    FigureId               active_figure_id_ = INVALID_FIGURE_ID;
    FigureRegistry::IdType primary_id_       = INVALID_FIGURE_ID;
    FigureRegistry::IdType secondary_id_     = INVALID_FIGURE_ID;
};

TEST_F(WindowUIContextBuilderTest, BuildsHeadlessSafeContextWithSharedServices)
{
    WindowUIContextBuildOptions options;
    options.registry               = &registry_;
    options.theme_mgr              = &theme_mgr_;
    options.initial_figure_id      = secondary_id_;
    options.active_figure          = &active_figure_;
    options.active_figure_id       = &active_figure_id_;
    options.plugin_manager         = &plugin_manager_;
    options.export_format_registry = &export_registry_;
    options.overlay_registry       = &overlay_registry_;
    options.series_clipboard       = &shared_clipboard_;

    auto ui_ctx = build_window_ui_context(options);
    ASSERT_NE(ui_ctx, nullptr);
    ASSERT_NE(ui_ctx->fig_mgr, nullptr);

    EXPECT_EQ(ui_ctx->theme_mgr, &theme_mgr_);
    EXPECT_EQ(ui_ctx->plugin_manager, &plugin_manager_);
    EXPECT_EQ(ui_ctx->overlay_registry, &overlay_registry_);

    const auto& figure_ids = ui_ctx->fig_mgr->figure_ids();
    ASSERT_EQ(figure_ids.size(), 1u);
    EXPECT_EQ(figure_ids.front(), secondary_id_);

    EXPECT_EQ(active_figure_id_, secondary_id_);
    EXPECT_EQ(active_figure_, registry_.get(secondary_id_));

    EXPECT_NE(ui_ctx->cmd_registry.find("view.reset"), nullptr);
    EXPECT_EQ(ui_ctx->shortcut_mgr.command_for_shortcut(Shortcut::from_string("Ctrl+K")),
              "app.command_palette");
    EXPECT_EQ(ui_ctx->imgui_ui, nullptr);
}

TEST_F(WindowUIContextBuilderTest, BuildsImGuiOnlyContextWithoutFigureManager)
{
    WindowUIContextBuildOptions options;
    options.theme_mgr = &theme_mgr_;
    options.mode      = WindowUIContextBuildMode::ImGuiOnly;

    auto ui_ctx = build_window_ui_context(options);
    ASSERT_NE(ui_ctx, nullptr);
    EXPECT_EQ(ui_ctx->theme_mgr, &theme_mgr_);
    ASSERT_NE(ui_ctx->imgui_ui, nullptr);
    EXPECT_EQ(ui_ctx->fig_mgr, nullptr);
    EXPECT_EQ(ui_ctx->cmd_registry.count(), 0u);
}

TEST_F(WindowUIContextBuilderTest, BuildsMinimalHeadlessContext)
{
    WindowUIContextBuildOptions options;
    options.registry  = &registry_;
    options.theme_mgr = &theme_mgr_;
    options.mode      = WindowUIContextBuildMode::Headless;

    auto ui_ctx = build_window_ui_context(options);
    ASSERT_NE(ui_ctx, nullptr);
    ASSERT_NE(ui_ctx->fig_mgr, nullptr);
    EXPECT_EQ(ui_ctx->fig_mgr->figure_ids().size(), 2u);
    EXPECT_EQ(ui_ctx->imgui_ui, nullptr);
    EXPECT_EQ(ui_ctx->cmd_registry.count(), 0u);
}

TEST_F(WindowUIContextBuilderTest, CaptureHomeLimitsStoresAxesExtents)
{
    WindowUIContextBuildOptions options;
    options.registry          = &registry_;
    options.theme_mgr         = &theme_mgr_;
    options.initial_figure_id = primary_id_;

    auto ui_ctx = build_window_ui_context(options);
    ASSERT_NE(ui_ctx, nullptr);
    ASSERT_NE(ui_ctx->fig_mgr, nullptr);

    Figure* fig = registry_.get(primary_id_);
    ASSERT_NE(fig, nullptr);
    auto& ax = fig->subplot(1, 1, 1);
    ax.xlim(1.0, 5.0);
    ax.ylim(-2.0, 3.0);

    capture_figure_home_limits(registry_, *ui_ctx->fig_mgr);

    const auto& vm = ui_ctx->fig_mgr->state(primary_id_);
    const auto  it = vm.home_limits().find(&ax);
    ASSERT_NE(it, vm.home_limits().end());
    EXPECT_DOUBLE_EQ(it->second.x.min, 1.0);
    EXPECT_DOUBLE_EQ(it->second.x.max, 5.0);
    EXPECT_DOUBLE_EQ(it->second.y.min, -2.0);
    EXPECT_DOUBLE_EQ(it->second.y.max, 3.0);
}

#endif
