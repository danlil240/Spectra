// test_pending_series_data.cpp — Unit tests for SpinLock and PendingSeriesData.

#include <gtest/gtest.h>

#include "core/pending_series_data.hpp"
#include "core/spinlock.hpp"

#include <thread>
#include <vector>

using namespace spectra;

// ═══════════════════════════════════════════════════════════════════════════════
// SpinLock
// ═══════════════════════════════════════════════════════════════════════════════

TEST(SpinLock, LockUnlock)
{
    SpinLock lock;
    lock.lock();
    lock.unlock();
}

TEST(SpinLock, TryLockSucceeds)
{
    SpinLock lock;
    EXPECT_TRUE(lock.try_lock());
    lock.unlock();
}

TEST(SpinLock, TryLockFailsWhenHeld)
{
    SpinLock lock;
    lock.lock();
    EXPECT_FALSE(lock.try_lock());
    lock.unlock();
}

TEST(SpinLock, GuardLocksAndUnlocks)
{
    SpinLock lock;
    {
        SpinLockGuard guard(lock);
        EXPECT_FALSE(lock.try_lock());
    }
    EXPECT_TRUE(lock.try_lock());
    lock.unlock();
}

TEST(SpinLock, ConcurrentIncrement)
{
    SpinLock lock;
    int      counter = 0;
    constexpr int num_threads    = 4;
    constexpr int increments     = 10000;

    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t)
    {
        threads.emplace_back([&]()
        {
            for (int i = 0; i < increments; ++i)
            {
                SpinLockGuard guard(lock);
                ++counter;
            }
        });
    }
    for (auto& th : threads)
        th.join();

    EXPECT_EQ(counter, num_threads * increments);
}

// ═══════════════════════════════════════════════════════════════════════════════
// PendingSeriesData
// ═══════════════════════════════════════════════════════════════════════════════

TEST(PendingSeriesData, InitialState)
{
    PendingSeriesData pending;
    EXPECT_FALSE(pending.has_pending());
}

TEST(PendingSeriesData, EmptyCommitReturnsFalse)
{
    PendingSeriesData      pending;
    std::vector<float> x, y;
    EXPECT_FALSE(pending.commit(x, y));
}

TEST(PendingSeriesData, AppendAccumulates)
{
    PendingSeriesData      pending;
    std::vector<float> x, y;

    pending.append(1.0f, 10.0f);
    pending.append(2.0f, 20.0f);
    pending.append(3.0f, 30.0f);
    EXPECT_TRUE(pending.has_pending());

    EXPECT_TRUE(pending.commit(x, y));
    EXPECT_EQ(x.size(), 3u);
    EXPECT_EQ(y.size(), 3u);
    EXPECT_FLOAT_EQ(x[0], 1.0f);
    EXPECT_FLOAT_EQ(x[2], 3.0f);
    EXPECT_FLOAT_EQ(y[1], 20.0f);

    EXPECT_FALSE(pending.has_pending());
}

TEST(PendingSeriesData, AppendToExistingData)
{
    PendingSeriesData      pending;
    std::vector<float> x = {0.0f};
    std::vector<float> y = {0.0f};

    pending.append(1.0f, 10.0f);
    EXPECT_TRUE(pending.commit(x, y));
    EXPECT_EQ(x.size(), 2u);
    EXPECT_FLOAT_EQ(x[0], 0.0f);
    EXPECT_FLOAT_EQ(x[1], 1.0f);
}

TEST(PendingSeriesData, ReplaceX)
{
    PendingSeriesData      pending;
    std::vector<float> x = {1.0f, 2.0f};
    std::vector<float> y = {10.0f, 20.0f};

    std::vector<float> new_x = {5.0f, 6.0f, 7.0f};
    pending.replace_x(new_x);
    EXPECT_TRUE(pending.commit(x, y));

    EXPECT_EQ(x.size(), 3u);
    EXPECT_FLOAT_EQ(x[0], 5.0f);
    // y unchanged
    EXPECT_EQ(y.size(), 2u);
}

