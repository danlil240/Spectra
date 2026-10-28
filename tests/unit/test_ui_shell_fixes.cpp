// UI shell follow-ups (issue #107): File > Exit, menu parity with and without
// the app shell, loading a workspace with no figure open, the data tooltip
// under popups, the light-theme modal scrim, status-bar pills and the
// contrast of the welcome-screen buttons.

#include <gtest/gtest.h>

#ifdef SPECTRA_USE_IMGUI

    #include <cstdlib>
    #include <filesystem>
    #include <memory>
    #include <spectra/axes.hpp>
    #include <spectra/axes3d.hpp>
    #include <spectra/figure.hpp>
    #include <spectra/figure_registry.hpp>
    #include <string>
    #include <vector>

    #include "imgui.h"
    #include "imgui_internal.h"
    #include "render/backend.hpp"
    #include "render/renderer.hpp"
    #include "ui/app/session_runtime.hpp"
    #include "ui/app/window_ui_context.hpp"
    #include "ui/app/window_ui_context_builder.hpp"
    #include "ui/commands/command_registry.hpp"
    #include "ui/commands/shortcut_manager.hpp"
    #include "ui/figures/figure_manager.hpp"
    #include "ui/imgui/imgui_integration.hpp"
    #include "ui/layout/layout_manager.hpp"
    #include "ui/overlay/tooltip.hpp"
    #include "ui/shell/menu_bar.hpp"
    #include "ui/shell/panel.hpp"
    #include "ui/shell/spectra_app_shell.hpp"
    #include "ui/shell/status_bar.hpp"
    #include "ui/theme/design_tokens.hpp"
    #include "ui/theme/theme.hpp"
    #include "ui/workspace/workspace.hpp"

using namespace spectra;

namespace
{

constexpr const char* kThemes[] = {"dark", "night", "light", "high_contrast"};

// Backend that does nothing; enough to construct a Renderer / SessionRuntime.
class NullBackend : public Backend
{
   public:
    bool           init(bool) override { return true; }
    void           shutdown() override {}
    void           wait_idle() override {}
    bool           create_surface(void*) override { return true; }
    bool           create_swapchain(uint32_t, uint32_t) override { return true; }
    bool           recreate_swapchain(uint32_t, uint32_t) override { return true; }
    bool           create_offscreen_framebuffer(uint32_t, uint32_t) override { return true; }
    PipelineHandle create_pipeline(PipelineType) override { return PipelineHandle{1}; }
    BufferHandle   create_buffer(BufferUsage, size_t) override { return BufferHandle{1}; }
    void           destroy_buffer(BufferHandle) override {}
    void           upload_buffer(BufferHandle, const void*, size_t, size_t) override {}
    TextureHandle  create_texture(uint32_t, uint32_t, const uint8_t*) override
    {
        return TextureHandle{1};
    }
    void     destroy_texture(TextureHandle) override {}
    bool     begin_frame(FrameProfiler*) override { return true; }
    void     end_frame(FrameProfiler*) override {}
    void     begin_render_pass(const Color&) override {}
    void     end_render_pass() override {}
    void     bind_pipeline(PipelineHandle) override {}
    void     bind_buffer(BufferHandle, uint32_t) override {}
    void     bind_index_buffer(BufferHandle) override {}
    void     bind_texture(TextureHandle, uint32_t) override {}
    void     push_constants(const SeriesPushConstants&) override {}
    void     set_viewport(float, float, float, float) override {}
    void     set_scissor(int32_t, int32_t, uint32_t, uint32_t) override {}
    void     set_line_width(float) override {}
    void     draw(uint32_t, uint32_t) override {}
    void     draw_instanced(uint32_t, uint32_t, uint32_t, uint32_t) override {}
    void     draw_indexed(uint32_t, uint32_t, int32_t) override {}
    bool     readback_framebuffer(uint8_t*, uint32_t, uint32_t) override { return false; }
    uint32_t swapchain_width() const override { return 0; }
    uint32_t swapchain_height() const override { return 0; }
};

// ImGui context with a built font atlas, for drawing outside a GPU frame.
struct ImGuiScope
{
    ImGuiScope()
    {
        ImGui::CreateContext();
        ImGuiIO& io    = ImGui::GetIO();
        io.DisplaySize = ImVec2(1280.0f, 800.0f);
        io.DeltaTime   = 1.0f / 60.0f;
        io.Fonts->AddFontDefault();
        unsigned char* px = nullptr;
        int            tw = 0, th = 0;
        io.Fonts->GetTexDataAsRGBA32(&px, &tw, &th);
    }
    ~ImGuiScope() { ImGui::DestroyContext(); }
};

class UiShellFixture : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        ui::ThemeManager::set_current(&theme_mgr_);
        theme_mgr_.ensure_initialized();
        renderer_ = std::make_unique<Renderer>(backend_, theme_mgr_);
        session_  = std::make_unique<SessionRuntime>(backend_, *renderer_, registry_);
    }

    void TearDown() override
    {
        ui_ctx_.reset();
        session_.reset();
        renderer_.reset();
        ui::ThemeManager::set_current(nullptr);
    }

    WindowUIContext& build(bool with_imgui)
    {
        WindowUIContextBuildOptions options;
        options.registry                 = &registry_;
        options.theme_mgr                = &theme_mgr_;
        options.initial_figure_id        = initial_id_;
        options.active_figure            = &active_figure_;
        options.active_figure_id         = &active_figure_id_;
        options.session                  = session_.get();
        options.create_imgui_integration = with_imgui;
        ui_ctx_                          = build_window_ui_context(options);
        return *ui_ctx_;
    }

    NullBackend                      backend_;
    ui::ThemeManager                 theme_mgr_;
    FigureRegistry                   registry_;
    std::unique_ptr<Renderer>        renderer_;
    std::unique_ptr<SessionRuntime>  session_;
    std::unique_ptr<WindowUIContext> ui_ctx_;
    Figure*                          active_figure_    = nullptr;
    FigureId                         active_figure_id_ = INVALID_FIGURE_ID;
    FigureId                         initial_id_       = INVALID_FIGURE_ID;
};

}   // namespace

