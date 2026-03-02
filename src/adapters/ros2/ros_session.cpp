// ros_session.cpp — RosSession save/load implementation (G3).

#include "ros_session.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace spectra::adapters::ros2
{

// ===========================================================================
// RosSessionManager — public API
// ===========================================================================

RosSessionManager::RosSessionManager() = default;

// ---------------------------------------------------------------------------
// save
// ---------------------------------------------------------------------------

SaveResult RosSessionManager::save(const RosSession& session, const std::string& path)
{
    SaveResult result;
    result.path = path;

    if (path.empty()) {
        result.error = "path is empty";
        return result;
    }

    if (!ensure_directory(path)) {
        result.error = "failed to create parent directory for: " + path;
        return result;
    }

    // Stamp the saved_at field.
    RosSession stamped = session;
    stamped.saved_at   = current_iso8601();

    std::string json = serialize(stamped);
    if (!write_file(path, json)) {
        result.error = "failed to write session file: " + path;
        return result;
    }

    last_path_ = path;
    push_recent(path, session.node_name, stamped.saved_at);

    result.ok = true;
    return result;
}

// ---------------------------------------------------------------------------
// load
// ---------------------------------------------------------------------------

LoadResult RosSessionManager::load(const std::string& path)
{
    LoadResult result;
    result.path = path;

    if (path.empty()) {
        result.error = "path is empty";
        return result;
    }

    std::string content;
    if (!read_file(path, content)) {
        result.error = "failed to read session file: " + path;
        return result;
    }

    std::string err;
    if (!deserialize(content, result.session, err)) {
        result.error = "JSON parse error: " + err;
        return result;
    }

    last_path_ = path;
    push_recent(path, result.session.node_name, result.session.saved_at);

    result.ok = true;
    return result;
}

// ---------------------------------------------------------------------------
// auto_save
// ---------------------------------------------------------------------------

SaveResult RosSessionManager::auto_save(const RosSession& session)
{
    if (last_path_.empty()) {
        SaveResult r;
        r.ok    = false;
        r.error = "no last_path set — use save() first or set_last_path()";
        return r;
    }
    return save(session, last_path_);
}

// ---------------------------------------------------------------------------
// recent list
// ---------------------------------------------------------------------------

std::vector<RecentEntry> RosSessionManager::load_recent()
{
    std::string path = default_recent_path();
    std::string content;
    if (!read_file(path, content)) {
        return {};
    }
    return deserialize_recent(content);
}

bool RosSessionManager::save_recent(const std::vector<RecentEntry>& entries)
{
    std::string path = default_recent_path();
    if (!ensure_directory(path)) {
        return false;
    }
    std::string json = serialize_recent(entries);
    return write_file(path, json);
}

void RosSessionManager::push_recent(const std::string& path,
                                    const std::string& node,
                                    const std::string& saved_at)
{
    auto list = load_recent();

    // Remove any existing entry with the same path.
    list.erase(
        std::remove_if(list.begin(), list.end(),
                       [&path](const RecentEntry& e){ return e.path == path; }),
        list.end());

    // Insert at front.
    RecentEntry e;
    e.path     = path;
    e.node     = node;
    e.saved_at = saved_at.empty() ? current_iso8601() : saved_at;
    list.insert(list.begin(), e);

    // Trim to MAX_RECENT.
    if (static_cast<int>(list.size()) > MAX_RECENT) {
        list.resize(MAX_RECENT);
    }

    save_recent(list);
}

void RosSessionManager::remove_recent(const std::string& path)
{
    auto list = load_recent();
    list.erase(
        std::remove_if(list.begin(), list.end(),
                       [&path](const RecentEntry& e){ return e.path == path; }),
        list.end());
    save_recent(list);
}

void RosSessionManager::clear_recent()
{
    save_recent({});
}

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

std::string RosSessionManager::default_recent_path()
{
    // Use $HOME/.config/spectra/ros_recent.json
    const char* home = std::getenv("HOME");
    if (!home || home[0] == '\0') {
        home = "/tmp";
    }
    std::string dir = std::string(home) + "/.config/spectra";
    return dir + "/ros_recent.json";
}

std::string RosSessionManager::default_session_path(const std::string& node_name)
{
    const char* home = std::getenv("HOME");
    if (!home || home[0] == '\0') {
        home = "/tmp";
    }
    std::string name = node_name.empty() ? "default" : node_name;
    // Sanitise: replace '/' with '_'
    for (char& c : name) {
        if (c == '/' || c == '\\' || c == ':') c = '_';
    }
    return std::string(home) + "/.config/spectra/sessions/" + name + ".spectra-ros-session";
}

// ===========================================================================
// JSON serialization
// ===========================================================================

// ---------------------------------------------------------------------------
// json_escape
// ---------------------------------------------------------------------------

std::string RosSessionManager::json_escape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// json_get_* — extract scalar values from a flat JSON object string
// ---------------------------------------------------------------------------

std::string RosSessionManager::json_get_string(const std::string& json,
                                               const std::string& key)
{
    // Look for "key":"value" pattern.
    std::string pattern = "\"" + key + "\"";
    auto pos = json.find(pattern);
    if (pos == std::string::npos) return {};

    pos += pattern.size();
    // Skip whitespace and ':'
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                  json[pos] == '\n' || json[pos] == ':')) {
        ++pos;
    }
    if (pos >= json.size() || json[pos] != '"') return {};
    ++pos;  // skip opening quote

    std::string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            ++pos;
            switch (json[pos]) {
            case '"':  result += '"';  break;
            case '\\': result += '\\'; break;
            case 'n':  result += '\n'; break;
            case 'r':  result += '\r'; break;
            case 't':  result += '\t'; break;
            default:   result += json[pos]; break;
            }
        } else {
            result += json[pos];
        }
        ++pos;
    }
    return result;
}

