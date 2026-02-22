#include <atomic>
#include <functional>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

#include "ui/command_queue.hpp"

using namespace spectra;

// ─── Basic push/pop ──────────────────────────────────────────────────────────

TEST(CommandQueue, InitiallyEmpty)
{
    CommandQueue q;
    EXPECT_TRUE(q.empty());
}

TEST(CommandQueue, PushMakesNonEmpty)
{
    CommandQueue q;
    bool         ok = q.push([]() {});
    EXPECT_TRUE(ok);
    EXPECT_FALSE(q.empty());
}

TEST(CommandQueue, PopRetrievesCommand)
{
    CommandQueue q;
    int          value = 0;
    q.push([&value]() { value = 42; });

    std::function<void()> cmd;
    bool                  ok = q.pop(cmd);
    EXPECT_TRUE(ok);
    EXPECT_NE(cmd, nullptr);
    cmd();
    EXPECT_EQ(value, 42);
}

TEST(CommandQueue, PopFromEmptyReturnsFalse)
{
    CommandQueue          q;
    std::function<void()> cmd;
    EXPECT_FALSE(q.pop(cmd));
}

TEST(CommandQueue, FIFOOrder)
{
    CommandQueue     q;
    std::vector<int> order;

    q.push([&order]() { order.push_back(1); });
    q.push([&order]() { order.push_back(2); });
    q.push([&order]() { order.push_back(3); });

    std::function<void()> cmd;
    while (q.pop(cmd))
    {
        cmd();
    }

    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

TEST(CommandQueue, EmptyAfterAllPopped)
{
    CommandQueue q;
    q.push([]() {});
    q.push([]() {});

    std::function<void()> cmd;
    q.pop(cmd);
    q.pop(cmd);
    EXPECT_TRUE(q.empty());
}

// ─── Drain ───────────────────────────────────────────────────────────────────

TEST(CommandQueue, DrainExecutesAll)
{
    CommandQueue q;
    int          sum = 0;
    q.push([&sum]() { sum += 10; });
    q.push([&sum]() { sum += 20; });
    q.push([&sum]() { sum += 30; });

    size_t count = q.drain();
    EXPECT_EQ(count, 3u);
    EXPECT_EQ(sum, 60);
    EXPECT_TRUE(q.empty());
}

TEST(CommandQueue, DrainOnEmptyReturnsZero)
{
    CommandQueue q;
    EXPECT_EQ(q.drain(), 0u);
}

TEST(CommandQueue, DrainPreservesOrder)
{
    CommandQueue     q;
    std::vector<int> order;
    for (int i = 0; i < 10; ++i)
    {
        q.push([&order, i]() { order.push_back(i); });
    }
    q.drain();
    ASSERT_EQ(order.size(), 10u);
    for (int i = 0; i < 10; ++i)
    {
        EXPECT_EQ(order[i], i);
    }
}

// ─── Capacity ────────────────────────────────────────────────────────────────

TEST(CommandQueue, DefaultCapacity)
{
    CommandQueue q;
    EXPECT_EQ(q.capacity(), CommandQueue::DEFAULT_CAPACITY);
}

TEST(CommandQueue, CustomCapacity)
{
    CommandQueue q(128);
    EXPECT_EQ(q.capacity(), 128u);
}

TEST(CommandQueue, FullQueueRejectsPush)
{
    CommandQueue q(4);   // 3 usable slots
    EXPECT_TRUE(q.push([]() {}));
    EXPECT_TRUE(q.push([]() {}));
    EXPECT_TRUE(q.push([]() {}));
    EXPECT_FALSE(q.push([]() {}));
}

TEST(CommandQueue, FullQueueAcceptsAfterPop)
{
    CommandQueue q(4);
    q.push([]() {});
    q.push([]() {});
    q.push([]() {});
    EXPECT_FALSE(q.push([]() {}));

    std::function<void()> cmd;
    q.pop(cmd);
    EXPECT_TRUE(q.push([]() {}));
}

// ─── Wraparound ──────────────────────────────────────────────────────────────

TEST(CommandQueue, WraparoundCorrectness)
{
    CommandQueue          q(4);
    std::function<void()> cmd;

    for (int cycle = 0; cycle < 10; ++cycle)
    {
        int value = 0;
        q.push([&value, cycle]() { value = cycle * 10 + 1; });
        q.push([&value, cycle]() { value = cycle * 10 + 2; });

        ASSERT_TRUE(q.pop(cmd));
        cmd();
        EXPECT_EQ(value, cycle * 10 + 1);

        ASSERT_TRUE(q.pop(cmd));
        cmd();
        EXPECT_EQ(value, cycle * 10 + 2);

        EXPECT_TRUE(q.empty());
    }
}

// ─── Null command handling ───────────────────────────────────────────────────

TEST(CommandQueue, DrainSkipsNullCommands)
{
    CommandQueue q;
    q.push(std::function<void()>(nullptr));
    q.push([]() {});
    size_t count = q.drain();
    EXPECT_EQ(count, 2u);
}

// ─── SPSC cross-thread correctness ──────────────────────────────────────────

TEST(CommandQueue, SPSCProducerConsumer)
{
    CommandQueue     q(1024);
    constexpr int    NUM_ITEMS = 500;
    std::atomic<int> sum{0};

    std::thread producer(
        [&q, &sum]()
        {
            for (int i = 1; i <= NUM_ITEMS; ++i)
            {
                while (!q.push([&sum, i]() { sum.fetch_add(i, std::memory_order_relaxed); }))
                {
                    std::this_thread::yield();
                }
            }
        });

    std::thread consumer(
        [&q]()
        {
            int consumed = 0;
            while (consumed < NUM_ITEMS)
            {
                std::function<void()> cmd;
                if (q.pop(cmd))
                {
                    if (cmd)
                        cmd();
                    ++consumed;
                }
                else
                {
                    std::this_thread::yield();
                }
            }
        });

    producer.join();
    consumer.join();

    int expected = NUM_ITEMS * (NUM_ITEMS + 1) / 2;
    EXPECT_EQ(sum.load(), expected);
}

TEST(CommandQueue, SPSCDrainConsumer)
{
    CommandQueue     q(256);
    constexpr int    NUM_ITEMS = 200;
    std::atomic<int> count{0};

    std::thread producer(
        [&q, &count]()
        {
            for (int i = 0; i < NUM_ITEMS; ++i)
            {
                while (!q.push([&count]() { count.fetch_add(1, std::memory_order_relaxed); }))
                {
                    std::this_thread::yield();
                }
            }
        });

    std::thread consumer(
        [&q, &count]()
        {
            while (count.load(std::memory_order_relaxed) < NUM_ITEMS)
            {
                q.drain();
                std::this_thread::yield();
            }
        });

    producer.join();
    consumer.join();
    EXPECT_EQ(count.load(), NUM_ITEMS);
}

// ─── Interleaved push/pop ────────────────────────────────────────────────────

TEST(CommandQueue, InterleavedPushPop)
{
    CommandQueue q(8);
    int          total = 0;

    for (int i = 0; i < 50; ++i)
    {
        q.push([&total, i]() { total += i; });
        std::function<void()> cmd;
        ASSERT_TRUE(q.pop(cmd));
        cmd();
    }
    EXPECT_EQ(total, 50 * 49 / 2);
}