// ─── File > Exit ─────────────────────────────────────────────────────────────

TEST_F(UiShellFixture, AppQuitStopsTheSession)
{
    auto fig = std::make_unique<Figure>(FigureConfig{.width = 640, .height = 480});
    fig->subplot(1, 1, 1);
    initial_id_ = registry_.register_figure(std::move(fig));

    auto& ui = build(false);
    ASSERT_NE(ui.cmd_registry.find("app.quit"), nullptr);
    EXPECT_EQ(ui.shortcut_mgr.command_for_shortcut(Shortcut::from_string("Ctrl+Q")), "app.quit");

    ASSERT_FALSE(session_->should_exit());
    EXPECT_TRUE(ui.cmd_registry.execute("app.quit"));
    EXPECT_TRUE(session_->should_exit());
}

TEST_F(UiShellFixture, AppCancelDoesNotStopTheSession)
{
    auto& ui = build(false);
    EXPECT_TRUE(ui.cmd_registry.execute("app.cancel"));
    EXPECT_FALSE(session_->should_exit());
}

// ─── Menus: app shell and the no-shell fallback share one model ──────────────

namespace
{
const ui::shell::MenuAction* find_item(const ui::shell::MenuBar& bar,
                                       std::string_view          menu,
                                       std::string_view          label)
{
    auto& m = const_cast<ui::shell::MenuBar&>(bar).menu(menu);
    for (const auto& item : m.items())
        if (item.label == label)
            return &item;
    return nullptr;
}
}   // namespace

TEST_F(UiShellFixture, ShellFileExitRunsAppQuit)
{
    auto& ui = build(true);
    ASSERT_NE(ui.app_shell, nullptr);
    ui.app_shell->ensure_initialized();

    const auto* exit_item = find_item(ui.app_shell->menu_bar(), "File", "Exit");
    ASSERT_NE(exit_item, nullptr);
    EXPECT_EQ(exit_item->command_id, "app.quit");
    EXPECT_EQ(ui.app_shell->menu_bar().shortcut_resolver()("app.quit"), "Ctrl+Q");

    ASSERT_TRUE(exit_item->on_click);
    exit_item->on_click();
    EXPECT_TRUE(session_->should_exit());
}

