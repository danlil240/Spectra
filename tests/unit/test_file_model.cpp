// test_file_model.cpp — File menu model: workspace documents (Save / Save As /
// Open), unsaved-changes tracking, the close prompt, autosave and recovery,
// toasts, CSV export and the File menu / shortcut wiring.

#include <gtest/gtest.h>

#ifdef SPECTRA_USE_IMGUI

    #include <algorithm>
    #include <chrono>
    #include <cstdlib>
    #include <filesystem>
    #include <fstream>
    #include <memory>
    #include <spectra/axes.hpp>
    #include <spectra/figure.hpp>
    #include <spectra/figure_registry.hpp>
    #include <spectra/series.hpp>
    #include <sstream>
    #include <string>
    #include <vector>

    #include "render/backend.hpp"
    #include "render/renderer.hpp"
    #include "ui/app/session_runtime.hpp"
    #include "ui/app/window_runtime.hpp"
    #include "ui/app/window_ui_context.hpp"
    #include "ui/app/window_ui_context_builder.hpp"
    #include "ui/commands/command_registry.hpp"
    #include "ui/commands/shortcut_manager.hpp"
    #include "ui/data/clipboard_export.hpp"
    #include "ui/figures/figure_manager.hpp"
    #include "ui/imgui/imgui_integration.hpp"
    #include "ui/overlay/data_interaction.hpp"
    #include "ui/overlay/toast_queue.hpp"
    #include "ui/settings/settings_store.hpp"
    #include "ui/shell/menu_bar.hpp"
    #include "ui/shell/spectra_app_shell.hpp"
    #include "ui/theme/theme.hpp"
    #include "ui/window/window_placement.hpp"
    #include "ui/workspace/document_session.hpp"
    #include "ui/workspace/document_tracker.hpp"
    #include "ui/workspace/figure_serializer.hpp"
    #include "ui/workspace/workspace_document.hpp"

    #ifdef _WIN32
        #include <process.h>
    #else
        #include <unistd.h>
    #endif

using namespace spectra;
namespace fs = std::filesystem;

