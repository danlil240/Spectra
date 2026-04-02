#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <spectra/event_bus.hpp>
#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/series.hpp>
#include <string>
#include <vector>

using namespace spectra;

// ═══════════════════════════════════════════════════════════════════════════════
// EventBus template tests
// ═══════════════════════════════════════════════════════════════════════════════

struct TestEvent
{
    int value;
};

TEST(EventBus, SubscribeAndEmit)
{
    EventBus<TestEvent> bus;
    int                 received = 0;
    bus.subscribe([&](const TestEvent& e) { received = e.value; });
    bus.emit({42});
    EXPECT_EQ(received, 42);
}

TEST(EventBus, MultipleSubscribers)
{
    EventBus<TestEvent> bus;
    int                 sum = 0;
    bus.subscribe([&](const TestEvent& e) { sum += e.value; });
    bus.subscribe([&](const TestEvent& e) { sum += e.value * 10; });
    bus.emit({3});
    EXPECT_EQ(sum, 33);   // 3 + 30
}

TEST(EventBus, Unsubscribe)
{
    EventBus<TestEvent> bus;
    int                 count = 0;
    auto                id    = bus.subscribe([&](const TestEvent&) { ++count; });
    bus.emit({0});
    EXPECT_EQ(count, 1);

    bus.unsubscribe(id);
    bus.emit({0});
    EXPECT_EQ(count, 1);   // Should not increment
}

TEST(EventBus, UnsubscribeInvalidId)
{
    EventBus<TestEvent> bus;
    bus.unsubscribe(999);   // No-op, should not crash
    EXPECT_EQ(bus.subscriber_count(), 0u);
}

TEST(EventBus, SubscriberCount)
{
    EventBus<TestEvent> bus;
    EXPECT_EQ(bus.subscriber_count(), 0u);

    auto id1 = bus.subscribe([](const TestEvent&) {});
    EXPECT_EQ(bus.subscriber_count(), 1u);

    auto id2 = bus.subscribe([](const TestEvent&) {});
    EXPECT_EQ(bus.subscriber_count(), 2u);

    bus.unsubscribe(id1);
    EXPECT_EQ(bus.subscriber_count(), 1u);

    bus.unsubscribe(id2);
    EXPECT_EQ(bus.subscriber_count(), 0u);
}

TEST(EventBus, UnsubscribeDuringEmit)
{
    EventBus<TestEvent> bus;
    SubscriptionId      id2    = 0;
    int                 count1 = 0, count2 = 0;

    bus.subscribe(
        [&](const TestEvent&)
        {
            ++count1;
            // Unsubscribe the second handler mid-emit.
            bus.unsubscribe(id2);
        });
    id2 = bus.subscribe([&](const TestEvent&) { ++count2; });

    bus.emit({0});
    // Both should have fired in the first emit (deferred unsub).
    EXPECT_EQ(count1, 1);
    EXPECT_EQ(count2, 1);

    // Second emit: only first handler should fire.
    bus.emit({0});
    EXPECT_EQ(count1, 2);
    EXPECT_EQ(count2, 1);
}

TEST(EventBus, EmitWithNoSubscribers)
{
    EventBus<TestEvent> bus;
    bus.emit({42});   // No-op, should not crash
}

