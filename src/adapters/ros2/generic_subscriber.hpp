#pragma once

// GenericSubscriber — subscribe any ROS2 topic and extract numeric fields.
//
// Uses rclcpp::GenericSubscription to receive serialized CDR messages for any
// topic without compile-time type knowledge.  Multiple FieldExtractors can be
// attached to a single subscription (one subscriber, many outputs).
//
// Each FieldExtractor has its own SPSC lock-free ring buffer of
// (timestamp_ns, value) pairs.  The render thread reads from the ring buffer
// without blocking the ROS2 executor thread.
//
// Typical usage:
//   MessageIntrospector intr;
//   auto schema = intr.introspect("geometry_msgs/msg/Twist");
//   auto accessor_x = intr.make_accessor(*schema, "linear.x");
//   auto accessor_y = intr.make_accessor(*schema, "linear.y");
//
//   GenericSubscriber sub(node, "/cmd_vel", "geometry_msgs/msg/Twist",
//                         intr, /*buffer_depth=*/10000);
//   int id_x = sub.add_field("linear.x", accessor_x);
//   int id_y = sub.add_field("linear.y", accessor_y);
//   sub.start();
//
//   // Render thread:
//   FieldSample s;
//   while (sub.pop(id_x, s)) { /* use s.timestamp_ns, s.value */ }
//
// Thread-safety:
//   - add_field() / remove_field() must be called before start() or after
//     stop().  Calling them while running is not safe.
//   - start() / stop() are thread-safe with respect to each other.
//   - pop() is safe to call from any thread concurrently with the ROS2
//     executor callback (SPSC ring buffer: one producer, one consumer per
//     extractor).
//   - stats() is atomic and safe to call from any thread.

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <rclcpp/rclcpp.hpp>

#include "message_introspector.hpp"

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// FieldSample — one extracted data point.
// ---------------------------------------------------------------------------

struct FieldSample
{
    int64_t timestamp_ns{0};   // ROS2 message header stamp, or wall clock if absent
    double  value{0.0};
};

// ---------------------------------------------------------------------------
// RingBuffer — SPSC lock-free ring buffer of FieldSample.
//
// One producer (ROS2 executor thread) and one consumer (render/poll thread).
// Power-of-two capacity; oldest samples dropped when full.
// ---------------------------------------------------------------------------

class RingBuffer
{
   public:
    explicit RingBuffer(size_t capacity);

    // Push a sample (producer side).  If full, drops oldest entry (overwrites).
    void push(const FieldSample& s);

    // Pop the oldest sample (consumer side).
    // Returns false if the buffer is empty.
    bool pop(FieldSample& out);

    // Peek at all available samples without consuming them.
    // Fills `out` with up to max_count samples; returns actual count written.
    size_t peek(FieldSample* out, size_t max_count) const;

    // Number of samples currently in the buffer (approximate).
    size_t size() const;

    // Buffer capacity (constant after construction).
    size_t capacity() const { return capacity_; }

    // Drain all samples, discarding them.
    void clear();

   private:
    const size_t capacity_;   // must be power-of-two
    const size_t mask_;

    // Each slot: one sample.
    std::vector<FieldSample> data_;

    // Head = next write position; Tail = next read position.
    // Producer owns head_; consumer owns tail_.
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
};

// ---------------------------------------------------------------------------
// FieldExtractor — one named field + its ring buffer.
// ---------------------------------------------------------------------------

struct FieldExtractor
{
    int           id{-1};
    std::string   field_path;
    FieldAccessor accessor;
    RingBuffer    ring;

    FieldExtractor(int id_, std::string path, FieldAccessor acc, size_t depth)
        : id(id_), field_path(std::move(path)), accessor(std::move(acc)), ring(depth)
    {
    }

    // Non-copyable (owns ring buffer storage).
    FieldExtractor(const FieldExtractor&)            = delete;
    FieldExtractor& operator=(const FieldExtractor&) = delete;
    FieldExtractor(FieldExtractor&&)                 = delete;
    FieldExtractor& operator=(FieldExtractor&&)      = delete;
};

// ---------------------------------------------------------------------------
// SubscriberStats — counters exposed for diagnostics / Hz computation.
// ---------------------------------------------------------------------------

struct SubscriberStats
{
    uint64_t messages_received{0};   // total messages received
    uint64_t messages_dropped{0};    // messages where deserialization failed
    uint64_t samples_written{0};     // total field samples pushed to ring buffers
    uint64_t samples_dropped{0};     // samples dropped due to full ring buffer
};

// ---------------------------------------------------------------------------
// GenericSubscriber — main class.
// ---------------------------------------------------------------------------