namespace
{

int64_t own_pid()
{
    #ifdef _WIN32
    return static_cast<int64_t>(_getpid());
    #else
    return static_cast<int64_t>(::getpid());
    #endif
}

fs::path fresh_dir(const std::string& name)
{
    fs::path dir = fs::temp_directory_path() / ("spectra_file_model_" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

std::unique_ptr<Figure> figure_with_line()
{
    auto                     fig = std::make_unique<Figure>();
    auto&                    ax  = fig->subplot(1, 1, 1);
    const std::vector<float> x{0.0f, 1.0f, 2.0f};
    const std::vector<float> y{1.0f, 4.0f, 9.0f};
    ax.line(x, y).label("squares");
    return fig;
}

DocumentTracker::Snapshot snapshot_of(const std::vector<std::pair<FigureId, Figure*>>& figs)
{
    DocumentTracker::Snapshot s;
    for (const auto& [id, f] : figs)
        s.figures.emplace_back(id, figure_fingerprint(*f));
    s.structure = figs.size();
    return s;
}

}   // namespace

// ─── Fingerprint and tracker ─────────────────────────────────────────────────

TEST(FigureFingerprint, ChangesWithContentNotWithView)
{
    auto           fig  = figure_with_line();
    const uint64_t base = figure_fingerprint(*fig);
    EXPECT_EQ(base, figure_fingerprint(*fig));

    // Zooming is view state, not a document change.
    fig->axes()[0]->xlim(-5.0, 5.0);
    EXPECT_EQ(base, figure_fingerprint(*fig));

    auto* line = dynamic_cast<LineSeries*>(fig->axes()[0]->series()[0].get());
    ASSERT_NE(line, nullptr);
    const std::vector<float> y2{2.0f, 3.0f, 4.0f};
    line->set_y(y2);   // data edit
    const uint64_t after_data = figure_fingerprint(*fig);
    EXPECT_NE(base, after_data);

    line->width(5.0f);   // style
    const uint64_t after_style = figure_fingerprint(*fig);
    EXPECT_NE(after_data, after_style);

    line->label("cubes");   // label
    EXPECT_NE(after_style, figure_fingerprint(*fig));

    const uint64_t before_title = figure_fingerprint(*fig);
    fig->axes()[0]->title("Title");   // axes text
    EXPECT_NE(before_title, figure_fingerprint(*fig));

    const uint64_t before_add = figure_fingerprint(*fig);
    fig->axes()[0]->line();   // series added
    EXPECT_NE(before_add, figure_fingerprint(*fig));
}

TEST(FigureFingerprint, ThemeColourResolutionIsNotAnEdit)
{
    // A Light startup theme resolves palette and letter colours that differ
    // from the default (dark) shades while the UI is built; that must not
    // mark the figure modified (QA H5).
    auto fig = figure_with_line();
    fig->axes()[0]
        ->line(std::vector<float>{0.0f, 1.0f}, std::vector<float>{0.0f, 1.0f})
        .format("r--");
    auto&          auto_line   = *fig->axes()[0]->series()[0];
    auto&          letter_line = *fig->axes()[0]->series()[1];
    const uint64_t base        = figure_fingerprint(*fig);
    const uint64_t gen         = auto_line.change_generation();
    auto_line.clear_dirty();
    letter_line.clear_dirty();

    EXPECT_TRUE(auto_line.resolve_auto_color(Color{0.1f, 0.2f, 0.3f, 1.0f}));
    EXPECT_TRUE(letter_line.resolve_letter_color(Color{0.7f, 0.1f, 0.1f, 1.0f}));
    EXPECT_TRUE(auto_line.is_dirty());   // still re-uploaded
    EXPECT_TRUE(letter_line.is_dirty());
    EXPECT_EQ(gen, auto_line.change_generation());
    EXPECT_EQ(base, figure_fingerprint(*fig));
}

TEST(DocumentTracker, DetectFlagsUserChangesAbsorbKeepsProgramChanges)
{
    auto            fig = figure_with_line();
    DocumentTracker t;
    t.detect(snapshot_of({{1, fig.get()}}));   // first call = baseline
    EXPECT_FALSE(t.is_modified());

    // Program change (on_frame / IPC): absorbed.
    fig->axes()[0]->title("animated");
    t.absorb(snapshot_of({{1, fig.get()}}));
    EXPECT_FALSE(t.detect(snapshot_of({{1, fig.get()}})));
    EXPECT_FALSE(t.is_modified());

    // User change: detected and sticky, even across absorb().
    fig->axes()[0]->title("edited");
    EXPECT_TRUE(t.detect(snapshot_of({{1, fig.get()}})));
    EXPECT_TRUE(t.is_modified());
    EXPECT_TRUE(t.is_figure_modified(1));
    t.absorb(snapshot_of({{1, fig.get()}}));
    EXPECT_TRUE(t.is_modified());
    EXPECT_EQ(t.modified_figure_count(), 1u);

    t.mark_saved(snapshot_of({{1, fig.get()}}));
    EXPECT_FALSE(t.is_modified());

    // Adding a tab changes the structure.
    auto fig2 = figure_with_line();
    EXPECT_TRUE(t.detect(snapshot_of({{1, fig.get()}, {2, fig2.get()}})));
    EXPECT_TRUE(t.is_modified());
    EXPECT_FALSE(t.is_figure_modified(2));

    t.mark_saved(snapshot_of({{1, fig.get()}}));
    EXPECT_FALSE(t.is_figure_modified(1));
    t.mark_modified();
    EXPECT_TRUE(t.is_modified());
    EXPECT_TRUE(t.is_figure_modified(1));   // document-wide mark covers each figure
    EXPECT_FALSE(t.is_figure_modified(99));
}

// ─── Quit / close prompt state machine ───────────────────────────────────────

TEST(UnsavedChangesPrompt, UnmodifiedProceedsImmediately)
{
    UnsavedChangesPrompt p;
    int                  ran = 0;
    EXPECT_TRUE(p.request(UnsavedChangesPrompt::Reason::Quit, false, [&]() { ++ran; }));
    EXPECT_EQ(ran, 1);
    EXPECT_FALSE(p.is_open());
}

TEST(UnsavedChangesPrompt, ChoicesDecideWhetherTheActionRuns)
{
    using Choice = UnsavedChangesPrompt::Choice;
    UnsavedChangesPrompt p;
    int                  ran = 0;

    // Cancel
    EXPECT_FALSE(p.request(UnsavedChangesPrompt::Reason::Quit, true, [&]() { ++ran; }));
    EXPECT_TRUE(p.is_open());
    EXPECT_EQ(p.reason(), UnsavedChangesPrompt::Reason::Quit);
    // A second request while open is ignored.
    EXPECT_FALSE(p.request(UnsavedChangesPrompt::Reason::CloseFigure, true, [&]() { ran += 10; }));
    EXPECT_FALSE(p.resolve(Choice::Cancel));
    EXPECT_FALSE(p.is_open());
    EXPECT_EQ(ran, 0);

    // Don't Save
    p.request(UnsavedChangesPrompt::Reason::CloseFigure, true, [&]() { ++ran; });
    EXPECT_EQ(p.reason(), UnsavedChangesPrompt::Reason::CloseFigure);
    EXPECT_TRUE(p.resolve(Choice::DontSave));
    EXPECT_EQ(ran, 1);

    // Save that fails (dialog cancelled / write error): nothing happens.
    p.request(UnsavedChangesPrompt::Reason::Quit, true, [&]() { ++ran; });
    EXPECT_FALSE(p.resolve(Choice::Save, []() { return false; }));
    EXPECT_FALSE(p.is_open());
    EXPECT_EQ(ran, 1);

    // Save that succeeds: the action runs after it.
    int saves = 0;
    p.request(UnsavedChangesPrompt::Reason::Quit, true, [&]() { ++ran; });
    EXPECT_TRUE(p.resolve(Choice::Save,
                          [&]()
                          {
                              ++saves;
                              return true;
                          }));
    EXPECT_EQ(saves, 1);
    EXPECT_EQ(ran, 2);

    // Resolving with nothing open does nothing.
    EXPECT_FALSE(p.resolve(Choice::DontSave));
}

// ─── Workspace document format ───────────────────────────────────────────────

TEST(WorkspaceDocument, EncodeDecodeRoundTripAndSniff)
{
    WorkspaceDocumentContents c;
    c.workspace_json        = "{\"version\": 4}";
    c.figure_blobs          = {"abc", std::string("\0\1\2", 3)};
    const std::string bytes = WorkspaceDocument::encode(c);
    EXPECT_EQ(WorkspaceDocument::sniff(bytes), SpectraFileKind::WorkspaceDocument);

    WorkspaceDocumentContents out;
    ASSERT_TRUE(WorkspaceDocument::decode(bytes, out));
    EXPECT_EQ(out.workspace_json, c.workspace_json);
    EXPECT_EQ(out.figure_blobs, c.figure_blobs);

    // Truncated or foreign data is rejected, never read past the end.
    for (size_t n = 0; n < bytes.size(); ++n)
        EXPECT_FALSE(WorkspaceDocument::decode(std::string_view(bytes).substr(0, n), out)) << n;
    EXPECT_FALSE(WorkspaceDocument::decode("{\"a\":1}", out));

    EXPECT_EQ(WorkspaceDocument::sniff("  {\"theme\": 1}"), SpectraFileKind::Json);
    EXPECT_EQ(WorkspaceDocument::sniff("hello"), SpectraFileKind::Unknown);
    auto               fig = figure_with_line();
    std::ostringstream os(std::ios::binary);
    ASSERT_TRUE(FigureSerializer::save(os, *fig));
    EXPECT_EQ(WorkspaceDocument::sniff(os.str()), SpectraFileKind::Figure);
    EXPECT_EQ(WorkspaceDocument::sniff_file("/nonexistent/x.spectra"), SpectraFileKind::Missing);
}

TEST(WorkspaceDocument, WriteFileReportsFailures)
{
    const fs::path dir = fresh_dir("write");
    std::string    error;
    EXPECT_TRUE(WorkspaceDocument::write_file((dir / "a.spectra").string(), "SPWSxx", &error));
    std::string back;
    ASSERT_TRUE(WorkspaceDocument::read_file((dir / "a.spectra").string(), back));
    EXPECT_EQ(back, "SPWSxx");
    EXPECT_FALSE(fs::exists(dir / "a.spectra.tmp"));

    EXPECT_FALSE(
        WorkspaceDocument::write_file((dir / "missing" / "b.spectra").string(), "x", &error));
    EXPECT_NE(error.find("does not exist"), std::string::npos);
    fs::remove_all(dir);
}

TEST(FigureSerializerStream, RoundTripsThroughMemory)
{
    auto               fig = figure_with_line();
    std::ostringstream os(std::ios::binary);
    ASSERT_TRUE(FigureSerializer::save(os, *fig));
    Figure             loaded;
    std::istringstream is(os.str(), std::ios::binary);
    ASSERT_TRUE(FigureSerializer::load(is, loaded));
    ASSERT_EQ(loaded.axes().size(), 1u);
    ASSERT_EQ(loaded.axes()[0]->series().size(), 1u);
    auto* line = dynamic_cast<LineSeries*>(loaded.axes()[0]->series()[0].get());
    ASSERT_NE(line, nullptr);
    EXPECT_EQ(line->label(), "squares");
    ASSERT_EQ(line->point_count(), 3u);
    EXPECT_FLOAT_EQ(line->y_data()[2], 9.0f);
}

// ─── DocumentSession: save, close guard, autosave, recovery ──────────────────

TEST(DocumentSession, SaveWritesTheDocumentAndClearsModified)
{
    const fs::path dir = fresh_dir("save");
    auto           fig = figure_with_line();

    ui::ToastQueue  toasts;
    DocumentSession doc;
    doc.set_toasts(&toasts);
    doc.set_snapshot_fn([&]() { return snapshot_of({{1, fig.get()}}); });
    doc.set_encode_fn(
        [&]()
        {
            WorkspaceDocumentContents c;
            std::ostringstream        os(std::ios::binary);
            FigureSerializer::save(os, *fig);
            c.figure_blobs.push_back(os.str());
            return WorkspaceDocument::encode(c);
        });
    std::vector<std::string> saved;
    doc.set_on_saved([&](const std::string& p) { saved.push_back(p); });

    doc.detect_changes();
    EXPECT_FALSE(doc.is_modified());
    EXPECT_EQ(doc.display_name(), "Untitled");

    fig->axes()[0]->title("changed");
    doc.detect_changes();
    EXPECT_TRUE(doc.is_modified());

    // Save with no path asks (Save As); a cancelled dialog saves nothing.
    std::string suggested;
    doc.set_ask_save_path_fn(
        [&](const std::string& s)
        {
            suggested = s;
            return std::optional<std::string>{};
        });
    EXPECT_FALSE(doc.save());
    EXPECT_EQ(suggested, "untitled.spectra");
    EXPECT_TRUE(doc.is_modified());

    // The extension is added when the user leaves it out.
    const std::string target = (dir / "analysis").string();
    doc.set_ask_save_path_fn([&](const std::string&)
                             { return std::optional<std::string>(target); });
    ASSERT_TRUE(doc.save());
    EXPECT_EQ(doc.path(), target + ".spectra");
    EXPECT_EQ(doc.display_name(), "analysis.spectra");
    EXPECT_FALSE(doc.is_modified());
    EXPECT_EQ(WorkspaceDocument::sniff_file(doc.path()), SpectraFileKind::WorkspaceDocument);
    ASSERT_EQ(saved.size(), 1u);
    ASSERT_FALSE(toasts.empty());
    EXPECT_EQ(toasts.toasts().back().kind, ui::ToastKind::Success);

    // Save with a path does not ask again.
    doc.set_ask_save_path_fn(
        [](const std::string&) -> std::optional<std::string>
        {
            ADD_FAILURE() << "Save asked for a path";
            return std::nullopt;
        });
    fig->axes()[0]->title("again");
    doc.detect_changes();
    EXPECT_TRUE(doc.save());
    EXPECT_FALSE(doc.is_modified());

    // A failing write leaves the document modified and shows a persistent error.
    fig->axes()[0]->title("third");
    doc.detect_changes();
    EXPECT_FALSE(doc.save_to((dir / "nope" / "x.spectra").string()));
    EXPECT_TRUE(doc.is_modified());
    EXPECT_EQ(toasts.toasts().back().kind, ui::ToastKind::Error);
    EXPECT_EQ(doc.path(), target + ".spectra");
    fs::remove_all(dir);
}

TEST(DocumentSession, CloseWindowAsksOnlyWhenModified)
{
    auto            fig = figure_with_line();
    DocumentSession doc;
    doc.set_snapshot_fn([&]() { return snapshot_of({{1, fig.get()}}); });
    doc.detect_changes();

    int closes = 0;
    EXPECT_TRUE(doc.confirm_close_window([&]() { ++closes; }));
    EXPECT_FALSE(doc.prompt().is_open());

    fig->axes()[0]->title("edited");
    doc.detect_changes();
    EXPECT_FALSE(doc.confirm_close_window([&]() { ++closes; }));
    EXPECT_TRUE(doc.prompt().is_open());

    // Cancel keeps the window.
    EXPECT_FALSE(doc.resolve_prompt(UnsavedChangesPrompt::Choice::Cancel));
    EXPECT_EQ(closes, 0);

    // Don't Save closes, and the close that follows is let through.
    EXPECT_FALSE(doc.confirm_close_window([&]() { ++closes; }));
    EXPECT_TRUE(doc.resolve_prompt(UnsavedChangesPrompt::Choice::DontSave));
    EXPECT_EQ(closes, 1);
    EXPECT_TRUE(doc.confirm_close_window([&]() { ++closes; }));
}

TEST(DocumentSession, AutosaveWritesWhileModifiedAndCleansUp)
{
    const fs::path dir = fresh_dir("autosave");
    auto           fig = figure_with_line();
    std::string    autosave_path;
    {
        DocumentSession doc;
        doc.set_snapshot_fn([&]() { return snapshot_of({{1, fig.get()}}); });
        doc.set_encode_fn([]() { return WorkspaceDocument::encode({"{}", {}}); });
        doc.enable_autosave(dir.string());
        doc.autosave().set_debounce(std::chrono::seconds(0));
        doc.autosave().set_interval(std::chrono::seconds(0));
        autosave_path = doc.autosave().autosave_path();
        EXPECT_EQ(DocumentSession::autosave_pid(fs::path(autosave_path).filename().string()),
                  DocumentSession::autosave_pid(fs::path(autosave_path).filename().string()));
        EXPECT_GT(DocumentSession::autosave_pid(fs::path(autosave_path).filename().string()), 0);

        doc.detect_changes();
        doc.tick_autosave();
        EXPECT_FALSE(fs::exists(autosave_path));   // nothing to protect yet

        fig->axes()[0]->title("edited");
        doc.detect_changes();
        doc.tick_autosave();
        ASSERT_TRUE(fs::exists(autosave_path));
        EXPECT_EQ(WorkspaceDocument::sniff_file(autosave_path), SpectraFileKind::WorkspaceDocument);

        // Our own (running) process's file is never offered for recovery.
        EXPECT_TRUE(DocumentSession::find_orphan_autosaves(dir.string()).empty());

        // Discarding (Don't Save / saved) removes it.
        doc.discard_autosave();
        EXPECT_FALSE(fs::exists(autosave_path));

        fig->axes()[0]->title("edited again");
        doc.detect_changes();
        doc.tick_autosave();
        ASSERT_TRUE(fs::exists(autosave_path));
    }
    // Destroyed while modified (signal, crash path): the file stays for recovery.
    EXPECT_TRUE(fs::exists(autosave_path));

    {
        DocumentSession clean;
        clean.set_snapshot_fn([&]() { return snapshot_of({{1, fig.get()}}); });
        clean.set_encode_fn([]() { return WorkspaceDocument::encode({"{}", {}}); });
        clean.enable_autosave(dir.string());
        clean.detect_changes();
    }
    // A clean close leaves only the earlier orphan.
    size_t files = 0;
    for (const auto& e : fs::directory_iterator(dir))
        files += e.is_regular_file() ? 1 : 0;
    EXPECT_EQ(files, 1u);
    fs::remove_all(dir);
}

TEST(DocumentSession, RecoveryOffersOrphansOfDeadProcessesOnly)
{
    const fs::path dir   = fresh_dir("recovery");
    const auto     write = [&](const std::string& name)
    { std::ofstream(dir / name, std::ios::binary) << WorkspaceDocument::encode({"{}", {}}); };
    write("autosave-999999-aaaa.spectra");   // dead
    // This test process is alive on every platform (pid 1 is not on Windows).
    const int64_t     self       = own_pid();
    const std::string alive_name = "autosave-" + std::to_string(self) + "-bbbb.spectra";
    write(alive_name);
    write("recovering-888888-cccc.spectra");   // claimed by a process that died too
    write("notes.txt");
    write("autosave-x-dddd.spectra");   // malformed

    EXPECT_EQ(DocumentSession::autosave_pid("autosave-999999-aaaa.spectra"), 999999);
    EXPECT_EQ(DocumentSession::autosave_pid("recovering-888888-cccc.spectra"), 888888);
    EXPECT_EQ(DocumentSession::autosave_pid("autosave-x-dddd.spectra"), -1);
    EXPECT_EQ(DocumentSession::autosave_pid("autosave-12.spectra"), -1);
    EXPECT_EQ(DocumentSession::autosave_pid("notes.txt"), -1);

    const auto orphans =
        DocumentSession::find_orphan_autosaves(dir.string(),
                                               [self](int64_t pid) { return pid == self; });
    EXPECT_EQ(orphans.size(), 2u);

    // Restore: each claimed file is opened, marked unsaved and deleted.
    // (A live pid 999999 / 888888 is unlikely; the real check is used here.)
    if (DocumentSession::process_alive(999999) || DocumentSession::process_alive(888888))
        GTEST_SKIP() << "test pids are in use";
    ui::ToastQueue  toasts;
    DocumentSession doc;
    doc.set_toasts(&toasts);
    std::vector<std::string> opened;
    doc.set_open_fn(
        [&](const std::string& p)
        {
            opened.push_back(p);
            return true;
        });
    doc.enable_autosave(dir.string());
    // This process is alive, so only the two dead ones are claimed.
    EXPECT_TRUE(doc.check_recovery());
    EXPECT_EQ(doc.recovery_files().size(), 2u);
    EXPECT_TRUE(doc.check_recovery());   // checked once; stays pending
    for (const auto& f : doc.recovery_files())
        EXPECT_EQ(fs::path(f).filename().string().rfind("recovering-", 0), 0u);

    EXPECT_EQ(doc.restore_recovery(), 2u);
    EXPECT_EQ(opened.size(), 2u);
    EXPECT_FALSE(doc.recovery_pending());
    EXPECT_TRUE(doc.is_modified());
    for (const auto& f : opened)
        EXPECT_FALSE(fs::exists(f));
    EXPECT_EQ(toasts.toasts().back().kind, ui::ToastKind::Success);
    EXPECT_TRUE(fs::exists(dir / alive_name));
    fs::remove_all(dir);
}

// ─── Toasts ──────────────────────────────────────────────────────────────────

TEST(ToastQueue, AutoDismissErrorsPersistAndCap)
{
    ui::ToastQueue q;
    const uint64_t ok  = q.success("Saved");
    const uint64_t err = q.error("Could not export", "permission denied");
    EXPECT_EQ(q.size(), 2u);
    EXPECT_NE(ok, err);

    q.update(ui::ToastQueue::DEFAULT_LIFETIME + 0.1f);
    ASSERT_EQ(q.size(), 1u);   // the error stays
    EXPECT_EQ(q.toasts().front().id, err);
    q.update(1000.0f);
    EXPECT_EQ(q.size(), 1u);

    EXPECT_TRUE(q.dismiss(err));   // click
    EXPECT_TRUE(q.empty());
    EXPECT_FALSE(q.dismiss(err));

    // More than MAX_VISIBLE: the oldest self-dismissing toast goes first.
    const uint64_t first_err = q.error("e1");
    for (int i = 0; i < 10; ++i)
        q.info("i" + std::to_string(i));
    EXPECT_EQ(q.size(), ui::ToastQueue::MAX_VISIBLE);
    EXPECT_EQ(q.toasts().front().id, first_err);

    int      acted = 0;
    uint64_t id    = q.warning("with action");
    EXPECT_TRUE(q.set_action(id, "Show", [&]() { ++acted; }));
    EXPECT_TRUE(q.activate(id));
    EXPECT_EQ(acted, 1);
    EXPECT_FALSE(q.activate(id));
}

TEST(ToastQueue, NeedsFramesOnlyWhileSomethingMoves)
{
    ui::ToastQueue q;
    EXPECT_FALSE(q.needs_frames());
    q.success("Saved");
    EXPECT_TRUE(q.needs_frames());   // counting down to dismissal
    q.update(ui::ToastQueue::DEFAULT_LIFETIME + 0.1f);
    EXPECT_FALSE(q.needs_frames());

    q.error("Could not save");
    EXPECT_TRUE(q.needs_frames());   // fading in
    q.update(1.0f);
    EXPECT_FALSE(q.needs_frames());   // a settled error can idle until clicked

    q.watch_file_write("/nonexistent/spectra_toast.png", "ok", "fail");
    EXPECT_TRUE(q.needs_frames());

    EXPECT_EQ(ui::ToastQueue::folder_detail("/data/runs/a.spectra"), "in /data/runs");
    EXPECT_EQ(ui::ToastQueue::folder_detail("a.spectra"), "a.spectra");
    const std::string long_dir = "/" + std::string(80, 'x') + "/tail";
    const std::string detail   = ui::ToastQueue::folder_detail(long_dir + "/a.png");
    EXPECT_EQ(detail.rfind("in ...", 0), 0u);
    EXPECT_EQ(detail.substr(detail.size() - 5), "/tail");
}

TEST(ToastQueue, WatchedFileWriteBecomesSuccessOrError)
{
    const fs::path dir = fresh_dir("toast_watch");
    ui::ToastQueue q;
    const auto     png = (dir / "a.png").string();
    q.watch_file_write(png, "Exported a.png", "Could not export PNG");
    q.update(0.1f);
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.watched_count(), 1u);
    std::ofstream(png) << "x";
    q.update(0.1f);
    ASSERT_EQ(q.size(), 1u);
    EXPECT_EQ(q.toasts().front().kind, ui::ToastKind::Success);
    EXPECT_EQ(q.watched_count(), 0u);

    q.clear();
    q.watch_file_write((dir / "never.png").string(), "ok", "Could not export PNG");
    q.update(ui::ToastQueue::WRITE_TIMEOUT + 0.1f);
    ASSERT_EQ(q.size(), 1u);
    EXPECT_EQ(q.toasts().front().kind, ui::ToastKind::Error);
    fs::remove_all(dir);
}

// ─── CSV export and window title ─────────────────────────────────────────────

TEST(ExportFigureCsv, WritesLineSeriesAndRefusesEmptyFigures)
{
    const fs::path dir = fresh_dir("csv");
    auto           fig = figure_with_line();
    fig->axes()[0]->series()[0]->label("a,b");
    std::string error;
    ASSERT_TRUE(export_figure_csv(*fig, (dir / "d.csv").string(), &error)) << error;
    {
        // Scoped: Windows cannot remove the folder while the file is open.
        std::ifstream in(dir / "d.csv");
        std::string   header;
        std::getline(in, header);
        EXPECT_EQ(header, "\"a,b_x\",\"a,b_y\"");
        std::string row;
        std::getline(in, row);
        EXPECT_EQ(row, "0,1");
    }

    Figure empty;
    empty.subplot(1, 1, 1);
    EXPECT_FALSE(export_figure_csv(empty, (dir / "e.csv").string(), &error));
    EXPECT_FALSE(error.empty());
    fs::remove_all(dir);
}

TEST(WindowTitle, ShowsDocumentAndModifiedMarker)
{
    EXPECT_EQ(window_title_for_document("Figure 1", "", false), "Figure 1 — Spectra");
    EXPECT_EQ(window_title_for_document("Figure 1", "a.spectra", false),
              "Figure 1 — a.spectra — Spectra");
    EXPECT_EQ(window_title_for_document("Figure 1", "a.spectra", true),
              "• Figure 1 — a.spectra — Spectra");
    EXPECT_EQ(window_title_for_document("", "", true), "• Spectra");
}

// ─── File menu / command wiring through a real window context ────────────────

namespace
{

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

class FileModelFixture : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        // Recent files and autosaves go to a private config dir.
        if (const char* v = std::getenv("SPECTRA_CONFIG_DIR"))
            old_config_dir_ = v;
        config_dir_ = fresh_dir("config");
    #ifdef _WIN32
        _putenv_s("SPECTRA_CONFIG_DIR", config_dir_.string().c_str());
    #else
        setenv("SPECTRA_CONFIG_DIR", config_dir_.string().c_str(), 1);
    #endif
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
    #ifdef _WIN32
        _putenv_s("SPECTRA_CONFIG_DIR", old_config_dir_.c_str());
    #else
        if (old_config_dir_.empty())
            unsetenv("SPECTRA_CONFIG_DIR");
        else
            setenv("SPECTRA_CONFIG_DIR", old_config_dir_.c_str(), 1);
    #endif
        fs::remove_all(config_dir_);
    }