int RosSessionManager::json_get_int(const std::string& json,
                                    const std::string& key,
                                    int                default_val)
{
    std::string pattern = "\"" + key + "\"";
    auto pos = json.find(pattern);
    if (pos == std::string::npos) return default_val;

    pos += pattern.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                  json[pos] == '\n' || json[pos] == ':')) {
        ++pos;
    }
    if (pos >= json.size()) return default_val;

    try {
        size_t consumed = 0;
        int val = std::stoi(json.substr(pos), &consumed);
        (void)consumed;
        return val;
    } catch (...) {
        return default_val;
    }
}

double RosSessionManager::json_get_double(const std::string& json,
                                          const std::string& key,
                                          double             default_val)
{
    std::string pattern = "\"" + key + "\"";
    auto pos = json.find(pattern);
    if (pos == std::string::npos) return default_val;

    pos += pattern.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                  json[pos] == '\n' || json[pos] == ':')) {
        ++pos;
    }
    if (pos >= json.size()) return default_val;

    try {
        size_t consumed = 0;
        double val = std::stod(json.substr(pos), &consumed);
        (void)consumed;
        return val;
    } catch (...) {
        return default_val;
    }
}

bool RosSessionManager::json_get_bool(const std::string& json,
                                      const std::string& key,
                                      bool               default_val)
{
    std::string pattern = "\"" + key + "\"";
    auto pos = json.find(pattern);
    if (pos == std::string::npos) return default_val;

    pos += pattern.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                  json[pos] == '\n' || json[pos] == ':')) {
        ++pos;
    }
    if (pos >= json.size()) return default_val;

    if (json.compare(pos, 4, "true") == 0)  return true;
    if (json.compare(pos, 5, "false") == 0) return false;
    return default_val;
}

// ---------------------------------------------------------------------------
// current_iso8601
// ---------------------------------------------------------------------------

std::string RosSessionManager::current_iso8601()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    struct std::tm tm_utc{};
#if defined(_WIN32)
    gmtime_s(&tm_utc, &t);
#else
    gmtime_r(&t, &tm_utc);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
                  tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);
    return buf;
}

// ---------------------------------------------------------------------------
// build_object — minimal JSON object builder from initializer list
// ---------------------------------------------------------------------------

std::string RosSessionManager::build_object(
    std::initializer_list<std::pair<std::string, std::string>> kv)
{
    std::string out = "{";
    bool first = true;
    for (const auto& [k, v] : kv) {
        if (!first) out += ",";
        out += "\n  \"" + k + "\": " + v;
        first = false;
    }
    out += "\n}";
    return out;
}

// ---------------------------------------------------------------------------
// serialize_subscription
// ---------------------------------------------------------------------------

