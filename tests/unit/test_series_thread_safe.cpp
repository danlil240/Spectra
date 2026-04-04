// test_series_thread_safe.cpp — Multi-threaded tests for Series thread-safe mode.

#include <gtest/gtest.h>
#include <spectra/series.hpp>

#include <atomic>
#include <thread>
#include <vector>

using namespace spectra;

// ═══════════════════════════════════════════════════════════════════════════════
// LineSeries thread-safe mode
// ═══════════════════════════════════════════════════════════════════════════════

TEST(SeriesThreadSafe, DefaultIsNotThreadSafe)
{
    LineSeries s;
    EXPECT_FALSE(s.is_thread_safe());
}

TEST(SeriesThreadSafe, EnableThreadSafe)
{
    LineSeries s;
    s.set_thread_safe(true);
    EXPECT_TRUE(s.is_thread_safe());
}

TEST(SeriesThreadSafe, AppendRoutesToPending)
{
    LineSeries s;
    s.set_thread_safe(true);
    s.clear_dirty();

    s.append(1.0f, 10.0f);

    // Data not yet in x_/y_ — still pending
    EXPECT_EQ(s.point_count(), 0u);
    EXPECT_FALSE(s.is_dirty());

    // Commit applies the data
    EXPECT_TRUE(s.commit_pending());
    EXPECT_EQ(s.point_count(), 1u);
    EXPECT_FLOAT_EQ(s.x_data()[0], 1.0f);
    EXPECT_FLOAT_EQ(s.y_data()[0], 10.0f);
    EXPECT_TRUE(s.is_dirty());
}

TEST(SeriesThreadSafe, SetXRoutesToPending)
{
    LineSeries s;
    s.set_thread_safe(true);

    std::vector<float> new_x = {1.0f, 2.0f, 3.0f};
    s.set_x(new_x);

    EXPECT_EQ(s.point_count(), 0u);   // Not committed yet

    EXPECT_TRUE(s.commit_pending());
    EXPECT_EQ(s.x_data().size(), 3u);
    EXPECT_FLOAT_EQ(s.x_data()[1], 2.0f);
}

TEST(SeriesThreadSafe, SetYRoutesToPending)
{
    LineSeries s;
    s.set_thread_safe(true);

    std::vector<float> new_y = {10.0f, 20.0f};
    s.set_y(new_y);

    EXPECT_TRUE(s.commit_pending());
    EXPECT_EQ(s.y_data().size(), 2u);
    EXPECT_FLOAT_EQ(s.y_data()[0], 10.0f);
}

TEST(SeriesThreadSafe, EraseBeforeRoutesToPending)
{
    std::vector<float> init_x = {1.0f, 2.0f, 3.0f, 4.0f};
    std::vector<float> init_y = {10.0f, 20.0f, 30.0f, 40.0f};
    LineSeries s(init_x, init_y);
    s.set_thread_safe(true);
    s.clear_dirty();

    size_t result = s.erase_before(3.0f);
    EXPECT_EQ(result, 0u);   // Returns 0 in thread-safe mode

    // Data unchanged until commit
    EXPECT_EQ(s.point_count(), 4u);

    EXPECT_TRUE(s.commit_pending());
    EXPECT_EQ(s.point_count(), 2u);
    EXPECT_FLOAT_EQ(s.x_data()[0], 3.0f);
}

TEST(SeriesThreadSafe, CommitPendingReturnsFalseWhenNoPending)
{
    LineSeries s;
    s.set_thread_safe(true);
    EXPECT_FALSE(s.commit_pending());
}

TEST(SeriesThreadSafe, CommitPendingWithoutThreadSafe)
{
    LineSeries s;
    EXPECT_FALSE(s.commit_pending());
}

TEST(SeriesThreadSafe, NonThreadSafeAppendStillDirectlyMutates)
{
    LineSeries s;
    s.clear_dirty();

    s.append(1.0f, 10.0f);
    EXPECT_EQ(s.point_count(), 1u);
    EXPECT_TRUE(s.is_dirty());
}

// ═══════════════════════════════════════════════════════════════════════════════
// ScatterSeries thread-safe mode
// ═══════════════════════════════════════════════════════════════════════════════

TEST(SeriesThreadSafe, ScatterAppendRoutesToPending)
{
    ScatterSeries s;
    s.set_thread_safe(true);
    s.clear_dirty();

    s.append(1.0f, 10.0f);
    EXPECT_EQ(s.point_count(), 0u);

    EXPECT_TRUE(s.commit_pending());
    EXPECT_EQ(s.point_count(), 1u);
    EXPECT_TRUE(s.is_dirty());
}

TEST(SeriesThreadSafe, ScatterSetXRoutesToPending)
{
    ScatterSeries s;
    s.set_thread_safe(true);

    std::vector<float> new_x = {5.0f, 6.0f};
    s.set_x(new_x);

    EXPECT_TRUE(s.commit_pending());
    EXPECT_EQ(s.x_data().size(), 2u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Multi-threaded stress tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(SeriesThreadSafe, ConcurrentAppend)
{
    LineSeries s;
    s.set_thread_safe(true);

    constexpr int num_threads = 4;
    constexpr int appends     = 5000;

    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t)
    {
        threads.emplace_back([&s, t]()
        {
            for (int i = 0; i < appends; ++i)
            {
                float val = static_cast<float>(t * appends + i);
                s.append(val, val * 10.0f);
            }
        });
    }
    for (auto& th : threads)
        th.join();

    EXPECT_TRUE(s.commit_pending());
    EXPECT_EQ(s.point_count(), static_cast<size_t>(num_threads * appends));

    // Verify y values are consistent with x values
    for (size_t i = 0; i < s.point_count(); ++i)
    {
        EXPECT_FLOAT_EQ(s.y_data()[i], s.x_data()[i] * 10.0f);
    }
}

TEST(SeriesThreadSafe, ConcurrentAppendWithIntermediateCommits)
{
    LineSeries s;
    s.set_thread_safe(true);

    constexpr int num_threads = 2;
    constexpr int appends     = 10000;

    std::atomic<int> threads_done{0};

    // Producer threads
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t)
    {
        threads.emplace_back([&s, t, &threads_done]()
        {
            for (int i = 0; i < appends; ++i)
            {
                float val = static_cast<float>(t * appends + i);
                s.append(val, val);
            }
            threads_done.fetch_add(1, std::memory_order_release);
        });
    }

    // "Main thread" periodically commits while producers are running
    while (threads_done.load(std::memory_order_acquire) < num_threads)
    {
        s.commit_pending();
        std::this_thread::yield();
    }

    for (auto& th : threads)
        th.join();

    // Final commit to get any remaining data
    s.commit_pending();

    EXPECT_EQ(s.point_count(), static_cast<size_t>(num_threads * appends));
}

TEST(SeriesThreadSafe, WakeFnIntegration)
{
    LineSeries s;
    s.set_thread_safe(true);

    int wake_count = 0;
    s.set_wake_fn([&]() { ++wake_count; });

    s.append(1.0f, 2.0f);
    EXPECT_EQ(wake_count, 1);

    s.append(3.0f, 4.0f);
    EXPECT_EQ(wake_count, 1);   // Still pending, no re-wake

    s.commit_pending();

    s.append(5.0f, 6.0f);
    EXPECT_EQ(wake_count, 2);   // New batch triggers wake
}
