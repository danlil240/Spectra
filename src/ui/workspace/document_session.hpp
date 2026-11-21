#pragma once

// document_session.hpp — one window's "file": the .spectra path it was last
// saved to or opened from, whether it changed since (DocumentTracker), the
// Save / Don't Save / Cancel prompt shown before closing a modified window
// or figure, and the crash-recovery autosave.
//
// The session holds no ImGui or figure code: the window wires callbacks
// that snapshot, encode and write its figures (commands_file.cpp), and
// ImGuiIntegration draws the prompt and toasts.

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "document_tracker.hpp"
#include "workspace_autosave.hpp"

namespace spectra
{

namespace ui
{
class ToastQueue;
}

// Save / Don't Save / Cancel before an action that would lose changes.
class UnsavedChangesPrompt
{
   public:
    enum class Reason
    {
        Quit,          // window close button, File > Exit, closing the last tab
        CloseFigure,   // Ctrl+W / Q on a modified figure
        Open,          // replacing the window contents
    };
    enum class Choice
    {
        Save,
        DontSave,
        Cancel,
    };

    // Runs @p proceed right away when @p modified is false (returns true);
    // otherwise opens the prompt and returns false.  A request while the
    // prompt is already open is ignored (returns false).
    // @p subject names what closes in the headline, e.g. "\"Run 7\"" or
    // "3 figures" (empty: the active figure).
    bool request(Reason                reason,
                 bool                  modified,
                 std::function<void()> proceed,
                 std::string           subject = {});

    bool               is_open() const { return open_; }
    Reason             reason() const { return reason_; }
    const std::string& subject() const { return subject_; }

    // Save: runs @p save; proceeds only if it returns true (a cancelled
    // Save As dialog or a failed write keeps everything as it was).
    // Don't Save: proceeds.  Cancel: forgets the request.
    // Returns true if the pending action ran.
    bool resolve(Choice choice, const std::function<bool()>& save = {});

   private:
    bool                  open_   = false;
    Reason                reason_ = Reason::Quit;
    std::function<void()> proceed_;
    std::string           subject_;
};

class DocumentSession
{
   public:
    using SnapshotFn = std::function<DocumentTracker::Snapshot()>;
    // Encodes the window as a workspace document (WorkspaceDocument bytes).
    using EncodeFn = std::function<std::string()>;
    // Native save dialog: returns the chosen path or nullopt (cancelled).
    using AskSavePathFn = std::function<std::optional<std::string>(const std::string& suggested)>;
    // Opens a document file into the window (used by recovery).
    using OpenFn = std::function<bool(const std::string& path)>;

    DocumentSession();
    ~DocumentSession();

    DocumentSession(const DocumentSession&)            = delete;
    DocumentSession& operator=(const DocumentSession&) = delete;

    // ─── Wiring ──────────────────────────────────────────────────────────
    void            set_snapshot_fn(SnapshotFn fn) { snapshot_fn_ = std::move(fn); }
    void            set_encode_fn(EncodeFn fn);
    void            set_ask_save_path_fn(AskSavePathFn fn) { ask_save_path_fn_ = std::move(fn); }
    void            set_open_fn(OpenFn fn) { open_fn_ = std::move(fn); }
    void            set_toasts(ui::ToastQueue* toasts) { toasts_ = toasts; }
    ui::ToastQueue* toasts() const { return toasts_; }
    // Called after every successful save (e.g. to update the recent list).
    void set_on_saved(std::function<void(const std::string&)> fn) { on_saved_ = std::move(fn); }

    // ─── File ────────────────────────────────────────────────────────────
    const std::string& path() const { return path_; }
    void               set_path(std::string path) { path_ = std::move(path); }
    // File name of path(), or "Untitled".
    std::string display_name() const;
    // Suggested file name for Save As.
    std::string suggested_file_name() const;