std::string RosSessionManager::serialize_subscription(const SubscriptionEntry& e)
{
    std::string out = "{";
    out += "\"topic\":\"" + json_escape(e.topic) + "\",";
    out += "\"field_path\":\"" + json_escape(e.field_path) + "\",";
    out += "\"type_name\":\"" + json_escape(e.type_name) + "\",";
    out += "\"subplot_slot\":" + std::to_string(e.subplot_slot) + ",";
    out += "\"time_window_s\":" + std::to_string(e.time_window_s) + ",";
    out += "\"scroll_paused\":" + std::string(e.scroll_paused ? "true" : "false");
    out += "}";
    return out;
}

static std::string serialize_var_binding(const ExpressionEntry::VarBinding& b)
{
    std::string out = "{";
    out += "\"variable\":\"" + RosSessionManager::json_escape(b.variable) + "\",";
    out += "\"topic\":\"" + RosSessionManager::json_escape(b.topic) + "\",";
    out += "\"field_path\":\"" + RosSessionManager::json_escape(b.field_path) + "\"";
    out += "}";
    return out;
}

// ---------------------------------------------------------------------------
// serialize_expression
// ---------------------------------------------------------------------------

std::string RosSessionManager::serialize_expression(const ExpressionEntry& e)
{
    std::string out = "{";
    out += "\"expression\":\"" + json_escape(e.expression) + "\",";
    out += "\"label\":\"" + json_escape(e.label) + "\",";
    out += "\"subplot_slot\":" + std::to_string(e.subplot_slot) + ",";
    out += "\"bindings\":[";
    for (size_t i = 0; i < e.bindings.size(); ++i) {
        if (i > 0) out += ",";
        out += serialize_var_binding(e.bindings[i]);
    }
    out += "]}";
    return out;
}

// ---------------------------------------------------------------------------
// serialize_preset
// ---------------------------------------------------------------------------

std::string RosSessionManager::serialize_preset(const ExpressionPresetEntry& e)
{
    std::string out = "{";
    out += "\"name\":\"" + json_escape(e.name) + "\",";
    out += "\"expression\":\"" + json_escape(e.expression) + "\",";
    out += "\"variables\":[";
    for (size_t i = 0; i < e.variables.size(); ++i) {
        if (i > 0) out += ",";
        out += "\"" + json_escape(e.variables[i]) + "\"";
    }
    out += "]}";
    return out;
}

// ---------------------------------------------------------------------------
// serialize_panels
// ---------------------------------------------------------------------------

std::string RosSessionManager::serialize_panels(const PanelVisibility& p)
{
    std::string out = "{";
    out += "\"topic_list\":" + std::string(p.topic_list  ? "true" : "false") + ",";
    out += "\"topic_echo\":" + std::string(p.topic_echo  ? "true" : "false") + ",";
    out += "\"topic_stats\":" + std::string(p.topic_stats ? "true" : "false") + ",";
    out += "\"plot_area\":" + std::string(p.plot_area   ? "true" : "false") + ",";
    out += "\"bag_info\":" + std::string(p.bag_info    ? "true" : "false");
    out += "}";
    return out;
}

// ---------------------------------------------------------------------------
// serialize (full session)
// ---------------------------------------------------------------------------

std::string RosSessionManager::serialize(const RosSession& session)
{
    std::string out = "{\n";
    out += "  \"version\": " + std::to_string(session.version) + ",\n";
    out += "  \"node_name\": \"" + json_escape(session.node_name) + "\",\n";
    out += "  \"node_ns\": \"" + json_escape(session.node_ns) + "\",\n";
    out += "  \"layout\": \"" + json_escape(session.layout) + "\",\n";
    out += "  \"subplot_rows\": " + std::to_string(session.subplot_rows) + ",\n";
    out += "  \"subplot_cols\": " + std::to_string(session.subplot_cols) + ",\n";
    out += "  \"time_window_s\": " + std::to_string(session.time_window_s) + ",\n";
    out += "  \"saved_at\": \"" + json_escape(session.saved_at) + "\",\n";
    out += "  \"description\": \"" + json_escape(session.description) + "\",\n";

    // Subscriptions array.
    out += "  \"subscriptions\": [";
    for (size_t i = 0; i < session.subscriptions.size(); ++i) {
        if (i > 0) out += ",";
        out += "\n    " + serialize_subscription(session.subscriptions[i]);
    }
    out += (session.subscriptions.empty() ? "" : "\n  ") + std::string("],\n");

    // Expressions array.
    out += "  \"expressions\": [";
    for (size_t i = 0; i < session.expressions.size(); ++i) {
        if (i > 0) out += ",";
        out += "\n    " + serialize_expression(session.expressions[i]);
    }
    out += (session.expressions.empty() ? "" : "\n  ") + std::string("],\n");

    // Expression presets array.
    out += "  \"expression_presets\": [";
    for (size_t i = 0; i < session.expression_presets.size(); ++i) {
        if (i > 0) out += ",";
        out += "\n    " + serialize_preset(session.expression_presets[i]);
    }
    out += (session.expression_presets.empty() ? "" : "\n  ") + std::string("],\n");

    // Panels object.
    out += "  \"panels\": " + serialize_panels(session.panels) + "\n";
    out += "}\n";
    return out;
}

