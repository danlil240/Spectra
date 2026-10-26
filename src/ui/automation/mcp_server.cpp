#include "mcp_server.hpp"

#include "automation_security.hpp"
#include "automation_server.hpp"

#include <spectra/logger.hpp>
#include <spectra/version.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
    #include <arpa/inet.h>
    #include <fcntl.h>
    #include <netinet/in.h>
    #include <poll.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

namespace spectra
{
namespace
{
constexpr const char* kProtocolVersion = "2025-06-18";
constexpr const char* kServerName      = "spectra-automation";
constexpr size_t      kMaxBodyBytes    = 64u * 1024u * 1024u;
constexpr size_t      kMaxHeaderBytes  = 64u * 1024u;
constexpr const char* kInstructions =
    "Control a running Spectra application. Use these tools to interact with Spectra's UI: execute "
    "commands, simulate mouse and keyboard input, create figures, manage series data, and capture "
    "screenshots.";

struct ToolSpec
{
    const char* name;
    const char* description;
    const char* input_schema;
    const char* automation_method;
};

constexpr std::array<ToolSpec, 28> kTools = {{
    {"ping",
     "Ping the Spectra application to verify the connection is alive.",
     R"json({"type":"object","properties":{},"additionalProperties":false})json",
     "ping"},
    {"get_state",
     "Get the current state of the Spectra application.",
     R"json({"type":"object","properties":{},"additionalProperties":false})json",
     "get_state"},
    {"list_commands",
     "List all registered UI commands in the Spectra application.",
     R"json({"type":"object","properties":{},"additionalProperties":false})json",
     "list_commands"},
    {"list_menus",
     "List top-level menu bar entries and their actionable items.",
     R"json({"type":"object","properties":{},"additionalProperties":false})json",
     "list_menus"},
    {"execute_command",
     "Execute a registered Spectra UI command by its ID.",
     R"json({"type":"object","properties":{"command_id":{"type":"string","description":"The command ID to execute."}},"required":["command_id"],"additionalProperties":false})json",
     "execute_command"},
    {"mouse_move",
     "Move the mouse cursor to the specified position.",
     R"json({"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"}},"required":["x","y"],"additionalProperties":false})json",
     "mouse_move"},
    {"mouse_click",
     "Click at the specified position.",
     R"json({"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"button":{"type":"integer","default":0},"modifiers":{"type":"integer","default":0}},"required":["x","y"],"additionalProperties":false})json",
     "mouse_click"},
    {"mouse_drag",
     "Drag the mouse from one position to another.",
     R"json({"type":"object","properties":{"x1":{"type":"number"},"y1":{"type":"number"},"x2":{"type":"number"},"y2":{"type":"number"},"button":{"type":"integer","default":0},"modifiers":{"type":"integer","default":0},"steps":{"type":"integer","default":10}},"required":["x1","y1","x2","y2"],"additionalProperties":false})json",
     "mouse_drag"},
    {"scroll",
     "Scroll at the specified position.",
     R"json({"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"dx":{"type":"number","default":0.0},"dy":{"type":"number","default":1.0}},"required":["x","y"],"additionalProperties":false})json",
     "scroll"},
    {"key_press",
     "Press and release a keyboard key.",
     R"json({"type":"object","properties":{"key":{"type":"integer"},"modifiers":{"type":"integer","default":0}},"required":["key"],"additionalProperties":false})json",
     "key_press"},
    {"create_figure",
     "Create a new figure in Spectra.",
     R"json({"type":"object","properties":{"width":{"type":"integer","default":1280},"height":{"type":"integer","default":720}},"additionalProperties":false})json",
     "create_figure"},
    {"switch_figure",
     "Switch to a specific figure by its ID.",
     R"json({"type":"object","properties":{"figure_id":{"type":"integer"}},"required":["figure_id"],"additionalProperties":false})json",
     "switch_figure"},
    {"add_series",
     "Add a data series to a figure. Provide x/y arrays for custom data, or n_points for "
     "auto-generated sine data.",
     R"json({"type":"object","properties":{"figure_id":{"type":"integer"},"type":{"type":"string","default":"line","description":"Series type: line, scatter, bar, histogram"},"x":{"type":"array","items":{"type":"number"},"description":"X data values"},"y":{"type":"array","items":{"type":"number"},"description":"Y data values"},"bins":{"type":"integer","default":30,"description":"Number of bins (histogram only)"},"n_points":{"type":"integer","default":100,"description":"Points to auto-generate if x/y not provided"},"label":{"type":"string","default":""}},"required":["figure_id"],"additionalProperties":false})json",
     "add_series"},
    {"pump_frames",
     "Advance the application by rendering the specified number of frames.",
     R"json({"type":"object","properties":{"count":{"type":"integer","default":1}},"additionalProperties":false})json",
     "pump_frames"},
    {"capture_screenshot",
     "Capture a screenshot of the current active figure. path is a file name relative to the "
     "capture directory ($SPECTRA_CAPTURE_DIR); the result returns the absolute path.",
     R"json({"type":"object","properties":{"path":{"type":"string","default":"spectra_auto_screenshot.png"}},"additionalProperties":false})json",
     "capture_screenshot"},
    {"capture_window",
     "Capture a full-window screenshot including UI chrome. path is a file name relative to the "
     "capture directory ($SPECTRA_CAPTURE_DIR); the result returns the absolute path.",
     R"json({"type":"object","properties":{"path":{"type":"string","default":"spectra_auto_window.png"}},"additionalProperties":false})json",
     "capture_window"},
    {"resize_window",
     "Resize the active Spectra window.",
     R"json({"type":"object","properties":{"width":{"type":"integer"},"height":{"type":"integer"}},"required":["width","height"],"additionalProperties":false})json",
     "resize_window"},
    {"get_screenshot_base64",
     "Capture a full-window screenshot and return it as base64-encoded PNG data inline.",
     R"json({"type":"object","properties":{},"additionalProperties":false})json",
     "get_screenshot_base64"},
    {"wait_frames",
     "Wait for N frames to be rendered before responding. Blocks the MCP call until the frames "
     "have actually elapsed.",
     R"json({"type":"object","properties":{"count":{"type":"integer","default":1,"description":"Number of frames to wait (1-600)."}},"additionalProperties":false})json",
     "wait_frames"},
    {"text_input",
     "Type text characters into the currently focused UI widget (e.g. text field, rename dialog).",
     R"json({"type":"object","properties":{"text":{"type":"string","description":"The text string to type."}},"required":["text"],"additionalProperties":false})json",
     "text_input"},
    {"double_click",
     "Double-click at the specified position.",
     R"json({"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"button":{"type":"integer","default":0},"modifiers":{"type":"integer","default":0}},"required":["x","y"],"additionalProperties":false})json",
     "double_click"},
    {"get_figure_info",
     "Get detailed information about a specific figure including axes, series, and limits.",
     R"json({"type":"object","properties":{"figure_id":{"type":"integer","description":"The figure ID to query."}},"required":["figure_id"],"additionalProperties":false})json",
     "get_figure_info"},
    {"get_window_size",
     "Get the current window dimensions in pixels.",
     R"json({"type":"object","properties":{},"additionalProperties":false})json",
     "get_window_size"},
    {"fuzz_step",
     "Execute one weighted-random QA fuzz action (same distribution as spectra_qa_agent). "
     "Returns action name, step index, and suggested pump_frames count.",
     R"json({"type":"object","properties":{"seed":{"type":"integer","description":"Reset RNG to this seed before stepping."},"action":{"type":"string","description":"Force a specific fuzz action name (see list_fuzz_actions)."}},"additionalProperties":false})json",
     "fuzz_step"},
    {"fuzz_reset",
     "Reset fuzz RNG state and step counter.",
     R"json({"type":"object","properties":{"seed":{"type":"integer","description":"Optional deterministic seed."}},"additionalProperties":false})json",
     "fuzz_reset"},
    {"list_fuzz_actions",
     "List available fuzz action names and their weights.",
     R"json({"type":"object","properties":{},"additionalProperties":false})json",
     "list_fuzz_actions"},
    {"list_methods",
     "List all automation methods with metadata (parameters and context requirements).",
     R"json({"type":"object","properties":{},"additionalProperties":false})json",
     "list_methods"},
    {"dismiss_ui_capture",
     "Cancel tab drag, dock drag, and open menus so automation is not stuck in drag mode.",
     R"json({"type":"object","properties":{},"additionalProperties":false})json",
     "dismiss_ui_capture"},
}};

