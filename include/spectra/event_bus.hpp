#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <mutex>
#include <spectra/fwd.hpp>
#include <string>
#include <vector>

namespace spectra
{

// ═══════════════════════════════════════════════════════════════════════════════
// EventBus<Event> — Lightweight typed observer for cross-subsystem notifications
//
// Each event type gets its own EventBus instance.  Subscribers register
// callbacks via subscribe() and receive a SubscriptionId for later
// unsubscribe().  emit() invokes all registered callbacks synchronously
// on the calling thread.
//
// Features:
//   - Re-entrancy guard: nested emit() calls queue events and drain after the
//     outer emission completes, preventing infinite recursion.
//   - Cross-thread deferred emission: emit_deferred() enqueues events from any
//     thread.  The main thread drains them via drain_deferred().
//   - Priority tiers: subscribers specify High, Normal, or Low priority.
//     Callbacks run in priority order within each emission.
//   - ScopedSubscription: RAII wrapper that auto-unsubscribes on destruction.
//
// Thread safety: subscribe/unsubscribe/emit must happen on the main thread.
// Only emit_deferred() and drain_deferred() are thread-safe.
//
// Usage:
//   EventBus<FigureCreatedEvent> bus;
//   auto id = bus.subscribe([](const FigureCreatedEvent& e) { ... });
//   bus.emit(FigureCreatedEvent{figure_id});
//   bus.unsubscribe(id);
// ═══════════════════════════════════════════════════════════════════════════════

using SubscriptionId = uint64_t;

// Priority tiers for event subscribers.
enum class Priority : uint8_t
{
    High   = 0,   // ViewModel state updates, data model sync
    Normal = 1,   // General UI refresh, default
    Low    = 2,   // Logging, analytics, non-critical side effects
};

template <typename Event>
class EventBus
{
   public:
    using Callback = std::function<void(const Event&)>;

    EventBus()  = default;
    ~EventBus() = default;

    EventBus(const EventBus&)            = delete;
    EventBus& operator=(const EventBus&) = delete;

    // Register a callback with default (Normal) priority.
    SubscriptionId subscribe(Callback cb) { return subscribe(std::move(cb), Priority::Normal); }

    // Register a callback with explicit priority.
    SubscriptionId subscribe(Callback cb, Priority priority)
    {
        SubscriptionId id = next_id_++;
        subs_.push_back({id, std::move(cb), priority});
        sort_subs();
        return id;
    }

    // Remove a previously registered callback.  No-op if id is invalid.
    void unsubscribe(SubscriptionId id)
    {
        if (emitting_)
        {
            // Defer removal to avoid invalidating the iteration in emit().
            pending_unsubs_.push_back(id);
            return;
        }
        erase_sub(id);
    }

    // Invoke all registered callbacks with the given event.
    // If called re-entrantly (from within a callback), the event is queued
    // and dispatched after the outer emission completes.
    void emit(const Event& event)
    {
        if (emitting_)
        {
            // Re-entrant emit: queue for later.
            reentrant_queue_.push_back(event);
            return;
        }

        emitting_ = true;
        dispatch(event);

        // Drain any events that were queued by re-entrant emit() calls.
        while (!reentrant_queue_.empty())
        {
            // Move the queue out so further re-entrant pushes go into a fresh queue.
            std::vector<Event> queued;
            queued.swap(reentrant_queue_);
            for (const auto& queued_event : queued)
                dispatch(queued_event);
        }

        emitting_ = false;

        // Process deferred unsubscribes.
        for (auto uid : pending_unsubs_)
            erase_sub(uid);
        pending_unsubs_.clear();
    }

    // Returns the number of active subscriptions.
    size_t subscriber_count() const { return subs_.size(); }

    // ── Thread-safe deferred emission ────────────────────────────────
    // Producer threads call emit_deferred() to queue events for later
    // main-thread delivery.  The main thread calls drain_deferred() at
    // the start of each tick to process the queue.

    void emit_deferred(const Event& event)
    {
        std::lock_guard<std::mutex> lock(deferred_mutex_);
        deferred_queue_.push_back(event);
    }

    void emit_deferred(Event&& event)
    {
        std::lock_guard<std::mutex> lock(deferred_mutex_);
        deferred_queue_.push_back(std::move(event));
    }