TEST_F(UiShellFixture, FallbackMenusMatchAppShellMenus)
{
    auto& ui = build(true);
    ASSERT_NE(ui.app_shell, nullptr);
    ASSERT_NE(ui.imgui_ui, nullptr);
    ui.app_shell->ensure_initialized();

    ui::shell::MenuBar& shell_bar    = ui.app_shell->menu_bar();
    ui::shell::MenuBar& fallback_bar = ui.imgui_ui->fallback_menu_bar();

    EXPECT_EQ(fallback_bar.menu_names(), shell_bar.menu_names());
    for (const std::string& name : shell_bar.menu_names())
    {
        SCOPED_TRACE(name);
        const auto shell_items = ui::shell::to_imgui_menu_items(shell_bar.menu(name).items(),
                                                                shell_bar.shortcut_resolver());
        const auto fallback_items =
            ui::shell::to_imgui_menu_items(fallback_bar.menu(name).items(),
                                           fallback_bar.shortcut_resolver());
        ASSERT_EQ(fallback_items.size(), shell_items.size());
        for (size_t i = 0; i < shell_items.size(); ++i)
        {
            SCOPED_TRACE(shell_items[i].label);
            EXPECT_EQ(fallback_items[i].label, shell_items[i].label);
            EXPECT_EQ(fallback_items[i].shortcut, shell_items[i].shortcut);
            EXPECT_EQ(fallback_items[i].icon, shell_items[i].icon);
            EXPECT_EQ(static_cast<bool>(fallback_items[i].callback),
                      static_cast<bool>(shell_items[i].callback));
            EXPECT_EQ(static_cast<bool>(fallback_items[i].checked),
                      static_cast<bool>(shell_items[i].checked));
        }
    }

    // The old fallback ran "app.cancel" for Exit.
    const auto* exit_item = find_item(fallback_bar, "File", "Exit");
    ASSERT_NE(exit_item, nullptr);
    EXPECT_EQ(exit_item->command_id, "app.quit");
}

TEST_F(UiShellFixture, CorePanelListMatchesShellRegistry)
{
    auto& ui = build(true);
    ASSERT_NE(ui.app_shell, nullptr);
    ui.app_shell->ensure_initialized();

    const auto& entries = ui::shell::SpectraAppShell::core_panels();
    const auto  panels  = ui.app_shell->panels().all();
    ASSERT_EQ(panels.size(), entries.size());
    for (size_t i = 0; i < entries.size(); ++i)
    {
        EXPECT_EQ(panels[i]->id(), entries[i].id);
        EXPECT_EQ(panels[i]->title(), entries[i].title);
        EXPECT_EQ(panels[i]->icon(), entries[i].icon);
    }
}

TEST_F(UiShellFixture, FallbackPanelItemsTrackPanelState)
{
    auto& ui = build(true);
    ASSERT_NE(ui.imgui_ui, nullptr);
    ui::shell::MenuBar& bar = ui.imgui_ui->fallback_menu_bar();

    const ui::shell::MenuAction* panels = find_item(bar, "View", "Panels");
    ASSERT_NE(panels, nullptr);
    const ui::shell::MenuAction* plugins = nullptr;
    for (const auto& item : panels->submenu)
        if (item.label == "Plugins")
            plugins = &item;
    ASSERT_NE(plugins, nullptr);
    ASSERT_TRUE(plugins->checked);
    EXPECT_EQ(plugins->command_id, "panel.toggle_plugins");
    // Without a shell the checkmark reads ImGuiIntegration's own panel flag.
    EXPECT_FALSE(plugins->checked());
    ui.imgui_ui->set_plugins_panel_visible(true);
    EXPECT_TRUE(plugins->checked());
}

// ─── file.load_workspace with no figure open ─────────────────────────────────

