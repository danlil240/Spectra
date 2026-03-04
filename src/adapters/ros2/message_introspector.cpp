// message_introspector.cpp — Runtime ROS2 message introspection engine.
//
// Internally uses rosidl_typesupport_introspection_cpp to walk message member
// descriptors and build a MessageSchema tree.  FieldAccessor then uses the
// recorded byte offsets to extract numeric values from in-memory message structs.

#include "message_introspector.hpp"

#include <cassert>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <limits>
#include <sstream>

#include <rosidl_typesupport_introspection_cpp/field_types.hpp>
#include <rosidl_typesupport_introspection_cpp/identifier.hpp>
#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>
#include <rosidl_typesupport_introspection_cpp/service_introspection.hpp>

// rosidl_runtime_c provides rosidl_message_type_support_t
#include <rosidl_runtime_c/message_type_support_struct.h>
// rosidl_typesupport_c for get_message_type_support_handle
#include <rosidl_typesupport_cpp/message_type_support.hpp>

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// Helpers — FieldType utilities
// ---------------------------------------------------------------------------

const char* field_type_name(FieldType t)
{
    switch (t)
    {
    case FieldType::Bool:    return "bool";
    case FieldType::Byte:    return "byte";
    case FieldType::Char:    return "char";
    case FieldType::Float32: return "float32";
    case FieldType::Float64: return "float64";
    case FieldType::Int8:    return "int8";
    case FieldType::Uint8:   return "uint8";
    case FieldType::Int16:   return "int16";
    case FieldType::Uint16:  return "uint16";
    case FieldType::Int32:   return "int32";
    case FieldType::Uint32:  return "uint32";
    case FieldType::Int64:   return "int64";
    case FieldType::Uint64:  return "uint64";
    case FieldType::String:  return "string";
    case FieldType::WString: return "wstring";
    case FieldType::Message: return "message";
    default:                 return "unknown";
    }
}

bool is_numeric(FieldType t)
{
    switch (t)
    {
    case FieldType::Bool:
    case FieldType::Byte:
    case FieldType::Char:
    case FieldType::Float32:
    case FieldType::Float64:
    case FieldType::Int8:
    case FieldType::Uint8:
    case FieldType::Int16:
    case FieldType::Uint16:
    case FieldType::Int32:
    case FieldType::Uint32:
    case FieldType::Int64:
    case FieldType::Uint64:
        return true;
    default:
        return false;
    }
}

// Map rosidl field type IDs to our FieldType enum.
static FieldType from_rosidl_type(uint8_t rosidl_type_id)
{
    // rosidl_typesupport_introspection_cpp/field_types.hpp defines these constants.
    using namespace rosidl_typesupport_introspection_cpp;
    switch (rosidl_type_id)
    {
    case ROS_TYPE_BOOL:    return FieldType::Bool;
    case ROS_TYPE_BYTE:    return FieldType::Byte;
    case ROS_TYPE_CHAR:    return FieldType::Char;
    case ROS_TYPE_FLOAT:   return FieldType::Float32;
    case ROS_TYPE_DOUBLE:  return FieldType::Float64;
    case ROS_TYPE_INT8:    return FieldType::Int8;
    case ROS_TYPE_UINT8:   return FieldType::Uint8;
    case ROS_TYPE_INT16:   return FieldType::Int16;
    case ROS_TYPE_UINT16:  return FieldType::Uint16;
    case ROS_TYPE_INT32:   return FieldType::Int32;
    case ROS_TYPE_UINT32:  return FieldType::Uint32;
    case ROS_TYPE_INT64:   return FieldType::Int64;
    case ROS_TYPE_UINT64:  return FieldType::Uint64;
    case ROS_TYPE_STRING:  return FieldType::String;
    case ROS_TYPE_WSTRING: return FieldType::WString;
    case ROS_TYPE_MESSAGE: return FieldType::Message;
    default:               return FieldType::Unknown;
    }
}