    // File > Save: writes to path() or asks for one (Save As).
    bool save();
    // File > Save As...: always asks.
    bool save_as();
    // Writes the document to @p path; on success it becomes path() and the
    // document is clean.  Shows a toast either way.
    bool save_to(const std::string& path);

    // The window was just filled from @p path (File > Open into an empty
    // window): it becomes path() and the document is clean.
    void adopt_opened(const std::string& path);

    // ─── Change tracking (called by the frame loop) ──────────────────────
    void detect_changes();
    void absorb_external_changes();
    void mark_modified();
    bool is_modified() const { return tracker_.is_modified(); }
    bool is_figure_modified(FigureId id) const { return tracker_.is_figure_modified(id); }
    DocumentTracker& tracker() { return tracker_; }

    // ─── Closing ─────────────────────────────────────────────────────────
    UnsavedChangesPrompt& prompt() { return prompt_; }
    // Window close request: true = close now.  Modified: opens the prompt
    // and returns false; once the user picks Save (and it succeeds) or Don't
    // Save, @p close_again runs and the next call returns true.
    bool confirm_close_window(std::function<void()> close_again);
    // Resolves the open prompt (Save runs save()).
    bool resolve_prompt(UnsavedChangesPrompt::Choice choice);

    // ─── Autosave and recovery ───────────────────────────────────────────
    // Starts writing <dir>/autosave-<pid>-<token>.spectra while modified.
    void                     enable_autosave(const std::string& dir);
    bool                     autosave_enabled() const { return autosave_enabled_; }
    WorkspaceAutosave&       autosave() { return autosave_; }
    const WorkspaceAutosave& autosave() const { return autosave_; }
    // Call every frame: writes the autosave when due.
    void tick_autosave();
    // Removes this window's autosave file (clean close, saved, discarded).
    void discard_autosave();

    // Autosave files in @p dir left behind by processes that are no longer
    // running (crash / kill).  @p is_alive defaults to a process check.
    static std::vector<std::string> find_orphan_autosaves(
        const std::string&                  dir,
        const std::function<bool(int64_t)>& is_alive = {});
    // Parses the pid out of "autosave-<pid>-<token>.spectra" (-1 if not one).
    static int64_t autosave_pid(const std::string& file_name);
    static bool    process_alive(int64_t pid);
    // <config dir>/autosave/<program>: each program (spectra, spectra-px4, an
    // example, ...) recovers only its own work, never another program's.
    static std::string default_autosave_dir();
    static std::string autosave_dir_for(std::string_view program);
    // Executable base name without extension, reduced to [A-Za-z0-9._-]
    // ("spectra" when unknown).
    static std::string current_program_name();

    // Looks for orphaned autosaves once (claims them so another window does
    // not offer the same ones).  Returns true if recovery should be offered.
    bool                            check_recovery();
    const std::vector<std::string>& recovery_files() const { return recovery_files_; }
    bool                            recovery_pending() const { return !recovery_files_.empty(); }
    // Opens every recovery file (as unsaved changes) and deletes it.
    size_t restore_recovery();
    void   discard_recovery();

   private:
    DocumentTracker::Snapshot snapshot() const;
    void                      toast_error(const std::string& title, const std::string& detail);

    DocumentTracker          tracker_;
    UnsavedChangesPrompt     prompt_;
    WorkspaceAutosave        autosave_;
    std::string              path_;
    std::string              autosave_dir_;
    bool                     autosave_enabled_ = false;
    bool                     close_confirmed_  = false;
    bool                     recovery_checked_ = false;
    bool                     last_modified_    = false;
    std::vector<std::string> recovery_files_;

    SnapshotFn                              snapshot_fn_;
    EncodeFn                                encode_fn_;
    AskSavePathFn                           ask_save_path_fn_;
    OpenFn                                  open_fn_;
    ui::ToastQueue*                         toasts_ = nullptr;
    std::function<void(const std::string&)> on_saved_;
};

}   // namespace spectra
