#include "document_session.hpp"

#include <algorithm>
#include <filesystem>
#include <random>
#include <system_error>

#include "core/config_paths.hpp"
#include "spectra/logger.hpp"
#include "ui/overlay/toast_queue.hpp"
#include "workspace_document.hpp"

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <process.h>
    #include <windows.h>
#else
    #include <cerrno>
    #include <csignal>
    #include <unistd.h>
#endif

namespace spectra
{

namespace fs = std::filesystem;

namespace
{

int64_t current_pid()
{
#ifdef _WIN32
    return static_cast<int64_t>(_getpid());
#else
    return static_cast<int64_t>(::getpid());
#endif
}

std::string random_token()
{
    std::random_device              rd;
    std::uniform_int_distribution<> dist(0, 15);
    std::string                     token;
    for (int i = 0; i < 8; ++i)
        token += "0123456789abcdef"[dist(rd)];
    return token;
}

constexpr const char* SPECTRA_EXT = ".spectra";

}   // namespace

// ─── UnsavedChangesPrompt ────────────────────────────────────────────────────

bool UnsavedChangesPrompt::request(Reason reason, bool modified, std::function<void()> proceed)
{
    if (open_)
        return false;
    if (!modified)
    {
        if (proceed)
            proceed();
        return true;
    }
    open_    = true;
    reason_  = reason;
    proceed_ = std::move(proceed);
    return false;
}

bool UnsavedChangesPrompt::resolve(Choice choice, const std::function<bool()>& save)
{
    if (!open_)
        return false;
    auto proceed = std::move(proceed_);
    proceed_     = nullptr;
    open_        = false;
    switch (choice)
    {
        case Choice::Cancel:
            return false;
        case Choice::Save:
            if (!save || !save())
                return false;
            break;
        case Choice::DontSave:
            break;
    }
    if (proceed)
        proceed();
    return true;
}

// ─── DocumentSession ─────────────────────────────────────────────────────────

DocumentSession::DocumentSession()
{
    // Off until enable_autosave(): tests and headless windows write nothing.
    autosave_.set_autosave_path({});
    autosave_.set_interval(std::chrono::seconds(60));
    autosave_.set_debounce(std::chrono::seconds(5));
}

DocumentSession::~DocumentSession()
{
    // A window closed with nothing unsaved needs no recovery file.  One that
    // goes away modified without the prompt (signal, backend exit) keeps it.
    if (!tracker_.is_modified())
        discard_autosave();
}

void DocumentSession::set_encode_fn(EncodeFn fn)
{
    encode_fn_ = std::move(fn);
    autosave_.set_serialize_fn(encode_fn_);
}

std::string DocumentSession::display_name() const
{
    if (path_.empty())
        return "Untitled";
    return fs::path(path_).filename().string();
}

std::string DocumentSession::suggested_file_name() const
{
    if (path_.empty())
        return std::string("untitled") + SPECTRA_EXT;
    return fs::path(path_).filename().string();
}

DocumentTracker::Snapshot DocumentSession::snapshot() const
{
    return snapshot_fn_ ? snapshot_fn_() : DocumentTracker::Snapshot{};
}

void DocumentSession::toast_error(const std::string& title, const std::string& detail)
{
    SPECTRA_LOG_WARN("document", "{}: {}", title, detail);
    if (toasts_)
        toasts_->error(title, detail);
}

bool DocumentSession::save()
{
    if (path_.empty())
        return save_as();
    return save_to(path_);
}

bool DocumentSession::save_as()
{
    if (!ask_save_path_fn_)
        return false;
    auto chosen = ask_save_path_fn_(suggested_file_name());
    if (!chosen || chosen->empty())
        return false;
    std::string path = *chosen;
    if (!fs::path(path).has_extension())
        path += SPECTRA_EXT;
    return save_to(path);
}

bool DocumentSession::save_to(const std::string& path)
{
    const std::string name = fs::path(path).filename().string();
    if (!encode_fn_)
    {
        toast_error("Could not save " + name, "nothing to save in this window");
        return false;
    }
    std::string bytes = encode_fn_();
    std::string error;
    if (!WorkspaceDocument::write_file(path, bytes, &error))
    {
        toast_error("Could not save " + name, error);
        return false;
    }
    path_ = path;
    tracker_.mark_saved(snapshot());
    discard_autosave();
    SPECTRA_LOG_INFO("document", "Saved {}", path);
    if (toasts_)
        toasts_->success("Saved " + name, ui::ToastQueue::folder_detail(path));
    if (on_saved_)
        on_saved_(path);
    return true;
}

void DocumentSession::adopt_opened(const std::string& path)
{
    path_ = path;
    tracker_.mark_saved(snapshot());
    discard_autosave();
}

void DocumentSession::detect_changes()
{
    if (!snapshot_fn_)
        return;
    const auto now = snapshot();
    if (now.figures.empty())
    {
        // Every figure is gone (closed with "Don't Save", or never made):
        // nothing is left to lose, so quitting must not ask.
        if (tracker_.is_modified())
        {
            tracker_.mark_saved(now);
            discard_autosave();
        }
        return;
    }
    if (tracker_.detect(now))
        autosave_.mark_dirty();
}

void DocumentSession::absorb_external_changes()
{
    if (!snapshot_fn_)
        return;
    tracker_.absorb(snapshot());
}

void DocumentSession::mark_modified()
{
    tracker_.mark_modified();
    autosave_.mark_dirty();
}

bool DocumentSession::confirm_close_window(std::function<void()> close_again)
{
    if (close_confirmed_ || !tracker_.is_modified())
        return true;
    prompt_.request(UnsavedChangesPrompt::Reason::Quit,
                    true,
                    [this, close_again = std::move(close_again)]()
                    {
                        close_confirmed_ = true;
                        if (close_again)
                            close_again();
                    });
    return false;
}

bool DocumentSession::resolve_prompt(UnsavedChangesPrompt::Choice choice)
{
    const bool discard = choice == UnsavedChangesPrompt::Choice::DontSave;
    const bool ran     = prompt_.resolve(choice, [this]() { return save(); });
    if (ran && discard)
        discard_autosave();
    return ran;
}

// ─── Autosave ────────────────────────────────────────────────────────────────

std::string DocumentSession::default_autosave_dir()
{
    fs::path dir = user_config_dir();
    if (dir.empty())
        dir = fs::path(".") / ".config" / "spectra";
    return (dir / "autosave").string();
}

void DocumentSession::enable_autosave(const std::string& dir)
{
    autosave_dir_ = dir;
    const std::string file =
        "autosave-" + std::to_string(current_pid()) + "-" + random_token() + SPECTRA_EXT;
    autosave_.set_autosave_path((fs::path(dir) / file).string());
    autosave_enabled_ = true;
}

void DocumentSession::tick_autosave()
{
    if (!autosave_enabled_ || !tracker_.is_modified())
        return;
    autosave_.tick();
}

void DocumentSession::discard_autosave()
{
    autosave_.clear_dirty();
    if (!autosave_enabled_ || autosave_.autosave_path().empty())
        return;
    std::error_code ec;
    fs::remove(autosave_.autosave_path(), ec);
}

int64_t DocumentSession::autosave_pid(const std::string& file_name)
{
    std::string_view name(file_name);
    for (std::string_view prefix : {std::string_view("autosave-"), std::string_view("recovering-")})
    {
        if (name.substr(0, prefix.size()) != prefix)
            continue;
        name.remove_prefix(prefix.size());
        if (name.size() < std::char_traits<char>::length(SPECTRA_EXT)
            || name.substr(name.size() - std::char_traits<char>::length(SPECTRA_EXT))
                   != SPECTRA_EXT)
            return -1;
        int64_t pid    = 0;
        size_t  digits = 0;
        while (digits < name.size() && name[digits] >= '0' && name[digits] <= '9' && digits < 18)
        {
            pid = pid * 10 + (name[digits] - '0');
            ++digits;
        }
        if (digits == 0 || digits >= name.size() || name[digits] != '-')
            return -1;
        return pid;
    }
    return -1;
}

bool DocumentSession::process_alive(int64_t pid)
{
    if (pid <= 0)
        return false;
#ifdef _WIN32
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h)
        return false;
    DWORD code = 0;
    BOOL  ok   = GetExitCodeProcess(h, &code);
    CloseHandle(h);
    return ok && code == STILL_ACTIVE;
#else
    if (::kill(static_cast<pid_t>(pid), 0) == 0)
        return true;
    return errno == EPERM;
#endif
}