    WindowUIContext& build(FigureId initial, bool with_imgui = false)
    {
        WindowUIContextBuildOptions options;
        options.registry                 = &registry_;
        options.theme_mgr                = &theme_mgr_;
        options.initial_figure_id        = initial;
        options.active_figure            = &active_figure_;
        options.active_figure_id         = &active_figure_id_;
        options.session                  = session_.get();
        options.on_window_close_request  = [this]() { ++window_close_requests_; };
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
    Figure*                          active_figure_         = nullptr;
    FigureId                         active_figure_id_      = INVALID_FIGURE_ID;
    int                              window_close_requests_ = 0;
    fs::path                         config_dir_;
    std::string                      old_config_dir_;
};

}   // namespace

TEST_F(FileModelFixture, FileCommandsAndShortcuts)
{
    auto& ui = build(INVALID_FIGURE_ID);
    struct Expect
    {
        const char* id;
        const char* shortcut;
    };
    const Expect expected[] = {
        {"file.open", "Ctrl+O"},
        {"file.save", "Ctrl+S"},
        {"file.save_as", "Ctrl+Shift+S"},
        {"file.export_png", "Ctrl+E"},
        {"file.export_svg", "Ctrl+Shift+E"},
        {"file.export_pdf", ""},
        {"file.export_csv", ""},
        {"figure.close", "Ctrl+W"},
    };
    for (const auto& e : expected)
    {
        const Command* cmd = ui.cmd_registry.find(e.id);
        ASSERT_NE(cmd, nullptr) << e.id;
        EXPECT_EQ(cmd->category, std::string(e.id).rfind("file.", 0) == 0 ? "File" : "Figure");
        if (*e.shortcut)
        {
            EXPECT_EQ(ui.shortcut_mgr.command_for_shortcut(Shortcut::from_string(e.shortcut)), e.id)
                << e.shortcut;
        }
    }
    // Ctrl+S no longer exports a PNG.
    EXPECT_NE(ui.shortcut_mgr.command_for_shortcut(Shortcut::from_string("Ctrl+S")),
              "file.export_png");
}

