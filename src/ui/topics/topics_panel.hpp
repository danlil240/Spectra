// topics_panel.hpp — Spectra Topics discovery panel.
//
// Phase 2 of plans/SPECTRA_TOPICS_PLAN.md.
//
// A dockable ImGui panel that lists every topic currently known to the
// daemon's TopicRegistry. Users can:
//   - See live Hz, sample count and publisher-online status per topic
//   - Drag a topic row onto a figure (drop target supplied elsewhere)
//   - Click a "Subscribe" button to subscribe the active figure / axes 0
//
// The panel is transport-agnostic: it never talks to IPC directly.  Wiring is
// done through three small callback hooks:
//   - request_list:        called when the panel wants a fresh snapshot
//   - request_subscribe:   called when the user commits a subscription
// And one push setter:
//   - set_topics(...):     called by the transport layer with the latest list
//
// In multi-process mode the agent (src/agent/main.cpp) provides the
// callbacks and sends/handles the corresponding IPC messages.

#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "../../ipc/message.hpp"

namespace spectra::ui::topics
{

// ImGui drag-and-drop payload type id used by topic rows.
inline constexpr const char* TOPIC_DRAG_TYPE = "SPECTRA_TOPIC";

// Lightweight, fixed-size payload safely passed through ImGui drag/drop.
struct TopicDragPayload
{
    // Topic name (null-terminated, truncated at 127 bytes).
    char     name[128];
    uint32_t kind;   // matches ipc::TopicKind
};

// A subscription request emitted by the panel UI.
struct SubscribeRequest
{
    std::string topic_name;
    uint64_t    figure_id    = 0;
    uint32_t    axes_index   = 0;
    uint32_t    series_index = 0xFFFFFFFFu;   // auto-create
};

class TopicsPanel
{
   public:
    using ListRequestFn      = std::function<void()>;
    using SubscribeRequestFn = std::function<void(const SubscribeRequest&)>;

    // ─── Wiring ──────────────────────────────────────────────────────────

    void set_list_request_callback(ListRequestFn cb) { list_cb_ = std::move(cb); }
    void set_subscribe_request_callback(SubscribeRequestFn cb) { subscribe_cb_ = std::move(cb); }

    // Called by the transport layer with the freshest topic list.
    // Safe to call from any thread.
    void set_topics(std::vector<ipc::TopicInfoEntry> topics);

    // ─── UI state ────────────────────────────────────────────────────────

    bool is_visible() const { return visible_; }
    void set_visible(bool v) { visible_ = v; }

    // Set the figure id that "Subscribe" buttons should target.  Typically
    // wired to the per-window active figure id, updated each frame.
    void     set_target_figure_id(uint64_t id) { target_figure_id_ = id; }
    uint64_t target_figure_id() const { return target_figure_id_; }

    // True iff a subscribe callback has been wired (used to gate UI affordances
    // such as the canvas drop target when the IPC layer isn't available).
    bool has_subscribe_callback() const { return static_cast<bool>(subscribe_cb_); }

    // ─── Render ──────────────────────────────────────────────────────────

    // Draw the panel.  No-op when SPECTRA_USE_IMGUI is not defined.
    // Returns true if the user committed a subscribe action this frame.
    bool draw();

    // ─── Drop-target helpers (for callers integrating drop zones) ───────

    // Build a drag payload from a topic info entry.
    static TopicDragPayload make_payload(const ipc::TopicInfoEntry& t);

    // Issue a subscribe action programmatically (used by drop handlers).
    // Returns false if no subscribe callback is wired.
    bool submit_subscribe(const std::string& topic_name, uint64_t figure_id, uint32_t axes_index);

   private:
    mutable std::mutex               mutex_;
    std::vector<ipc::TopicInfoEntry> topics_;
    bool                             visible_          = false;
    uint64_t                         target_figure_id_ = 0;
    uint64_t                         last_list_req_ms_ = 0;
    char                             filter_buf_[64]   = {};

    ListRequestFn      list_cb_;
    SubscribeRequestFn subscribe_cb_;
};

}   // namespace spectra::ui::topics
