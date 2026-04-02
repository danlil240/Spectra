#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
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
// Thread safety: EventBus is NOT thread-safe.  All subscribe/unsubscribe/emit
// calls must happen on the same thread (typically the main/app thread), which
// matches Spectra's existing threading model for Series and Figure data.
//
// Usage:
//   EventBus<FigureCreatedEvent> bus;
//   auto id = bus.subscribe([](const FigureCreatedEvent& e) { ... });
//   bus.emit(FigureCreatedEvent{figure_id});
//   bus.unsubscribe(id);
// ═══════════════════════════════════════════════════════════════════════════════

using SubscriptionId = uint64_t;

template <typename Event>
class EventBus
{
   public:
    using Callback = std::function<void(const Event&)>;

    EventBus()  = default;
    ~EventBus() = default;

    EventBus(const EventBus&)            = delete;
    EventBus& operator=(const EventBus&) = delete;

    // Register a callback.  Returns a SubscriptionId for later unsubscribe().
    SubscriptionId subscribe(Callback cb)
    {
        SubscriptionId id = next_id_++;
        subs_.push_back({id, std::move(cb)});
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
    void emit(const Event& event)
    {
        emitting_ = true;
        for (auto& [id, cb] : subs_)
        {
            if (cb)
                cb(event);
        }
        emitting_ = false;

        // Process deferred unsubscribes.
        for (auto uid : pending_unsubs_)
            erase_sub(uid);
        pending_unsubs_.clear();
    }

    // Returns the number of active subscriptions.
    size_t subscriber_count() const { return subs_.size(); }

   private:
    struct Sub
    {
        SubscriptionId id;
        Callback       cb;
    };

    void erase_sub(SubscriptionId id)
    {
        subs_.erase(
            std::remove_if(subs_.begin(), subs_.end(), [id](const Sub& s) { return s.id == id; }),
            subs_.end());
    }

    std::vector<Sub>            subs_;
    std::vector<SubscriptionId> pending_unsubs_;
    SubscriptionId              next_id_  = 1;
    bool                        emitting_ = false;
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
};

}   // namespace spectra