TEST_F(FileModelFixture, FileMenuHasOpenSaveAndExportSubmenu)
{
    auto& ui = build(INVALID_FIGURE_ID, true);
    ASSERT_NE(ui.app_shell, nullptr);
    ui.app_shell->ensure_initialized();
    auto& bar = ui.app_shell->menu_bar();
    ui::shell::SpectraAppShell::sync_recent_layouts_menu(bar, ui.imgui_ui.get());

    std::vector<std::string>     labels;
    const ui::shell::MenuAction* exp    = nullptr;
    const ui::shell::MenuAction* recent = nullptr;
    for (const auto& item : bar.menu("File").items())
    {
        labels.push_back(item.label);
        if (item.label == ui::shell::SpectraAppShell::EXPORT_MENU)
            exp = &item;
        if (item.label == ui::shell::SpectraAppShell::RECENT_LAYOUTS_MENU)
            recent = &item;
    }
    // One save concept: no Save Workspace / Save Figure / Load Workspace items.
    for (const char* gone :
         {"Save Workspace", "Load Workspace", "Save Figure...", "Load Figure...", "Export PNG"})
        EXPECT_EQ(std::find(labels.begin(), labels.end(), gone), labels.end()) << gone;
    for (const char* want :
         {"Open...", "Open Log...", "Open Recent", "Save", "Save As...", "Export", "Exit"})
        EXPECT_NE(std::find(labels.begin(), labels.end(), want), labels.end()) << want;
    EXPECT_LT(std::find(labels.begin(), labels.end(), "Open...") - labels.begin(),
              std::find(labels.begin(), labels.end(), "Save") - labels.begin());

    ASSERT_NE(recent, nullptr);
    EXPECT_TRUE(recent->cascade);
    ASSERT_FALSE(recent->submenu.empty());
    EXPECT_EQ(recent->submenu.front().label, "No Recent Files");

    ASSERT_NE(exp, nullptr);
    EXPECT_TRUE(exp->cascade);
    std::vector<std::string> export_ids;
    for (const auto& sub : exp->submenu)
        export_ids.push_back(sub.command_id);
    for (const char* id :
         {"file.export_png", "file.export_svg", "file.export_pdf", "file.export_csv"})
        EXPECT_NE(std::find(export_ids.begin(), export_ids.end(), id), export_ids.end()) << id;

    // The command bar draws Export as a cascading submenu with shortcuts.
    const auto items =
        ui::shell::to_imgui_menu_items(bar.menu("File").items(), bar.shortcut_resolver());
    const ImGuiIntegration::MenuItem* exp_item  = nullptr;
    const ImGuiIntegration::MenuItem* save_item = nullptr;
    for (const auto& it : items)
    {
        if (it.label == "Export")
            exp_item = &it;
        if (it.label == "Save")
            save_item = &it;
    }
    ASSERT_NE(exp_item, nullptr);
    ASSERT_GE(exp_item->children.size(), 4u);
    EXPECT_EQ(exp_item->children[0].label, "PNG Image...");
    EXPECT_EQ(exp_item->children[0].shortcut, "Ctrl+E");
    ASSERT_NE(save_item, nullptr);
    EXPECT_EQ(save_item->shortcut, "Ctrl+S");

    // Open Recent lists saved files once there are some.
    const fs::path dir = fresh_dir("menu_recent");
    std::ofstream(dir / "a.spectra") << "{}";
    ui.layout_session.recent().push((dir / "a.spectra").string());
    ui::shell::SpectraAppShell::sync_recent_layouts_menu(bar, ui.imgui_ui.get());
    for (const auto& item : bar.menu("File").items())
    {
        if (item.label == ui::shell::SpectraAppShell::RECENT_LAYOUTS_MENU)
        {
            ASSERT_FALSE(item.submenu.empty());
            EXPECT_EQ(item.submenu.front().label, "a.spectra");
        }
    }
    ui.layout_session.recent().clear();
    fs::remove_all(dir);
}