class GenericSubscriber
{
   public:
    // Construct but do not subscribe yet.
    // node       — ROS2 node to create the subscription on (must outlive this)
    // topic      — fully-qualified topic name, e.g. "/cmd_vel"
    // type_name  — ROS2 message type, e.g. "geometry_msgs/msg/Twist"
    // intr       — MessageIntrospector (shared; must outlive this)
    // buffer_depth — ring buffer capacity per field (default 10000; rounded up
    //               to next power-of-two)
    GenericSubscriber(rclcpp::Node::SharedPtr node,
                      std::string             topic,
                      std::string             type_name,
                      MessageIntrospector&    intr,
                      size_t                  buffer_depth = 10000);

    ~GenericSubscriber();

    // Non-copyable, non-movable (owns ring buffers + subscription).
    GenericSubscriber(const GenericSubscriber&)            = delete;
    GenericSubscriber& operator=(const GenericSubscriber&) = delete;
    GenericSubscriber(GenericSubscriber&&)                 = delete;
    GenericSubscriber& operator=(GenericSubscriber&&)      = delete;

    // ---------- field management (call before start()) -------------------

    // Add a field extractor.  The accessor must have been created from the
    // same message type.  Returns an extractor ID (>= 0) on success, or -1
    // if the accessor is invalid.
    // Must be called before start() (or after stop()).
    int add_field(const std::string& field_path, const FieldAccessor& accessor);

    // Add a field by path only — introspects the schema automatically.
    // Returns -1 if the field path is not found or not numeric.
    int add_field(const std::string& field_path);

    // Remove an extractor by ID.  Silently ignored if not found.
    // Must be called before start() (or after stop()).
    void remove_field(int extractor_id);

    // Number of registered field extractors.
    size_t field_count() const { return extractors_.size(); }

    // ---------- lifecycle -------------------------------------------------

    // Create the ROS2 subscription and start receiving messages.
    // Idempotent (does nothing if already started).
    // Returns false if the schema could not be introspected.
    bool start();

    // Destroy the ROS2 subscription.  Ring buffers are NOT cleared;
    // existing samples can still be popped after stop().
    // Idempotent (does nothing if not started).
    void stop();

    bool is_running() const { return running_.load(std::memory_order_acquire); }

    // ---------- data access (consumer / render thread) -------------------

    // Pop the oldest sample from the named extractor's ring buffer.
    // Returns false if the buffer is empty or the id is invalid.
    bool pop(int extractor_id, FieldSample& out);

    // Pop up to max_count samples into `out` array.
    // Returns count actually written.
    size_t pop_bulk(int extractor_id, FieldSample* out, size_t max_count);

    // Peek at samples without consuming them.
    size_t peek(int extractor_id, FieldSample* out, size_t max_count) const;

    // Number of samples waiting in a ring buffer.
    size_t pending(int extractor_id) const;

    // ---------- accessors ------------------------------------------------

    const std::string& topic() const { return topic_; }
    const std::string& type_name() const { return type_name_; }
    size_t             buffer_depth() const { return buffer_depth_; }

    // Snapshot of stats (thread-safe approximate read).
    SubscriberStats stats() const;

    // ---------- callbacks -------------------------------------------------

    // Called (from executor thread) each time a message arrives.
    // Signature: void(const SubscriberStats&)
    using MessageCallback = std::function<void(const SubscriberStats&)>;
    void set_message_callback(MessageCallback cb) { msg_cb_ = std::move(cb); }

   private:
    // Called by rclcpp on each incoming message.
    void on_message(std::shared_ptr<rclcpp::SerializedMessage> msg);

    // Extract a wall-clock timestamp in nanoseconds.
    static int64_t wall_clock_ns();

    // Find extractor by id; returns nullptr if not found.
    FieldExtractor*       find_extractor(int id);
    const FieldExtractor* find_extractor(int id) const;

    // Round up to next power-of-two (minimum 16).
    static size_t next_pow2(size_t n);

    // ---------- members ---------------------------------------------------

    rclcpp::Node::SharedPtr node_;
    std::string             topic_;
    std::string             type_name_;
    MessageIntrospector&    intr_;
    size_t                  buffer_depth_;

    std::shared_ptr<const MessageSchema> schema_;

    // Extractor list — built before start(), read-only during running.
    std::vector<std::unique_ptr<FieldExtractor>> extractors_;
    int                                          next_id_{0};

    rclcpp::GenericSubscription::SharedPtr subscription_;
    std::atomic<bool>                      running_{false};

    // Stats (updated atomically by executor thread, read by any thread).
    std::atomic<uint64_t> stat_received_{0};
    std::atomic<uint64_t> stat_dropped_{0};
    std::atomic<uint64_t> stat_written_{0};
    std::atomic<uint64_t> stat_ring_dropped_{0};

    MessageCallback msg_cb_;
};

}   // namespace spectra::adapters::ros2