std::vector<std::string> DocumentSession::find_orphan_autosaves(
    const std::string&                  dir,
    const std::function<bool(int64_t)>& is_alive)
{
    std::vector<std::pair<fs::file_time_type, std::string>> found;
    std::error_code                                         ec;
    if (dir.empty() || !fs::is_directory(dir, ec))
        return {};
    const int64_t self = current_pid();
    for (const auto& entry : fs::directory_iterator(dir, ec))
    {
        if (!entry.is_regular_file(ec))
            continue;
        const std::string name = entry.path().filename().string();
        const int64_t     pid  = autosave_pid(name);
        if (pid < 0 || pid == self)
            continue;
        const bool alive = is_alive ? is_alive(pid) : process_alive(pid);
        if (alive)
            continue;
        found.emplace_back(entry.last_write_time(ec), entry.path().string());
    }
    std::sort(found.begin(),
              found.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<std::string> out;
    for (auto& f : found)
        out.push_back(std::move(f.second));
    return out;
}

bool DocumentSession::check_recovery()
{
    if (recovery_checked_ || !autosave_enabled_)
        return recovery_pending();
    recovery_checked_ = true;

    const std::string token = random_token();
    size_t            n     = 0;
    for (const auto& file : find_orphan_autosaves(autosave_dir_))
    {
        // Claim the file: rename fails if another window took it first.
        const fs::path claimed = fs::path(autosave_dir_)
                                 / ("recovering-" + std::to_string(current_pid()) + "-" + token
                                    + "x" + std::to_string(n++) + SPECTRA_EXT);
        std::error_code ec;
        fs::rename(file, claimed, ec);
        if (!ec)
            recovery_files_.push_back(claimed.string());
    }
    return recovery_pending();
}

size_t DocumentSession::restore_recovery()
{
    size_t restored = 0;
    for (const auto& file : recovery_files_)
    {
        if (open_fn_ && open_fn_(file))
            ++restored;
        else
            toast_error("Could not restore autosave", file);
        // Opened or unreadable: either way it must not be offered again.
        std::error_code ec;
        fs::remove(file, ec);
    }
    recovery_files_.clear();
    if (restored > 0)
    {
        mark_modified();
        if (toasts_)
            toasts_->success("Restored unsaved work",
                             "Recovered from the last session. Save to keep it.");
    }
    return restored;
}

void DocumentSession::discard_recovery()
{
    for (const auto& file : recovery_files_)
    {
        std::error_code ec;
        fs::remove(file, ec);
    }
    recovery_files_.clear();
}

}   // namespace spectra