std::string json_escape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s)
    {
        switch (c)
        {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += c;
                break;
        }
    }
    return out;
}

size_t skip_ws(const std::string& text, size_t pos)
{
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])) != 0)
        ++pos;
    return pos;
}

size_t parse_json_value_end(const std::string& text, size_t pos)
{
    pos = skip_ws(text, pos);
    if (pos >= text.size())
        return pos;

    if (text[pos] == '"')
    {
        ++pos;
        while (pos < text.size())
        {
            if (text[pos] == '\\')
            {
                pos += 2;
                continue;
            }
            if (text[pos] == '"')
                return pos + 1;
            ++pos;
        }
        return text.size();
    }

    if (text[pos] == '{' || text[pos] == '[')
    {
        const char open  = text[pos];
        const char close = open == '{' ? '}' : ']';
        int        depth = 0;
        for (size_t i = pos; i < text.size(); ++i)
        {
            if (text[i] == '"')
            {
                ++i;
                while (i < text.size())
                {
                    if (text[i] == '\\')
                    {
                        i += 2;
                        continue;
                    }
                    if (text[i] == '"')
                        break;
                    ++i;
                }
                continue;
            }
            if (text[i] == open)
                ++depth;
            else if (text[i] == close)
            {
                --depth;
                if (depth == 0)
                    return i + 1;
            }
        }
        return text.size();
    }

    size_t end = pos;
    while (end < text.size())
    {
        const char c = text[end];
        if (c == ',' || c == '}' || c == ']' || std::isspace(static_cast<unsigned char>(c)) != 0)
            break;
        ++end;
    }
    return end;
}