// Return sizeof for a given scalar FieldType.
static size_t field_type_size(FieldType t)
{
    switch (t)
    {
    case FieldType::Bool:    return sizeof(bool);
    case FieldType::Byte:    return sizeof(uint8_t);
    case FieldType::Char:    return sizeof(char);
    case FieldType::Float32: return sizeof(float);
    case FieldType::Float64: return sizeof(double);
    case FieldType::Int8:    return sizeof(int8_t);
    case FieldType::Uint8:   return sizeof(uint8_t);
    case FieldType::Int16:   return sizeof(int16_t);
    case FieldType::Uint16:  return sizeof(uint16_t);
    case FieldType::Int32:   return sizeof(int32_t);
    case FieldType::Uint32:  return sizeof(uint32_t);
    case FieldType::Int64:   return sizeof(int64_t);
    case FieldType::Uint64:  return sizeof(uint64_t);
    default:                 return 0;
    }
}

// Split a dot-separated path into parts.
static std::vector<std::string> split_path(const std::string& path)
{
    std::vector<std::string> parts;
    std::string token;
    std::istringstream ss(path);
    while (std::getline(ss, token, '.'))
        if (!token.empty())
            parts.push_back(token);
    return parts;
}

// ---------------------------------------------------------------------------
// MessageSchema helpers
// ---------------------------------------------------------------------------

static void collect_numeric_paths_recursive(
    const std::vector<FieldDescriptor>& fields,
    std::vector<std::string>& out)
{
    for (const auto& f : fields)
    {
        if (f.is_numeric_leaf())
            out.push_back(f.full_path);
        else if (!f.children.empty())
            collect_numeric_paths_recursive(f.children, out);
    }
}

static const FieldDescriptor* find_field_recursive(
    const std::vector<FieldDescriptor>& fields,
    const std::string& path)
{
    for (const auto& f : fields)
    {
        if (f.full_path == path)
            return &f;
        if (!f.children.empty())
        {
            const auto* child = find_field_recursive(f.children, path);
            if (child) return child;
        }
    }
    return nullptr;
}

const FieldDescriptor* MessageSchema::find_field(const std::string& path) const
{
    return find_field_recursive(fields, path);
}

std::vector<std::string> MessageSchema::numeric_paths() const
{
    std::vector<std::string> result;
    collect_numeric_paths_recursive(fields, result);
    return result;
}

// ---------------------------------------------------------------------------
// FieldAccessor — extract_double / extract_int64
// ---------------------------------------------------------------------------

double FieldAccessor::extract_double(const void* msg_ptr, size_t array_index) const
{
    if (!valid() || !msg_ptr)
        return std::numeric_limits<double>::quiet_NaN();

    // Walk all steps except the last step to get the parent struct pointer.
    // The leaf_offset_ then points within that struct.
    const uint8_t* parent = static_cast<const uint8_t*>(msg_ptr);

    // Walk all intermediate steps (everything up to and including steps_).
    for (const auto& step : steps_)
    {
        if (!parent) return std::numeric_limits<double>::quiet_NaN();
        parent += step.offset;
        if (step.is_dynamic)
        {
            const void* const* data_ptr =
                reinterpret_cast<const void* const*>(parent);
            parent = static_cast<const uint8_t*>(*data_ptr);
        }
    }

    if (!parent) return std::numeric_limits<double>::quiet_NaN();

    // Now parent points to the struct that directly owns the leaf field.
    const uint8_t* leaf_ptr = parent + leaf_offset_;

    if (is_array_)
    {
        if (is_dynamic_array_)
        {
            // Dynamic array: leaf_ptr points to a std::vector<T>.
            // std::vector layout (libstdc++/libc++): {start, finish, end_of_storage}.
            const uint8_t* const* start_ptr =
                reinterpret_cast<const uint8_t* const*>(leaf_ptr);
            const uint8_t* const* finish_ptr =
                reinterpret_cast<const uint8_t* const*>(leaf_ptr + sizeof(void*));
            const uint8_t* data = *start_ptr;
            if (!data) return std::numeric_limits<double>::quiet_NaN();

            // Compute element count from (finish - start) / element_size.
            const size_t byte_count = static_cast<size_t>(*finish_ptr - data);
            const size_t elem_count = element_size_ > 0
                                        ? byte_count / element_size_ : 0;
            if (array_index >= elem_count)
                return std::numeric_limits<double>::quiet_NaN();

            leaf_ptr = data + array_index * element_size_;
        }
        else
        {
            // Fixed-size array: C array stored inline.
            if (array_index >= array_size_)
                return std::numeric_limits<double>::quiet_NaN();
            leaf_ptr = leaf_ptr + array_index * element_size_;
        }
    }

    switch (leaf_type_)
    {
    case FieldType::Float64: { double   v; std::memcpy(&v, leaf_ptr, sizeof(v)); return v; }
    case FieldType::Float32: { float    v; std::memcpy(&v, leaf_ptr, sizeof(v)); return static_cast<double>(v); }
    case FieldType::Int8:    { int8_t   v; std::memcpy(&v, leaf_ptr, sizeof(v)); return static_cast<double>(v); }
    case FieldType::Uint8:   { uint8_t  v; std::memcpy(&v, leaf_ptr, sizeof(v)); return static_cast<double>(v); }
    case FieldType::Byte:    { uint8_t  v; std::memcpy(&v, leaf_ptr, sizeof(v)); return static_cast<double>(v); }
    case FieldType::Char:    { char     v; std::memcpy(&v, leaf_ptr, sizeof(v)); return static_cast<double>(v); }
    case FieldType::Int16:   { int16_t  v; std::memcpy(&v, leaf_ptr, sizeof(v)); return static_cast<double>(v); }
    case FieldType::Uint16:  { uint16_t v; std::memcpy(&v, leaf_ptr, sizeof(v)); return static_cast<double>(v); }
    case FieldType::Int32:   { int32_t  v; std::memcpy(&v, leaf_ptr, sizeof(v)); return static_cast<double>(v); }
    case FieldType::Uint32:  { uint32_t v; std::memcpy(&v, leaf_ptr, sizeof(v)); return static_cast<double>(v); }
    case FieldType::Int64:   { int64_t  v; std::memcpy(&v, leaf_ptr, sizeof(v)); return static_cast<double>(v); }
    case FieldType::Uint64:  { uint64_t v; std::memcpy(&v, leaf_ptr, sizeof(v)); return static_cast<double>(v); }
    case FieldType::Bool:    { bool     v; std::memcpy(&v, leaf_ptr, sizeof(v)); return v ? 1.0 : 0.0; }
    default:
        return std::numeric_limits<double>::quiet_NaN();
    }
}

