#pragma once

// ServiceCaller — ROS2 service call backend for the spectra-ros adapter.
//
// Discovers available services via TopicDiscovery, introspects their request
// and response schemas at runtime via MessageIntrospector, and issues
// asynchronous service calls with configurable timeouts.
//
// Call history (up to max_history entries) is maintained so the panel can
// replay prior requests.  Individual history entries can be serialized to /
// deserialized from JSON strings for clipboard import/export.
//
// Thread model:
//   - service_call() dispatches the async call onto the ROS2 executor thread
//     and returns a CallHandle immediately.
//   - The response callback fires on the executor thread; it updates the
//     CallRecord atomically so the render thread can poll it safely.
//   - All history mutations are protected by history_mutex_.
//
// Typical usage:
//   ServiceCaller caller(node, &introspector, &discovery);
//   auto h = caller.call("/set_bool", R"({"data": true})", 5.0);
//   // ... later, from render thread:
//   if (auto* rec = caller.record(h)) {
//       if (rec->state == CallState::Done) { /* use rec->response_json */ }
//   }

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <rclcpp/rclcpp.hpp>

#include "message_introspector.hpp"
#include "topic_discovery.hpp"

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// CallState — lifecycle of one async service call
// ---------------------------------------------------------------------------

enum class CallState : uint8_t
{
    Pending,    // call dispatched, waiting for response
    Done,       // response received (may still be an error)
    TimedOut,   // no response before timeout
    Error,      // rclcpp error (service unavailable, etc.)
};

const char* call_state_name(CallState s);

// ---------------------------------------------------------------------------
// ServiceFieldValue — one editable field in the request form
// ---------------------------------------------------------------------------

struct ServiceFieldValue
{
    std::string path;           // dot-separated path, e.g. "data" or "pose.position.x"
    std::string display_name;   // leaf name
    FieldType   type{FieldType::Unknown};
    int         depth{0};   // nesting level for indentation

    // Editable value (all stored as string; converted on call).
    std::string value_str{"0"};

    // For bool fields: treat value_str as "true"/"false".
    bool is_bool() const { return type == FieldType::Bool; }

    // For nested struct heads: no editable value, just a label.
    bool is_struct_head() const { return type == FieldType::Message; }
};

// ---------------------------------------------------------------------------
// CallRecord — result of one service call
// ---------------------------------------------------------------------------

struct CallRecord
{
    uint64_t    id{0};            // monotonic call ID
    std::string service_name;     // e.g. "/set_bool"
    std::string service_type;     // e.g. "std_srvs/srv/SetBool"
    std::string request_json;     // JSON of request fields at call time
    std::string response_json;    // JSON of response (filled on Done)
    std::string error_message;    // human-readable error (Error/TimedOut)
    double      timeout_s{5.0};   // configured timeout

    std::atomic<CallState> state{CallState::Pending};

    // Wall-clock timestamps (seconds since epoch).
    double call_time_s{0.0};
    double response_time_s{0.0};

    // Latency in milliseconds (filled on Done).
    double latency_ms{0.0};

    // Non-copyable, non-movable (std::atomic member).
    CallRecord()                             = default;
    CallRecord(const CallRecord&)            = delete;
    CallRecord& operator=(const CallRecord&) = delete;
    CallRecord(CallRecord&&)                 = delete;
    CallRecord& operator=(CallRecord&&)      = delete;
};

using CallHandle                                = uint64_t;
static constexpr CallHandle INVALID_CALL_HANDLE = 0;

// ---------------------------------------------------------------------------
// ServiceInfo extended — augments TopicDiscovery::ServiceInfo with type cache
// ---------------------------------------------------------------------------

struct ServiceEntry
{
    std::string              name;
    std::string              type;   // first type string, or ""
    std::vector<std::string> all_types;

    // Introspected request schema (nullptr = not yet introspected or failed).
    std::shared_ptr<const MessageSchema> request_schema;
    std::shared_ptr<const MessageSchema> response_schema;

    bool schema_loaded{false};   // true if introspection was attempted
    bool schema_ok{false};       // true if introspection succeeded
};

// ---------------------------------------------------------------------------
// ServiceCaller
// ---------------------------------------------------------------------------

class ServiceCaller
{
   public:
    // Construct with a ROS2 node pointer, optional introspector, and optional
    // TopicDiscovery for service list refresh.  All pointers must outlive this.
    explicit ServiceCaller(rclcpp::Node::SharedPtr node,
                           MessageIntrospector*    introspector = nullptr,
                           TopicDiscovery*         discovery    = nullptr);
    ~ServiceCaller();

    ServiceCaller(const ServiceCaller&)            = delete;
    ServiceCaller& operator=(const ServiceCaller&) = delete;
    ServiceCaller(ServiceCaller&&)                 = delete;
    ServiceCaller& operator=(ServiceCaller&&)      = delete;

    // ---------- service discovery ------------------------------------------