namespace
{
void set_env_var(const char* name, const std::string& value)
{
    #ifdef _WIN32
    _putenv_s(name, value.c_str());
    #else
    setenv(name, value.c_str(), 1);
    #endif
}

void unset_env_var(const char* name)
{
    #ifdef _WIN32
    _putenv_s(name, "");
    #else
    unsetenv(name);
    #endif
}

struct ScopedHome
{
    explicit ScopedHome(const std::filesystem::path& dir)
    {
        if (const char* h = std::getenv("HOME"))
            old_ = h;
        std::filesystem::create_directories(dir);
        set_env_var("HOME", dir.string());
    }
    ~ScopedHome()
    {
        if (old_.empty())
            unset_env_var("HOME");
        else
            set_env_var("HOME", old_);
    }
    std::string old_;
};

WorkspaceData two_figure_workspace()
{
    WorkspaceData data;
    data.theme_name = "";   // keep the current theme

    WorkspaceData::FigureState grid;
    grid.width            = 900;
    grid.height           = 500;
    grid.grid_rows        = 2;
    grid.grid_cols        = 1;
    grid.custom_tab_title = "Grid";
    WorkspaceData::AxisState a0;
    a0.x_min                    = 1.0f;
    a0.x_max                    = 5.0f;
    a0.y_min                    = -2.0f;
    a0.y_max                    = 3.0f;
    WorkspaceData::AxisState a1 = a0;
    a1.x_min                    = 10.0f;
    a1.x_max                    = 20.0f;
    grid.axes                   = {a0, a1};
    data.figures.push_back(grid);

    WorkspaceData::FigureState solid;
    solid.width  = 640;
    solid.height = 480;
    WorkspaceData::AxisState a3;
    a3.is_3d   = true;
    solid.axes = {a3};
    data.figures.push_back(solid);
    return data;
}
}   // namespace

TEST(WorkspaceBuildFigure, RecreatesSizeAndSubplotLayout)
{
    const WorkspaceData data = two_figure_workspace();

    auto grid = Workspace::build_figure(data.figures[0]);
    ASSERT_NE(grid, nullptr);
    EXPECT_EQ(grid->width(), 900u);
    EXPECT_EQ(grid->height(), 500u);
    EXPECT_EQ(grid->grid_rows(), 2);
    EXPECT_EQ(grid->grid_cols(), 1);
    EXPECT_EQ(grid->axes().size(), 2u);

    auto solid = Workspace::build_figure(data.figures[1]);
    ASSERT_NE(solid, nullptr);
    ASSERT_EQ(solid->all_axes().size(), 1u);
    EXPECT_NE(dynamic_cast<Axes3D*>(solid->all_axes()[0].get()), nullptr);

    // A grid too small for its axes grows so every axes gets a slot.
    WorkspaceData::FigureState cramped = data.figures[0];
    cramped.grid_rows                  = 1;
    auto grown                         = Workspace::build_figure(cramped);
    EXPECT_EQ(grown->axes().size(), 2u);
}

TEST_F(UiShellFixture, LoadWorkspaceWithNoFigureCreatesFigures)
{
    const auto home = std::filesystem::temp_directory_path() / "spectra_ui_shell_ws_home";
    ScopedHome scoped(home);
    ImGuiScope imgui;   // theme application touches ImGui style
    const auto data = two_figure_workspace();
    ASSERT_TRUE(Workspace::save(Workspace::default_path(), data));

    auto& ui = build(false);
    ASSERT_NE(ui.fig_mgr, nullptr);
    ASSERT_EQ(ui.fig_mgr->count(), 0u);

    EXPECT_TRUE(ui.cmd_registry.execute("file.load_workspace"));

    ASSERT_EQ(ui.fig_mgr->count(), 2u);
    Figure* first = registry_.get(ui.fig_mgr->figure_ids()[0]);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->width(), 900u);
    ASSERT_EQ(first->axes().size(), 2u);
    EXPECT_FLOAT_EQ(first->axes()[0]->x_limits().min, 1.0f);
    EXPECT_FLOAT_EQ(first->axes()[1]->x_limits().max, 20.0f);
    EXPECT_EQ(ui.fig_mgr->get_title(ui.fig_mgr->figure_ids()[0]), "Grid");

    Figure* second = registry_.get(ui.fig_mgr->figure_ids()[1]);
    ASSERT_NE(second, nullptr);
    ASSERT_EQ(second->all_axes().size(), 1u);
    EXPECT_NE(dynamic_cast<Axes3D*>(second->all_axes()[0].get()), nullptr);

    std::filesystem::remove_all(home);
}