int64_t FieldAccessor::extract_int64(const void* msg_ptr, size_t array_index) const
{
    if (!valid() || !msg_ptr)
        return 0;

    const uint8_t* parent = static_cast<const uint8_t*>(msg_ptr);
    for (const auto& step : steps_)
    {
        if (!parent) return 0;
        parent += step.offset;
        if (step.is_dynamic)
        {
            const void* const* data_ptr =
                reinterpret_cast<const void* const*>(parent);
            parent = static_cast<const uint8_t*>(*data_ptr);
        }
    }
    if (!parent) return 0;

    const uint8_t* leaf_ptr = parent + leaf_offset_;

    if (is_array_)
    {
        if (is_dynamic_array_)
        {
            const uint8_t* const* start_ptr =
                reinterpret_cast<const uint8_t* const*>(leaf_ptr);
            const uint8_t* const* finish_ptr =
                reinterpret_cast<const uint8_t* const*>(leaf_ptr + sizeof(void*));
            const uint8_t* data = *start_ptr;
            if (!data) return 0;
            const size_t byte_count = static_cast<size_t>(*finish_ptr - data);
            const size_t elem_count = element_size_ > 0
                                        ? byte_count / element_size_ : 0;
            if (array_index >= elem_count) return 0;
            leaf_ptr = data + array_index * element_size_;
        }
        else
        {
            if (array_index >= array_size_) return 0;
            leaf_ptr = leaf_ptr + array_index * element_size_;
        }
    }

    switch (leaf_type_)
    {
    case FieldType::Int8:    { int8_t   v; std::memcpy(&v, leaf_ptr, sizeof(v)); return v; }
    case FieldType::Uint8:   { uint8_t  v; std::memcpy(&v, leaf_ptr, sizeof(v)); return v; }
    case FieldType::Byte:    { uint8_t  v; std::memcpy(&v, leaf_ptr, sizeof(v)); return v; }
    case FieldType::Char:    { char     v; std::memcpy(&v, leaf_ptr, sizeof(v)); return v; }
    case FieldType::Int16:   { int16_t  v; std::memcpy(&v, leaf_ptr, sizeof(v)); return v; }
    case FieldType::Uint16:  { uint16_t v; std::memcpy(&v, leaf_ptr, sizeof(v)); return v; }
    case FieldType::Int32:   { int32_t  v; std::memcpy(&v, leaf_ptr, sizeof(v)); return v; }
    case FieldType::Uint32:  { uint32_t v; std::memcpy(&v, leaf_ptr, sizeof(v)); return v; }
    case FieldType::Int64:   { int64_t  v; std::memcpy(&v, leaf_ptr, sizeof(v)); return v; }
    case FieldType::Uint64:  { uint64_t v; std::memcpy(&v, leaf_ptr, sizeof(v)); return static_cast<int64_t>(v); }
    case FieldType::Bool:    { bool     v; std::memcpy(&v, leaf_ptr, sizeof(v)); return v ? 1 : 0; }
    case FieldType::Float32: { float    v; std::memcpy(&v, leaf_ptr, sizeof(v)); return static_cast<int64_t>(v); }
    case FieldType::Float64: { double   v; std::memcpy(&v, leaf_ptr, sizeof(v)); return static_cast<int64_t>(v); }
    default: return 0;
    }
}