// ═══════════════════════════════════════════════════════════════════════════════
// EventSystem aggregate tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(EventSystem, AllBusesAccessible)
{
    EventSystem es;
    // Verify all buses exist and have 0 subscribers
    EXPECT_EQ(es.figure_created().subscriber_count(), 0u);
    EXPECT_EQ(es.figure_destroyed().subscriber_count(), 0u);
    EXPECT_EQ(es.series_data_changed().subscriber_count(), 0u);
    EXPECT_EQ(es.series_added().subscriber_count(), 0u);
    EXPECT_EQ(es.series_removed().subscriber_count(), 0u);
    EXPECT_EQ(es.axes_limits_changed().subscriber_count(), 0u);
    EXPECT_EQ(es.theme_changed().subscriber_count(), 0u);
    EXPECT_EQ(es.window_opened().subscriber_count(), 0u);
    EXPECT_EQ(es.window_closed().subscriber_count(), 0u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// FigureRegistry integration
// ═══════════════════════════════════════════════════════════════════════════════

TEST(EventSystem, FigureCreatedEvent)
{
    EventSystem    es;
    FigureRegistry registry;
    registry.set_event_system(&es);

    FigureId received_id  = 0;
    Figure*  received_fig = nullptr;
    es.figure_created().subscribe(
        [&](const FigureCreatedEvent& e)
        {
            received_id  = e.figure_id;
            received_fig = e.figure;
        });

    auto  fig = std::make_unique<Figure>();
    auto* raw = fig.get();
    auto  id  = registry.register_figure(std::move(fig));

    EXPECT_EQ(received_id, id);
    EXPECT_EQ(received_fig, raw);
}

TEST(EventSystem, FigureDestroyedEvent)
{
    EventSystem    es;
    FigureRegistry registry;
    registry.set_event_system(&es);

    FigureId destroyed_id = 0;
    es.figure_destroyed().subscribe([&](const FigureDestroyedEvent& e)
                                    { destroyed_id = e.figure_id; });

    auto id = registry.register_figure(std::make_unique<Figure>());
    EXPECT_EQ(destroyed_id, 0u);

    registry.unregister_figure(id);
    EXPECT_EQ(destroyed_id, id);
}

TEST(EventSystem, NoEventWithoutEventSystem)
{
    FigureRegistry registry;
    // No event system set — should not crash.
    auto id = registry.register_figure(std::make_unique<Figure>());
    registry.unregister_figure(id);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Series integration
// ═══════════════════════════════════════════════════════════════════════════════

TEST(EventSystem, SeriesMarkDirtyEmitsEvent)
{
    EventSystem es;

    int     emit_count = 0;
    Series* received   = nullptr;
    es.series_data_changed().subscribe(
        [&](const SeriesDataChangedEvent& e)
        {
            ++emit_count;
            received = e.series;
        });

    LineSeries ls;
    Axes       ax;
    ls.set_event_context(&es, &ax);
    ls.mark_dirty();

    EXPECT_EQ(emit_count, 1);
    EXPECT_EQ(received, &ls);
}

TEST(EventSystem, SeriesWithoutEventContextNoEvent)
{
    LineSeries ls;
    // No event context set — should not crash.
    ls.mark_dirty();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Axes integration
// ═══════════════════════════════════════════════════════════════════════════════

TEST(EventSystem, SeriesAddedEvent)
{
    EventSystem es;
    Axes        ax;
    ax.set_event_system(&es);

    int     added_count = 0;
    Series* added_ptr   = nullptr;
    es.series_added().subscribe(
        [&](const SeriesAddedEvent& e)
        {
            ++added_count;
            added_ptr = e.series;
        });

    auto& ls = ax.line();
    EXPECT_EQ(added_count, 1);
    EXPECT_EQ(added_ptr, &ls);
}

TEST(EventSystem, SeriesRemovedEvent)
{
    EventSystem es;
    Axes        ax;
    ax.set_event_system(&es);

    int           removed_count = 0;
    const Series* removed_ptr   = nullptr;
    es.series_removed().subscribe(
        [&](const SeriesRemovedEvent& e)
        {
            ++removed_count;
            removed_ptr = e.series;
        });

    auto& ls = ax.line();
    EXPECT_TRUE(ax.remove_series(0));
    EXPECT_EQ(removed_count, 1);
    EXPECT_EQ(removed_ptr, &ls);
}

TEST(EventSystem, ClearSeriesEmitsRemoved)
{
    EventSystem es;
    Axes        ax;
    ax.set_event_system(&es);

    int removed_count = 0;
    es.series_removed().subscribe([&](const SeriesRemovedEvent&) { ++removed_count; });

    ax.line();
    ax.scatter();
    ax.clear_series();
    EXPECT_EQ(removed_count, 2);
}

TEST(EventSystem, AddedSeriesGetsEventContext)
{
    EventSystem es;
    Axes        ax;
    ax.set_event_system(&es);

    int data_changed = 0;
    es.series_data_changed().subscribe([&](const SeriesDataChangedEvent&) { ++data_changed; });

    auto& ls = ax.line();
    ls.mark_dirty();
    EXPECT_EQ(data_changed, 1);
}

TEST(EventSystem, AxesLimitsChangedEvent)
{
    EventSystem es;
    Axes        ax;
    ax.set_event_system(&es);

    int    limits_count = 0;
    double rx_min = 0, rx_max = 0, ry_min = 0, ry_max = 0;
    es.axes_limits_changed().subscribe(
        [&](const AxesLimitsChangedEvent& e)
        {
            ++limits_count;
            rx_min = e.x_min;
            rx_max = e.x_max;
            ry_min = e.y_min;
            ry_max = e.y_max;
        });

    ax.xlim(1.0, 10.0);
    EXPECT_EQ(limits_count, 1);
    EXPECT_DOUBLE_EQ(rx_min, 1.0);
    EXPECT_DOUBLE_EQ(rx_max, 10.0);

    ax.ylim(2.0, 20.0);
    EXPECT_EQ(limits_count, 2);
    EXPECT_DOUBLE_EQ(ry_min, 2.0);
    EXPECT_DOUBLE_EQ(ry_max, 20.0);
}

TEST(EventSystem, AxesWithoutEventSystemNoEvent)
{
    Axes ax;
    // No event system set — should not crash.
    ax.line();
    ax.xlim(0, 1);
    ax.ylim(0, 1);
    ax.remove_series(0);
    ax.clear_series();
}