TEST_F(FileModelFixture, SaveThenOpenRestoresFiguresWithData)
{
    const fs::path dir = fresh_dir("roundtrip");
    auto           fig = figure_with_line();
    fig->axes()[0]->title("Pressure");
    const FigureId id = registry_.register_figure(std::move(fig));
    auto&          ui = build(id);
    ASSERT_EQ(ui.fig_mgr->count(), 1u);
    ui.fig_mgr->set_title(id, "Run 7");

    const std::string path = (dir / "run.spectra").string();
    ui.document.set_path(path);
    ASSERT_TRUE(ui.cmd_registry.execute("file.save"));
    ASSERT_EQ(WorkspaceDocument::sniff_file(path), SpectraFileKind::WorkspaceDocument);
    EXPECT_FALSE(ui.document.is_modified());
    ASSERT_FALSE(ui.layout_session.recent().empty());
    EXPECT_EQ(fs::path(ui.layout_session.recent().entries().front()).filename(), "run.spectra");

    // A second, empty window opens it: figure, data and tab title come back,
    // and the window adopts the file (clean, same path).
    ui_ctx_.reset();
    auto& ui2 = build(INVALID_FIGURE_ID);
    ASSERT_EQ(ui2.fig_mgr->count(), 0u);
    ASSERT_TRUE(ui2.open_file_fn);
    ASSERT_TRUE(ui2.open_file_fn(path));
    ASSERT_EQ(ui2.fig_mgr->count(), 1u);
    const FigureId new_id = ui2.fig_mgr->figure_ids()[0];
    EXPECT_EQ(ui2.fig_mgr->get_title(new_id), "Run 7");
    Figure* back = registry_.get(new_id);
    ASSERT_NE(back, nullptr);
    ASSERT_EQ(back->axes().size(), 1u);
    EXPECT_EQ(back->axes()[0]->get_title(), "Pressure");
    ASSERT_EQ(back->axes()[0]->series().size(), 1u);
    auto* line = dynamic_cast<LineSeries*>(back->axes()[0]->series()[0].get());
    ASSERT_NE(line, nullptr);
    ASSERT_EQ(line->point_count(), 3u);
    EXPECT_FLOAT_EQ(line->y_data()[1], 4.0f);
    EXPECT_EQ(ui2.document.path(), path);
    ui2.document.detect_changes();
    EXPECT_FALSE(ui2.document.is_modified());
    EXPECT_EQ(ui2.toasts.toasts().back().kind, ui::ToastKind::Success);

    // Opening a missing file fails with an error toast.
    EXPECT_FALSE(ui2.open_file_fn((dir / "gone.spectra").string()));
    EXPECT_EQ(ui2.toasts.toasts().back().kind, ui::ToastKind::Error);
    fs::remove_all(dir);
}