// ---------------------------------------------------------------------------
// MessageIntrospector — schema builder
// ---------------------------------------------------------------------------

// Build a FieldDescriptor list from a rosidl message introspection descriptor.
void MessageIntrospector::build_fields(
    const rosidl_message_type_support_t* ts,
    std::vector<FieldDescriptor>& out,
    const std::string& path_prefix)
{
    using namespace rosidl_typesupport_introspection_cpp;

    // Retrieve the introspection-specific type support.
    const auto* introspection_ts =
        get_message_typesupport_handle(ts, rosidl_typesupport_introspection_cpp::typesupport_identifier);
    if (!introspection_ts) return;

    const auto* members =
        static_cast<const MessageMembers*>(introspection_ts->data);
    if (!members) return;

    for (uint32_t i = 0; i < members->member_count_; ++i)
    {
        const auto& m = members->members_[i];

        FieldDescriptor fd;
        fd.name      = m.name_;
        fd.full_path = path_prefix.empty() ? m.name_ : (path_prefix + "." + m.name_);
        fd.type      = from_rosidl_type(m.type_id_);
        fd.type_id   = m.type_id_;
        fd.offset    = m.offset_;
        fd.is_array  = m.is_array_;
        fd.is_dynamic_array = m.is_array_ && m.array_size_ == 0;
        fd.array_size       = m.is_array_ ? m.array_size_ : 0u;

        if (m.type_id_ == ROS_TYPE_MESSAGE && m.members_ != nullptr)
        {
            // Recurse into nested message.
            build_fields(m.members_, fd.children, fd.full_path);
        }

        out.push_back(std::move(fd));
    }
}

std::shared_ptr<MessageSchema> MessageIntrospector::build_schema(
    const rosidl_message_type_support_t* ts,
    const std::string& type_name)
{
    auto schema = std::make_shared<MessageSchema>();
    schema->type_name = type_name;
    build_fields(ts, schema->fields, "");
    return schema;
}

// Convert "package/msg/TypeName" → library name and symbol name.
// e.g. "std_msgs/msg/Float64"
//   → lib:  libstd_msgs__rosidl_typesupport_introspection_cpp.so
//   → sym:  rosidl_typesupport_introspection_cpp__get_message_type_support_handle__std_msgs__msg__Float64
static bool parse_type_string(
    const std::string& type_name,
    std::string& package,
    std::string& subfolder,
    std::string& msg_name)
{
    // Format: "package/subfolder/MsgName"
    const auto first_slash = type_name.find('/');
    if (first_slash == std::string::npos) return false;
    const auto second_slash = type_name.find('/', first_slash + 1);
    if (second_slash == std::string::npos) return false;

    package   = type_name.substr(0, first_slash);
    subfolder = type_name.substr(first_slash + 1, second_slash - first_slash - 1);
    msg_name  = type_name.substr(second_slash + 1);
    return !package.empty() && !subfolder.empty() && !msg_name.empty();
}

// Replace '/' with '__' for symbol names.
[[maybe_unused]]
static std::string to_symbol_part(const std::string& s)
{
    std::string r = s;
    for (char& c : r) if (c == '/') c = '_';
    return r;
}