    // Drain all deferred events (main thread only).  Each event is
    // dispatched synchronously via emit().
    void drain_deferred()
    {
        std::vector<Event> batch;
        {
            std::lock_guard<std::mutex> lock(deferred_mutex_);
            if (deferred_queue_.empty())
                return;
            batch.swap(deferred_queue_);
        }
        for (const auto& event : batch)
            emit(event);
    }

   private:
    struct Sub
    {
        SubscriptionId id;
        Callback       cb;
        Priority       priority = Priority::Normal;
    };

    void dispatch(const Event& event)
    {
        for (auto& sub : subs_)
        {
            if (sub.cb)
                sub.cb(event);
        }
    }

    void sort_subs()
    {
        std::stable_sort(
            subs_.begin(),
            subs_.end(),
            [](const Sub& a, const Sub& b)
            { return static_cast<uint8_t>(a.priority) < static_cast<uint8_t>(b.priority); });
    }

    void erase_sub(SubscriptionId id)
    {
        subs_.erase(
            std::remove_if(subs_.begin(), subs_.end(), [id](const Sub& s) { return s.id == id; }),
            subs_.end());
    }

    std::vector<Sub>            subs_;
    std::vector<SubscriptionId> pending_unsubs_;
    std::vector<Event>          reentrant_queue_;
    SubscriptionId              next_id_  = 1;
    bool                        emitting_ = false;

    // Cross-thread deferred emission queue.
    std::mutex         deferred_mutex_;
    std::vector<Event> deferred_queue_;
};

// ═══════════════════════════════════════════════════════════════════════════════
// Event types — lightweight POD structs carrying notification payloads
// ═══════════════════════════════════════════════════════════════════════════════

// ── Figure lifecycle ─────────────────────────────────────────────────────────

struct FigureCreatedEvent
{
    FigureId figure_id;
    Figure*  figure;
};

struct FigureDestroyedEvent
{
    FigureId figure_id;
};

// ── Series data / style changes ──────────────────────────────────────────────

struct SeriesDataChangedEvent
{
    AxesBase* axes;   // Owning axes (may be nullptr if unknown)
    Series*   series;
};

struct SeriesAddedEvent
{
    AxesBase* axes;
    Series*   series;
};

struct SeriesRemovedEvent
{
    AxesBase*     axes;
    const Series* series;   // Pointer valid during callback only.
};

// ── Axes changes ─────────────────────────────────────────────────────────────

struct AxesLimitsChangedEvent
{
    Axes*  axes;
    double x_min;
    double x_max;
    double y_min;
    double y_max;
};

// ── Theme changes ────────────────────────────────────────────────────────────

struct ThemeChangedEvent
{
    std::string theme_name;
};

// ── Window lifecycle ─────────────────────────────────────────────────────────

struct WindowOpenedEvent
{
    uint32_t window_id;
};

struct WindowClosedEvent
{
    uint32_t window_id;
};

// ── Animation lifecycle ──────────────────────────────────────────────────────

struct AnimationStartedEvent
{
    FigureId figure_id;
};

struct AnimationStoppedEvent
{
    FigureId figure_id;
};

// ── Export lifecycle ─────────────────────────────────────────────────────────

struct ExportCompletedEvent
{
    FigureId    figure_id;
    std::string path;   // Output file path.
};

// ── Plugin lifecycle ─────────────────────────────────────────────────────────

struct PluginLoadedEvent
{
    std::string plugin_name;
};

struct PluginUnloadedEvent
{
    std::string plugin_name;
};

// ═══════════════════════════════════════════════════════════════════════════════
// ScopedSubscription — RAII wrapper for automatic unsubscribe on destruction
//
// Usage:
//   EventBus<MyEvent> bus;
//   {
//       ScopedSubscription sub(bus, [](const MyEvent& e) { ... });
//       bus.emit(MyEvent{});  // callback fires
//   }
//   bus.emit(MyEvent{});  // callback does NOT fire — auto-unsubscribed
// ═══════════════════════════════════════════════════════════════════════════════

template <typename Event>
class ScopedSubscription
{
   public:
    ScopedSubscription() = default;

    ScopedSubscription(EventBus<Event>&                   bus,
                       typename EventBus<Event>::Callback cb,
                       Priority                           priority = Priority::Normal)
        : bus_(&bus), id_(bus.subscribe(std::move(cb), priority))
    {
    }

    ~ScopedSubscription()
    {
        if (bus_)
            bus_->unsubscribe(id_);
    }

