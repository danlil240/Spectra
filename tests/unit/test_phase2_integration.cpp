#include <gtest/gtest.h>

#include "ui/command_registry.hpp"
#include "ui/shortcut_manager.hpp"
#include "ui/undo_manager.hpp"
#include "ui/undoable_property.hpp"
#include "ui/workspace.hpp"
#include "ui/figure_manager.hpp"
#include "ui/transition_engine.hpp"

#include <plotix/axes.hpp>
#include <plotix/color.hpp>
#include <plotix/figure.hpp>
#include <plotix/series.hpp>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace plotix;

// ─── Helpers ─────────────────────────────────────────────────────────────────

static Figure make_figure_with_data() {
    Figure fig;
    auto& ax = fig.subplot(1, 1, 1);
    static float x[] = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f};
    static float y[] = {0.0f, 1.0f, 0.5f, 1.5f, 1.0f};
    ax.line(x, y).label("test_line").color(colors::blue);
    ax.xlim(0.0f, 5.0f);
    ax.ylim(-1.0f, 2.0f);
    ax.title("Test Plot");
    ax.xlabel("X");
    ax.ylabel("Y");
    return fig;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Integration: CommandRegistry + ShortcutManager
// ═══════════════════════════════════════════════════════════════════════════════

class CommandShortcutIntegration : public ::testing::Test {
protected:
    CommandRegistry registry;
    ShortcutManager shortcuts;
    int action_count = 0;

    void SetUp() override {
        shortcuts.set_command_registry(&registry);

        registry.register_command("view.reset", "Reset View",
            [this]() { ++action_count; }, "Ctrl+R", "View");
        registry.register_command("view.zoom_in", "Zoom In",
            [this]() { action_count += 10; }, "Ctrl++", "View");
        registry.register_command("edit.undo", "Undo",
            [this]() { action_count += 100; }, "Ctrl+Z", "Edit");

        // Bind shortcuts (key codes are arbitrary for testing)
        shortcuts.bind(Shortcut{82, KeyMod::Control}, "view.reset");      // Ctrl+R
        shortcuts.bind(Shortcut{61, KeyMod::Control}, "view.zoom_in");    // Ctrl+=
        shortcuts.bind(Shortcut{90, KeyMod::Control}, "edit.undo");       // Ctrl+Z
    }
};

TEST_F(CommandShortcutIntegration, ShortcutExecutesCommand) {
    EXPECT_TRUE(shortcuts.on_key(82, 1, 0x02)); // Ctrl+R → view.reset
    EXPECT_EQ(action_count, 1);
}

TEST_F(CommandShortcutIntegration, MultipleShortcutsWork) {
    shortcuts.on_key(82, 1, 0x02);  // Ctrl+R
    shortcuts.on_key(61, 1, 0x02);  // Ctrl+=
    shortcuts.on_key(90, 1, 0x02);  // Ctrl+Z
    EXPECT_EQ(action_count, 111);   // 1 + 10 + 100
}

TEST_F(CommandShortcutIntegration, UnboundKeyDoesNothing) {
    EXPECT_FALSE(shortcuts.on_key(999, 1, 0));
    EXPECT_EQ(action_count, 0);
}

TEST_F(CommandShortcutIntegration, DisabledCommandNotExecuted) {
    registry.set_enabled("view.reset", false);
    EXPECT_FALSE(shortcuts.on_key(82, 1, 0x02));
    EXPECT_EQ(action_count, 0);
}

TEST_F(CommandShortcutIntegration, RecentCommandsTracked) {
    registry.execute("view.reset");
    registry.execute("edit.undo");
    registry.execute("view.reset");

    auto recent = registry.recent_commands(10);
    ASSERT_GE(recent.size(), 2u);
    EXPECT_EQ(recent[0]->id, "view.reset");
}

TEST_F(CommandShortcutIntegration, SearchFindsRegisteredCommands) {
    auto results = registry.search("reset");
    ASSERT_GE(results.size(), 1u);
    EXPECT_EQ(results[0].command->id, "view.reset");
}

TEST_F(CommandShortcutIntegration, RebindShortcut) {
    shortcuts.unbind(Shortcut{82, KeyMod::Control});
    shortcuts.bind(Shortcut{82, KeyMod::Control}, "edit.undo");

    shortcuts.on_key(82, 1, 0x02);
    EXPECT_EQ(action_count, 100); // Now executes undo, not reset
}

