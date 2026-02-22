#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <new>
#include <type_traits>

namespace spectra
{

// Lock-free SPSC (Single-Producer Single-Consumer) ring buffer for cross-thread
// command passing. The producer (app thread) enqueues mutation commands, and the
// consumer (render thread) drains them at frame start.
//
// Commands are stored as type-erased std::function<void()>.
class CommandQueue
{
   public:
    static constexpr size_t DEFAULT_CAPACITY = 4096;

    explicit CommandQueue(size_t capacity = DEFAULT_CAPACITY)
        : capacity_(capacity), buffer_(new Slot[capacity])
    {
    }

    ~CommandQueue() { delete[] buffer_; }

    CommandQueue(const CommandQueue&)            = delete;
    CommandQueue& operator=(const CommandQueue&) = delete;

    // Producer side: enqueue a command. Returns false if the queue is full.
    bool push(std::function<void()> cmd)
    {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = (head + 1) % capacity_;

        if (next == tail_.load(std::memory_order_acquire))
        {
            return false;   // Full
        }

        buffer_[head].command = std::move(cmd);
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer side: dequeue a command. Returns false if the queue is empty.
    bool pop(std::function<void()>& out)
    {
        const size_t tail = tail_.load(std::memory_order_relaxed);

        if (tail == head_.load(std::memory_order_acquire))
        {
            return false;   // Empty
        }

        out = std::move(buffer_[tail].command);
        tail_.store((tail + 1) % capacity_, std::memory_order_release);
        return true;
    }

    // Consumer side: drain all pending commands, executing each one.
    size_t drain()
    {
        size_t                count = 0;
        std::function<void()> cmd;
        while (pop(cmd))
        {
            if (cmd)
            {
                cmd();
            }
            ++count;
        }
        return count;
    }

    bool empty() const
    {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

    size_t capacity() const { return capacity_; }

   private:
    struct Slot
    {
        std::function<void()> command;
    };

    const size_t capacity_;
    Slot*        buffer_;

    // Cache-line aligned to avoid false sharing
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
};

}   // namespace spectra