// ─── Data tooltip hides while a popup is open ────────────────────────────────

TEST(UiShellTooltip, HiddenWhileAPopupIsOpen)
{
    ImGuiScope       imgui;
    ui::ThemeManager tm;
    ui::ThemeManager::set_current(&tm);
    tm.ensure_initialized();

    Axes               ax;
    std::vector<float> x = {0.0f, 1.0f};
    auto&              s = ax.line(x, x);
    s.label("signal");

    NearestPointResult nearest;
    nearest.found       = true;
    nearest.series      = &s;
    nearest.data_x      = 0.5;
    nearest.data_y      = 0.5;
    nearest.screen_x    = 400.0f;
    nearest.screen_y    = 300.0f;
    nearest.distance_px = 1.0f;

    auto draw_frame = [&](Tooltip& tip, bool popup_open) -> int
    {
        ImGui::NewFrame();
        if (popup_open)
        {
            ImGui::OpenPopup("##ctx");
            ImGui::BeginPopup("##ctx");
            ImGui::TextUnformatted("menu");
        }
        ImDrawList* dl     = ImGui::GetBackgroundDrawList();
        const int   before = dl->VtxBuffer.Size;
        tip.draw(nearest, 1280.0f, 800.0f, dl);
        const int added = dl->VtxBuffer.Size - before;
        if (popup_open)
            ImGui::EndPopup();
        ImGui::Render();
        return added;
    };

    Tooltip shown;
    shown.set_theme_manager(&tm);
    EXPECT_GT(draw_frame(shown, false), 0);

    Tooltip hidden;
    hidden.set_theme_manager(&tm);
    EXPECT_EQ(draw_frame(hidden, true), 0);
    // A tooltip that was showing hides as soon as the popup opens.
    EXPECT_EQ(draw_frame(shown, true), 0);

    ui::ThemeManager::set_current(nullptr);
}

// ─── Modal scrim ─────────────────────────────────────────────────────────────

TEST(UiShellTheme, LightModalScrimIsStrong)
{
    ImGuiScope       imgui;
    ui::ThemeManager tm;
    ui::ThemeManager::set_current(&tm);
    tm.ensure_initialized();
    for (const char* name : kThemes)
    {
        SCOPED_TRACE(name);
        tm.set_theme(name);
        tm.apply_to_imgui();
        const auto&     c   = tm.colors();
        const ui::Color dim = ui::modal_dim_color(c);
        const ImVec4    got = ImGui::GetStyle().Colors[ImGuiCol_ModalWindowDimBg];
        EXPECT_FLOAT_EQ(got.w, dim.a);
        if (ui::is_light_chrome(c))
        {
            EXPECT_GE(dim.a, 0.6f);
            // The scrim pulls the light chrome far down (~0.16 with the old 50% black).
            const ui::Color dimmed = dim.composite_over(c.bg_primary);
            EXPECT_LT(dimmed.luminance(), 0.11f);
            // and the dialog surface stands well apart from it.
            EXPECT_GE(c.bg_elevated.contrast_ratio(dimmed), 5.5f);
        }
        else
        {
            EXPECT_FLOAT_EQ(dim.a, 0.5f);
        }
    }
    ui::ThemeManager::set_current(nullptr);
}

// ─── Status bar ──────────────────────────────────────────────────────────────

TEST(UiShellStatusBar, PerfColorsReadOnEveryTheme)
{
    ui::ThemeManager tm;
    tm.ensure_initialized();
    for (const char* name : kThemes)
    {
        tm.set_theme(name);
        const auto& c = tm.colors();
        for (bool live : {false, true})
        {
            for (float glow : {0.0f, 0.6f})
            {
                SCOPED_TRACE(std::string(name) + (live ? " live" : " idle") + " glow "
                             + std::to_string(glow));
                const auto p = ui::status_perf_colors(c, live, glow);
                EXPECT_GE(p.fps_text.contrast_ratio(p.fps_fill), 4.5f);
                EXPECT_GE(p.gpu_text.contrast_ratio(c.bg_primary), 4.5f);
                if (ui::is_light_chrome(c))
                {
                    // No dark-green badge on light chrome.
                    EXPECT_GT(p.fps_fill.luminance(), 0.5f);
                }
            }
        }
    }
}