std::string json_extract_raw(const std::string& json, const std::string& key)
{
    const std::string quoted_key = "\"" + key + "\"";
    size_t            pos        = json.find(quoted_key);
    if (pos == std::string::npos)
        return "";
    pos = json.find(':', pos + quoted_key.size());
    if (pos == std::string::npos)
        return "";
    pos              = skip_ws(json, pos + 1);
    const size_t end = parse_json_value_end(json, pos);
    if (end <= pos)
        return "";
    return json.substr(pos, end - pos);
}

std::string json_unquote(const std::string& raw)
{
    if (raw.size() < 2 || raw.front() != '"' || raw.back() != '"')
        return raw;

    std::string out;
    out.reserve(raw.size() - 2);
    for (size_t i = 1; i + 1 < raw.size(); ++i)
    {
        char c = raw[i];
        if (c == '\\' && i + 1 < raw.size() - 1)
        {
            const char esc = raw[++i];
            switch (esc)
            {
                case 'n':
                    out += '\n';
                    break;
                case 'r':
                    out += '\r';
                    break;
                case 't':
                    out += '\t';
                    break;
                case '"':
                    out += '"';
                    break;
                case '\\':
                    out += '\\';
                    break;
                default:
                    out += esc;
                    break;
            }
        }
        else
        {
            out += c;
        }
    }
    return out;
}

std::string json_extract_string(const std::string& json, const std::string& key)
{
    return json_unquote(json_extract_raw(json, key));
}

std::string json_extract_object(const std::string& json, const std::string& key)
{
    std::string raw = json_extract_raw(json, key);
    if (!raw.empty() && raw.front() == '{')
        return raw;
    return "{}";
}

std::string jsonrpc_result(const std::string& id_raw, const std::string& result_json)
{
    return R"({"jsonrpc":"2.0","id":)" + (id_raw.empty() ? "null" : id_raw)
           + ",\"result\":" + result_json + "}";
}

