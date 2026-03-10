#pragma once

// MessageIntrospector — runtime message schema discovery and field extraction.
//
// Uses rosidl_typesupport_introspection_cpp to build a MessageSchema tree from
// any ROS2 message type at runtime (no codegen required).  A FieldAccessor then
// extracts numeric values from the raw serialized CDR bytes using the offsets
// recorded in the schema.
//
// Typical usage:
//   MessageIntrospector intr;
//   auto schema = intr.introspect("geometry_msgs/msg/Twist");
//   // schema is a tree of FieldDescriptor nodes
//
//   // For deserialized (in-memory) messages:
//   FieldAccessor acc = intr.make_accessor(*schema, "linear.x");
//   double val = acc.extract_double(msg_ptr);
//
// Thread-safety: introspect() is thread-safe (mutex-protected cache).
//                FieldAccessor::extract_* are const and thread-safe after creation.

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations from rosidl introspection — included in .cpp only.
struct rosidl_message_type_support_t;

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// FieldType — mirrors rosidl_typesupport_introspection_cpp field type IDs.
// ---------------------------------------------------------------------------

enum class FieldType : uint8_t
{
    Bool    = 1,
    Byte    = 2,
    Char    = 3,
    Float32 = 4,
    Float64 = 5,
    Int8    = 6,
    Uint8   = 7,
    Int16   = 8,
    Uint16  = 9,
    Int32   = 10,
    Uint32  = 11,
    Int64   = 12,
    Uint64  = 13,
    String  = 14,
    WString = 15,
    Message = 16,   // nested message
    Unknown = 255,
};

// Human-readable name for a FieldType.
const char* field_type_name(FieldType t);

// Returns true if the field type is a numeric scalar extractable as double.
bool is_numeric(FieldType t);

// ---------------------------------------------------------------------------
// FieldDescriptor — one node in the schema tree.
// ---------------------------------------------------------------------------

struct FieldDescriptor
{
    std::string name;        // field name (leaf)
    std::string full_path;   // dot-separated path from root, e.g. "linear.x"
    FieldType   type{FieldType::Unknown};

    bool     is_array{false};
    bool     is_dynamic_array{false};   // true = std::vector<T>
    uint32_t array_size{0};             // 0 = dynamic; N = fixed N elements
    uint32_t type_id{0};                // raw rosidl type ID (for internal use)

    // Byte offset of this field within its parent message struct.
    size_t offset{0};

    // For nested messages: children describe the sub-struct fields.
    std::vector<FieldDescriptor> children;

    // Convenience: true if this is a leaf numeric field.
    bool is_numeric_leaf() const { return children.empty() && is_numeric(type); }
};

// ---------------------------------------------------------------------------
// MessageSchema — root of the introspected schema tree for one message type.
// ---------------------------------------------------------------------------

struct MessageSchema
{
    std::string                  type_name;   // e.g. "geometry_msgs/msg/Twist"
    std::vector<FieldDescriptor> fields;      // top-level fields

    // Find a FieldDescriptor by dot-separated path.
    // Returns nullptr if the path does not exist.
    const FieldDescriptor* find_field(const std::string& path) const;

    // Collect all leaf numeric field paths into a flat vector.
    std::vector<std::string> numeric_paths() const;
};

// ---------------------------------------------------------------------------
// FieldAccessor — extracts a single numeric field from an in-memory message.
//
// Created via MessageIntrospector::make_accessor().  Stores the chain of byte
// offsets needed to walk from the message root to the target leaf field.
// Array index can be specified at extraction time.
// ---------------------------------------------------------------------------

class FieldAccessor
{
   public:
    // Default-constructed accessor is invalid; extract_* will return NaN.
    FieldAccessor() = default;

    // Returns true if this accessor can extract a value.
    bool valid() const { return !steps_.empty() || leaf_type_ != FieldType::Unknown; }

    // The dot-separated field path this accessor was built for.
    const std::string& path() const { return path_; }

    // The leaf field type.
    FieldType leaf_type() const { return leaf_type_; }

