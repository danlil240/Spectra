#pragma once

#include <atomic>
#if defined(_MSC_VER)
    #include <intrin.h>
#endif

namespace spectra
{

// ═══════════════════════════════════════════════════════════════════════════════
// SpinLock — lightweight spinlock for very short critical sections (<1μs).
//
// Uses std::atomic_flag (guaranteed lock-free) with acquire/release ordering.
// No kernel syscalls, pure user-space spinning.  Suitable for protecting
// vector push_back, move-assign, and similar sub-microsecond operations.
//
// Usage:
//   SpinLock lock;
//   {
//       SpinLockGuard guard(lock);
//       // ... critical section ...
//   }
// ═══════════════════════════════════════════════════════════════════════════════

class SpinLock
{
   public:
    SpinLock()  = default;
    ~SpinLock() = default;

    SpinLock(const SpinLock&)            = delete;
    SpinLock& operator=(const SpinLock&) = delete;

    void lock() noexcept
    {
        while (flag_.test_and_set(std::memory_order_acquire))
        {
            // Spin — hint to the CPU that we are in a spin-wait loop.
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
            _mm_pause();
#elif defined(__x86_64__) || defined(__i386__)
            __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(_M_ARM64)
            asm volatile("yield" ::: "memory");
#endif
        }
    }

    bool try_lock() noexcept { return !flag_.test_and_set(std::memory_order_acquire); }

    void unlock() noexcept { flag_.clear(std::memory_order_release); }

   private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

// RAII guard for SpinLock.
class SpinLockGuard
{
   public:
    explicit SpinLockGuard(SpinLock& lock) noexcept : lock_(lock) { lock_.lock(); }
    ~SpinLockGuard() noexcept { lock_.unlock(); }

    SpinLockGuard(const SpinLockGuard&)            = delete;
    SpinLockGuard& operator=(const SpinLockGuard&) = delete;

   private:
    SpinLock& lock_;
};

}   // namespace spectra