    // Refresh the known service list from TopicDiscovery (if wired) or by
    // querying the node directly.  Thread-safe.
    void refresh_services();

    // Current known services snapshot.  Thread-safe.
    std::vector<ServiceEntry> services() const;

    // Number of known services.  Thread-safe.
    std::size_t service_count() const;

    // Find a service entry by name.  Returns nullopt if not found.
    std::optional<ServiceEntry> find_service(const std::string& name) const;

    // ---------- schema introspection ---------------------------------------

    // Introspect the request + response schemas for a given service.
    // Populates the ServiceEntry's schema fields.  Thread-safe (mutex).
    // Returns false if introspection fails.
    bool load_schema(const std::string& service_name);

    // Build an editable field list for a schema (for the request form).
    // Returns flat list of ServiceFieldValue ordered depth-first.
    static std::vector<ServiceFieldValue> fields_from_schema(const MessageSchema& schema);

    // ---------- call dispatch ----------------------------------------------

    // Dispatch an async service call.  request_json is a flat JSON object
    // mapping field paths to values (strings for all types).
    // Returns a CallHandle that can be used to poll the result.
    // Returns INVALID_CALL_HANDLE if the service is not found / node null.
    CallHandle call(const std::string& service_name,
                    const std::string& request_json,
                    double             timeout_s = 5.0);

    // ---------- result polling ---------------------------------------------

    // Get a pointer to the CallRecord for a given handle.
    // Returns nullptr if the handle is not found.
    // The pointer is valid until the history is pruned.
    // NOTE: caller must NOT store the pointer across frames — access under lock
    //       via get_record_copy() for thread-safe snapshots.
    const CallRecord* record(CallHandle h) const;

    // ---------- history ----------------------------------------------------

    // All call records in chronological order (oldest first).
    // Returns a snapshot vector of shared_ptrs.
    std::vector<std::shared_ptr<CallRecord>> history() const;

    // Number of history entries.
    std::size_t history_count() const;

    // Clear all history.
    void clear_history();

    // Prune to at most max_history entries (drops oldest).
    void prune_history(std::size_t max_history);

    // Maximum retained history entries (default 200).
    void        set_max_history(std::size_t n) { max_history_ = n; }
    std::size_t max_history() const { return max_history_; }

    // ---------- JSON import / export ---------------------------------------

    // Serialize a single CallRecord to a JSON string.
    // Format: {"service":"...", "type":"...", "request":{...}, "response":{...},
    //          "state":"...", "latency_ms": N, "call_time": N}
    static std::string record_to_json(const CallRecord& rec);

    // Deserialize a JSON string back into a CallRecord (for import/replay).
    // Returns false if parsing fails.
    static bool record_from_json(const std::string& json, CallRecord& out);

    // Serialize the full history as a JSON array.
    std::string history_to_json() const;

    // Import history from a JSON array string.  Appends to existing history.
    // Returns number of records successfully imported.
    std::size_t history_from_json(const std::string& json);

    // Build a request JSON from an editable field list (for the form→call path).
    static std::string fields_to_json(const std::vector<ServiceFieldValue>& fields);

    // Parse a JSON object string into a field list (for import→form path).
    static bool json_to_fields(const std::string& json, std::vector<ServiceFieldValue>& fields);

    // ---------- configuration ----------------------------------------------

    void   set_default_timeout(double s) { default_timeout_ = s; }
    double default_timeout() const { return default_timeout_; }

    // ---------- callbacks --------------------------------------------------

    using CallDoneCallback = std::function<void(CallHandle, const CallRecord&)>;
    void set_call_done_callback(CallDoneCallback cb);

   private:
    // Internal: actually dispatch a generic service call.
    // Runs on the calling thread; the response fires the done callback on
    // the executor thread.
    void dispatch_call(std::shared_ptr<CallRecord> rec);

    // Query services directly from the node (fallback if no discovery).
    std::vector<ServiceEntry> query_services_from_node() const;

    // Build a ServiceEntry from a ServiceInfo.
    ServiceEntry entry_from_info(const ServiceInfo& info) const;

    // Monotonic ID generator.
    CallHandle next_id() { return ++id_counter_; }

    // Wall-clock seconds.
    static double wall_time_s();

    rclcpp::Node::SharedPtr node_;
    MessageIntrospector*    introspector_{nullptr};
    TopicDiscovery*         discovery_{nullptr};

    // Service list (protected by services_mutex_).
    mutable std::mutex        services_mutex_;
    std::vector<ServiceEntry> services_;

    // Call history (protected by history_mutex_).
    mutable std::mutex                                          history_mutex_;
    std::vector<std::shared_ptr<CallRecord>>                    history_;
    std::unordered_map<CallHandle, std::shared_ptr<CallRecord>> handle_map_;

    std::atomic<CallHandle> id_counter_{0};
    std::size_t             max_history_{200};
    double                  default_timeout_{5.0};

    CallDoneCallback   done_cb_;
    mutable std::mutex cb_mutex_;
};

}   // namespace spectra::adapters::ros2