// ===========================================================================
// JSON deserialization
// ===========================================================================

// ---------------------------------------------------------------------------
// extract_array — pull the JSON array body for a given key
// ---------------------------------------------------------------------------

std::string RosSessionManager::extract_array(const std::string& json,
                                             const std::string& key)
{
    std::string pattern = "\"" + key + "\"";
    auto pos = json.find(pattern);
    if (pos == std::string::npos) return {};

    pos += pattern.size();
    // Skip whitespace and ':'
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                  json[pos] == '\n' || json[pos] == ':')) {
        ++pos;
    }
    if (pos >= json.size() || json[pos] != '[') return {};

    // Collect until matching ']'
    int depth = 0;
    size_t start = pos;
    while (pos < json.size()) {
        char c = json[pos];
        if (c == '[')      ++depth;
        else if (c == ']') { --depth; if (depth == 0) { ++pos; break; } }
        else if (c == '"') {
            ++pos;
            while (pos < json.size() && json[pos] != '"') {
                if (json[pos] == '\\') ++pos;
                ++pos;
            }
        }
        ++pos;
    }
    return json.substr(start, pos - start);
}

// ---------------------------------------------------------------------------
// split_objects — split a "[{...},{...}]" string into object strings
// ---------------------------------------------------------------------------

std::vector<std::string> RosSessionManager::split_objects(const std::string& array_body)
{
    std::vector<std::string> result;
    size_t pos = 0;
    // Skip leading '['
    while (pos < array_body.size() && array_body[pos] != '{') ++pos;

    while (pos < array_body.size()) {
        if (array_body[pos] != '{') { ++pos; continue; }
        int depth = 0;
        size_t start = pos;
        while (pos < array_body.size()) {
            char c = array_body[pos];
            if (c == '{')      ++depth;
            else if (c == '}') { --depth; if (depth == 0) { ++pos; break; } }
            else if (c == '"') {
                ++pos;
                while (pos < array_body.size() && array_body[pos] != '"') {
                    if (array_body[pos] == '\\') ++pos;
                    ++pos;
                }
            }
            ++pos;
        }
        result.push_back(array_body.substr(start, pos - start));
    }
    return result;
}

// ---------------------------------------------------------------------------
// extract_nested_object — find the value object for a given key
// ---------------------------------------------------------------------------
static std::string extract_nested_object(const std::string& json, const std::string& key)
{
    std::string pattern = "\"" + key + "\"";
    auto pos = json.find(pattern);
    if (pos == std::string::npos) return {};

    pos += pattern.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                  json[pos] == '\n' || json[pos] == ':')) {
        ++pos;
    }
    if (pos >= json.size() || json[pos] != '{') return {};

    int depth = 0;
    size_t start = pos;
    while (pos < json.size()) {
        char c = json[pos];
        if (c == '{')      ++depth;
        else if (c == '}') { --depth; if (depth == 0) { ++pos; break; } }
        else if (c == '"') {
            ++pos;
            while (pos < json.size() && json[pos] != '"') {
                if (json[pos] == '\\') ++pos;
                ++pos;
            }
        }
        ++pos;
    }
    return json.substr(start, pos - start);
}

