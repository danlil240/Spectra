#pragma once

// toast_queue.hpp — transient notifications ("Saved analysis.spectra",
// "Could not export: permission denied") shown as a stack in the bottom-right
// corner of the window (ImGuiIntegration::draw_toasts()).
//
// Info and success toasts dismiss themselves after a few seconds; error
// toasts stay until clicked.  The queue is plain data so it can be unit
// tested and filled from command callbacks without ImGui.

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <string>

namespace spectra::ui
{

enum class ToastKind
{
    Info,
    Success,
    Warning,
    Error,
};

struct Toast
{
    uint64_t    id   = 0;
    ToastKind   kind = ToastKind::Info;
    std::string title;
    std::string detail;            // optional second line (path, reason)
    float       age      = 0.0f;   // seconds shown
    float       lifetime = 0.0f;   // seconds; 0 = until dismissed
    // Optional action button ("Show", "Undo"); runs then dismisses the toast.
    std::string           action_label;
    std::function<void()> action;
};

class ToastQueue
{
   public:
    static constexpr float  DEFAULT_LIFETIME = 4.0f;
    static constexpr size_t MAX_VISIBLE      = 5;

    uint64_t push(ToastKind kind, std::string title, std::string detail = {});
    uint64_t info(std::string title, std::string detail = {})
    {
        return push(ToastKind::Info, std::move(title), std::move(detail));
    }
    uint64_t success(std::string title, std::string detail = {})
    {
        return push(ToastKind::Success, std::move(title), std::move(detail));
    }
    uint64_t warning(std::string title, std::string detail = {})
    {
        return push(ToastKind::Warning, std::move(title), std::move(detail));
    }
    uint64_t error(std::string title, std::string detail = {})
    {
        return push(ToastKind::Error, std::move(title), std::move(detail));
    }

    // Adds an action button to toast @p id.  False if it is gone.
    bool set_action(uint64_t id, std::string label, std::function<void()> action);

    // Ages every toast by @p dt seconds and drops expired ones.
    void update(float dt);
    // Removes toast @p id (click on it).  False if it is gone.
    bool dismiss(uint64_t id);
    // Runs toast @p id's action, then dismisses it.
    bool activate(uint64_t id);
    void clear() { toasts_.clear(); }

    // Oldest first.  At most MAX_VISIBLE: pushing more drops the oldest
    // self-dismissing toast (errors are dropped only when all are errors).
    const std::deque<Toast>& toasts() const { return toasts_; }
    bool                     empty() const { return toasts_.empty(); }
    size_t                   size() const { return toasts_.size(); }

    // Error toasts persist; others use DEFAULT_LIFETIME.
    static float lifetime_for(ToastKind kind);

    // Short detail line for a file toast: the folder that holds `path`.
    static std::string folder_detail(const std::string& path);

    // Exports are written a frame or two after the request (PNG readback,
    // SVG/PDF on the render loop).  Watching the file turns the outcome into
    // a toast: success once @p path is (re)written, an error if it is not
    // within WRITE_TIMEOUT seconds.  update() checks.
    static constexpr float WRITE_TIMEOUT = 8.0f;
    void   watch_file_write(std::string path, std::string success_title, std::string error_title);
    size_t watched_count() const { return watches_.size(); }

    // True while a toast is fading in, counting down to dismissal, or a file
    // write is being watched: the event-driven loop must keep drawing frames.
    static constexpr float FADE_IN = 0.15f;
    bool                   needs_frames() const;

   private:
    struct Watch
    {
        std::string                     path;
        std::string                     success_title;
        std::string                     error_title;
        std::filesystem::file_time_type requested;
        bool                            existed = false;
        float                           age     = 0.0f;
    };
    void poll_watches(float dt);

    std::deque<Toast> toasts_;
    std::deque<Watch> watches_;
    uint64_t          next_id_ = 1;
};

}   // namespace spectra::ui