std::shared_ptr<const MessageSchema>
MessageIntrospector::introspect(const std::string& type_name)
{
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = cache_.find(type_name);
        if (it != cache_.end()) return it->second;
    }

    std::string package, subfolder, msg_name;
    if (!parse_type_string(type_name, package, subfolder, msg_name))
        return nullptr;

    // Load the typesupport introspection shared library for this package.
    // Library name pattern:
    //   lib<package>__rosidl_typesupport_introspection_cpp.so
    const std::string lib_name =
        "lib" + package + "__rosidl_typesupport_introspection_cpp.so";

    void* handle = dlopen(lib_name.c_str(), RTLD_LAZY | RTLD_GLOBAL);
    if (!handle)
    {
        // Try without 'lib' prefix (some platforms).
        handle = dlopen((package + "__rosidl_typesupport_introspection_cpp.so").c_str(),
                        RTLD_LAZY | RTLD_GLOBAL);
        if (!handle) return nullptr;
    }

    // Symbol name pattern:
    //   rosidl_typesupport_introspection_cpp__get_message_type_support_handle__<pkg>__<sub>__<Msg>
    const std::string sym_name =
        "rosidl_typesupport_introspection_cpp__get_message_type_support_handle__" +
        package + "__" + subfolder + "__" + msg_name;

    using GetTypeSupportFn = const rosidl_message_type_support_t* (*)();
    auto fn = reinterpret_cast<GetTypeSupportFn>(dlsym(handle, sym_name.c_str()));
    if (!fn) return nullptr;

    const rosidl_message_type_support_t* ts = fn();
    if (!ts) return nullptr;

    auto schema = build_schema(ts, type_name);

    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        cache_[type_name] = schema;
    }

    return schema;
}

std::shared_ptr<const MessageSchema>
MessageIntrospector::introspect_type_support(
    const rosidl_message_type_support_t* ts,
    const std::string& type_name)
{
    if (!ts) return nullptr;

    if (!type_name.empty())
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = cache_.find(type_name);
        if (it != cache_.end()) return it->second;
    }

    auto schema = build_schema(ts, type_name);

    if (!type_name.empty())
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        cache_[type_name] = schema;
    }

    return schema;
}

// ---------------------------------------------------------------------------
// MessageIntrospector — accessor builder
// ---------------------------------------------------------------------------

bool MessageIntrospector::build_accessor_steps(
    const std::vector<FieldDescriptor>& fields,
    const std::vector<std::string>& parts,
    size_t part_idx,
    std::vector<FieldAccessor::Step>& steps,
    FieldAccessor& acc)
{
    if (part_idx >= parts.size()) return false;

    const std::string& name = parts[part_idx];

    for (const auto& fd : fields)
    {
        if (fd.name != name) continue;

        if (part_idx + 1 == parts.size())
        {
            // This IS the leaf field.
            if (!is_numeric(fd.type)) return false;

            acc.leaf_offset_       = fd.offset;
            acc.leaf_type_         = fd.type;
            acc.is_array_          = fd.is_array;
            acc.is_dynamic_array_  = fd.is_dynamic_array;
            acc.array_size_        = fd.array_size;
            acc.element_size_      = static_cast<uint32_t>(field_type_size(fd.type));
            return true;
        }
        else
        {
            // Intermediate step — must be a nested message.
            if (fd.type != FieldType::Message || fd.children.empty())
                return false;

            FieldAccessor::Step step;
            step.offset     = fd.offset;
            step.is_dynamic = fd.is_dynamic_array;
            steps.push_back(step);

            return build_accessor_steps(fd.children, parts, part_idx + 1, steps, acc);
        }
    }

    return false;
}

FieldAccessor MessageIntrospector::make_accessor(
    const MessageSchema& schema,
    const std::string& path) const
{
    FieldAccessor acc;
    acc.path_ = path;

    const auto parts = split_path(path);
    if (parts.empty()) return acc;

    std::vector<FieldAccessor::Step> steps;
    if (!build_accessor_steps(schema.fields, parts, 0, steps, acc))
        return FieldAccessor{};   // invalid

    acc.steps_ = std::move(steps);
    return acc;
}

// ---------------------------------------------------------------------------
// Cache management
// ---------------------------------------------------------------------------

void MessageIntrospector::clear_cache()
{
    std::lock_guard<std::mutex> lock(cache_mutex_);
    cache_.clear();
}

std::size_t MessageIntrospector::cache_size() const
{
    std::lock_guard<std::mutex> lock(cache_mutex_);
    return cache_.size();
}

}   // namespace spectra::adapters::ros2