// ---------------------------------------------------------------------------
// extract_string_array — pull ["a","b",...] for a given key
// ---------------------------------------------------------------------------
static std::vector<std::string> extract_string_array(const std::string& json,
                                                      const std::string& key)
{
    // Inline the array body extraction (avoids calling private static member).
    std::string body;
    {
        std::string pattern = "\"" + key + "\"";
        auto pos = json.find(pattern);
        if (pos != std::string::npos) {
            pos += pattern.size();
            while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                          json[pos] == '\n' || json[pos] == ':')) {
                ++pos;
            }
            if (pos < json.size() && json[pos] == '[') {
                int depth = 0;
                size_t start = pos;
                while (pos < json.size()) {
                    char c = json[pos];
                    if (c == '[')      ++depth;
                    else if (c == ']') { --depth; if (depth == 0) { ++pos; break; } }
                    else if (c == '"') {
                        ++pos;
                        while (pos < json.size() && json[pos] != '"') {
                            if (json[pos] == '\\') ++pos;
                            ++pos;
                        }
                    }
                    ++pos;
                }
                body = json.substr(start, pos - start);
            }
        }
    }
    if (body.empty()) return {};

    std::vector<std::string> result;
    size_t pos = 0;
    while (pos < body.size()) {
        if (body[pos] != '"') { ++pos; continue; }
        ++pos;
        std::string val;
        while (pos < body.size() && body[pos] != '"') {
            if (body[pos] == '\\' && pos + 1 < body.size()) {
                ++pos;
                switch (body[pos]) {
                case '"':  val += '"';  break;
                case '\\': val += '\\'; break;
                case 'n':  val += '\n'; break;
                case 't':  val += '\t'; break;
                default:   val += body[pos]; break;
                }
            } else {
                val += body[pos];
            }
            ++pos;
        }
        ++pos;  // closing quote
        result.push_back(std::move(val));
    }
    return result;
}

// ---------------------------------------------------------------------------
// deserialize_subscription
// ---------------------------------------------------------------------------

SubscriptionEntry RosSessionManager::deserialize_subscription(const std::string& json)
{
    SubscriptionEntry e;
    e.topic         = json_get_string(json, "topic");
    e.field_path    = json_get_string(json, "field_path");
    e.type_name     = json_get_string(json, "type_name");
    e.subplot_slot  = json_get_int(json, "subplot_slot", 0);
    e.time_window_s = json_get_double(json, "time_window_s", 0.0);
    e.scroll_paused = json_get_bool(json, "scroll_paused", false);
    return e;
}

// ---------------------------------------------------------------------------
// deserialize_expression
// ---------------------------------------------------------------------------

ExpressionEntry RosSessionManager::deserialize_expression(const std::string& json)
{
    ExpressionEntry e;
    e.expression  = json_get_string(json, "expression");
    e.label       = json_get_string(json, "label");
    e.subplot_slot = json_get_int(json, "subplot_slot", 0);

    std::string bindings_body = extract_array(json, "bindings");
    for (const auto& obj : split_objects(bindings_body)) {
        ExpressionEntry::VarBinding b;
        b.variable   = json_get_string(obj, "variable");
        b.topic      = json_get_string(obj, "topic");
        b.field_path = json_get_string(obj, "field_path");
        if (!b.variable.empty()) {
            e.bindings.push_back(std::move(b));
        }
    }
    return e;
}

// ---------------------------------------------------------------------------
// deserialize_preset
// ---------------------------------------------------------------------------

ExpressionPresetEntry RosSessionManager::deserialize_preset(const std::string& json)
{
    ExpressionPresetEntry e;
    e.name       = json_get_string(json, "name");
    e.expression = json_get_string(json, "expression");
    e.variables  = extract_string_array(json, "variables");
    return e;
}

// ---------------------------------------------------------------------------
// deserialize_panels
// ---------------------------------------------------------------------------

PanelVisibility RosSessionManager::deserialize_panels(const std::string& json)
{
    PanelVisibility p;
    p.topic_list  = json_get_bool(json, "topic_list",  true);
    p.topic_echo  = json_get_bool(json, "topic_echo",  true);
    p.topic_stats = json_get_bool(json, "topic_stats", true);
    p.plot_area   = json_get_bool(json, "plot_area",   true);
    p.bag_info    = json_get_bool(json, "bag_info",    false);
    return p;
}

// ---------------------------------------------------------------------------
// deserialize (full session)
// ---------------------------------------------------------------------------

