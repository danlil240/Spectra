// generic_subscriber.cpp — subscribe any ROS2 topic; extract numeric fields
// into SPSC lock-free ring buffers.
//
// The ROS2 executor thread is the sole producer for all ring buffers.
// The render / polling thread is the sole consumer.  No mutex is held during
// message delivery; atomic head/tail indices guarantee correct ordering.

#include "generic_subscriber.hpp"

#include <cassert>
#include <chrono>
#include <cstring>

#include <rclcpp/serialized_message.hpp>
#include <rclcpp/serialization.hpp>
#include <rmw/types.h>
#include <rosidl_runtime_cpp/message_type_support_decl.hpp>

// CDR deserialization into a raw byte buffer uses the generic type support.
#include <rclcpp/generic_subscription.hpp>

#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>
#include <rosidl_runtime_cpp/message_initialization.hpp>

namespace spectra::adapters::ros2
{

// ===========================================================================
// RingBuffer
// ===========================================================================

// Round up to next power-of-two; minimum 16.
static size_t ring_next_pow2(size_t n)
{
    if (n < 16) return 16;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n |= n >> 32;
    return n + 1;
}

RingBuffer::RingBuffer(size_t capacity)
    : capacity_(ring_next_pow2(capacity))
    , mask_(capacity_ - 1)
    , data_(capacity_)
{}

void RingBuffer::push(const FieldSample& s)
{
    const size_t h = head_.load(std::memory_order_relaxed);
    const size_t t = tail_.load(std::memory_order_acquire);

    if (h - t >= capacity_)
    {
        // Buffer full — advance tail to drop oldest sample.
        tail_.store(t + 1, std::memory_order_release);
    }

    data_[h & mask_] = s;
    head_.store(h + 1, std::memory_order_release);
}

bool RingBuffer::pop(FieldSample& out)
{
    const size_t t = tail_.load(std::memory_order_relaxed);
    const size_t h = head_.load(std::memory_order_acquire);

    if (t == h)
        return false;

    out = data_[t & mask_];
    tail_.store(t + 1, std::memory_order_release);
    return true;
}

size_t RingBuffer::peek(FieldSample* out, size_t max_count) const
{
    const size_t t = tail_.load(std::memory_order_acquire);
    const size_t h = head_.load(std::memory_order_acquire);
    const size_t avail = h - t;
    const size_t count = (avail < max_count) ? avail : max_count;
    for (size_t i = 0; i < count; ++i)
        out[i] = data_[(t + i) & mask_];
    return count;
}

size_t RingBuffer::size() const
{
    const size_t t = tail_.load(std::memory_order_acquire);
    const size_t h = head_.load(std::memory_order_acquire);
    return (h >= t) ? (h - t) : 0;
}

void RingBuffer::clear()
{
    const size_t h = head_.load(std::memory_order_acquire);
    tail_.store(h, std::memory_order_release);
}

// ===========================================================================
// GenericSubscriber
// ===========================================================================

GenericSubscriber::GenericSubscriber(rclcpp::Node::SharedPtr node,
                                     std::string             topic,
                                     std::string             type_name,
                                     MessageIntrospector&    intr,
                                     size_t                  buffer_depth)
    : node_(std::move(node))
    , topic_(std::move(topic))
    , type_name_(std::move(type_name))
    , intr_(intr)
    , buffer_depth_(ring_next_pow2(buffer_depth))
{}

GenericSubscriber::~GenericSubscriber()
{
    stop();
}

// ---------------------------------------------------------------------------
// Field management
// ---------------------------------------------------------------------------

int GenericSubscriber::add_field(const std::string& field_path,
                                 const FieldAccessor& accessor)
{
    if (!accessor.valid())
        return -1;

    const int id = next_id_++;
    extractors_.push_back(
        std::make_unique<FieldExtractor>(id, field_path, accessor, buffer_depth_));
    return id;
}

int GenericSubscriber::add_field(const std::string& field_path)
{
    // Lazily introspect schema if not yet done.
    if (!schema_)
        schema_ = intr_.introspect(type_name_);

    if (!schema_)
        return -1;

    FieldAccessor acc = intr_.make_accessor(*schema_, field_path);
    if (!acc.valid())
        return -1;

    return add_field(field_path, acc);
}

void GenericSubscriber::remove_field(int extractor_id)
{
    auto it = std::find_if(extractors_.begin(), extractors_.end(),
        [extractor_id](const auto& e){ return e->id == extractor_id; });
    if (it != extractors_.end())
        extractors_.erase(it);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool GenericSubscriber::start()
{
    if (running_.load(std::memory_order_acquire))
        return true;

    // Introspect schema (needed for field extraction during message delivery).
    if (!schema_)
        schema_ = intr_.introspect(type_name_);

    if (!schema_ && !extractors_.empty())
    {
        // Cannot extract fields without schema; still allow zero-extractor
        // subscriptions used only for statistics.
    }

    // Create the generic subscription.
    // QoS: best_effort sensor data — matches most ROS2 sensor topics.
    auto qos = rclcpp::SystemDefaultsQoS();
    subscription_ = node_->create_generic_subscription(
        topic_,
        type_name_,
        qos,
        [this](std::shared_ptr<rclcpp::SerializedMessage> msg)
        {
            on_message(std::move(msg));
        });

    if (!subscription_)
        return false;

    running_.store(true, std::memory_order_release);
    return true;
}

void GenericSubscriber::stop()
{
    if (!running_.load(std::memory_order_acquire))
        return;

    running_.store(false, std::memory_order_release);
    subscription_.reset();
}

// ---------------------------------------------------------------------------
// Message delivery (executor thread)
// ---------------------------------------------------------------------------

void GenericSubscriber::on_message(std::shared_ptr<rclcpp::SerializedMessage> serialized_msg)
{
    stat_received_.fetch_add(1, std::memory_order_relaxed);

    if (extractors_.empty())
    {
        if (msg_cb_)
            msg_cb_(stats());
        return;
    }

    if (!schema_)
    {
        stat_dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // Deserialize CDR payload into a plain byte buffer.
    // rclcpp::GenericSubscription delivers the raw serialized CDR bytes.
    // We need a deserialized in-memory struct to use the FieldAccessor offsets.
    // Strategy: use rcl_serialized_message_t raw buffer + CDR deserializer.
    //
    // The rosidl_typesupport_introspection_cpp type support includes a
    // deserialize function.  We call it via the type support handle stored
    // in the schema's type support pointer.
    //
    // Simpler and portable: use rclcpp's TypedMessageFactory with the
    // serialization API.  Since we don't have a compile-time type, we use
    // the raw CDR buffer directly.
    //
    // For field extraction we need the deserialized struct bytes.
    // We allocate a buffer of the right size (from the rosidl members struct)
    // and call the type support's init + deserialize functions.

    // Retrieve type support for deserialization.
    // schema_ was built from the type support; we need the raw handle again.
    // Re-use introspector to get the type support pointer.
    auto ts_schema = intr_.introspect(type_name_);
    if (!ts_schema)
    {
        stat_dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // Use rclcpp::get_typesupport_handle to obtain the type support needed
    // for deserialization, then use the serialization callback.
    //
    // Portable approach that works without knowing the C++ type at compile
    // time: ask rclcpp for the serialization type support and call its
    // deserialize function via the rcutils allocator path.
    //
    // rclcpp::SerializedMessage wraps an rcl_serialized_message_t.
    // We can access the raw CDR bytes via serialized_msg->get_rcl_serialized_message().
    //
    // We rely on the fact that rclcpp::GenericSubscription stores a type
    // support library handle; however the public API exposes only the
    // serialized bytes.  To deserialize into a raw struct we go through
    // the type support introspection library's init_function and
    // deserializer.  These are stored in the rosidl_message_type_support_t
    // data pointer (cast to MessageMembers).

    // Get the introspection type support to read size/init/fini functions.
    // MessageIntrospector::introspect_type_support already loaded the lib;
    // we call it again (cache hit) to get the handle.
    // However, MessageIntrospector does not expose the raw ts pointer publicly.
    // We use a workaround: rclcpp provides
    //   rclcpp::get_typesupport_library(type_name, "rosidl_typesupport_introspection_cpp")
    // and the corresponding handle.

    // Obtain the type support for introspection.
    std::shared_ptr<rcpputils::SharedLibrary> ts_lib;
    const rosidl_message_type_support_t* ts_handle = nullptr;
    try
    {
        ts_lib = rclcpp::get_typesupport_library(
            type_name_, "rosidl_typesupport_introspection_cpp");
        ts_handle = rclcpp::get_typesupport_handle(
            type_name_, "rosidl_typesupport_introspection_cpp", *ts_lib);
    }
    catch (const std::exception&)
    {
        stat_dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    if (!ts_handle)
    {
        stat_dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // Cast to MessageMembers to read size_of_ and call init_function.
    using namespace rosidl_typesupport_introspection_cpp;
    const auto* members =
        static_cast<const MessageMembers*>(ts_handle->data);

    if (!members || members->size_of_ == 0)
    {
        stat_dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // Allocate and initialise the message buffer.
    std::vector<uint8_t> msg_buf(members->size_of_, 0);
    if (members->init_function)
        members->init_function(msg_buf.data(), rosidl_runtime_cpp::MessageInitialization::ZERO);

    // Deserialize CDR bytes into the buffer.
    // rclcpp's SerializationBase provides the deserialize path.
    {
        rclcpp::SerializationBase serializer(ts_handle);
        try
        {
            serializer.deserialize_message(serialized_msg.get(), msg_buf.data());
        }
        catch (const std::exception&)
        {
            if (members->fini_function)
                members->fini_function(msg_buf.data());
            stat_dropped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }

    // Determine timestamp.
    // Prefer the message's header.stamp field if available.
    // Fall back to wall clock.
    int64_t ts_ns = wall_clock_ns();

    // Try to read header.stamp.sec / header.stamp.nanosec (standard pattern).
    {
        FieldAccessor sec_acc, nsec_acc;
        if (schema_)
        {
            sec_acc  = intr_.make_accessor(*schema_, "header.stamp.sec");
            nsec_acc = intr_.make_accessor(*schema_, "header.stamp.nanosec");
        }
        if (sec_acc.valid() && nsec_acc.valid())
        {
            const int64_t sec  = sec_acc.extract_int64(msg_buf.data());
            const int64_t nsec = nsec_acc.extract_int64(msg_buf.data());
            ts_ns = sec * int64_t(1000000000) + nsec;
        }
    }

    // Push extracted values to each ring buffer.
    for (const auto& ex : extractors_)
    {
        if (!ex->accessor.valid())
            continue;

        const double val = ex->accessor.extract_double(msg_buf.data());

        FieldSample sample;
        sample.timestamp_ns = ts_ns;
        sample.value        = val;

        const size_t before = ex->ring.size();
        ex->ring.push(sample);
        const size_t after  = ex->ring.size();

        stat_written_.fetch_add(1, std::memory_order_relaxed);
        // If size did not grow the ring was full and a sample was silently
        // replaced (push() drops oldest).  Count as dropped.
        if (after <= before)
            stat_ring_dropped_.fetch_add(1, std::memory_order_relaxed);
    }

    // Call fini to release any heap allocations inside the message struct
    // (e.g. std::string, std::vector).
    if (members->fini_function)
        members->fini_function(msg_buf.data());

    if (msg_cb_)
        msg_cb_(stats());
}

// ---------------------------------------------------------------------------
// Data access
// ---------------------------------------------------------------------------

bool GenericSubscriber::pop(int extractor_id, FieldSample& out)
{
    FieldExtractor* ex = find_extractor(extractor_id);
    if (!ex)
        return false;
    return ex->ring.pop(out);
}

size_t GenericSubscriber::pop_bulk(int extractor_id, FieldSample* out, size_t max_count)
{
    FieldExtractor* ex = find_extractor(extractor_id);
    if (!ex)
        return 0;

    size_t count = 0;
    while (count < max_count && ex->ring.pop(out[count]))
        ++count;
    return count;
}

size_t GenericSubscriber::peek(int extractor_id, FieldSample* out, size_t max_count) const
{
    const FieldExtractor* ex = find_extractor(extractor_id);
    if (!ex)
        return 0;
    return ex->ring.peek(out, max_count);
}

size_t GenericSubscriber::pending(int extractor_id) const
{
    const FieldExtractor* ex = find_extractor(extractor_id);
    return ex ? ex->ring.size() : 0;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

SubscriberStats GenericSubscriber::stats() const
{
    SubscriberStats s;
    s.messages_received = stat_received_.load(std::memory_order_relaxed);
    s.messages_dropped  = stat_dropped_.load(std::memory_order_relaxed);
    s.samples_written   = stat_written_.load(std::memory_order_relaxed);
    s.samples_dropped   = stat_ring_dropped_.load(std::memory_order_relaxed);
    return s;
}

FieldExtractor* GenericSubscriber::find_extractor(int id)
{
    for (auto& e : extractors_)
        if (e->id == id) return e.get();
    return nullptr;
}

const FieldExtractor* GenericSubscriber::find_extractor(int id) const
{
    for (const auto& e : extractors_)
        if (e->id == id) return e.get();
    return nullptr;
}

int64_t GenericSubscriber::wall_clock_ns()
{
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch())
        .count());
}

size_t GenericSubscriber::next_pow2(size_t n)
{
    return ring_next_pow2(n);
}

}   // namespace spectra::adapters::ros2