TEST_F(FileModelFixture, CloseLastFigureLeavesStartPageNotQuit)
{
    const FigureId id = registry_.register_figure(figure_with_line());
    auto&          ui = build(id);
    ui.document.detect_changes();   // baseline
    ASSERT_EQ(ui.fig_mgr->count(), 1u);

    // Modified: Ctrl+W asks first.
    registry_.get(id)->axes()[0]->title("edited");
    ui.document.detect_changes();
    ASSERT_TRUE(ui.document.is_figure_modified(id));
    ASSERT_TRUE(ui.cmd_registry.execute("figure.close"));
    ui.fig_mgr->process_pending();
    EXPECT_TRUE(ui.document.prompt().is_open());
    EXPECT_EQ(ui.fig_mgr->count(), 1u);

    // Cancel keeps it; Don't Save closes it — and the window stays open.
    ui.document.resolve_prompt(UnsavedChangesPrompt::Choice::Cancel);
    ui.fig_mgr->process_pending();
    EXPECT_EQ(ui.fig_mgr->count(), 1u);
    ui.cmd_registry.execute("figure.close");
    ui.document.resolve_prompt(UnsavedChangesPrompt::Choice::DontSave);
    ui.fig_mgr->process_pending();
    EXPECT_EQ(ui.fig_mgr->count(), 0u);
    EXPECT_EQ(ui.fig_mgr->active_index(), INVALID_FIGURE_ID);
    EXPECT_EQ(window_close_requests_, 0);

    // An empty window has nothing to lose: quitting now goes straight through.
    ui.document.detect_changes();
    EXPECT_FALSE(ui.document.is_modified());
    EXPECT_TRUE(ui.document.confirm_close_window({}));
}