std::string jsonrpc_error(const std::string& id_raw, int code, const std::string& message)
{
    return R"({"jsonrpc":"2.0","id":)" + (id_raw.empty() ? "null" : id_raw) + R"(,"error":{"code":)"
           + std::to_string(code) + R"(,"message":")" + json_escape(message) + "\"}}";
}

std::string http_status_text(int status)
{
    switch (status)
    {
        case 200:
            return "OK";
        case 202:
            return "Accepted";
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 413:
            return "Payload Too Large";
        case 415:
            return "Unsupported Media Type";
        case 500:
            return "Internal Server Error";
        default:
            return "OK";
    }
}

std::string make_http_response(int                status,
                               const std::string& body,
                               const std::string& content_type  = "application/json",
                               const std::string& extra_headers = "")
{
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << ' ' << http_status_text(status) << "\r\n";
    oss << "Content-Type: " << content_type << "\r\n";
    oss << extra_headers;
    oss << "Content-Length: " << body.size() << "\r\n";
    oss << "Connection: close\r\n";
    oss << "MCP-Protocol-Version: " << kProtocolVersion << "\r\n\r\n";
    oss << body;
    return oss.str();
}

std::string to_lower(std::string s)
{
    std::transform(s.begin(),
                   s.end(),
                   s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string& s)
{
    size_t b = 0;
    size_t e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t'))
        ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t'))
        --e;
    return s.substr(b, e - b);
}

// Line-based header lookup (exact, case-insensitive field-name match; skips the
// request line).  Returns nullopt when the header is absent.
std::optional<std::string> find_header(const std::string& headers, const std::string& key)
{
    const std::string lower_key = to_lower(key);
    size_t            pos       = headers.find("\r\n");
    while (pos != std::string::npos)
    {
        pos += 2;
        size_t end = headers.find("\r\n", pos);
        if (end == std::string::npos)
            end = headers.size();
        if (end == pos)
            break;   // blank line: end of headers
        const std::string line  = headers.substr(pos, end - pos);
        const size_t      colon = line.find(':');
        if (colon != std::string::npos && to_lower(trim(line.substr(0, colon))) == lower_key)
            return trim(line.substr(colon + 1));
        pos = end < headers.size() ? end : std::string::npos;
    }
    return std::nullopt;
}

std::string extract_header_value(const std::string& headers, const std::string& key)
{
    return find_header(headers, key).value_or("");
}

// Host must name the loopback interface (or the explicit bind host), optionally
// with our port.  Blocks DNS-rebinding attacks that reach 127.0.0.1 via a foreign name.
bool host_header_allowed(const std::string& host_value, const std::string& bind_host, uint16_t port)
{
    std::string host = to_lower(trim(host_value));
    if (host.empty())
        return false;

    std::string name;
    std::string port_str;
    if (host.front() == '[')
    {
        const size_t close = host.find(']');
        if (close == std::string::npos)
            return false;
        name = host.substr(0, close + 1);
        if (close + 1 < host.size())
        {
            if (host[close + 1] != ':')
                return false;
            port_str = host.substr(close + 2);
        }
    }
    else
    {
        const size_t colon = host.find(':');
        name               = host.substr(0, colon);
        if (colon != std::string::npos)
            port_str = host.substr(colon + 1);
    }

    const bool name_ok = name == "127.0.0.1" || name == "localhost" || name == "[::1]"
                         || (!bind_host.empty() && name == to_lower(bind_host));
    if (!name_ok)
        return false;

    if (host.find(':') != std::string::npos && host.front() != '[' && port_str.empty())
        return false;
    if (!port_str.empty())
    {
        if (port_str.size() > 5
            || !std::all_of(port_str.begin(),
                            port_str.end(),
                            [](unsigned char c) { return std::isdigit(c) != 0; }))
            return false;
        if (port != 0 && std::stoul(port_str) != port)
            return false;
    }
    return true;
}

std::optional<size_t> parse_content_length(const std::string& headers)
{
    const std::string raw = extract_header_value(headers, "Content-Length");
    if (raw.empty())
        return std::nullopt;
    try
    {
        return static_cast<size_t>(std::stoull(raw));
    }
    catch (...)
    {
        return std::nullopt;
    }
}

const ToolSpec* find_tool(const std::string& name)
{
    for (const auto& tool : kTools)
        if (name == tool.name)
            return &tool;
    return nullptr;
}

std::string remap_arguments_for_tool(const std::string& tool_name,
                                     const std::string& arguments_json)
{
    if (tool_name != "add_series")
        return arguments_json.empty() ? "{}" : arguments_json;

    const std::string figure_id   = json_extract_raw(arguments_json, "figure_id");
    const std::string series_type = json_extract_raw(arguments_json, "type");
    const std::string n_points    = json_extract_raw(arguments_json, "n_points");
    const std::string label       = json_extract_raw(arguments_json, "label");
    const std::string x_arr       = json_extract_raw(arguments_json, "x");
    const std::string y_arr       = json_extract_raw(arguments_json, "y");
    const std::string bins        = json_extract_raw(arguments_json, "bins");

    std::ostringstream oss;
    oss << '{';
    bool first  = true;
    auto append = [&](const char* key, const std::string& value)
    {
        if (value.empty())
            return;
        if (!first)
            oss << ',';
        first = false;
        oss << '"' << key << "\":" << value;
    };

    append("figure_id", figure_id);
    append("type", series_type.empty() ? "\"line\"" : series_type);
    append("n_points", n_points.empty() ? "100" : n_points);
    append("label", label.empty() ? "\"\"" : label);
    append("x", x_arr);
    append("y", y_arr);
    append("bins", bins);
    oss << '}';
    return oss.str();
}

std::string build_tools_list_result()
{
    std::ostringstream oss;
    oss << "{\"tools\":[";
    for (size_t i = 0; i < kTools.size(); ++i)
    {
        if (i > 0)
            oss << ',';
        oss << R"({"name":")" << json_escape(kTools[i].name) << R"(","description":")"
            << json_escape(kTools[i].description) << R"(","inputSchema":)" << kTools[i].input_schema
            << '}';
    }
    oss << "]}";
    return oss.str();
}

