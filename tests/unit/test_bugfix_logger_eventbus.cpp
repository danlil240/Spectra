#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <spectra/event_bus.hpp>
#include <spectra/logger.hpp>
#include <string>
#include <vector>

using namespace spectra;

namespace
{
struct TestEvent
{
    int value = 0;
};

// Captures log entries and restores the global logger state afterwards.
class LoggerCapture
{
   public:
    LoggerCapture()
    {
        auto& logger = Logger::instance();
        saved_level_ = logger.get_level();
        logger.clear_sinks();
        logger.add_sink([this](const Logger::LogEntry& e) { entries.push_back(e); });
    }
    ~LoggerCapture()
    {
        auto& logger = Logger::instance();
        logger.clear_sinks();
        logger.set_level(saved_level_);
    }

    std::vector<Logger::LogEntry> entries;

   private:
    LogLevel saved_level_ = LogLevel::Info;
};
}   // namespace

// ─── Bug #16: subscribe during emit ─────────────────────────────────────────

TEST(BugfixEventBus, SubscribeDuringEmitIsDeferredAndSafe)
{
    EventBus<TestEvent> bus;
    std::vector<int>    order;
    int                 late_calls = 0;
    bool                added      = false;

    bus.subscribe(
        [&](const TestEvent&)
        {
            order.push_back(1);
            if (!added)
            {
                added = true;
                // Many subscriptions to force reallocation of the subscriber list.
                for (int i = 0; i < 64; ++i)
                    bus.subscribe([&](const TestEvent&) { ++late_calls; }, Priority::High);
            }
        });
    bus.subscribe([&](const TestEvent&) { order.push_back(2); });

    bus.emit(TestEvent{1});
    EXPECT_EQ(order, (std::vector<int>{1, 2}));
    EXPECT_EQ(late_calls, 0);   // Not delivered during the emission that added them
    EXPECT_EQ(bus.subscriber_count(), 66u);

    order.clear();
    bus.emit(TestEvent{2});
    EXPECT_EQ(late_calls, 64);
    EXPECT_EQ(order, (std::vector<int>{1, 2}));
}

TEST(BugfixEventBus, SubscribeThenUnsubscribeDuringEmit)
{
    EventBus<TestEvent> bus;
    int                 late_calls = 0;
    bus.subscribe(
        [&](const TestEvent&)
        {
            auto id = bus.subscribe([&](const TestEvent&) { ++late_calls; });
            bus.unsubscribe(id);
        });
    bus.emit(TestEvent{});
    EXPECT_EQ(bus.subscriber_count(), 1u);
    bus.emit(TestEvent{});
    EXPECT_EQ(late_calls, 0);
}

// ─── Bug #17: unsubscribed callbacks must not fire later in same emission ──

TEST(BugfixEventBus, UnsubscribeDuringEmitStopsDeliveryImmediately)
{
    EventBus<TestEvent> bus;

    struct Victim
    {
        explicit Victim(EventBus<TestEvent>& b, int* calls)
            : calls_(calls), sub_(b, [this](const TestEvent&) { ++*calls_; }, Priority::Normal)
        {
        }
        int*                          calls_;
        ScopedSubscription<TestEvent> sub_;
    };

    int  victim_calls = 0;
    auto victim       = std::make_unique<Victim>(bus, &victim_calls);

    bus.subscribe(
        [&](const TestEvent& e)
        {
            if (e.value == 1)
            {
                victim.reset();           // unsubscribes while emitting
                bus.emit(TestEvent{2});   // re-entrant: queued
            }
        },
        Priority::High);

    bus.emit(TestEvent{1});
    EXPECT_EQ(victim_calls, 0);
    EXPECT_EQ(bus.subscriber_count(), 1u);
}

TEST(BugfixEventBus, SelfUnsubscribeDuringEmit)
{
    EventBus<TestEvent> bus;
    int                 calls = 0;
    SubscriptionId      id    = 0;
    auto                token = std::make_shared<int>(42);
    id                        = bus.subscribe(
        [&, token](const TestEvent&)
        {
            ++calls;
            bus.unsubscribe(id);
            // Captures must remain valid after unsubscribing self.
            EXPECT_EQ(*token, 42);
            bus.emit(TestEvent{});
        });
    bus.emit(TestEvent{});
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(bus.subscriber_count(), 0u);
}

// ─── Bug #68: placeholder text inside arguments ────────────────────────────

TEST(BugfixLogger, FormatDoesNotReplaceBracesInsideArguments)
{
    LoggerCapture cap;
    Logger::instance().set_level(LogLevel::Trace);
    Logger::instance().log_formatted(LogLevel::Info,
                                     "io",
                                     "Loaded {} rows={}",
                                     std::string("/data/run{}.bin"),
                                     5);
    ASSERT_EQ(cap.entries.size(), 1u);
    EXPECT_EQ(cap.entries[0].message, "Loaded /data/run{}.bin rows=5");
}

TEST(BugfixLogger, FormatExtraPlaceholdersAndArgs)
{
    LoggerCapture cap;
    Logger::instance().set_level(LogLevel::Trace);
    Logger::instance().log_formatted(LogLevel::Info, "t", "{} {} {}", 1, "x");
    Logger::instance().log_formatted(LogLevel::Info, "t", "{}", 1, 2);
    ASSERT_EQ(cap.entries.size(), 2u);
    EXPECT_EQ(cap.entries[0].message, "1 x {}");
    EXPECT_EQ(cap.entries[1].message, "1");
}

// ─── Bug #72: Off level silences everything ────────────────────────────────

TEST(BugfixLogger, OffLevelDisablesAllLogging)
{
    LoggerCapture cap;
    Logger::instance().set_level(LogLevel::Off);
    EXPECT_FALSE(Logger::instance().is_enabled(LogLevel::Critical));
    Logger::instance().log(LogLevel::Critical, "t", "should not appear");
    EXPECT_TRUE(cap.entries.empty());
    EXPECT_EQ(Logger::level_to_string(LogLevel::Off), "OFF");
}

TEST(BugfixLogger, FilteredSinkAtOffDropsEverything)
{
    int  calls = 0;
    auto sink  = sinks::filtered_sink(LogLevel::Off, [&](const Logger::LogEntry&) { ++calls; });
    Logger::LogEntry entry{};
    entry.level = LogLevel::Critical;
    sink(entry);
    EXPECT_EQ(calls, 0);
}

namespace
{
void set_env(const char* name, const char* value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void unset_env(const char* name)
{
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}
}   // namespace

TEST(BugfixLogger, EnvOffMapsToOffLevel)
{
    const char* old     = std::getenv("SPECTRA_LOG_LEVEL");
    std::string old_val = old ? old : "";
    set_env("SPECTRA_LOG_LEVEL", "off");
    EXPECT_EQ(default_console_log_level(), LogLevel::Off);
    set_env("SPECTRA_LOG_LEVEL", "silent");
    EXPECT_EQ(default_console_log_level(), LogLevel::Off);
    if (old)
        set_env("SPECTRA_LOG_LEVEL", old_val.c_str());
    else
        unset_env("SPECTRA_LOG_LEVEL");
}