// ─── Review fixes: idle autosave, tab close, overlays ────────────────────────

TEST_F(FileModelFixture, AutosaveRunsWhileTheWindowIsIdle)
{
    const FigureId id  = registry_.register_figure(figure_with_line());
    auto&          ui  = build(id);
    const fs::path dir = fresh_dir("idle_autosave");
    ui.document.enable_autosave(dir.string());
    ui.document.autosave().set_debounce(std::chrono::seconds(0));
    ui.document.autosave().set_interval(std::chrono::seconds(0));
    ui.document.detect_changes();   // baseline

    // An edit seen by the last rendered frame, then the loop goes idle:
    // update() no longer runs, only the per-iteration background tick.
    registry_.get(id)->axes()[0]->title("edited");
    ui.document.detect_changes();
    const std::string path = ui.document.autosave().autosave_path();
    EXPECT_FALSE(fs::exists(path));
    WindowRuntime::background_tick(ui);
    EXPECT_TRUE(fs::exists(path));

    ui.document.discard_autosave();
    fs::remove_all(dir);
}

TEST_F(FileModelFixture, PaneTabCloseAsksForModifiedFigures)
{
    const FigureId id = registry_.register_figure(figure_with_line());
    auto&          ui = build(id, true);
    ASSERT_NE(ui.imgui_ui, nullptr);
    const auto& close_tab = ui.imgui_ui->pane_tab_close_cb();
    ASSERT_TRUE(close_tab);
    ui.fig_mgr->queue_create();
    ui.fig_mgr->process_pending();
    ASSERT_EQ(ui.fig_mgr->count(), 2u);
    const FigureId other = ui.fig_mgr->figure_ids()[1];
    ui.document.detect_changes();
    ASSERT_TRUE(ui.document.save_to((config_dir_ / "review.spectra").string()));

    // Unmodified tab: closes without asking.
    close_tab(other);
    ui.fig_mgr->process_pending();
    EXPECT_FALSE(ui.document.prompt().is_open());
    EXPECT_EQ(ui.fig_mgr->count(), 1u);

    // Modified tab (close button / middle click / menu): asks first, and the
    // prompt names the tab being closed.
    registry_.get(id)->axes()[0]->title("edited");
    ui.document.detect_changes();
    close_tab(id);
    ui.fig_mgr->process_pending();
    ASSERT_TRUE(ui.document.prompt().is_open());
    EXPECT_EQ(ui.document.prompt().subject(), "\"" + ui.fig_mgr->get_title(id) + "\"");
    EXPECT_EQ(ui.fig_mgr->count(), 1u);
    ui.document.resolve_prompt(UnsavedChangesPrompt::Choice::Cancel);
    ui.fig_mgr->process_pending();
    EXPECT_EQ(ui.fig_mgr->count(), 1u);

    // Don't Save closes it; the last tab leaves the start page, not the app.
    close_tab(id);
    ui.document.resolve_prompt(UnsavedChangesPrompt::Choice::DontSave);
    ui.fig_mgr->process_pending();
    EXPECT_EQ(ui.fig_mgr->count(), 0u);
    EXPECT_EQ(window_close_requests_, 0);
}