TEST(UiShellStatusBar, ColumnsSpanTheBarAndAreNotClipped)
{
    ImGuiScope       imgui;
    ui::ThemeManager tm;
    ui::ThemeManager::set_current(&tm);
    tm.ensure_initialized();
    tm.apply_to_imgui();   // theme CellPadding has y > 0

    LayoutManager lm;
    lm.update(1280.0f, 800.0f);
    const Rect bar = lm.status_bar_rect();
    ASSERT_GT(bar.h, 0.0f);

    ImRect child_rect;
    ImRect child_clip;
    float  child_h = 0.0f;

    ui::shell::StatusBar status;
    status.set_layout_manager(&lm);
    status.add_segment({.align   = ui::shell::StatusAlign::Left,
                        .draw_fn = [&]()
                        {
                            ImGuiWindow* w = ImGui::GetCurrentWindow();
                            child_rect     = w->Rect();
                            child_clip     = w->ClipRect;
                            child_h        = ImGui::GetWindowHeight();
                            ImGui::TextUnformatted("X: 1  Y: 2");
                        }});

    ImGui::NewFrame();
    status.draw();
    ImGui::Render();

    EXPECT_FLOAT_EQ(child_rect.Min.y, bar.y);
    EXPECT_FLOAT_EQ(child_rect.Max.y, bar.y + bar.h);
    EXPECT_FLOAT_EQ(child_h, bar.h);
    // A pill (pill height + vertical pad) centred in the column fits the clip rect.
    const float pill_h =
        ui::tokens::STATUS_BAR_PILL_HEIGHT + 2.0f * ui::tokens::STATUS_BAR_PILL_PAD_V;
    const float top = child_rect.Min.y + (child_h - pill_h) * 0.5f;
    EXPECT_GE(top, child_clip.Min.y);
    EXPECT_LE(top + pill_h, child_clip.Max.y);

    ui::ThemeManager::set_current(nullptr);
}

// ─── Welcome / empty-state buttons ───────────────────────────────────────────

TEST(UiShellButtons, ActionButtonsReachContrastTargets)
{
    ui::ThemeManager tm;
    tm.ensure_initialized();
    for (const char* name : kThemes)
    {
        tm.set_theme(name);
        const auto& c = tm.colors();
        for (bool primary : {true, false})
        {
            SCOPED_TRACE(std::string(name) + (primary ? " primary" : " secondary"));
            const auto b = ui::action_button_colors(c, primary, c.bg_primary);
            EXPECT_GE(b.text.contrast_ratio(b.fill), 4.5f);
            EXPECT_GE(b.text.contrast_ratio(b.hover), 4.5f);
            EXPECT_GE(b.text.contrast_ratio(b.active), 4.5f);
            if (primary)
            {
                EXPECT_GE(b.fill.contrast_ratio(c.bg_primary), 3.0f);
            }
            else
            {
                ASSERT_GT(b.border.a, 0.0f);
                EXPECT_GE(b.border.contrast_ratio(c.bg_primary), 4.5f);
            }
        }
    }
}

TEST(UiShellButtons, EnsureContrastLeavesGoodPairsAlone)
{
    const ui::Color ink(0.1f, 0.1f, 0.1f, 1.0f);
    const ui::Color paper(0.95f, 0.95f, 0.95f, 1.0f);
    const ui::Color same = ui::ensure_contrast(ink, paper, 4.5f);
    EXPECT_FLOAT_EQ(same.r, ink.r);

    const ui::Color grey(0.6f, 0.6f, 0.6f, 1.0f);
    EXPECT_LT(grey.contrast_ratio(paper), 4.5f);
    EXPECT_GE(ui::ensure_contrast(grey, paper, 4.5f).contrast_ratio(paper), 4.5f);
    const ui::Color night(0.05f, 0.05f, 0.08f, 1.0f);
    EXPECT_GE(ui::ensure_contrast(grey.with_alpha(1.0f), night, 12.0f).contrast_ratio(night),
              12.0f);
}

#endif   // SPECTRA_USE_IMGUI