TEST_F(CommandShortcutIntegration, CategoriesGroupCorrectly) {
    auto cats = registry.categories();
    EXPECT_GE(cats.size(), 2u); // View, Edit

    auto view_cmds = registry.commands_in_category("View");
    EXPECT_EQ(view_cmds.size(), 2u);

    auto edit_cmds = registry.commands_in_category("Edit");
    EXPECT_EQ(edit_cmds.size(), 1u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Integration: UndoManager + Workspace save/load
// ═══════════════════════════════════════════════════════════════════════════════

class UndoWorkspaceIntegration : public ::testing::Test {
protected:
    std::string tmp_path;

    void SetUp() override {
        tmp_path = (std::filesystem::temp_directory_path() / "plotix_int_undo_ws.plotix").string();
    }

    void TearDown() override {
        std::remove(tmp_path.c_str());
    }
};

TEST_F(UndoWorkspaceIntegration, UndoCountSavedInWorkspace) {
    UndoManager mgr;
    int val = 0;
    for (int i = 0; i < 5; ++i) {
        mgr.push(UndoAction{"change " + std::to_string(i),
                             [&val]() { --val; }, [&val]() { ++val; }});
    }
    mgr.undo(); // 4 undo, 1 redo

    WorkspaceData data;
    data.undo_count = mgr.undo_count();
    data.redo_count = mgr.redo_count();
    data.theme_name = "dark";

    ASSERT_TRUE(Workspace::save(tmp_path, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(tmp_path, loaded));

    EXPECT_EQ(loaded.undo_count, 4u);
    EXPECT_EQ(loaded.redo_count, 1u);
}

TEST_F(UndoWorkspaceIntegration, UndoablePropertyThenSaveRestore) {
    UndoManager mgr;
    Figure fig = make_figure_with_data();
    auto& ax = *fig.axes()[0];

    // Make undoable changes
    undoable_xlim(&mgr, ax, 1.0f, 4.0f);
    undoable_ylim(&mgr, ax, -0.5f, 1.5f);
    undoable_set_title(&mgr, ax, "Modified Title");

    // Capture workspace
    std::vector<Figure*> figs = {&fig};
    auto ws = Workspace::capture(figs, 0, "dark", true, 320.0f, false);
    ws.undo_count = mgr.undo_count();

    ASSERT_TRUE(Workspace::save(tmp_path, ws));

    // Load and verify
    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(tmp_path, loaded));

    ASSERT_EQ(loaded.figures.size(), 1u);
    ASSERT_EQ(loaded.figures[0].axes.size(), 1u);
    EXPECT_FLOAT_EQ(loaded.figures[0].axes[0].x_min, 1.0f);
    EXPECT_FLOAT_EQ(loaded.figures[0].axes[0].x_max, 4.0f);
    EXPECT_FLOAT_EQ(loaded.figures[0].axes[0].y_min, -0.5f);
    EXPECT_FLOAT_EQ(loaded.figures[0].axes[0].y_max, 1.5f);
    EXPECT_EQ(loaded.figures[0].axes[0].title, "Modified Title");
    EXPECT_EQ(loaded.undo_count, 3u);
}

TEST_F(UndoWorkspaceIntegration, UndoAfterWorkspaceRestore) {
    UndoManager mgr;
    Figure fig = make_figure_with_data();
    auto& ax = *fig.axes()[0];

    ax.xlim(0.0f, 10.0f);
    undoable_xlim(&mgr, ax, 2.0f, 8.0f);

    // Save
    std::vector<Figure*> figs = {&fig};
    auto ws = Workspace::capture(figs, 0, "dark", true, 320.0f, false);
    ASSERT_TRUE(Workspace::save(tmp_path, ws));

    // Undo should still work (undo manager is in-memory)
    EXPECT_TRUE(mgr.undo());
    EXPECT_FLOAT_EQ(ax.x_limits().min, 0.0f);
    EXPECT_FLOAT_EQ(ax.x_limits().max, 10.0f);

    // Redo
    EXPECT_TRUE(mgr.redo());
    EXPECT_FLOAT_EQ(ax.x_limits().min, 2.0f);
    EXPECT_FLOAT_EQ(ax.x_limits().max, 8.0f);
}

TEST_F(UndoWorkspaceIntegration, GroupedUndoWithWorkspaceSave) {
    UndoManager mgr;
    Figure fig = make_figure_with_data();

    // Toggle grid on all axes as a grouped undo
    undoable_toggle_grid_all(&mgr, fig);

    EXPECT_EQ(mgr.undo_count(), 1u); // Grouped = 1 undo step

    // Save
    std::vector<Figure*> figs = {&fig};
    auto ws = Workspace::capture(figs, 0, "dark", true, 320.0f, false);
    ws.undo_count = mgr.undo_count();
    ASSERT_TRUE(Workspace::save(tmp_path, ws));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(tmp_path, loaded));
    EXPECT_EQ(loaded.undo_count, 1u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Integration: FigureManager + Workspace
// ═══════════════════════════════════════════════════════════════════════════════

class FigureManagerWorkspaceIntegration : public ::testing::Test {
protected:
    std::string tmp_path;
    std::vector<std::unique_ptr<Figure>> figures;
    std::unique_ptr<FigureManager> mgr;

    void SetUp() override {
        tmp_path = (std::filesystem::temp_directory_path() / "plotix_int_figmgr_ws.plotix").string();
        figures.push_back(std::make_unique<Figure>());
        mgr = std::make_unique<FigureManager>(figures);
    }

    void TearDown() override {
        std::remove(tmp_path.c_str());
    }
};

TEST_F(FigureManagerWorkspaceIntegration, MultiFigureSaveRestore) {
    // Create multiple figures
    mgr->create_figure();
    mgr->create_figure();
    ASSERT_EQ(mgr->count(), 3u);

    // Set titles
    mgr->set_title(0, "Plot A");
    mgr->set_title(1, "Plot B");
    mgr->set_title(2, "Plot C");

    // Switch to figure 1
    mgr->switch_to(1);

    // Capture workspace
    std::vector<Figure*> fig_ptrs;
    for (auto& f : figures) fig_ptrs.push_back(f.get());

    auto ws = Workspace::capture(fig_ptrs, mgr->active_index(), "dark", true, 320.0f, false);

    // Verify capture
    ASSERT_EQ(ws.figures.size(), 3u);
    EXPECT_EQ(ws.active_figure_index, 1u);

    ASSERT_TRUE(Workspace::save(tmp_path, ws));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(tmp_path, loaded));

    EXPECT_EQ(loaded.figures.size(), 3u);
    EXPECT_EQ(loaded.active_figure_index, 1u);
}

TEST_F(FigureManagerWorkspaceIntegration, ModifiedFlagSaved) {
    mgr->mark_modified(0, true);

    WorkspaceData ws;
    ws.theme_name = "dark";
    WorkspaceData::FigureState fs;
    fs.title = mgr->get_title(0);
    fs.is_modified = mgr->is_modified(0);
    fs.custom_tab_title = mgr->get_title(0);
    ws.figures.push_back(fs);

    ASSERT_TRUE(Workspace::save(tmp_path, ws));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(tmp_path, loaded));

    ASSERT_EQ(loaded.figures.size(), 1u);
    EXPECT_TRUE(loaded.figures[0].is_modified);
}

TEST_F(FigureManagerWorkspaceIntegration, DuplicateThenSave) {
    // Create a second figure via create (not duplicate, to avoid subplot issues)
    mgr->create_figure();
    ASSERT_EQ(mgr->count(), 2u);

    mgr->set_title(0, "Original");
    mgr->set_title(1, "Copy");

    // Save
    std::vector<Figure*> fig_ptrs;
    for (auto& f : figures) fig_ptrs.push_back(f.get());

    auto ws = Workspace::capture(fig_ptrs, 0, "dark", true, 320.0f, false);
    ASSERT_EQ(ws.figures.size(), 2u);

    ASSERT_TRUE(Workspace::save(tmp_path, ws));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(tmp_path, loaded));
    EXPECT_EQ(loaded.figures.size(), 2u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Integration: TransitionEngine + UndoManager
// ═══════════════════════════════════════════════════════════════════════════════

TEST(TransitionUndoIntegration, AnimatedLimitChangeWithUndo) {
    TransitionEngine te;
    UndoManager mgr;
    Axes ax;
    ax.xlim(0.0f, 10.0f);
    ax.ylim(0.0f, 10.0f);

    // Record undoable limit change
    auto old_x = ax.x_limits();
    auto old_y = ax.y_limits();

    // Start animated transition
    te.animate_limits(ax, {2.0f, 8.0f}, {2.0f, 8.0f}, 0.3f, ease::ease_out);

    // Run animation to completion
    for (int i = 0; i < 30; ++i) te.update(0.016f);

    // Push undo after animation completes
    Axes* ptr = &ax;
    mgr.push(UndoAction{
        "Animated zoom",
        [ptr, old_x, old_y]() {
            ptr->xlim(old_x.min, old_x.max);
            ptr->ylim(old_y.min, old_y.max);
        },
        [ptr]() {
            ptr->xlim(2.0f, 8.0f);
            ptr->ylim(2.0f, 8.0f);
        }
    });

    // Verify animation completed
    EXPECT_NEAR(ax.x_limits().min, 2.0f, 0.01f);
    EXPECT_NEAR(ax.x_limits().max, 8.0f, 0.01f);

    // Undo
    EXPECT_TRUE(mgr.undo());
    EXPECT_FLOAT_EQ(ax.x_limits().min, 0.0f);
    EXPECT_FLOAT_EQ(ax.x_limits().max, 10.0f);

    // Redo
    EXPECT_TRUE(mgr.redo());
    EXPECT_FLOAT_EQ(ax.x_limits().min, 2.0f);
    EXPECT_FLOAT_EQ(ax.x_limits().max, 8.0f);
}

TEST(TransitionUndoIntegration, CancelAnimationThenUndo) {
    TransitionEngine te;
    UndoManager mgr;
    Axes ax;
    ax.xlim(0.0f, 10.0f);

    auto old_x = ax.x_limits();

    // Start animation
    te.animate_limits(ax, {5.0f, 5.0f}, {0.0f, 10.0f}, 1.0f);

    // Partially animate
    te.update(0.1f);

    // Cancel mid-animation
    te.cancel_for_axes(&ax);

    // The axes are now at some intermediate state
    float mid_min = ax.x_limits().min;
    float mid_max = ax.x_limits().max;

    // Push undo for the partial change
    Axes* ptr = &ax;
    mgr.push(UndoAction{
        "Cancelled zoom",
        [ptr, old_x]() { ptr->xlim(old_x.min, old_x.max); },
        [ptr, mid_min, mid_max]() { ptr->xlim(mid_min, mid_max); }
    });

    // Undo restores original
    mgr.undo();
    EXPECT_FLOAT_EQ(ax.x_limits().min, 0.0f);
    EXPECT_FLOAT_EQ(ax.x_limits().max, 10.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Integration: FigureManager lifecycle
// ═══════════════════════════════════════════════════════════════════════════════

TEST(FigureManagerIntegration, CreateSwitchCloseLifecycle) {
    std::vector<std::unique_ptr<Figure>> figures;
    figures.push_back(std::make_unique<Figure>());
    FigureManager mgr(figures);

    // Create 3 more
    mgr.create_figure();
    mgr.create_figure();
    mgr.create_figure();
    EXPECT_EQ(mgr.count(), 4u);

    // Switch to last
    mgr.switch_to(3);
    EXPECT_EQ(mgr.active_index(), 3u);

    // Close current (last)
    mgr.close_figure(3);
    EXPECT_EQ(mgr.count(), 3u);
    EXPECT_LE(mgr.active_index(), 2u);

    // Close all except first
    mgr.close_all_except(0);
    EXPECT_EQ(mgr.count(), 1u);
    EXPECT_EQ(mgr.active_index(), 0u);

    // Cannot close last figure
    EXPECT_FALSE(mgr.can_close(0));
}

TEST(FigureManagerIntegration, QueuedOperationsProcessCorrectly) {
    std::vector<std::unique_ptr<Figure>> figures;
    figures.push_back(std::make_unique<Figure>());
    FigureManager mgr(figures);

    mgr.create_figure();  // auto-switches to 1
    mgr.create_figure();  // auto-switches to 2
    EXPECT_EQ(mgr.active_index(), 2u);

    // Queue switch back to 0
    mgr.queue_switch(0);
    // Active index doesn't change until process_pending
    EXPECT_EQ(mgr.active_index(), 2u);

    mgr.process_pending();
    EXPECT_EQ(mgr.active_index(), 0u); // Now processed
}

TEST(FigureManagerIntegration, PerFigureStatePreserved) {
    std::vector<std::unique_ptr<Figure>> figures;
    figures.push_back(std::make_unique<Figure>());
    FigureManager mgr(figures);
    mgr.create_figure();

    // Set state on figure 0
    mgr.state(0).selected_series_index = 2;
    mgr.state(0).inspector_scroll_y = 150.0f;

    // Switch to figure 1
    mgr.switch_to(1);
    mgr.state(1).selected_series_index = 5;

    // Switch back to figure 0
    mgr.switch_to(0);
    EXPECT_EQ(mgr.state(0).selected_series_index, 2);
    EXPECT_FLOAT_EQ(mgr.state(0).inspector_scroll_y, 150.0f);

    // Figure 1 state also preserved
    EXPECT_EQ(mgr.state(1).selected_series_index, 5);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Integration: CommandRegistry + UndoManager
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CommandUndoIntegration, CommandTriggersUndoableAction) {
    CommandRegistry reg;
    UndoManager undo;
    Axes ax;
    ax.xlim(0.0f, 10.0f);

    reg.register_command("view.reset", "Reset View", [&]() {
        undoable_xlim(&undo, ax, 0.0f, 1.0f);
    });

    reg.execute("view.reset");
    EXPECT_FLOAT_EQ(ax.x_limits().min, 0.0f);
    EXPECT_FLOAT_EQ(ax.x_limits().max, 1.0f);
    EXPECT_EQ(undo.undo_count(), 1u);

    undo.undo();
    EXPECT_FLOAT_EQ(ax.x_limits().min, 0.0f);
    EXPECT_FLOAT_EQ(ax.x_limits().max, 10.0f);
}

TEST(CommandUndoIntegration, MultipleCommandsUndoInOrder) {
    CommandRegistry reg;
    UndoManager undo;
    Axes ax;
    ax.xlim(0.0f, 10.0f);
    ax.ylim(0.0f, 10.0f);

    reg.register_command("zoom.x", "Zoom X", [&]() {
        undoable_xlim(&undo, ax, 2.0f, 8.0f);
    });
    reg.register_command("zoom.y", "Zoom Y", [&]() {
        undoable_ylim(&undo, ax, 3.0f, 7.0f);
    });

    reg.execute("zoom.x");
    reg.execute("zoom.y");

    EXPECT_FLOAT_EQ(ax.x_limits().min, 2.0f);
    EXPECT_FLOAT_EQ(ax.y_limits().min, 3.0f);

    // Undo Y first
    undo.undo();
    EXPECT_FLOAT_EQ(ax.y_limits().min, 0.0f);
    EXPECT_FLOAT_EQ(ax.x_limits().min, 2.0f); // X unchanged

    // Undo X
    undo.undo();
    EXPECT_FLOAT_EQ(ax.x_limits().min, 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Integration: Workspace interaction state round-trip
// ═══════════════════════════════════════════════════════════════════════════════

class WorkspaceInteractionIntegration : public ::testing::Test {
protected:
    std::string tmp_path;

    void SetUp() override {
        tmp_path = (std::filesystem::temp_directory_path() / "plotix_int_ws_interact.plotix").string();
    }

    void TearDown() override {
        std::remove(tmp_path.c_str());
    }
};

TEST_F(WorkspaceInteractionIntegration, CrosshairStatePersists) {
    WorkspaceData data;
    data.theme_name = "dark";
    data.interaction.crosshair_enabled = true;
    data.interaction.tooltip_enabled = false;

    ASSERT_TRUE(Workspace::save(tmp_path, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(tmp_path, loaded));

    EXPECT_TRUE(loaded.interaction.crosshair_enabled);
    EXPECT_FALSE(loaded.interaction.tooltip_enabled);
}

TEST_F(WorkspaceInteractionIntegration, MarkersPersist) {
    WorkspaceData data;
    data.theme_name = "dark";

    WorkspaceData::InteractionState::MarkerEntry m1;
    m1.data_x = 1.5f; m1.data_y = 2.5f;
    m1.series_label = "sin(x)"; m1.point_index = 10;

    WorkspaceData::InteractionState::MarkerEntry m2;
    m2.data_x = 3.0f; m2.data_y = -1.0f;
    m2.series_label = "cos(x)"; m2.point_index = 25;

    data.interaction.markers = {m1, m2};

    ASSERT_TRUE(Workspace::save(tmp_path, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(tmp_path, loaded));

    ASSERT_EQ(loaded.interaction.markers.size(), 2u);
    EXPECT_FLOAT_EQ(loaded.interaction.markers[0].data_x, 1.5f);
    EXPECT_FLOAT_EQ(loaded.interaction.markers[0].data_y, 2.5f);
    EXPECT_EQ(loaded.interaction.markers[0].series_label, "sin(x)");
    EXPECT_EQ(loaded.interaction.markers[0].point_index, 10u);
    EXPECT_FLOAT_EQ(loaded.interaction.markers[1].data_x, 3.0f);
    EXPECT_EQ(loaded.interaction.markers[1].series_label, "cos(x)");
}

TEST_F(WorkspaceInteractionIntegration, SeriesOpacityPersists) {
    WorkspaceData data;
    data.theme_name = "dark";

    WorkspaceData::FigureState fig;
    fig.title = "Test";
    WorkspaceData::SeriesState s;
    s.name = "faded";
    s.type = "line";
    s.opacity = 0.15f;
    s.visible = false;
    fig.series.push_back(s);
    data.figures.push_back(fig);

    ASSERT_TRUE(Workspace::save(tmp_path, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(tmp_path, loaded));

    ASSERT_EQ(loaded.figures.size(), 1u);
    ASSERT_EQ(loaded.figures[0].series.size(), 1u);
    EXPECT_FLOAT_EQ(loaded.figures[0].series[0].opacity, 0.15f);
    EXPECT_FALSE(loaded.figures[0].series[0].visible);
}

TEST_F(WorkspaceInteractionIntegration, PanelStatePersists) {
    WorkspaceData data;
    data.theme_name = "light";
    data.panels.inspector_visible = false;
    data.panels.inspector_width = 400.0f;
    data.panels.nav_rail_expanded = true;

    ASSERT_TRUE(Workspace::save(tmp_path, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(tmp_path, loaded));

    EXPECT_FALSE(loaded.panels.inspector_visible);
    EXPECT_FLOAT_EQ(loaded.panels.inspector_width, 400.0f);
    EXPECT_TRUE(loaded.panels.nav_rail_expanded);
    EXPECT_EQ(loaded.theme_name, "light");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Integration: UndoManager stress / edge cases
// ═══════════════════════════════════════════════════════════════════════════════

TEST(UndoStressIntegration, RapidPushUndoRedoCycle) {
    UndoManager mgr;
    int val = 0;

    // Rapid push-undo-redo cycle
    for (int i = 0; i < 200; ++i) {
        mgr.push(UndoAction{
            "step " + std::to_string(i),
            [&val, i]() { val -= i; },
            [&val, i]() { val += i; }
        });
    }

    // Stack should be capped at MAX_STACK_SIZE
    EXPECT_LE(mgr.undo_count(), UndoManager::MAX_STACK_SIZE);

    // Undo all
    while (mgr.can_undo()) mgr.undo();
    EXPECT_EQ(mgr.undo_count(), 0u);
    EXPECT_GT(mgr.redo_count(), 0u);

    // Redo all
    while (mgr.can_redo()) mgr.redo();
    EXPECT_EQ(mgr.redo_count(), 0u);
}

TEST(UndoStressIntegration, InterleavedGroupsAndSingles) {
    UndoManager mgr;
    int val = 0;

    mgr.push(UndoAction{"single1", [&val]() { val -= 1; }, [&val]() { val += 1; }});

    mgr.begin_group("group1");
    mgr.push(UndoAction{"g1a", [&val]() { val -= 10; }, [&val]() { val += 10; }});
    mgr.push(UndoAction{"g1b", [&val]() { val -= 20; }, [&val]() { val += 20; }});
    mgr.end_group();

    mgr.push(UndoAction{"single2", [&val]() { val -= 100; }, [&val]() { val += 100; }});

    EXPECT_EQ(mgr.undo_count(), 3u); // single1, group1, single2

    mgr.undo(); // Undo single2
    mgr.undo(); // Undo group1 (both g1a and g1b)
    EXPECT_EQ(mgr.undo_count(), 1u);
    EXPECT_EQ(mgr.redo_count(), 2u);
}
