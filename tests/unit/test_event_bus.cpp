#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <spectra/event_bus.hpp>
#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/series.hpp>
#include <atomic>
#include <string>
#include <thread>
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

// ═══════════════════════════════════════════════════════════════════════════════
// Re-entrancy guard tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(EventBus, ReentrantEmitQueuesEvent)
{
    EventBus<TestEvent> bus;
    std::vector<int>    order;

    bus.subscribe(
        [&](const TestEvent& e)
        {
            order.push_back(e.value);
            if (e.value == 1)
            {
                // Re-entrant emit: should be queued.
                bus.emit({2});
            }
        });

    bus.emit({1});
    // Outer event fires first, then queued event drains.
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
}

TEST(EventBus, ReentrantEmitMultipleQueued)
{
    EventBus<TestEvent> bus;
    std::vector<int>    order;

    bus.subscribe(
        [&](const TestEvent& e)
        {
            order.push_back(e.value);
            if (e.value == 1)
            {
                bus.emit({2});
                bus.emit({3});
            }
        });

    bus.emit({1});
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

TEST(EventBus, ReentrantEmitChained)
{
    // Event 1 triggers event 2, which triggers event 3.
    EventBus<TestEvent> bus;
    std::vector<int>    order;

    bus.subscribe(
        [&](const TestEvent& e)
        {
            order.push_back(e.value);
            if (e.value == 1)
                bus.emit({2});
            else if (e.value == 2)
                bus.emit({3});
        });

    bus.emit({1});
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Priority tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(EventBus, PriorityOrdering)
{
    EventBus<TestEvent> bus;
    std::vector<int>    order;

    // Subscribe in reverse priority order.
    bus.subscribe([&](const TestEvent&) { order.push_back(3); }, Priority::Low);
    bus.subscribe([&](const TestEvent&) { order.push_back(1); }, Priority::High);
    bus.subscribe([&](const TestEvent&) { order.push_back(2); }, Priority::Normal);

    bus.emit({0});
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);   // High first
    EXPECT_EQ(order[1], 2);   // Normal second
    EXPECT_EQ(order[2], 3);   // Low last
}

TEST(EventBus, PriorityStableOrder)
{
    // Multiple subs at the same priority maintain registration order.
    EventBus<TestEvent> bus;
    std::vector<int>    order;

    bus.subscribe([&](const TestEvent&) { order.push_back(1); }, Priority::Normal);
    bus.subscribe([&](const TestEvent&) { order.push_back(2); }, Priority::Normal);
    bus.subscribe([&](const TestEvent&) { order.push_back(3); }, Priority::Normal);

    bus.emit({0});
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

TEST(EventBus, DefaultPriorityIsNormal)
{
    EventBus<TestEvent> bus;
    std::vector<int>    order;

    // Default subscribe (Normal) should run after High.
    bus.subscribe([&](const TestEvent&) { order.push_back(2); });   // default = Normal
    bus.subscribe([&](const TestEvent&) { order.push_back(1); }, Priority::High);

    bus.emit({0});
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Cross-thread deferred emission tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(EventBus, EmitDeferredAndDrain)
{
    EventBus<TestEvent> bus;
    int                 received = 0;
    bus.subscribe([&](const TestEvent& e) { received = e.value; });

    bus.emit_deferred({42});
    EXPECT_EQ(received, 0);   // Not delivered yet.

    bus.drain_deferred();
    EXPECT_EQ(received, 42);   // Now delivered.
}

TEST(EventBus, EmitDeferredMultiple)
{
    EventBus<TestEvent> bus;
    std::vector<int>    received;
    bus.subscribe([&](const TestEvent& e) { received.push_back(e.value); });

    bus.emit_deferred({1});
    bus.emit_deferred({2});
    bus.emit_deferred({3});
    EXPECT_TRUE(received.empty());

    bus.drain_deferred();
    ASSERT_EQ(received.size(), 3u);
    EXPECT_EQ(received[0], 1);
    EXPECT_EQ(received[1], 2);
    EXPECT_EQ(received[2], 3);
}

TEST(EventBus, DrainDeferredEmpty)
{
    EventBus<TestEvent> bus;
    bus.drain_deferred();   // No-op, should not crash.
}

TEST(EventBus, EmitDeferredFromThread)
{
    EventBus<TestEvent> bus;
    std::atomic<int>    received{0};
    bus.subscribe([&](const TestEvent& e) { received.store(e.value); });

    std::thread t([&]() { bus.emit_deferred({99}); });
    t.join();

    EXPECT_EQ(received.load(), 0);
    bus.drain_deferred();
    EXPECT_EQ(received.load(), 99);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ScopedSubscription tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(ScopedSubscription, AutoUnsubscribes)
{
    EventBus<TestEvent> bus;
    int                 count = 0;

    {
        ScopedSubscription<TestEvent> sub(bus, [&](const TestEvent&) { ++count; });
        EXPECT_TRUE(sub.active());
        EXPECT_EQ(bus.subscriber_count(), 1u);
        bus.emit({0});
        EXPECT_EQ(count, 1);
    }

    // Out of scope — should be unsubscribed.
    EXPECT_EQ(bus.subscriber_count(), 0u);
    bus.emit({0});
    EXPECT_EQ(count, 1);   // No increment.
}

TEST(ScopedSubscription, MoveConstruct)
{
    EventBus<TestEvent> bus;
    int                 count = 0;

    ScopedSubscription<TestEvent> sub1(bus, [&](const TestEvent&) { ++count; });
    ScopedSubscription<TestEvent> sub2(std::move(sub1));

    EXPECT_FALSE(sub1.active());
    EXPECT_TRUE(sub2.active());
    EXPECT_EQ(bus.subscriber_count(), 1u);

    bus.emit({0});
    EXPECT_EQ(count, 1);
}

TEST(ScopedSubscription, MoveAssign)
{
    EventBus<TestEvent> bus;
    int                 count1 = 0, count2 = 0;

    ScopedSubscription<TestEvent> sub1(bus, [&](const TestEvent&) { ++count1; });
    ScopedSubscription<TestEvent> sub2(bus, [&](const TestEvent&) { ++count2; });
    EXPECT_EQ(bus.subscriber_count(), 2u);

    sub2 = std::move(sub1);
    // sub2's original callback should be unsubscribed, sub1's transferred.
    EXPECT_EQ(bus.subscriber_count(), 1u);
    bus.emit({0});
    EXPECT_EQ(count1, 1);
    EXPECT_EQ(count2, 0);
}

TEST(ScopedSubscription, Release)
{
    EventBus<TestEvent> bus;
    int                 count = 0;

    ScopedSubscription<TestEvent> sub(bus, [&](const TestEvent&) { ++count; });
    auto                          id = sub.release();

    EXPECT_FALSE(sub.active());
    // Still subscribed since we released ownership.
    EXPECT_EQ(bus.subscriber_count(), 1u);
    bus.emit({0});
    EXPECT_EQ(count, 1);

    // Manual cleanup.
    bus.unsubscribe(id);
    EXPECT_EQ(bus.subscriber_count(), 0u);
}

TEST(ScopedSubscription, DefaultConstructed)
{
    ScopedSubscription<TestEvent> sub;
    EXPECT_FALSE(sub.active());
    // Destruction of default-constructed should not crash.
}

TEST(ScopedSubscription, WithPriority)
{
    EventBus<TestEvent> bus;
    std::vector<int>    order;

    ScopedSubscription<TestEvent> low(
        bus,
        [&](const TestEvent&) { order.push_back(3); },
        Priority::Low);
    ScopedSubscription<TestEvent> high(
        bus,
        [&](const TestEvent&) { order.push_back(1); },
        Priority::High);

    bus.emit({0});
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 3);
}

// ═══════════════════════════════════════════════════════════════════════════════
// EventSystem — new bus accessors
// ═══════════════════════════════════════════════════════════════════════════════

TEST(EventSystem, NewBusesAccessible)
{
    EventSystem es;
    EXPECT_EQ(es.animation_started().subscriber_count(), 0u);
    EXPECT_EQ(es.animation_stopped().subscriber_count(), 0u);
    EXPECT_EQ(es.export_completed().subscriber_count(), 0u);
    EXPECT_EQ(es.plugin_loaded().subscriber_count(), 0u);
    EXPECT_EQ(es.plugin_unloaded().subscriber_count(), 0u);
}

TEST(EventSystem, AnimationEvents)
{
    EventSystem es;
    FigureId    started_id = 0, stopped_id = 0;

    es.animation_started().subscribe([&](const AnimationStartedEvent& e)
                                     { started_id = e.figure_id; });
    es.animation_stopped().subscribe([&](const AnimationStoppedEvent& e)
                                     { stopped_id = e.figure_id; });

    es.animation_started().emit({42});
    EXPECT_EQ(started_id, 42u);

    es.animation_stopped().emit({99});
    EXPECT_EQ(stopped_id, 99u);
}

TEST(EventSystem, ExportCompletedEvent)
{
    EventSystem es;
    std::string received_path;

    es.export_completed().subscribe([&](const ExportCompletedEvent& e) { received_path = e.path; });

    es.export_completed().emit({1, "/tmp/test.png"});
    EXPECT_EQ(received_path, "/tmp/test.png");
}

TEST(EventSystem, PluginEvents)
{
    EventSystem es;
    std::string loaded_name, unloaded_name;

    es.plugin_loaded().subscribe([&](const PluginLoadedEvent& e) { loaded_name = e.plugin_name; });
    es.plugin_unloaded().subscribe([&](const PluginUnloadedEvent& e)
                                   { unloaded_name = e.plugin_name; });

    es.plugin_loaded().emit({"my_plugin"});
    EXPECT_EQ(loaded_name, "my_plugin");

    es.plugin_unloaded().emit({"my_plugin"});
    EXPECT_EQ(unloaded_name, "my_plugin");
}

TEST(EventSystem, DrainAllDeferred)
{
    EventSystem es;
    int         fig_count = 0, theme_count = 0;

    es.figure_created().subscribe([&](const FigureCreatedEvent&) { ++fig_count; });
    es.theme_changed().subscribe([&](const ThemeChangedEvent&) { ++theme_count; });

    es.figure_created().emit_deferred({1, nullptr});
    es.theme_changed().emit_deferred({"dark"});

    EXPECT_EQ(fig_count, 0);
    EXPECT_EQ(theme_count, 0);

    es.drain_all_deferred();

    EXPECT_EQ(fig_count, 1);
    EXPECT_EQ(theme_count, 1);
}