TEST(OverlaySnapshot, CaptureKeepsOnlyTheFiguresOwnOverlays)
{
    ui::ThemeManager theme;   // annotations pick their colour from the theme
    ui::ThemeManager::set_current(&theme);
    theme.ensure_initialized();
    struct ResetTheme
    {
        ~ResetTheme() { ui::ThemeManager::set_current(nullptr); }
    } reset_theme;

    auto fig_a = figure_with_line();
    auto fig_b = figure_with_line();
    fig_b->subplot(1, 1, 1);
    const Axes*   ax_a = fig_a->axes()[0].get();
    const Axes*   ax_b = fig_b->axes()[0].get();
    const Series* s_a  = ax_a->series()[0].get();
    const Series* s_b  = ax_b->series()[0].get();

    DataInteraction di;
    di.add_marker(1.0, 2.0, s_a, 1, ax_a);
    di.add_marker(3.0, 4.0, s_b, 3, ax_b);
    di.add_marker(5.0, 6.0, s_b, 5, ax_b);
    di.annotations().add(1.0f, 1.0f, ax_a);
    di.annotations().annotations_mut().back().text = "peak A";
    di.annotations().add(2.0f, 2.0f, ax_b);
    di.annotations().annotations_mut().back().text = "peak B";
    for (auto& a : di.annotations().annotations_mut())
        a.editing = false;

    const OverlaySnapshot a = di.capture_overlay_snapshot(*fig_a);
    const OverlaySnapshot b = di.capture_overlay_snapshot(*fig_b);
    ASSERT_EQ(a.markers.size(), 1u);
    EXPECT_DOUBLE_EQ(a.markers[0].data_x, 1.0);
    ASSERT_EQ(b.markers.size(), 2u);
    ASSERT_EQ(a.annotations.size(), 1u);
    EXPECT_EQ(a.annotations[0].text, "peak A");
    ASSERT_EQ(b.annotations.size(), 1u);
    EXPECT_EQ(b.annotations[0].text, "peak B");

    // Opening a two-figure workspace appends each figure's overlays.
    DataInteraction opened;
    opened.append_overlay_snapshot(a, *fig_a);
    opened.append_overlay_snapshot(b, *fig_b);
    EXPECT_EQ(opened.capture_overlay_snapshot(*fig_a).markers.size(), 1u);
    EXPECT_EQ(opened.capture_overlay_snapshot(*fig_b).markers.size(), 2u);
    EXPECT_EQ(opened.capture_overlay_snapshot(*fig_b).annotations.size(), 1u);
}

TEST_F(FileModelFixture, EditingDataTipsAndAnnotationsMarksTheDocument)
{
    const FigureId id = registry_.register_figure(figure_with_line());
    auto&          ui = build(id);
    ASSERT_TRUE(ui.data_interaction);
    ui.document.detect_changes();   // baseline
    ASSERT_FALSE(ui.document.is_modified());

    Figure*       fig = registry_.get(id);
    const Axes*   ax  = fig->axes()[0].get();
    const Series* s   = ax->series()[0].get();
    ui.data_interaction->add_marker(1.0, 2.0, s, 1, ax);
    ui.document.detect_changes();
    EXPECT_TRUE(ui.document.is_figure_modified(id));

    ASSERT_TRUE(ui.document.save_to((config_dir_ / "review.spectra").string()));
    ASSERT_FALSE(ui.document.is_modified());
    ui.data_interaction->annotations().add(1.0f, 1.0f, ax);
    auto& ann   = ui.data_interaction->annotations().annotations_mut().back();
    ann.text    = "note";
    ann.editing = false;
    ui.document.detect_changes();
    EXPECT_TRUE(ui.document.is_modified());

    // A save writes them, so the saved document is clean again.
    ASSERT_TRUE(ui.document.save_to((config_dir_ / "review.spectra").string()));
    ui.document.detect_changes();
    EXPECT_FALSE(ui.document.is_modified());
}

// ─── Start page (#163) reconciliation ────────────────────────────────────────

TEST_F(FileModelFixture, OpenPathAndOpenRecentShareTheFileModel)
{
    const fs::path dir = fresh_dir("open_path");
    const FigureId id  = registry_.register_figure(figure_with_line());
    auto&          ui  = build(id, true);
    ASSERT_NE(ui.imgui_ui, nullptr);
    ui::settings::SettingsStore store;
    ui.imgui_ui->set_settings_store(&store);

    // Saved workspace: listed by Open Recent next to recent data files.
    const std::string doc_path = (dir / "run.spectra").string();
    ASSERT_TRUE(ui.document.save_to(doc_path));
    const std::string csv_path = (dir / "data.csv").string();
    std::ofstream(csv_path) << "t,v\n0,1\n";
    store.add_recent_file(csv_path);

    auto& bar = ui.app_shell->menu_bar();
    ui.app_shell->ensure_initialized();
    ui::shell::SpectraAppShell::sync_recent_layouts_menu(bar, ui.imgui_ui.get());
    std::vector<std::string> recent_labels;
    for (const auto& item : bar.menu("File").items())
        if (item.label == ui::shell::SpectraAppShell::RECENT_LAYOUTS_MENU)
            for (const auto& sub : item.submenu)
                recent_labels.push_back(sub.label);
    ASSERT_GE(recent_labels.size(), 2u);
    EXPECT_EQ(recent_labels[0], "data.csv");
    EXPECT_EQ(recent_labels[1], "run.spectra");

    // open_path() (start page, command line, drops) goes through the
    // window's handler: a .spectra workspace opens as tabs, with a toast.
    ui.toasts.clear();
    EXPECT_TRUE(ui.imgui_ui->open_path(doc_path));
    EXPECT_EQ(ui.fig_mgr->count(), 2u);
    ASSERT_FALSE(ui.toasts.empty());
    EXPECT_EQ(ui.toasts.toasts().back().kind, ui::ToastKind::Success);

    // A missing file is reported and dropped from the recent list.
    store.add_recent_file((dir / "gone.csv").string());
    ui.toasts.clear();
    EXPECT_FALSE(ui.imgui_ui->open_path((dir / "gone.csv").string()));
    ASSERT_FALSE(ui.toasts.empty());
    EXPECT_EQ(ui.toasts.toasts().back().kind, ui::ToastKind::Error);
    const auto& files = store.data().recent_files;
    EXPECT_EQ(std::find(files.begin(), files.end(), (dir / "gone.csv").string()), files.end());

    ui.imgui_ui->set_settings_store(nullptr);
    fs::remove_all(dir);
}

#endif   // SPECTRA_USE_IMGUI
