#pragma once

// ros2msg schema parser + CDR decoder for MCAP files recorded by ROS 2.
//
// MCAP stores ROS 2 messages as CDR bytes with a "ros2msg" schema: the .msg
// text of the root type followed by every dependency, each introduced by a
// "====" separator line and "MSG: pkg/Type".  Ros2MsgSchema parses that text
// (no ROS install needed) and decode_cdr() walks one message, reporting every
// numeric / bool leaf by path ("pose.position.x", "covariance[3]").
//
// Supported: all primitive types, strings (skipped), nested messages, fixed
// arrays, unbounded and bounded sequences, classic CDR (XCDR1, what ROS 2
// middlewares write by default) little and big endian.  XCDR2 payloads are
// rejected.  wstring fields and unknown types stop the decode of that message
// (fields before them are still reported).

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace spectra::logs::ros2msg
{

enum class Primitive : uint8_t
{
    Bool,
    Byte,
    Char,
    Int8,
    UInt8,
    Int16,
    UInt16,
    Int32,
    UInt32,
    Int64,
    UInt64,
    Float32,
    Float64,
    String,
    WString,
    Message,
};

enum class ArrayKind : uint8_t
{
    None,
    Fixed,      // T[N]
    Sequence,   // T[] or T[<=N]
};

struct FieldDef
{
    std::string name;
    Primitive   type = Primitive::Float64;
    std::string type_name;   // as written, e.g. "float64", "geometry_msgs/Point"
    std::string msg_type;    // Primitive::Message: normalized "pkg/Type"
    ArrayKind   array      = ArrayKind::None;
    uint32_t    array_size = 0;   // ArrayKind::Fixed length
};

struct MessageDef
{
    std::string           name;   // normalized "pkg/Type"
    std::vector<FieldDef> fields;
};

// "pkg/msg/Type" → "pkg/Type"; other names unchanged.
std::string normalize_type_name(std::string_view name);

class Schema
{
   public:
    // Parse the schema text for `root_type` (e.g. "sensor_msgs/msg/Imu").
    // Returns false and sets *error on malformed text.
    bool parse(std::string_view root_type, std::string_view text, std::string* error = nullptr);

    const MessageDef* root() const;
    // Lookup by normalized name; falls back to a unique match on the short
    // type name (the part after the last '/').
    const MessageDef* find(std::string_view name) const;

    size_t message_count() const { return defs_.size(); }

   private:
    std::vector<MessageDef>                 defs_;   // defs_[0] is the root
    std::unordered_map<std::string, size_t> by_name_;
};

// Callback for one decoded leaf: its path, value and field definition.
using LeafFn = std::function<void(std::string_view path, double value, const FieldDef& field)>;

struct DecodeOptions
{
    // Arrays and sequences longer than this are consumed but not reported, so
    // images and point clouds do not explode into millions of fields.
    size_t max_array_elements = 64;
};

// Decode one CDR message (including its 4-byte encapsulation header).
// Returns false and sets *error if the bytes are truncated, the
// encapsulation is unsupported, or the schema uses an unsupported type; leaves
// decoded before the failure have already been reported.
bool decode_cdr(const Schema&              schema,
                std::span<const std::byte> data,
                const LeafFn&              on_leaf,
                const DecodeOptions&       options = {},
                std::string*               error   = nullptr);

}   // namespace spectra::logs::ros2msg