TEST(PendingSeriesData, ReplaceY)
{
    PendingSeriesData      pending;
    std::vector<float> x = {1.0f, 2.0f};
    std::vector<float> y = {10.0f, 20.0f};

    std::vector<float> new_y = {100.0f};
    pending.replace_y(new_y);
    EXPECT_TRUE(pending.commit(x, y));

    // x unchanged
    EXPECT_EQ(x.size(), 2u);
    EXPECT_EQ(y.size(), 1u);
    EXPECT_FLOAT_EQ(y[0], 100.0f);
}

TEST(PendingSeriesData, EraseBefore)
{
    PendingSeriesData      pending;
    std::vector<float> x = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    std::vector<float> y = {10.0f, 20.0f, 30.0f, 40.0f, 50.0f};

    pending.erase_before(3.0f);
    EXPECT_TRUE(pending.commit(x, y));

    EXPECT_EQ(x.size(), 3u);
    EXPECT_FLOAT_EQ(x[0], 3.0f);
    EXPECT_FLOAT_EQ(y[0], 30.0f);
}

TEST(PendingSeriesData, EraseBeforeNoEffect)
{
    PendingSeriesData      pending;
    std::vector<float> x = {5.0f, 6.0f};
    std::vector<float> y = {50.0f, 60.0f};

    pending.erase_before(1.0f);   // threshold below all data
    // has_pending is true (erase was queued) but commit should detect no removal
    EXPECT_TRUE(pending.has_pending());
    EXPECT_FALSE(pending.commit(x, y));
    EXPECT_EQ(x.size(), 2u);
}

TEST(PendingSeriesData, CommitOrdering_ReplaceThenEraseThenAppend)
{
    PendingSeriesData      pending;
    std::vector<float> x, y;

    // Replace with sorted data
    std::vector<float> new_x = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    std::vector<float> new_y = {10.0f, 20.0f, 30.0f, 40.0f, 50.0f};
    pending.replace_x(new_x);
    pending.replace_y(new_y);

    // Erase points before 3.0
    pending.erase_before(3.0f);

    // Append two more points
    pending.append(6.0f, 60.0f);
    pending.append(7.0f, 70.0f);

    EXPECT_TRUE(pending.commit(x, y));

    // Expected result: [3, 4, 5, 6, 7]
    ASSERT_EQ(x.size(), 5u);
    EXPECT_FLOAT_EQ(x[0], 3.0f);
    EXPECT_FLOAT_EQ(x[1], 4.0f);
    EXPECT_FLOAT_EQ(x[2], 5.0f);
    EXPECT_FLOAT_EQ(x[3], 6.0f);
    EXPECT_FLOAT_EQ(x[4], 7.0f);

    ASSERT_EQ(y.size(), 5u);
    EXPECT_FLOAT_EQ(y[0], 30.0f);
    EXPECT_FLOAT_EQ(y[4], 70.0f);
}

TEST(PendingSeriesData, SecondCommitReturnsFalse)
{
    PendingSeriesData      pending;
    std::vector<float> x, y;

    pending.append(1.0f, 2.0f);
    EXPECT_TRUE(pending.commit(x, y));
    EXPECT_FALSE(pending.commit(x, y));
}

TEST(PendingSeriesData, WakeFnCalledOnce)
{
    PendingSeriesData pending;
    int               wake_count = 0;
    pending.set_wake_fn([&]() { ++wake_count; });

    pending.append(1.0f, 2.0f);   // false→true: wake
    pending.append(3.0f, 4.0f);   // already true: no wake
    EXPECT_EQ(wake_count, 1);

    // After commit, has_pending goes false
    std::vector<float> x, y;
    pending.commit(x, y);

    // Next mutation triggers wake again
    pending.append(5.0f, 6.0f);
    EXPECT_EQ(wake_count, 2);
}

TEST(PendingSeriesData, ConcurrentAppendAndCommit)
{
    PendingSeriesData      pending;
    std::vector<float> x, y;

    constexpr int num_threads = 4;
    constexpr int appends     = 5000;

    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t)
    {
        threads.emplace_back([&, t]()
        {
            for (int i = 0; i < appends; ++i)
            {
                float val = static_cast<float>(t * appends + i);
                pending.append(val, val * 10.0f);
            }
        });
    }
    for (auto& th : threads)
        th.join();

    EXPECT_TRUE(pending.commit(x, y));
    EXPECT_EQ(x.size(), static_cast<size_t>(num_threads * appends));
    EXPECT_EQ(y.size(), x.size());
}
