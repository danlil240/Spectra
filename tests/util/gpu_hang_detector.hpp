#pragma once

// Timeout-based GPU hang detection for Spectra multi-window tests.
// Wraps a callable in a watchdog thread. If the callable does not
// complete within the timeout, the test is failed with a descriptive
// message (rather than hanging the CI runner indefinitely).
//
// Usage:
//   spectra::test::GpuHangDetector detector(std::chrono::seconds(5));
//   bool ok = detector.run("render two windows", [&]() {
//       app.run();
//   });
//   EXPECT_TRUE(ok) << detector.failure_reason();
//
// Day 0 scaffolding: no dependency on WindowContext or WindowManager.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <gtest/gtest.h>
#include <mutex>
#include <string>
#include <thread>

namespace spectra::test
{

class GpuHangDetector
{
   public:
    using Clock    = std::chrono::steady_clock;
    using Duration = Clock::duration;

    explicit GpuHangDetector(Duration timeout = std::chrono::seconds(10)) : timeout_(timeout) {}

    // Run a callable with hang detection.
    // Returns true if the callable completed within the timeout.
    // Returns false if it timed out (probable GPU hang / device lost).
    bool run(const std::string& description, std::function<void()> fn)
    {
        description_ = description;
        completed_.store(false, std::memory_order_relaxed);
        timed_out_.store(false, std::memory_order_relaxed);
        failure_reason_.clear();

        auto start = Clock::now();

        // Run the callable on the current thread, with a watchdog
        std::mutex              mtx;
        std::condition_variable cv;
        bool                    done = false;

        std::thread watchdog(
            [&]()
            {
                std::unique_lock<std::mutex> lock(mtx);
                if (cv.wait_for(lock, timeout_, [&]() { return done; }))
                {
                    // Completed in time
                    return;
                }
                // Timed out
                timed_out_.store(true, std::memory_order_relaxed);
            });

        // Execute the callable
        fn();

        auto elapsed = Clock::now() - start;
        elapsed_ms_  = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

        {
            std::lock_guard<std::mutex> lock(mtx);
            done = true;
        }
        cv.notify_one();
        watchdog.join();

        if (timed_out_.load(std::memory_order_relaxed))
        {
            failure_reason_ =
                "GPU hang detected: '" + description_ + "' did not complete within "
                + std::to_string(
                    std::chrono::duration_cast<std::chrono::milliseconds>(timeout_).count())
                + "ms (elapsed: " + std::to_string(elapsed_ms_) + "ms)";
            return false;
        }

        completed_.store(true, std::memory_order_relaxed);
        return true;
    }

    // Returns the failure reason if run() returned false
    const std::string& failure_reason() const { return failure_reason_; }

    // Returns elapsed time of last run() in milliseconds
    int64_t elapsed_ms() const { return elapsed_ms_; }

    // Returns true if last run() completed successfully
    bool completed() const { return completed_.load(std::memory_order_relaxed); }

    // Returns true if last run() timed out
    bool timed_out() const { return timed_out_.load(std::memory_order_relaxed); }

    // GTest assertion helper
    void expect_no_hang(const std::string& description, std::function<void()> fn)
    {
        bool ok = run(description, std::move(fn));
        EXPECT_TRUE(ok) << failure_reason();
    }

   private:
    Duration          timeout_;
    std::string       description_;
    std::string       failure_reason_;
    int64_t           elapsed_ms_ = 0;
    std::atomic<bool> completed_{false};
    std::atomic<bool> timed_out_{false};
};

// Convenience: run with default 10s timeout
inline bool run_with_hang_detection(const std::string&        description,
                                    std::function<void()>     fn,
                                    std::chrono::milliseconds timeout = std::chrono::seconds(10))
{
    GpuHangDetector detector(timeout);
    return detector.run(description, std::move(fn));
}

}   // namespace spectra::test