    // Extract the field value as double from a pointer to the message struct.
    // msg_ptr must point to the beginning of the top-level message object.
    // array_index: element index for array fields (0 for scalars).
    // Returns NaN on any error.
    double extract_double(const void* msg_ptr, size_t array_index = 0) const;

    // Extract as raw int64 (useful for integer fields without precision loss).
    // Returns 0 on any error.
    int64_t extract_int64(const void* msg_ptr, size_t array_index = 0) const;

    // Returns true if the leaf field is an array.
    bool     is_array() const { return is_array_; }
    bool     is_dynamic_array() const { return is_dynamic_array_; }
    uint32_t array_size() const { return array_size_; }

   private:
    friend class MessageIntrospector;

    // One hop in the offset chain.
    struct Step
    {
        size_t offset{0};           // byte offset within parent struct
        bool   is_dynamic{false};   // true = field is a std::vector<T>
    };

    std::string       path_;
    std::vector<Step> steps_;   // chain from root → parent of leaf
    size_t            leaf_offset_{0};
    FieldType         leaf_type_{FieldType::Unknown};
    bool              is_array_{false};
    bool              is_dynamic_array_{false};
    uint32_t          array_size_{0};
    uint32_t          element_size_{0};   // sizeof(T) for array elements
};

// ---------------------------------------------------------------------------
// MessageIntrospector — builds MessageSchema trees and FieldAccessors.
// ---------------------------------------------------------------------------

class MessageIntrospector
{
   public:
    MessageIntrospector()  = default;
    ~MessageIntrospector() = default;

    // Non-copyable, movable.
    MessageIntrospector(const MessageIntrospector&)            = delete;
    MessageIntrospector& operator=(const MessageIntrospector&) = delete;
    MessageIntrospector(MessageIntrospector&&)                 = delete;
    MessageIntrospector& operator=(MessageIntrospector&&)      = delete;

    // ---------- schema introspection ------------------------------------

    // Introspect a message type given its full ROS2 type string.
    // type_name format: "package/msg/TypeName" e.g. "std_msgs/msg/Float64"
    //
    // Returns a shared_ptr to the schema (cached after first call).
    // Returns nullptr if the type is not found or introspection fails.
    std::shared_ptr<const MessageSchema> introspect(const std::string& type_name);

    // Introspect directly from a rosidl type support pointer.
    // Useful when you already have the type support (e.g. from a subscription).
    std::shared_ptr<const MessageSchema> introspect_type_support(
        const rosidl_message_type_support_t* ts,
        const std::string&                   type_name = "");

    // ---------- accessor creation ---------------------------------------

    // Create a FieldAccessor for a specific field path within a schema.
    // path: dot-separated field path, e.g. "linear.x" or "orientation.w"
    //       For array element access use e.g. "data" and pass array_index to extract.
    // Returns an invalid accessor if the path is not found or the field is
    // not numeric.
    FieldAccessor make_accessor(const MessageSchema& schema, const std::string& path) const;

    // ---------- cache management ----------------------------------------

    // Clear the introspection cache.
    void clear_cache();

    // Number of cached schemas.
    std::size_t cache_size() const;

   private:
    // Build a schema from a raw introspection type support pointer.
    static std::shared_ptr<MessageSchema> build_schema(const rosidl_message_type_support_t* ts,
                                                       const std::string& type_name);

    // Recursively build FieldDescriptor children from the introspection members.
    static void build_fields(const rosidl_message_type_support_t* ts,
                             std::vector<FieldDescriptor>&        out,
                             const std::string&                   path_prefix);

    // Build the accessor's step chain for a given path in the schema.
    static bool build_accessor_steps(const std::vector<FieldDescriptor>& fields,
                                     const std::vector<std::string>&     parts,
                                     size_t                              part_idx,
                                     std::vector<FieldAccessor::Step>&   steps,
                                     FieldAccessor&                      acc);

    mutable std::mutex                                                    cache_mutex_;
    std::unordered_map<std::string, std::shared_ptr<const MessageSchema>> cache_;
};

}   // namespace spectra::adapters::ros2
