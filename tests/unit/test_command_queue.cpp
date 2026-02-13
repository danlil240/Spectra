#include <gtest/gtest.h>

// CommandQueue is an internal header in src/ui/
#include "ui/command_queue.hpp"

#include <atomic>
#include <thread>
#include <vector>

using namespace plotix;

TEST(CommandQueue, InitiallyEmpty) {
    CommandQueue q;
    EXPECT_TRUE(q.empty());
}

TEST(CommandQueue, PushAndPop) {
    CommandQueue q;
    int value = 0;

    EXPECT_TRUE(q.push([&value]() { value = 42; }));
    EXPECT_FALSE(q.empty());

    std::function<void()> cmd;
    EXPECT_TRUE(q.pop(cmd));
    EXPECT_TRUE(q.empty());

    cmd();
    EXPECT_EQ(value, 42);
}

TEST(CommandQueue, Drain) {
    CommandQueue q;
    int counter = 0;

    q.push([&counter]() { counter += 1; });
    q.push([&counter]() { counter += 10; });
    q.push([&counter]() { counter += 100; });

    size_t drained = q.drain();
    EXPECT_EQ(drained, 3u);
    EXPECT_EQ(counter, 111);
    EXPECT_TRUE(q.empty());
}

TEST(CommandQueue, DrainEmpty) {
    CommandQueue q;
    size_t drained = q.drain();
    EXPECT_EQ(drained, 0u);
}

TEST(CommandQueue, FIFO_Order) {
    CommandQueue q;
    std::vector<int> order;

    q.push([&order]() { order.push_back(1); });
    q.push([&order]() { order.push_back(2); });
    q.push([&order]() { order.push_back(3); });

    q.drain();

    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

TEST(CommandQueue, FullQueue) {
    // Small capacity to test full condition
    CommandQueue q(4);

    // Capacity 4 means 3 usable slots (one reserved for full/empty distinction)
    EXPECT_TRUE(q.push([]() {}));
    EXPECT_TRUE(q.push([]() {}));
    EXPECT_TRUE(q.push([]() {}));
    EXPECT_FALSE(q.push([]() {})); // Should fail — full
}

TEST(CommandQueue, PopFromEmpty) {
    CommandQueue q;
    std::function<void()> cmd;
    EXPECT_FALSE(q.pop(cmd));
}

TEST(CommandQueue, Capacity) {
    CommandQueue q(128);
    EXPECT_EQ(q.capacity(), 128u);
}

TEST(CommandQueue, NullCommandSafe) {
    CommandQueue q;
    q.push(nullptr);

    // drain() should handle null commands gracefully (it checks if(cmd))
    size_t drained = q.drain();
    EXPECT_EQ(drained, 1u);
}

TEST(CommandQueue, ProducerConsumerThreaded) {
    CommandQueue q;
    std::atomic<int> sum{0};
    constexpr int N = 1000;

    // Producer thread
    std::thread producer([&q, &sum]() {
        for (int i = 0; i < N; ++i) {
            while (!q.push([&sum]() { sum.fetch_add(1, std::memory_order_relaxed); })) {
                std::this_thread::yield();
            }
        }
    });

    // Consumer thread
    std::thread consumer([&q, &sum]() {
        int consumed = 0;
        while (consumed < N) {
            consumed += static_cast<int>(q.drain());
            if (consumed < N) {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(sum.load(), N);
}
