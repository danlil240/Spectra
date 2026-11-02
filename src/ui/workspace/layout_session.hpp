#pragma once

// layout_session.hpp — per-window state of opened layout files.
//
// Holds the binding providers used to resolve layout bindings, the
// most-recently-used layout list, and the slots of every opened layout so the
// "Data Bindings" window can show which bindings are bound, waiting for a
// topic, or unresolved, and let the user pick a replacement (another URI, or
// the CSV file to use for a missing file name).

#include <functional>
#include <string>
#include <vector>

#include "binding_resolver.hpp"
#include "layout.hpp"

namespace spectra
{

class FigureRegistry;

class LayoutSession
{
   public:
    // Opens a layout file; returns false (and logs why) on failure.
    using OpenFn = std::function<bool(const std::string& path)>;

    LayoutSession();
    LayoutSession(const LayoutSession&)            = delete;
    LayoutSession& operator=(const LayoutSession&) = delete;

    // ─── Wiring ──────────────────────────────────────────────────────────
    void                  set_figure_registry(FigureRegistry* registry) { registry_ = registry; }
    void                  set_topic_provider(TopicBindingProvider* provider) { topics_ = provider; }
    TopicBindingProvider* topic_provider() const { return topics_; }
    FileCsvBindingProvider& csv_provider() { return csv_; }
    BindingResolver         resolver() { return BindingResolver(topics_, &csv_); }

    void set_open_handler(OpenFn fn) { open_fn_ = std::move(fn); }
    bool open(const std::string& path) { return open_fn_ ? open_fn_(path) : false; }
    // Open @p path at the next poll() (startup --layout, menu clicks).
    void request_open(std::string path) { pending_opens_.push_back(std::move(path)); }
    bool has_pending_open() const { return !pending_opens_.empty(); }
    // Runs requested opens once an open handler is wired.  Called every frame.
    void poll();

    RecentLayouts& recent() { return recent_; }

    // ─── Opened layouts ──────────────────────────────────────────────────
    // Keep the slots of a layout just opened; shows the Data Bindings window
    // when some binding is unresolved.
    void adopt(LayoutOpenResult result);

    const std::vector<LayoutBindingSlot>& slots() const { return slots_; }
    size_t                                unresolved_count() const;

    // Re-resolve slot @p index with @p uri.  False if the series is gone.
    bool rebind(size_t index, const std::string& uri);
    // Use @p path for CSV bindings naming @p file_name and retry every
    // unresolved CSV slot.  Returns how many became bound.
    size_t use_csv_file(const std::string& file_name, const std::string& path);
    // Forget slots whose series no longer exists.
    void prune();

    // Runs @p fn with the saved labels on unresolved series (not the
    // "[unresolved: ...]" legend text), e.g. while capturing a layout to save.
    void with_saved_labels(const std::function<void()>& fn);

    // ─── UI ──────────────────────────────────────────────────────────────
    bool is_visible() const { return visible_; }
    void set_visible(bool v) { visible_ = v; }
    // Draws the Data Bindings window.  No-op unless visible.
    void draw();

    std::string last_dir;   // directory of the last layout dialog
    // Live topic names offered as replacements (e.g. from the Topics panel).
    std::function<std::vector<std::string>()> topic_names_fn;

   private:
    void pick_csv_for_selected();

    FigureRegistry*                registry_ = nullptr;
    TopicBindingProvider*          topics_   = nullptr;
    FileCsvBindingProvider         csv_;
    RecentLayouts                  recent_;
    OpenFn                         open_fn_;
    std::vector<std::string>       pending_opens_;
    std::vector<LayoutBindingSlot> slots_;
    bool                           visible_  = false;
    int                            selected_ = -1;
    char                           edit_buf_[512]{};
    std::string                    status_;
};

}   // namespace spectra
