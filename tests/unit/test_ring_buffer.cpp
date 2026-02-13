#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

// Minimal SPSC ring buffer for testing.
// Agent 4 owns the real CommandQueue in src/ui/, but we test the pattern here.
// This is a standalone implementation to verify the lock-free SPSC ring buffer algorithm.

template <typename T, size_t Capacity>
class SpscRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");

public:
    SpscRingBuffer() = default;

    bool try_push(const T& item) {
        size_t h = head_.load(std::memory_order_relaxed);
        size_t next = (h + 1) & mask_;
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;  // full
        }
        buffer_[h] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool try_pop(T& item) {
        size_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire)) {
            return false;  // empty
        }
        item = buffer_[t];
        tail_.store((t + 1) & mask_, std::memory_order_release);
        return true;
    }

    size_t size() const {
        size_t h = head_.load(std::memory_order_acquire);
        size_t t = tail_.load(std::memory_order_acquire);
        return (h - t) & mask_;
    }

    bool empty() const {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    bool full() const {
        size_t h = head_.load(std::memory_order_relaxed);
        size_t next = (h + 1) & mask_;
        return next == tail_.load(std::memory_order_acquire);
    }

private:
    static constexpr size_t mask_ = Capacity - 1;
    std::array<T, Capacity> buffer_{};
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};
};

// --- Tests ---

using RingBuf = SpscRingBuffer<int, 8>;

TEST(RingBuffer, InitiallyEmpty) {
    RingBuf rb;
    EXPECT_TRUE(rb.empty());
    EXPECT_FALSE(rb.full());
    EXPECT_EQ(rb.size(), 0u);
}

TEST(RingBuffer, PushAndPop) {
    RingBuf rb;
    EXPECT_TRUE(rb.try_push(42));
    EXPECT_EQ(rb.size(), 1u);

    int val = 0;
    EXPECT_TRUE(rb.try_pop(val));
    EXPECT_EQ(val, 42);
    EXPECT_TRUE(rb.empty());
}

TEST(RingBuffer, FillToCapacity) {
    // Capacity is 8, but usable slots = 7 (one slot reserved to distinguish full from empty)
    RingBuf rb;
    for (int i = 0; i < 7; ++i) {
        EXPECT_TRUE(rb.try_push(i)) << "push " << i;
    }
    EXPECT_TRUE(rb.full());
    EXPECT_FALSE(rb.try_push(99));  // should fail — full
}

TEST(RingBuffer, FIFO_Order) {
    RingBuf rb;
    for (int i = 0; i < 5; ++i) {
        rb.try_push(i * 10);
    }

    for (int i = 0; i < 5; ++i) {
        int val = -1;
        EXPECT_TRUE(rb.try_pop(val));
        EXPECT_EQ(val, i * 10);
    }
}

TEST(RingBuffer, WrapAround) {
    RingBuf rb;

    // Fill and drain several times to force wrap-around
    for (int round = 0; round < 5; ++round) {
        for (int i = 0; i < 7; ++i) {
            EXPECT_TRUE(rb.try_push(round * 100 + i)) << "round=" << round << " i=" << i;
        }
        for (int i = 0; i < 7; ++i) {
            int val = -1;
            EXPECT_TRUE(rb.try_pop(val));
            EXPECT_EQ(val, round * 100 + i) << "round=" << round << " i=" << i;
        }
        EXPECT_TRUE(rb.empty());
    }
}

TEST(RingBuffer, PopFromEmpty) {
    RingBuf rb;
    int val = -1;
    EXPECT_FALSE(rb.try_pop(val));
    EXPECT_EQ(val, -1);  // unchanged
}

TEST(RingBuffer, InterleavedPushPop) {
    RingBuf rb;

    rb.try_push(1);
    rb.try_push(2);

    int val = 0;
    rb.try_pop(val);
    EXPECT_EQ(val, 1);

    rb.try_push(3);
    rb.try_push(4);

    rb.try_pop(val);
    EXPECT_EQ(val, 2);
    rb.try_pop(val);
    EXPECT_EQ(val, 3);
    rb.try_pop(val);
    EXPECT_EQ(val, 4);

    EXPECT_TRUE(rb.empty());
}

TEST(RingBuffer, SizeTracking) {
    RingBuf rb;
    EXPECT_EQ(rb.size(), 0u);

    rb.try_push(10);
    EXPECT_EQ(rb.size(), 1u);
    rb.try_push(20);
    EXPECT_EQ(rb.size(), 2u);

    int val;
    rb.try_pop(val);
    EXPECT_EQ(rb.size(), 1u);
    rb.try_pop(val);
    EXPECT_EQ(rb.size(), 0u);
}