std::string build_initialize_result(const std::string& protocol_version)
{
    const std::string negotiated = protocol_version.empty() ? kProtocolVersion : protocol_version;
    return R"({"protocolVersion":")" + json_escape(negotiated)
           + R"(","capabilities":{"tools":{"listChanged":false}},"serverInfo":{"name":")"
           + std::string(kServerName) + R"(","version":")" + SPECTRA_VERSION_STRING
           + R"("},"instructions":")" + json_escape(kInstructions) + "\"}";
}

std::string build_call_result(const std::string& payload_json)
{
    return R"({"content":[{"type":"text","text":")" + json_escape(payload_json)
           + R"("}],"structuredContent":)" + payload_json + "}";
}

std::string build_call_error_result(const std::string& error_message)
{
    return R"({"content":[{"type":"text","text":")" + json_escape(error_message)
           + R"("}],"isError":true})";
}
}   // namespace

McpServer::McpServer() = default;

McpServer::~McpServer()
{
    stop();
}

std::string McpServer::endpoint() const
{
    return "http://" + bind_host_ + ':' + std::to_string(port_) + "/mcp";
}

bool McpServer::start(AutomationServer& automation, const std::string& bind_host, uint16_t port)
{
#ifdef _WIN32
    (void)automation;
    (void)bind_host;
    (void)port;
    return false;
#else
    if (running_.load(std::memory_order_relaxed))
        return false;

    automation_ = &automation;
    bind_host_  = bind_host.empty() ? "127.0.0.1" : bind_host;
    port_       = port;

    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0)
    {
        SPECTRA_LOG_ERROR("mcp", "socket(): " + std::string(std::strerror(errno)));
        return false;
    }

    const int cloexec_flags = ::fcntl(listen_fd_, F_GETFD);
    if (cloexec_flags >= 0)
        ::fcntl(listen_fd_, F_SETFD, cloexec_flags | FD_CLOEXEC);

    int reuse_addr = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port_);
    if (::inet_pton(AF_INET, bind_host_.c_str(), &addr.sin_addr) != 1)
    {
        SPECTRA_LOG_ERROR("mcp", "Invalid bind address: " + bind_host_);
        ::close(listen_fd_);
        listen_fd_  = -1;
        automation_ = nullptr;
        return false;
    }

    if (::bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        const int bind_errno = errno;
        if (bind_errno == EADDRINUSE)
            SPECTRA_LOG_WARN("mcp",
                             "Port " + std::to_string(port_)
                                 + " already in use (another Spectra instance?); "
                                   "MCP server disabled for this process");
        else
            SPECTRA_LOG_ERROR("mcp", "bind(): " + std::string(std::strerror(bind_errno)));
        ::close(listen_fd_);
        listen_fd_  = -1;
        automation_ = nullptr;
        return false;
    }

    if (::listen(listen_fd_, 8) < 0)
    {
        SPECTRA_LOG_ERROR("mcp", "listen(): " + std::string(std::strerror(errno)));
        ::close(listen_fd_);
        listen_fd_  = -1;
        automation_ = nullptr;
        return false;
    }

    struct sockaddr_in bound_addr{};
    socklen_t bound_len = sizeof(bound_addr);
    if (::getsockname(listen_fd_, reinterpret_cast<struct sockaddr*>(&bound_addr), &bound_len) == 0)
        port_ = ntohs(bound_addr.sin_port);

    // Auth token: explicit set_auth_token() > SPECTRA_MCP_TOKEN > random per-process.
    if (auth_token_.empty())
    {
        const char* env_token = std::getenv("SPECTRA_MCP_TOKEN");
        auth_token_           = (env_token && env_token[0] != '\0') ? std::string(env_token)
                                                                    : automation_security::generate_token();
    }

    // Publish endpoint + token in a 0600 file so local tooling can discover them.
    token_file_ =
        automation_security::runtime_dir() + "/spectra-mcp-" + std::to_string(::getpid()) + ".json";
    const std::string token_json = R"({"endpoint":")" + json_escape(endpoint()) + R"(","token":")"
                                   + json_escape(auth_token_) + R"(","pid":)"
                                   + std::to_string(::getpid()) + "}\n";
    if (automation_security::write_private_file(token_file_, token_json))
    {
        SPECTRA_LOG_INFO("mcp", "Token file: " + token_file_);
    }
    else
    {
        SPECTRA_LOG_WARN("mcp", "Could not write MCP token file " + token_file_);
        token_file_.clear();
    }

    running_.store(true, std::memory_order_release);
    listener_thread_ = std::thread(&McpServer::listener_thread_fn, this);
    SPECTRA_LOG_INFO("mcp",
                     "Listening on " + endpoint()
                         + " (requires 'Authorization: Bearer <token>'; see token file)");
    return true;
