#include "ui/topic_echo_panel.hpp"
#include "ui/field_drag_drop.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <dlfcn.h>

#ifdef SPECTRA_USE_IMGUI
#include <imgui.h>
#endif

// rclcpp deserialization helpers
#include <rclcpp/serialization.hpp>
#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/*static*/ double TopicEchoPanel::wall_time_s_now()
{
    using namespace std::chrono;
    return duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

TopicEchoPanel::TopicEchoPanel(rclcpp::Node::SharedPtr node, MessageIntrospector& intr)
    : node_(std::move(node)), intr_(intr)
{
    ring_.reserve(max_messages_);
}

TopicEchoPanel::~TopicEchoPanel()
{
    std::lock_guard<std::mutex> lk(sub_mutex_);
    subscription_.reset();
}

// ---------------------------------------------------------------------------
// Topic selection
// ---------------------------------------------------------------------------

void TopicEchoPanel::set_topic(const std::string& topic_name,
                                const std::string& type_name)
{
    std::lock_guard<std::mutex> lk(sub_mutex_);

    // Unsubscribe from old topic.
    subscription_.reset();
    schema_.reset();

    topic_name_ = topic_name;
    type_name_  = type_name;

    if (topic_name.empty() || type_name.empty()) return;

    // Introspect schema.
    schema_ = intr_.introspect(type_name);
    if (!schema_) return;

    // Create generic subscription.
    auto qos = rclcpp::QoS(rclcpp::KeepLast(10)).best_effort();
    subscription_ = node_->create_generic_subscription(
        topic_name,
        type_name,
        qos,
        [this](std::shared_ptr<rclcpp::SerializedMessage> raw_msg) {
            on_message(std::move(raw_msg));
        });
}

// ---------------------------------------------------------------------------
// Playback controls
// ---------------------------------------------------------------------------

void TopicEchoPanel::pause()
{
    paused_.store(true, std::memory_order_release);
}

void TopicEchoPanel::resume()
{
    paused_.store(false, std::memory_order_release);
}

void TopicEchoPanel::clear()
{
    std::lock_guard<std::mutex> lk(ring_mutex_);
    ring_.clear();
    next_seq_         = 0;
    selected_msg_idx_ = -1;
}

void TopicEchoPanel::set_max_messages(size_t n)
{
    std::lock_guard<std::mutex> lk(ring_mutex_);
    max_messages_ = (n > 0) ? n : 1;
    while (ring_.size() > max_messages_) {
        ring_.erase(ring_.begin());
    }
}

// ---------------------------------------------------------------------------
// Testing helpers
// ---------------------------------------------------------------------------

size_t TopicEchoPanel::message_count() const
{
    std::lock_guard<std::mutex> lk(ring_mutex_);
    return ring_.size();
}

std::vector<EchoMessage> TopicEchoPanel::messages_snapshot() const
{
    std::lock_guard<std::mutex> lk(ring_mutex_);
    return ring_;
}

std::unique_ptr<EchoMessage> TopicEchoPanel::latest_message() const
{
    std::lock_guard<std::mutex> lk(ring_mutex_);
    if (ring_.empty()) return nullptr;
    return std::make_unique<EchoMessage>(ring_.back());
}

void TopicEchoPanel::inject_message(EchoMessage msg)
{
    if (paused_.load(std::memory_order_acquire)) return;

    std::lock_guard<std::mutex> lk(ring_mutex_);
    if (ring_.size() >= max_messages_) {
        ring_.erase(ring_.begin());
    }
    ring_.push_back(std::move(msg));
    total_received_.fetch_add(1, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Message reception (executor thread)
// ---------------------------------------------------------------------------

void TopicEchoPanel::on_message(std::shared_ptr<rclcpp::SerializedMessage> raw_msg)
{
    if (paused_.load(std::memory_order_acquire)) return;

    // Snapshot schema pointer under sub_mutex_ to avoid TOCTOU.
    std::shared_ptr<const MessageSchema> schema_snap;
    {
        std::lock_guard<std::mutex> lk(sub_mutex_);
        schema_snap = schema_;
    }
    if (!schema_snap) return;

    // Deserialize using type support from the library.
    // We need to get the type support handle to call init/deserialize/fini.
    const std::string& type = type_name_;
    const size_t slash1 = type.find('/');
    const size_t slash2 = (slash1 != std::string::npos) ? type.find('/', slash1 + 1) : std::string::npos;
    if (slash1 == std::string::npos || slash2 == std::string::npos) return;

    const std::string pkg      = type.substr(0, slash1);
    const std::string msg_name = type.substr(slash2 + 1);

    // Build the lib + symbol names used by rosidl.
    const std::string lib_name = "lib" + pkg + "__rosidl_typesupport_introspection_cpp.so";
    const std::string sym_name = "rosidl_typesupport_introspection_cpp__get_message_type_support_handle__"
                                 + pkg + "__msg__" + msg_name;

    void* lib_handle = dlopen(lib_name.c_str(), RTLD_LAZY | RTLD_NOLOAD);
    if (!lib_handle) {
        lib_handle = dlopen(lib_name.c_str(), RTLD_LAZY | RTLD_GLOBAL);
    }
    if (!lib_handle) return;

    using GetTSFunc = const rosidl_message_type_support_t* (*)();
    auto get_ts = reinterpret_cast<GetTSFunc>(dlsym(lib_handle, sym_name.c_str()));
    if (!get_ts) return;

    const rosidl_message_type_support_t* ts = get_ts();
    if (!ts) return;

    const auto* members = reinterpret_cast<const rosidl_typesupport_introspection_cpp::MessageMembers*>(ts->data);
    if (!members) return;

    // Allocate and deserialize message.
    std::vector<uint8_t> buf(members->size_of_);
    members->init_function(buf.data(), rosidl_runtime_cpp::MessageInitialization::ALL);

    rclcpp::SerializationBase serializer(ts);
    try {
        serializer.deserialize_message(raw_msg.get(), buf.data());
    } catch (...) {
        members->fini_function(buf.data());
        return;
    }

    // Extract timestamp from header if present.
    int64_t ts_ns = 0;
    {
        const auto* hdr_fd = schema_snap->find_field("header.stamp.sec");
        if (hdr_fd) {
            // Try to read sec + nanosec.
            FieldAccessor acc_sec  = intr_.make_accessor(*schema_snap, "header.stamp.sec");
            FieldAccessor acc_nsec = intr_.make_accessor(*schema_snap, "header.stamp.nanosec");
            if (acc_sec.valid() && acc_nsec.valid()) {
                const int64_t sec  = acc_sec.extract_int64(buf.data());
                const int64_t nsec = acc_nsec.extract_int64(buf.data());
                ts_ns = sec * 1'000'000'000LL + nsec;
            }
        }
        if (ts_ns == 0) {
            using namespace std::chrono;
            ts_ns = duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
        }
    }

    uint64_t seq;
    {
        std::lock_guard<std::mutex> lk(ring_mutex_);
        seq = next_seq_++;
    }

    EchoMessage msg = build_echo_message(buf.data(), *schema_snap, seq, ts_ns, wall_time_s_now());
    members->fini_function(buf.data());

    {
        std::lock_guard<std::mutex> lk(ring_mutex_);
        if (ring_.size() >= max_messages_) {
            ring_.erase(ring_.begin());
        }
        ring_.push_back(std::move(msg));
    }

    total_received_.fetch_add(1, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// EchoMessage builder
// ---------------------------------------------------------------------------

/*static*/ EchoMessage TopicEchoPanel::build_echo_message(const void*          msg_ptr,
                                                           const MessageSchema& schema,
                                                           uint64_t             seq,
                                                           int64_t              timestamp_ns,
                                                           double               wall_time_s)
{
    EchoMessage msg;
    msg.seq          = seq;
    msg.timestamp_ns = timestamp_ns;
    msg.wall_time_s  = wall_time_s;
    extract_fields(msg_ptr, schema.fields, msg.fields, 0);
    return msg;
}

/*static*/ void TopicEchoPanel::extract_fields(const void*                        msg_ptr,
                                               const std::vector<FieldDescriptor>& fields,
                                               std::vector<EchoFieldValue>&        out,
                                               int                                 depth)
{
    for (const auto& fd : fields) {
        if (fd.type == FieldType::Message && !fd.children.empty() && !fd.is_array) {
            // Nested message: emit a header entry then recurse.
            EchoFieldValue hdr;
            hdr.path         = fd.full_path;
            hdr.display_name = fd.name;
            hdr.depth        = depth;
            hdr.kind         = EchoFieldValue::Kind::NestedHead;
            out.push_back(std::move(hdr));

            // The nested struct starts at offset fd.offset inside msg_ptr.
            const uint8_t* nested_ptr =
                static_cast<const uint8_t*>(msg_ptr) + fd.offset;
            extract_fields(nested_ptr, fd.children, out, depth + 1);
            continue;
        }

        if (fd.is_array || fd.is_dynamic_array) {
            // Array: emit a header with count, then up to 64 elements.
            const uint8_t* base = static_cast<const uint8_t*>(msg_ptr) + fd.offset;
            size_t count = 0;

            // Determine element byte-size from type (needed for dynamic array
            // count computation below).
            size_t elem_sz = 0;
            if (fd.type != FieldType::Message && is_numeric(fd.type)) {
                elem_sz = 8; // default: double/int64
                switch (fd.type) {
                    case FieldType::Bool:    elem_sz = 1; break;
                    case FieldType::Byte:    elem_sz = 1; break;
                    case FieldType::Char:    elem_sz = 1; break;
                    case FieldType::Int8:    elem_sz = 1; break;
                    case FieldType::Uint8:   elem_sz = 1; break;
                    case FieldType::Int16:   elem_sz = 2; break;
                    case FieldType::Uint16:  elem_sz = 2; break;
                    case FieldType::Int32:   elem_sz = 4; break;
                    case FieldType::Uint32:  elem_sz = 4; break;
                    case FieldType::Float32: elem_sz = 4; break;
                    case FieldType::Float64: elem_sz = 8; break;
                    case FieldType::Int64:   elem_sz = 8; break;
                    case FieldType::Uint64:  elem_sz = 8; break;
                    default:                 elem_sz = 8; break;
                }
            }

            if (fd.is_dynamic_array) {
                // std::vector layout (libstdc++/libc++):
                //   offset 0:              _M_start         (T*)
                //   offset sizeof(void*):  _M_finish        (T*)
                //   offset 2*sizeof(void*): _M_end_of_storage (T*)
                // Count = (_M_finish - _M_start) / sizeof(T).
                const uint8_t* start_ptr = nullptr;
                const uint8_t* finish_ptr = nullptr;
                std::memcpy(&start_ptr, base, sizeof(void*));
                std::memcpy(&finish_ptr, base + sizeof(void*), sizeof(void*));
                if (start_ptr && finish_ptr >= start_ptr && elem_sz > 0) {
                    count = static_cast<size_t>(finish_ptr - start_ptr) / elem_sz;
                }
            } else {
                count = static_cast<size_t>(fd.array_size);
            }

            EchoFieldValue arr_hdr;
            arr_hdr.path         = fd.full_path;
            arr_hdr.display_name = fd.name;
            arr_hdr.depth        = depth;
            arr_hdr.kind         = EchoFieldValue::Kind::ArrayHead;
            arr_hdr.array_len    = static_cast<int>(count);
            out.push_back(std::move(arr_hdr));

            if (fd.type != FieldType::Message && is_numeric(fd.type)) {
                // Numeric array elements.
                const uint8_t* data_ptr = nullptr;
                if (fd.is_dynamic_array) {
                    // First 8 bytes of std::vector are the data pointer.
                    std::memcpy(&data_ptr, base, sizeof(void*));
                } else {
                    data_ptr = base;
                }

                const size_t max_elems = std::min(count, size_t{64});
                for (size_t i = 0; i < max_elems && data_ptr; ++i) {
                    double val = 0.0;
                    const uint8_t* ep = data_ptr + i * elem_sz;
                    switch (fd.type) {
                        case FieldType::Bool:    { uint8_t  v; std::memcpy(&v, ep, 1); val = v ? 1.0 : 0.0; break; }
                        case FieldType::Byte:    { uint8_t  v; std::memcpy(&v, ep, 1); val = static_cast<double>(v); break; }
                        case FieldType::Char:    { int8_t   v; std::memcpy(&v, ep, 1); val = static_cast<double>(v); break; }
                        case FieldType::Int8:    { int8_t   v; std::memcpy(&v, ep, 1); val = static_cast<double>(v); break; }
                        case FieldType::Uint8:   { uint8_t  v; std::memcpy(&v, ep, 1); val = static_cast<double>(v); break; }
                        case FieldType::Int16:   { int16_t  v; std::memcpy(&v, ep, 2); val = static_cast<double>(v); break; }
                        case FieldType::Uint16:  { uint16_t v; std::memcpy(&v, ep, 2); val = static_cast<double>(v); break; }
                        case FieldType::Int32:   { int32_t  v; std::memcpy(&v, ep, 4); val = static_cast<double>(v); break; }
                        case FieldType::Uint32:  { uint32_t v; std::memcpy(&v, ep, 4); val = static_cast<double>(v); break; }
                        case FieldType::Float32: { float    v; std::memcpy(&v, ep, 4); val = static_cast<double>(v); break; }
                        case FieldType::Float64: { double   v; std::memcpy(&v, ep, 8); val = v; break; }
                        case FieldType::Int64:   { int64_t  v; std::memcpy(&v, ep, 8); val = static_cast<double>(v); break; }
                        case FieldType::Uint64:  { uint64_t v; std::memcpy(&v, ep, 8); val = static_cast<double>(v); break; }
                        default: break;
                    }

                    EchoFieldValue elem;
                    elem.path         = fd.full_path + "[" + std::to_string(i) + "]";
                    elem.display_name = "[" + std::to_string(i) + "]";
                    elem.depth        = depth + 1;
                    elem.kind         = EchoFieldValue::Kind::ArrayElement;
                    elem.numeric      = val;
                    out.push_back(std::move(elem));
                }
            }
            continue;
        }

        // Scalar field.
        out.push_back(make_scalar_value(msg_ptr, fd, depth));
    }
}

/*static*/ EchoFieldValue TopicEchoPanel::make_scalar_value(const void*            msg_ptr,
                                                             const FieldDescriptor& fd,
                                                             int                    depth)
{
    EchoFieldValue fv;
    fv.path         = fd.full_path;
    fv.display_name = fd.name;
    fv.depth        = depth;

    const uint8_t* ptr = static_cast<const uint8_t*>(msg_ptr) + fd.offset;

    switch (fd.type) {
        case FieldType::Bool: {
            uint8_t v = 0; std::memcpy(&v, ptr, 1);
            fv.kind    = EchoFieldValue::Kind::Numeric;
            fv.numeric = v ? 1.0 : 0.0;
            break;
        }
        case FieldType::Byte:
        case FieldType::Uint8: {
            uint8_t v = 0; std::memcpy(&v, ptr, 1);
            fv.kind = EchoFieldValue::Kind::Numeric;
            fv.numeric = static_cast<double>(v);
            break;
        }
        case FieldType::Char:
        case FieldType::Int8: {
            int8_t v = 0; std::memcpy(&v, ptr, 1);
            fv.kind = EchoFieldValue::Kind::Numeric;
            fv.numeric = static_cast<double>(v);
            break;
        }
        case FieldType::Int16: {
            int16_t v = 0; std::memcpy(&v, ptr, 2);
            fv.kind = EchoFieldValue::Kind::Numeric;
            fv.numeric = static_cast<double>(v);
            break;
        }
        case FieldType::Uint16: {
            uint16_t v = 0; std::memcpy(&v, ptr, 2);
            fv.kind = EchoFieldValue::Kind::Numeric;
            fv.numeric = static_cast<double>(v);
            break;
        }
        case FieldType::Int32: {
            int32_t v = 0; std::memcpy(&v, ptr, 4);
            fv.kind = EchoFieldValue::Kind::Numeric;
            fv.numeric = static_cast<double>(v);
            break;
        }
        case FieldType::Uint32: {
            uint32_t v = 0; std::memcpy(&v, ptr, 4);
            fv.kind = EchoFieldValue::Kind::Numeric;
            fv.numeric = static_cast<double>(v);
            break;
        }
        case FieldType::Int64: {
            int64_t v = 0; std::memcpy(&v, ptr, 8);
            fv.kind = EchoFieldValue::Kind::Numeric;
            fv.numeric = static_cast<double>(v);
            break;
        }
        case FieldType::Uint64: {
            uint64_t v = 0; std::memcpy(&v, ptr, 8);
            fv.kind = EchoFieldValue::Kind::Numeric;
            fv.numeric = static_cast<double>(v);
            break;
        }
        case FieldType::Float32: {
            float v = 0.0f; std::memcpy(&v, ptr, 4);
            fv.kind = EchoFieldValue::Kind::Numeric;
            fv.numeric = static_cast<double>(v);
            break;
        }
        case FieldType::Float64: {
            double v = 0.0; std::memcpy(&v, ptr, 8);
            fv.kind = EchoFieldValue::Kind::Numeric;
            fv.numeric = v;
            break;
        }
        case FieldType::String: {
            // std::string layout: SSO or heap pointer at offset 0.
            const std::string* s = reinterpret_cast<const std::string*>(ptr);
            fv.kind = EchoFieldValue::Kind::Text;
            if (s->size() > 128) {
                fv.text = s->substr(0, 128) + "...";
            } else {
                fv.text = *s;
            }
            break;
        }
        default: {
            fv.kind = EchoFieldValue::Kind::Text;
            fv.text = "(unsupported)";
            break;
        }
    }

    return fv;
}

// ---------------------------------------------------------------------------
// Formatting utilities
// ---------------------------------------------------------------------------

/*static*/ std::string TopicEchoPanel::format_timestamp(int64_t ns)
{
    const int64_t sec  = ns / 1'000'000'000LL;
    const int64_t nsec = ns % 1'000'000'000LL;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%lld.%09lld",
                  static_cast<long long>(sec),
                  static_cast<long long>(nsec));
    return buf;
}

/*static*/ std::string TopicEchoPanel::format_numeric(double v)
{
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v > 0.0 ? "inf" : "-inf";

    char buf[64];
    // Use shorter representation: if integer-valued, show no decimal.
    if (v == std::floor(v) && std::abs(v) < 1e15) {
        std::snprintf(buf, sizeof(buf), "%.0f", v);
    } else {
        std::snprintf(buf, sizeof(buf), "%g", v);
    }
    return buf;
}

// ---------------------------------------------------------------------------
// ImGui rendering
// ---------------------------------------------------------------------------

#ifdef SPECTRA_USE_IMGUI

namespace {

static constexpr ImVec4 kColorPaused   = {0.90f, 0.60f, 0.10f, 1.0f};  // amber
static constexpr ImVec4 kColorLive     = {0.20f, 0.80f, 0.30f, 1.0f};  // green
static constexpr ImVec4 kColorDisabled = {0.50f, 0.50f, 0.50f, 1.0f};  // gray
static constexpr ImVec4 kColorNumeric  = {0.60f, 0.88f, 1.00f, 1.0f};  // light blue
static constexpr ImVec4 kColorText     = {0.90f, 0.90f, 0.65f, 1.0f};  // light yellow
static constexpr ImVec4 kColorNested   = {0.80f, 0.70f, 0.50f, 1.0f};  // tan
static constexpr ImVec4 kColorArray    = {0.75f, 0.60f, 0.90f, 1.0f};  // lavender

}   // anonymous namespace

void TopicEchoPanel::draw(bool* p_open)
{
    // Rate throttle: only rebuild/display at display_hz.
    const double now = wall_time_s_now();
    const bool   should_refresh = (display_interval_s_ <= 0.0) ||
                                  (now - last_draw_time_s_ >= display_interval_s_);

    ImGui::SetNextWindowSize(ImVec2(600, 520), ImGuiCond_FirstUseEver);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (!ImGui::Begin(title_.c_str(), p_open, flags)) {
        ImGui::End();
        return;
    }

    draw_controls();

    ImGui::Separator();

    if (topic_name_.empty()) {
        ImGui::TextDisabled("No topic selected. Select a topic in the Topic List panel.");
        ImGui::End();
        return;
    }

    // Update last draw time only once we start drawing topic content.
    if (should_refresh) {
        last_draw_time_s_ = now;
    }

    // Snapshot ring buffer for display.
    std::vector<EchoMessage> snap;
    {
        std::lock_guard<std::mutex> lk(ring_mutex_);
        snap = ring_;
    }

    if (snap.empty()) {
        ImGui::TextDisabled("Waiting for messages on %s …", topic_name_.c_str());
        ImGui::End();
        return;
    }

    // Two-pane layout: left = message list, right = field tree detail.
    const float list_width = 200.0f;
    const float avail_h    = ImGui::GetContentRegionAvail().y;

    // Message list pane.
    ImGui::BeginChild("##msg_list", ImVec2(list_width, avail_h), true);

    ImGui::TextDisabled("%zu / %zu msg", snap.size(),
                        static_cast<size_t>(total_received_.load()));
    ImGui::Separator();

    for (int i = static_cast<int>(snap.size()) - 1; i >= 0; --i) {
        const auto& msg = snap[static_cast<size_t>(i)];
        char label[64];
        std::snprintf(label, sizeof(label), "#%llu", static_cast<unsigned long long>(msg.seq));

        const bool sel = (selected_msg_idx_ == i);
        if (ImGui::Selectable(label, sel, 0, ImVec2(0, 0))) {
            selected_msg_idx_ = i;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::BeginTooltip();
            ImGui::Text("t = %s", format_timestamp(msg.timestamp_ns).c_str());
            ImGui::EndTooltip();
        }
    }

    ImGui::EndChild();
    ImGui::SameLine();

    // Detail pane.
    ImGui::BeginChild("##msg_detail", ImVec2(0, avail_h), true);

    // Resolve which message to display (default: latest = back of snap).
    int display_idx = selected_msg_idx_;
    if (display_idx < 0 || display_idx >= static_cast<int>(snap.size())) {
        display_idx = static_cast<int>(snap.size()) - 1;
    }

    EchoMessage display_msg = snap[static_cast<size_t>(display_idx)];

    // Header: seq + timestamp.
    ImGui::TextDisabled("msg #%llu  |  t = %s",
                        static_cast<unsigned long long>(display_msg.seq),
                        format_timestamp(display_msg.timestamp_ns).c_str());
    ImGui::Separator();

    draw_message_tree(display_msg);

    ImGui::EndChild();

    ImGui::End();
}

void TopicEchoPanel::draw_controls()
{
    // Topic label.
    if (topic_name_.empty()) {
        ImGui::TextColored(kColorDisabled, "● No topic");
    } else if (is_paused()) {
        ImGui::TextColored(kColorPaused, "⏸ %s", topic_name_.c_str());
    } else {
        ImGui::TextColored(kColorLive, "● %s", topic_name_.c_str());
    }

    ImGui::SameLine(0, 16.0f);

    // Pause / Resume button.
    if (is_paused()) {
        if (ImGui::SmallButton("Resume")) resume();
    } else {
        if (ImGui::SmallButton("Pause")) pause();
    }

    ImGui::SameLine();

    if (ImGui::SmallButton("Clear")) clear();

    // Right-aligned: total count + type.
    if (!type_name_.empty()) {
        const auto slash = type_name_.rfind('/');
        const char* short_type = (slash != std::string::npos)
                                     ? type_name_.c_str() + slash + 1
                                     : type_name_.c_str();
        const float type_w = ImGui::CalcTextSize(short_type).x + 8.0f;
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - type_w + ImGui::GetCursorPosX());
        ImGui::TextDisabled("%s", short_type);
    }
}

void TopicEchoPanel::draw_message_tree(EchoMessage& msg)
{
    // We iterate through the flat field list and use depth to maintain a
    // logical tree via ImGui TreeNode.  Array heads track open state per
    // entry so the user can expand/collapse individual arrays.

    // Reset hovered_field_ at frame start — each field node sets it on hover.
    hovered_field_.clear();

    size_t idx = 0;
    while (idx < msg.fields.size()) {
        draw_field_node(msg.fields[idx], idx, msg.fields);
    }

    // Fire hover callback if the hovered field changed.
    if (hovered_field_ != prev_hovered_field_) {
        prev_hovered_field_ = hovered_field_;
        if (hover_cb_)
            hover_cb_(topic_name_, hovered_field_);
    }
}

void TopicEchoPanel::draw_field_node(EchoFieldValue& fv,
                                      size_t&         idx,
                                      const std::vector<EchoFieldValue>& all_fields)
{
    const float indent = static_cast<float>(fv.depth) * ImGui::GetStyle().IndentSpacing;
    if (indent > 0.0f) ImGui::Indent(indent);

    switch (fv.kind) {
        case EchoFieldValue::Kind::NestedHead: {
            // Non-leaf: just a label; children follow at depth+1.
            ImGui::TextColored(kColorNested, "%s:", fv.display_name.c_str());
            ++idx;
            // Consume children (depth > fv.depth).
            const int parent_depth = fv.depth;
            while (idx < all_fields.size() && all_fields[idx].depth > parent_depth) {
                auto& child = const_cast<EchoFieldValue&>(all_fields[idx]);
                draw_field_node(child, idx, all_fields);
            }
            break;
        }
        case EchoFieldValue::Kind::ArrayHead: {
            char label[128];
            std::snprintf(label, sizeof(label), "%s  [%d items]",
                          fv.display_name.c_str(), fv.array_len);
            const bool open = ImGui::TreeNodeEx(fv.path.c_str(),
                                                ImGuiTreeNodeFlags_SpanFullWidth,
                                                "%s", label);
            fv.is_open = open;
            ++idx;
            const int parent_depth = fv.depth;
            if (open) {
                // Render children (Kind::ArrayElement at depth+1).
                while (idx < all_fields.size() && all_fields[idx].depth > parent_depth) {
                    auto& child = const_cast<EchoFieldValue&>(all_fields[idx]);
                    draw_field_node(child, idx, all_fields);
                }
                ImGui::TreePop();
            } else {
                // Skip children.
                while (idx < all_fields.size() && all_fields[idx].depth > parent_depth) {
                    ++idx;
                }
            }
            break;
        }
        case EchoFieldValue::Kind::ArrayElement: {
            // Invisible selectable so this row gets a drag handle.
            char sel_id[256];
            std::snprintf(sel_id, sizeof(sel_id), "##sel_%s", fv.path.c_str());
            ImGui::Selectable(sel_id, false,
                              ImGuiSelectableFlags_AllowOverlap |
                              ImGuiSelectableFlags_SpanAllColumns,
                              ImVec2(0, ImGui::GetTextLineHeight()));
            // Hover highlight + copy button.
            if (ImGui::IsItemHovered()) {
                hovered_field_ = fv.path;
                // Small clipboard icon button on hover.
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 18.0f);
                char copy_id[280];
                std::snprintf(copy_id, sizeof(copy_id), "##cp_%s", fv.path.c_str());
                if (ImGui::SmallButton(copy_id)) {
                    ImGui::SetClipboardText(fv.path.c_str());
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                    ImGui::SetTooltip("Copy field path: %s", fv.path.c_str());
                }
            }
            // Drag source for numeric array element.
            if (drag_drop_) {
                FieldDragPayload payload;
                payload.topic_name = topic_name_;
                payload.field_path = fv.path;
                payload.type_name  = type_name_;
                payload.label      = FieldDragPayload::make_label(topic_name_, fv.path);
                drag_drop_->begin_drag_source(payload);
                char ctx_id[280];
                std::snprintf(ctx_id, sizeof(ctx_id), "##fctx_%s", fv.path.c_str());
                drag_drop_->show_context_menu(payload, ctx_id);
            }
            ImGui::SameLine();
            ImGui::TextColored(kColorArray, "%-8s", fv.display_name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(kColorNumeric, "%s", format_numeric(fv.numeric).c_str());
            ++idx;
            break;
        }
        case EchoFieldValue::Kind::Numeric: {
            // Invisible selectable so this row gets a drag handle.
            char sel_id[256];
            std::snprintf(sel_id, sizeof(sel_id), "##sel_%s", fv.path.c_str());
            ImGui::Selectable(sel_id, false,
                              ImGuiSelectableFlags_AllowOverlap |
                              ImGuiSelectableFlags_SpanAllColumns,
                              ImVec2(0, ImGui::GetTextLineHeight()));
            // Hover highlight + copy button.
            if (ImGui::IsItemHovered()) {
                hovered_field_ = fv.path;
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 18.0f);
                char copy_id[280];
                std::snprintf(copy_id, sizeof(copy_id), "##cp_%s", fv.path.c_str());
                if (ImGui::SmallButton(copy_id)) {
                    ImGui::SetClipboardText(fv.path.c_str());
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                    ImGui::SetTooltip("Copy field path: %s", fv.path.c_str());
                }
            }
            // Drag source + right-click menu.
            if (drag_drop_) {
                FieldDragPayload payload;
                payload.topic_name = topic_name_;
                payload.field_path = fv.path;
                payload.type_name  = type_name_;
                payload.label      = FieldDragPayload::make_label(topic_name_, fv.path);
                drag_drop_->begin_drag_source(payload);
                char ctx_id[280];
                std::snprintf(ctx_id, sizeof(ctx_id), "##fctx_%s", fv.path.c_str());
                drag_drop_->show_context_menu(payload, ctx_id);
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(fv.display_name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(kColorNumeric, "%s", format_numeric(fv.numeric).c_str());
            ++idx;
            break;
        }
        case EchoFieldValue::Kind::Text: {
            ImGui::TextUnformatted(fv.display_name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(kColorText, "\"%s\"", fv.text.c_str());
            ++idx;
            break;
        }
    }

    if (indent > 0.0f) ImGui::Unindent(indent);
}

#else   // !SPECTRA_USE_IMGUI

void TopicEchoPanel::draw(bool* /*p_open*/) {}
void TopicEchoPanel::draw_controls() {}
void TopicEchoPanel::draw_message_tree(EchoMessage& /*msg*/) {}
void TopicEchoPanel::draw_field_node(EchoFieldValue& /*fv*/,
                                      size_t&         /*idx*/,
                                      const std::vector<EchoFieldValue>& /*all_fields*/) {}
void TopicEchoPanel::draw_message_list() {}

#endif  // SPECTRA_USE_IMGUI

}   // namespace spectra::adapters::ros2