    // Move-only
    ScopedSubscription(ScopedSubscription&& other) noexcept : bus_(other.bus_), id_(other.id_)
    {
        other.bus_ = nullptr;
    }

    ScopedSubscription& operator=(ScopedSubscription&& other) noexcept
    {
        if (this != &other)
        {
            if (bus_)
                bus_->unsubscribe(id_);
            bus_       = other.bus_;
            id_        = other.id_;
            other.bus_ = nullptr;
        }
        return *this;
    }

    ScopedSubscription(const ScopedSubscription&)            = delete;
    ScopedSubscription& operator=(const ScopedSubscription&) = delete;

    // Release ownership without unsubscribing.
    SubscriptionId release()
    {
        bus_ = nullptr;
        return id_;
    }

    SubscriptionId id() const { return id_; }
    bool           active() const { return bus_ != nullptr; }

   private:
    EventBus<Event>* bus_ = nullptr;
    SubscriptionId   id_  = 0;
};

// ═══════════════════════════════════════════════════════════════════════════════
// EventSystem — Aggregate owner for all event buses
//
// A single EventSystem instance lives in App (or SessionRuntime) and is passed
// by pointer to subsystems that need to emit or subscribe to events.
// ═══════════════════════════════════════════════════════════════════════════════

class EventSystem
{
   public:
    EventSystem()  = default;
    ~EventSystem() = default;

    EventSystem(const EventSystem&)            = delete;
    EventSystem& operator=(const EventSystem&) = delete;

    // ── Access individual buses ──────────────────────────────────────────

    EventBus<FigureCreatedEvent>&     figure_created() { return figure_created_; }
    EventBus<FigureDestroyedEvent>&   figure_destroyed() { return figure_destroyed_; }
    EventBus<SeriesDataChangedEvent>& series_data_changed() { return series_data_changed_; }
    EventBus<SeriesAddedEvent>&       series_added() { return series_added_; }
    EventBus<SeriesRemovedEvent>&     series_removed() { return series_removed_; }
    EventBus<AxesLimitsChangedEvent>& axes_limits_changed() { return axes_limits_changed_; }
    EventBus<ThemeChangedEvent>&      theme_changed() { return theme_changed_; }
    EventBus<WindowOpenedEvent>&      window_opened() { return window_opened_; }
    EventBus<WindowClosedEvent>&      window_closed() { return window_closed_; }

    EventBus<AnimationStartedEvent>& animation_started() { return animation_started_; }
    EventBus<AnimationStoppedEvent>& animation_stopped() { return animation_stopped_; }
    EventBus<ExportCompletedEvent>&  export_completed() { return export_completed_; }
    EventBus<PluginLoadedEvent>&     plugin_loaded() { return plugin_loaded_; }
    EventBus<PluginUnloadedEvent>&   plugin_unloaded() { return plugin_unloaded_; }

    // ── Drain all deferred event queues (main thread only) ───────────

    void drain_all_deferred()
    {
        figure_created_.drain_deferred();
        figure_destroyed_.drain_deferred();
        series_data_changed_.drain_deferred();
        series_added_.drain_deferred();
        series_removed_.drain_deferred();
        axes_limits_changed_.drain_deferred();
        theme_changed_.drain_deferred();
        window_opened_.drain_deferred();
        window_closed_.drain_deferred();
        animation_started_.drain_deferred();
        animation_stopped_.drain_deferred();
        export_completed_.drain_deferred();
        plugin_loaded_.drain_deferred();
        plugin_unloaded_.drain_deferred();
    }

   private:
    EventBus<FigureCreatedEvent>     figure_created_;
    EventBus<FigureDestroyedEvent>   figure_destroyed_;
    EventBus<SeriesDataChangedEvent> series_data_changed_;
    EventBus<SeriesAddedEvent>       series_added_;
    EventBus<SeriesRemovedEvent>     series_removed_;
    EventBus<AxesLimitsChangedEvent> axes_limits_changed_;
    EventBus<ThemeChangedEvent>      theme_changed_;
    EventBus<WindowOpenedEvent>      window_opened_;
    EventBus<WindowClosedEvent>      window_closed_;
    EventBus<AnimationStartedEvent>  animation_started_;
    EventBus<AnimationStoppedEvent>  animation_stopped_;
    EventBus<ExportCompletedEvent>   export_completed_;
    EventBus<PluginLoadedEvent>      plugin_loaded_;
    EventBus<PluginUnloadedEvent>    plugin_unloaded_;
};

}   // namespace spectra