bool RosSessionManager::deserialize(const std::string& json,
                                    RosSession&        out,
                                    std::string&       error_out)
{
    if (json.empty()) {
        error_out = "empty input";
        return false;
    }

    int ver = json_get_int(json, "version", -1);
    if (ver < 0) {
        error_out = "missing or invalid 'version' field";
        return false;
    }
    if (ver > SESSION_FORMAT_VERSION) {
        error_out = "session format version " + std::to_string(ver) +
                    " is newer than supported (" +
                    std::to_string(SESSION_FORMAT_VERSION) + ")";
        return false;
    }

    out.version       = ver;
    out.node_name     = json_get_string(json, "node_name");
    out.node_ns       = json_get_string(json, "node_ns");
    out.layout        = json_get_string(json, "layout");
    if (out.layout.empty()) out.layout = "default";
    out.subplot_rows  = json_get_int(json, "subplot_rows", 4);
    out.subplot_cols  = json_get_int(json, "subplot_cols", 1);
    out.time_window_s = json_get_double(json, "time_window_s", 30.0);
    out.saved_at      = json_get_string(json, "saved_at");
    out.description   = json_get_string(json, "description");

    // Subscriptions.
    std::string subs_body = extract_array(json, "subscriptions");
    for (const auto& obj : split_objects(subs_body)) {
        auto e = deserialize_subscription(obj);
        if (!e.topic.empty()) {
            out.subscriptions.push_back(std::move(e));
        }
    }

    // Expressions.
    std::string expr_body = extract_array(json, "expressions");
    for (const auto& obj : split_objects(expr_body)) {
        auto e = deserialize_expression(obj);
        if (!e.expression.empty()) {
            out.expressions.push_back(std::move(e));
        }
    }

    // Expression presets.
    std::string presets_body = extract_array(json, "expression_presets");
    for (const auto& obj : split_objects(presets_body)) {
        auto e = deserialize_preset(obj);
        if (!e.name.empty()) {
            out.expression_presets.push_back(std::move(e));
        }
    }

    // Panels.
    std::string panels_obj = extract_nested_object(json, "panels");
    if (!panels_obj.empty()) {
        out.panels = deserialize_panels(panels_obj);
    }

    return true;
}

// ---------------------------------------------------------------------------
// serialize_recent
// ---------------------------------------------------------------------------

std::string RosSessionManager::serialize_recent(const std::vector<RecentEntry>& entries)
{
    std::string out = "[\n";
    for (size_t i = 0; i < entries.size(); ++i) {
        if (i > 0) out += ",\n";
        const auto& e = entries[i];
        out += "  {\"path\":\"" + json_escape(e.path) + "\","
               "\"node\":\"" + json_escape(e.node) + "\","
               "\"saved_at\":\"" + json_escape(e.saved_at) + "\"}";
    }
    out += "\n]\n";
    return out;
}

// ---------------------------------------------------------------------------
// deserialize_recent
// ---------------------------------------------------------------------------

std::vector<RecentEntry> RosSessionManager::deserialize_recent(const std::string& json)
{
    std::vector<RecentEntry> result;
    // The recent file is a top-level array — parse objects directly.
    for (const auto& obj : split_objects(json)) {
        RecentEntry e;
        e.path     = json_get_string(obj, "path");
        e.node     = json_get_string(obj, "node");
        e.saved_at = json_get_string(obj, "saved_at");
        if (!e.path.empty()) {
            result.push_back(std::move(e));
        }
    }
    return result;
}

// ===========================================================================
// File I/O helpers
// ===========================================================================

bool RosSessionManager::ensure_directory(const std::string& path)
{
    try {
        std::filesystem::path p(path);
        auto dir = p.parent_path();
        if (!dir.empty() && !std::filesystem::exists(dir)) {
            std::filesystem::create_directories(dir);
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool RosSessionManager::write_file(const std::string& path, const std::string& content)
{
    // Atomic write: write to a temp file then rename.
    std::string tmp = path + ".tmp";
    {
        std::ofstream ofs(tmp, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!ofs.is_open()) return false;
        ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!ofs.good()) return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        // Fallback: copy then remove (different filesystems).
        std::filesystem::copy_file(tmp, path,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::remove(tmp, ec);
        return !ec;
    }
    return true;
}

bool RosSessionManager::read_file(const std::string& path, std::string& content_out)
{
    std::ifstream ifs(path, std::ios::in | std::ios::binary);
    if (!ifs.is_open()) return false;
    std::ostringstream ss;
    ss << ifs.rdbuf();
    if (!ifs.good() && !ifs.eof()) return false;
    content_out = ss.str();
    return true;
}

}  // namespace spectra::adapters::ros2