#endif
}

void McpServer::stop()
{
#ifndef _WIN32
    if (!running_.load(std::memory_order_relaxed))
        return;

    running_.store(false, std::memory_order_release);

    // The listener thread reads listen_fd_, so join it before closing the fd.
    // shutdown() wakes its poll() without modifying listen_fd_.
    if (listen_fd_ >= 0)
        ::shutdown(listen_fd_, SHUT_RDWR);
    if (listener_thread_.joinable())
        listener_thread_.join();
    if (listen_fd_ >= 0)
    {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    {
        std::lock_guard lock(clients_mutex_);
        for (int fd : client_fds_)
        {
            ::shutdown(fd, SHUT_RDWR);
            ::close(fd);
        }
        client_fds_.clear();
    }

    if (!token_file_.empty())
    {
        ::unlink(token_file_.c_str());
        token_file_.clear();
    }

    automation_ = nullptr;
    bind_host_.clear();
    port_ = 0;
#endif
}

void McpServer::listener_thread_fn()
{
#ifndef _WIN32
    while (running_.load(std::memory_order_relaxed))
    {
        struct pollfd pfd{};
        pfd.fd        = listen_fd_;
        pfd.events    = POLLIN;
        const int ret = ::poll(&pfd, 1, 200);
        if (ret <= 0 || (pfd.revents & POLLIN) == 0)
            continue;

        struct sockaddr_in client_addr{};
        socklen_t          client_len = sizeof(client_addr);
        int                cfd =
            ::accept(listen_fd_, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
        if (cfd < 0)
            continue;

        {
            std::lock_guard lock(clients_mutex_);
            client_fds_.push_back(cfd);
        }
        std::thread(&McpServer::handle_client, this, cfd).detach();
    }
#endif
}

void McpServer::handle_client(int client_fd)
{
#ifndef _WIN32
    std::string request;
    std::string response;
    char        chunk[4096];
    size_t      content_length = 0;
    size_t      header_end     = std::string::npos;

    while (running_.load(std::memory_order_relaxed))
    {
        const ssize_t n = ::read(client_fd, chunk, sizeof(chunk));
        if (n <= 0)
            break;
        request.append(chunk, static_cast<size_t>(n));

        if (header_end == std::string::npos)
        {
            header_end = request.find("\r\n\r\n");
            if (header_end == std::string::npos)
            {
                if (request.size() > kMaxHeaderBytes)
                {
                    response = make_http_response(
                        400,
                        jsonrpc_error("null", -32600, "Request headers too large"));
                    break;
                }
                continue;
            }

            const std::string headers               = request.substr(0, header_end + 4);
            const auto        parsed_content_length = parse_content_length(headers);
            if (!parsed_content_length.has_value())
            {
                response =
                    make_http_response(400,
                                       jsonrpc_error("null", -32700, "Missing Content-Length"));
                break;
            }
            content_length = *parsed_content_length;
            if (content_length > kMaxBodyBytes)
            {
                response =
                    make_http_response(413, jsonrpc_error("null", -32600, "Request too large"));
                break;
            }
        }

        if (request.size() >= header_end + 4 + content_length)
        {
            const std::string headers = request.substr(0, header_end + 4);
            const std::string body    = request.substr(header_end + 4, content_length);
            response                  = handle_http_request(headers, body);
            break;
        }
    }

    if (response.empty())
        response =
            make_http_response(400, jsonrpc_error("null", -32700, "Failed to parse request"));

    size_t total = 0;
    while (total < response.size())
    {
        const ssize_t n = ::write(client_fd, response.data() + total, response.size() - total);
        if (n <= 0)
            break;
        total += static_cast<size_t>(n);
    }

    ::close(client_fd);
    {
        std::lock_guard lock(clients_mutex_);
        client_fds_.erase(std::remove(client_fds_.begin(), client_fds_.end(), client_fd),
                          client_fds_.end());
    }
#else
    (void)client_fd;
#endif
}

std::string McpServer::handle_http_request(const std::string& headers, const std::string& body)
{
    const size_t line_end = headers.find("\r\n");
    if (line_end == std::string::npos)
        return make_http_response(400, jsonrpc_error("null", -32700, "Invalid HTTP request"));

    const std::string  request_line = headers.substr(0, line_end);
    std::istringstream iss(request_line);
    std::string        method;
    std::string        path;
    std::string        version;
    iss >> method >> path >> version;

    // ── Security gate (applies to every request, including GET) ──────────────
    // 1. Host must be loopback: defeats DNS rebinding from a web page.
    const auto host = find_header(headers, "Host");
    if (!host || !host_header_allowed(*host, bind_host_, port_))
        return make_http_response(403, jsonrpc_error("null", -32600, "Forbidden: bad Host"));

    // 2. Browsers attach Origin to cross-site (and most same-site) requests; legitimate
    //    MCP clients (CLI tools, agents) do not.  Reject any Origin outright.
    if (find_header(headers, "Origin"))
        return make_http_response(403,
                                  jsonrpc_error("null", -32600, "Forbidden: Origin not allowed"));

    // 3. Bearer token (or X-Spectra-Token).  Fail closed when no token is configured.
    {
        std::string presented;
        if (const auto auth = find_header(headers, "Authorization"))
        {
            const std::string v = *auth;
            if (v.size() > 7 && to_lower(v.substr(0, 7)) == "bearer ")
                presented = trim(v.substr(7));
        }
        if (presented.empty())
            presented = extract_header_value(headers, "X-Spectra-Token");

        const bool ok = !auth_token_.empty() && !presented.empty()
                        && automation_security::constant_time_equals(presented, auth_token_);
        if (!ok)
            return make_http_response(
                401,
                jsonrpc_error("null", -32001, "Unauthorized: missing or invalid token"),
                "application/json",
                "WWW-Authenticate: Bearer realm=\"spectra\"\r\n");
    }

    if (method == "GET")
    {
        const std::string health = R"({"name":"spectra-automation","status":"ok","endpoint":")"
                                   + json_escape(endpoint()) + "\"}";
        return make_http_response(200, health);
    }

    if (method != "POST")
        return make_http_response(405, jsonrpc_error("null", -32600, "Unsupported HTTP method"));

    if (path != "/mcp" && path != "/")
        return make_http_response(404, jsonrpc_error("null", -32601, "Unknown endpoint"));

    // 4. JSON only: text/plain and form posts are "simple" CORS requests that a browser
    //    sends without a preflight.
    {
        std::string ct = to_lower(extract_header_value(headers, "Content-Type"));
        if (const size_t semi = ct.find(';'); semi != std::string::npos)
            ct = ct.substr(0, semi);
        if (trim(ct) != "application/json")
            return make_http_response(
                415,
                jsonrpc_error("null", -32600, "Content-Type must be application/json"));
    }

    const std::string rpc_method = json_extract_string(body, "method");
    const std::string id_raw     = json_extract_raw(body, "id");

    if (rpc_method.empty())
        return make_http_response(400, jsonrpc_error(id_raw, -32600, "Missing method"));

    if (rpc_method == "initialize")
    {
        const std::string params_json      = json_extract_object(body, "params");
        const std::string protocol_version = json_extract_string(params_json, "protocolVersion");
        return make_http_response(
            200,
            jsonrpc_result(id_raw, build_initialize_result(protocol_version)));
    }

    if (rpc_method == "notifications/initialized")
        return make_http_response(202, "", "application/json");

    if (rpc_method == "tools/list")
        return make_http_response(200, jsonrpc_result(id_raw, build_tools_list_result()));

    if (rpc_method == "tools/call")
    {
        const std::string params_json    = json_extract_object(body, "params");
        const std::string tool_name      = json_extract_string(params_json, "name");
        const ToolSpec*   tool           = find_tool(tool_name);
        const std::string arguments_json = json_extract_raw(params_json, "arguments");

        if (!tool)
            return make_http_response(
                200,
                jsonrpc_result(id_raw, build_call_error_result("Unknown tool: " + tool_name)));

        if (!automation_ || !automation_->is_running())
            return make_http_response(
                200,
                jsonrpc_result(
                    id_raw,
                    build_call_error_result("Spectra automation server is not running")));

        const std::string normalized_arguments =
            remap_arguments_for_tool(tool_name, arguments_json);
        const std::string automation_response =
            automation_->invoke(tool->automation_method, normalized_arguments);
        const std::string ok_raw = json_extract_raw(automation_response, "ok");

        if (ok_raw == "true")
        {
            const std::string result_json = json_extract_raw(automation_response, "result");
            return make_http_response(
                200,
                jsonrpc_result(id_raw,
                               build_call_result(result_json.empty() ? "{}" : result_json)));
        }

        const std::string error_text = json_extract_string(automation_response, "error");
        return make_http_response(
            200,
            jsonrpc_result(id_raw,
                           build_call_error_result(error_text.empty() ? "Unknown Spectra error"
                                                                      : error_text)));
    }

    return make_http_response(400, jsonrpc_error(id_raw, -32601, "Method not found"));
}

}   // namespace spectra
