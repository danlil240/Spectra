// Tests for the automation / MCP server security policy:
//   - automation is opt-in (off for a default AppConfig)
//   - MCP HTTP requests need a bearer token, loopback Host, no Origin, JSON body
//   - capture output paths are confined to the capture directory

#include <gtest/gtest.h>

#include <spectra/app.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#include "ui/automation/automation_security.hpp"
#include "ui/automation/automation_server.hpp"
#include "ui/automation/mcp_server.hpp"

#ifndef _WIN32
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

using namespace spectra;
namespace sec = spectra::automation_security;

namespace
{

// Saves an environment variable on construction and restores it on destruction.
class ScopedEnv
{
   public:
    ScopedEnv(const char* name, const char* value) : name_(name)
    {
        if (const char* old = std::getenv(name))
            old_ = std::string(old);
        if (value)
            ::setenv(name, value, 1);
        else
            ::unsetenv(name);
    }
    ~ScopedEnv()
    {
        if (old_)
            ::setenv(name_.c_str(), old_->c_str(), 1);
        else
            ::unsetenv(name_.c_str());
    }
    ScopedEnv(const ScopedEnv&)            = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

   private:
    std::string                name_;
    std::optional<std::string> old_;
};

std::filesystem::path make_temp_dir(const std::string& tag)
{
    std::string templ =
        (std::filesystem::temp_directory_path() / ("spectra_sec_" + tag + "_XXXXXX")).string();
    char* p = ::mkdtemp(templ.data());
    EXPECT_NE(p, nullptr);
    return std::filesystem::path(templ);
}

int status_of(const std::string& response)
{
    // "HTTP/1.1 200 OK\r\n..."
    if (response.size() < 12)
        return -1;
    return std::stoi(response.substr(9, 3));
}

const std::string kToken = "test-token-0123456789abcdef";
const std::string kBody  = R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})";

std::string make_headers(const std::string& extra,
                         const std::string& method = "POST",
                         const std::string& host   = "127.0.0.1:8765")
{
    std::string h = method + " /mcp HTTP/1.1\r\n";
    if (!host.empty())
        h += "Host: " + host + "\r\n";
    h += extra;
    h += "Content-Length: " + std::to_string(kBody.size()) + "\r\n\r\n";
    return h;
}

const std::string kJson = "Content-Type: application/json\r\n";
const std::string kAuth = "Authorization: Bearer " + kToken + "\r\n";

}   // namespace

// ─── Opt-in ──────────────────────────────────────────────────────────────────

TEST(AutomationOptIn, DefaultConfigDoesNotStartServers)
{
    ScopedEnv a("SPECTRA_AUTOMATION", nullptr);
    ScopedEnv b("SPECTRA_AUTO_SOCKET", nullptr);
    AppConfig cfg;
    EXPECT_FALSE(cfg.enable_automation);
    EXPECT_FALSE(sec::automation_requested(cfg.enable_automation));
}

TEST(AutomationOptIn, ConfigFlagEnables)
{
    ScopedEnv a("SPECTRA_AUTOMATION", nullptr);
    ScopedEnv b("SPECTRA_AUTO_SOCKET", nullptr);
    AppConfig cfg;
    cfg.enable_automation = true;
    EXPECT_TRUE(sec::automation_requested(cfg.enable_automation));
}

TEST(AutomationOptIn, EnvFlagEnables)
{
    ScopedEnv b("SPECTRA_AUTO_SOCKET", nullptr);
    {
        ScopedEnv a("SPECTRA_AUTOMATION", "1");
        EXPECT_TRUE(sec::automation_requested(false));
    }
    {
        ScopedEnv a("SPECTRA_AUTOMATION", "TRUE");
        EXPECT_TRUE(sec::automation_requested(false));
    }
    {
        ScopedEnv a("SPECTRA_AUTOMATION", "0");
        EXPECT_FALSE(sec::automation_requested(false));
    }
    {
        ScopedEnv a("SPECTRA_AUTOMATION", "");
        EXPECT_FALSE(sec::automation_requested(false));
    }
}

TEST(AutomationOptIn, ExplicitSocketPathEnables)
{
    ScopedEnv a("SPECTRA_AUTOMATION", nullptr);
    ScopedEnv b("SPECTRA_AUTO_SOCKET", "/tmp/whatever.sock");
    EXPECT_TRUE(sec::automation_requested(false));
}

// ─── Tokens ──────────────────────────────────────────────────────────────────

TEST(AutomationToken, GeneratedTokensAreLongAndUnique)
{
    const std::string a = sec::generate_token();
    const std::string b = sec::generate_token();
    EXPECT_EQ(a.size(), 64u);
    EXPECT_NE(a, b);
    for (char c : a)
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
}

TEST(AutomationToken, ConstantTimeEquals)
{
    EXPECT_TRUE(sec::constant_time_equals("abc", "abc"));
    EXPECT_FALSE(sec::constant_time_equals("abc", "abd"));
    EXPECT_FALSE(sec::constant_time_equals("abc", "abcd"));
    EXPECT_FALSE(sec::constant_time_equals("", "a"));
    EXPECT_TRUE(sec::constant_time_equals("", ""));
}

// ─── MCP request gate ────────────────────────────────────────────────────────

class McpGate : public ::testing::Test
{
   protected:
    void SetUp() override { server.set_auth_token(kToken); }

    std::string post(const std::string& extra_headers, const std::string& host = "127.0.0.1:8765")
    {
        return server.handle_http_request(make_headers(extra_headers, "POST", host), kBody);
    }

    McpServer server;
};

TEST_F(McpGate, RequestWithoutTokenRejected)
{
    const std::string r = post(kJson);
    EXPECT_EQ(status_of(r), 401);
    EXPECT_NE(r.find("WWW-Authenticate: Bearer"), std::string::npos);
}

TEST_F(McpGate, RequestWithWrongTokenRejected)
{
    EXPECT_EQ(status_of(post(kJson + "Authorization: Bearer nope\r\n")), 401);
    EXPECT_EQ(status_of(post(kJson + "Authorization: Basic " + kToken + "\r\n")), 401);
}

TEST_F(McpGate, RequestWithTokenAccepted)
{
    const std::string r = post(kJson + kAuth);
    EXPECT_EQ(status_of(r), 200);
    EXPECT_NE(r.find("\"tools\""), std::string::npos);
}

TEST_F(McpGate, XSpectraTokenHeaderAccepted)
{
    EXPECT_EQ(status_of(post(kJson + "X-Spectra-Token: " + kToken + "\r\n")), 200);
}

TEST_F(McpGate, BearerSchemeIsCaseInsensitive)
{
    EXPECT_EQ(status_of(post(kJson + "authorization: bearer " + kToken + "\r\n")), 200);
}

TEST_F(McpGate, EmptyConfiguredTokenFailsClosed)
{
    McpServer         unconfigured;
    const std::string r =
        unconfigured.handle_http_request(make_headers(kJson + "Authorization: Bearer \r\n"), kBody);
    EXPECT_EQ(status_of(r), 401);
}

TEST_F(McpGate, OriginBearingRequestRejected)
{
    EXPECT_EQ(status_of(post(kJson + kAuth + "Origin: https://evil.example\r\n")), 403);
    EXPECT_EQ(status_of(post(kJson + kAuth + "Origin: null\r\n")), 403);
    EXPECT_EQ(status_of(post(kJson + kAuth + "Origin: http://127.0.0.1:8765\r\n")), 403);
}

TEST_F(McpGate, ForeignHostRejected)
{
    EXPECT_EQ(status_of(post(kJson + kAuth, "evil.example:8765")), 403);
    EXPECT_EQ(status_of(post(kJson + kAuth, "127.0.0.1.evil.example")), 403);
    EXPECT_EQ(status_of(post(kJson + kAuth, "")), 403);   // missing Host
}

TEST_F(McpGate, LoopbackHostsAccepted)
{
    EXPECT_EQ(status_of(post(kJson + kAuth, "localhost:8765")), 200);
    EXPECT_EQ(status_of(post(kJson + kAuth, "127.0.0.1")), 200);
    EXPECT_EQ(status_of(post(kJson + kAuth, "[::1]:8765")), 200);
}

TEST_F(McpGate, NonJsonContentTypeRejected)
{
    // text/plain is a CORS "simple request" — the classic cross-origin attack vector.
    EXPECT_EQ(status_of(post("Content-Type: text/plain\r\n" + kAuth)), 415);
    EXPECT_EQ(status_of(post("Content-Type: application/x-www-form-urlencoded\r\n" + kAuth)), 415);
    EXPECT_EQ(status_of(post(kAuth)), 415);
    EXPECT_EQ(status_of(post("Content-Type: application/json; charset=utf-8\r\n" + kAuth)), 200);
}

TEST_F(McpGate, HeaderNameSubstringDoesNotSpoofAuth)
{
    // "X-Authorization" must not be mistaken for "Authorization".
    EXPECT_EQ(status_of(post(kJson + "X-Authorization: Bearer " + kToken + "\r\n")), 401);
}

TEST_F(McpGate, GetHealthRequiresToken)
{
    const std::string no_tok =
        server.handle_http_request("GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", "");
    EXPECT_EQ(status_of(no_tok), 401);
    const std::string tok =
        server.handle_http_request("GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n" + kAuth + "\r\n", "");
    EXPECT_EQ(status_of(tok), 200);
}

// ─── Path confinement ────────────────────────────────────────────────────────

TEST(CapturePath, RelativeNameResolvedInsideBase)
{
    std::string out, err;
    ASSERT_TRUE(sec::resolve_confined_path("shot.png", "d.png", "/base/caps", out, err)) << err;
    EXPECT_EQ(out, "/base/caps/shot.png");
    ASSERT_TRUE(sec::resolve_confined_path("./sub//x.png", "d.png", "/base/caps/", out, err));
    EXPECT_EQ(out, "/base/caps/sub/x.png");
}

TEST(CapturePath, EmptyUsesDefault)
{
    std::string out, err;
    ASSERT_TRUE(sec::resolve_confined_path("", "d.png", "/base/caps", out, err));
    EXPECT_EQ(out, "/base/caps/d.png");
}

TEST(CapturePath, TraversalRejected)
{
    std::string out, err;
    EXPECT_FALSE(sec::resolve_confined_path("../x.png", "d.png", "/base/caps", out, err));
    EXPECT_FALSE(sec::resolve_confined_path("a/../../x.png", "d.png", "/base/caps", out, err));
    EXPECT_FALSE(sec::resolve_confined_path("a/..", "d.png", "/base/caps", out, err));
    EXPECT_FALSE(
        sec::resolve_confined_path("/base/caps/../../etc/x", "d.png", "/base/caps", out, err));
    EXPECT_NE(err.find(".."), std::string::npos);
}

TEST(CapturePath, AbsoluteOutsideBaseRejected)
{
    std::string out, err;
    EXPECT_FALSE(sec::resolve_confined_path("/etc/passwd", "d.png", "/base/caps", out, err));
    EXPECT_FALSE(sec::resolve_confined_path("/home/u/.bashrc", "d.png", "/base/caps", out, err));
    // Prefix trick: /base/capsule is not inside /base/caps.
    EXPECT_FALSE(
        sec::resolve_confined_path("/base/capsule/x.png", "d.png", "/base/caps", out, err));
    // The base itself is not a file.
    EXPECT_FALSE(sec::resolve_confined_path("/base/caps", "d.png", "/base/caps", out, err));
}

TEST(CapturePath, AbsoluteInsideBaseAccepted)
{
    std::string out, err;
    ASSERT_TRUE(sec::resolve_confined_path("/base/caps/a/b.png", "d.png", "/base/caps", out, err));
    EXPECT_EQ(out, "/base/caps/a/b.png");
}

TEST(CapturePath, DirectoryOrBadCharsRejected)
{
    std::string out, err;
    EXPECT_FALSE(sec::resolve_confined_path("dir/", "d.png", "/base/caps", out, err));
    EXPECT_FALSE(
        sec::resolve_confined_path(std::string("a\0b", 3), "d.png", "/base/caps", out, err));
}

#ifndef _WIN32
TEST(CapturePath, ResolveCaptureCreatesPrivateDir)
{
    const auto tmp  = make_temp_dir("cap");
    const auto base = tmp / "captures";
    ScopedEnv  env("SPECTRA_CAPTURE_DIR", base.c_str());

    std::string out, err;
    ASSERT_TRUE(sec::resolve_capture_path("sub/x.png", "d.png", out, err)) << err;
    EXPECT_EQ(out, (base / "sub" / "x.png").string());

    struct stat st
    {
    };
    ASSERT_EQ(::lstat(base.c_str(), &st), 0);
    EXPECT_TRUE(S_ISDIR(st.st_mode));
    EXPECT_EQ(st.st_mode & 0777, 0700u);
    EXPECT_TRUE(std::filesystem::is_directory(base / "sub"));

    EXPECT_FALSE(sec::resolve_capture_path("/tmp/evil.png", "d.png", out, err));
    EXPECT_FALSE(sec::resolve_capture_path("../evil.png", "d.png", out, err));

    std::filesystem::remove_all(tmp);
}

TEST(CapturePath, SymlinkedCaptureDirRejected)
{
    const auto tmp  = make_temp_dir("caplink");
    const auto real = tmp / "real";
    std::filesystem::create_directory(real);
    const auto link = tmp / "link";
    std::filesystem::create_directory_symlink(real, link);

    std::string err;
    EXPECT_FALSE(sec::ensure_private_dir(link.string(), err));
    std::filesystem::remove_all(tmp);
}

TEST(AutomationFiles, PrivateFileIsOwnerOnly)
{
    const auto tmp  = make_temp_dir("priv");
    const auto file = tmp / "tok.json";
    ASSERT_TRUE(sec::write_private_file(file.string(), "secret"));
    ASSERT_TRUE(sec::write_private_file(file.string(), "secret2"));   // overwrite works
    struct stat st
    {
    };
    ASSERT_EQ(::stat(file.c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600u);
    std::ifstream in(file);
    std::string   content;
    in >> content;
    EXPECT_EQ(content, "secret2");
    std::filesystem::remove_all(tmp);
}

TEST(AutomationFiles, DefaultSocketUsesXdgRuntimeDir)
{
    const auto        tmp = make_temp_dir("xdg");
    ScopedEnv         env("XDG_RUNTIME_DIR", tmp.c_str());
    const std::string path = AutomationServer::default_socket_path();
    EXPECT_EQ(path.rfind(tmp.string() + "/spectra-auto-", 0), 0u) << path;
    std::filesystem::remove_all(tmp);
}

TEST(AutomationFiles, SocketCreatedOwnerOnly)
{
    const auto        tmp  = make_temp_dir("sock");
    const std::string path = (tmp / "a.sock").string();
    AutomationServer  server;
    ASSERT_TRUE(server.start(path));
    struct stat st
    {
    };
    ASSERT_EQ(::lstat(path.c_str(), &st), 0);
    EXPECT_TRUE(S_ISSOCK(st.st_mode));
    EXPECT_EQ(st.st_mode & 077, 0u);
    server.stop();
    std::filesystem::remove_all(tmp);
}

// End-to-end over a real TCP socket: a browser-style text/plain cross-origin POST with
// no token must be refused, and the token file must be written 0600.
TEST(McpServerLive, CrossOriginSimpleRequestRefusedAndTokenFilePrivate)
{
    const auto tmp = make_temp_dir("mcp");
    ScopedEnv  xdg("XDG_RUNTIME_DIR", tmp.c_str());
    ScopedEnv  tok("SPECTRA_MCP_TOKEN", "env-token-xyz");

    AutomationServer automation;
    McpServer        mcp;
    ASSERT_TRUE(mcp.start(automation, "127.0.0.1", 0));
    EXPECT_EQ(mcp.auth_token(), "env-token-xyz");
    ASSERT_FALSE(mcp.token_file().empty());

    struct stat st
    {
    };
    ASSERT_EQ(::stat(mcp.token_file().c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600u);
    {
        std::ifstream     in(mcp.token_file());
        std::stringstream ss;
        ss << in.rdbuf();
        EXPECT_NE(ss.str().find("env-token-xyz"), std::string::npos);
        EXPECT_NE(ss.str().find(mcp.endpoint()), std::string::npos);
    }

    auto send = [&](const std::string& req) -> std::string
    {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        EXPECT_GE(fd, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(mcp.port());
        ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        EXPECT_EQ(::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
        EXPECT_EQ(::write(fd, req.data(), req.size()), static_cast<ssize_t>(req.size()));
        std::string out;
        char        buf[4096];
        ssize_t     n;
        while ((n = ::read(fd, buf, sizeof(buf))) > 0)
            out.append(buf, static_cast<size_t>(n));
        ::close(fd);
        return out;
    };

    const std::string body =
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"capture_window","arguments":{"path":"/tmp/x.png"}}})";
    const std::string host = "Host: 127.0.0.1:" + std::to_string(mcp.port()) + "\r\n";
    const std::string len  = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";

    const std::string attack = "POST /mcp HTTP/1.1\r\n" + host
                               + "Origin: https://evil.example\r\nContent-Type: text/plain\r\n"
                               + len + body;
    EXPECT_EQ(status_of(send(attack)), 403);

    const std::string no_token =
        "POST /mcp HTTP/1.1\r\n" + host + "Content-Type: application/json\r\n" + len + body;
    EXPECT_EQ(status_of(send(no_token)), 401);

    const std::string list_body = kBody;
    const std::string ok =
        "POST /mcp HTTP/1.1\r\n" + host
        + "Content-Type: application/json\r\nAuthorization: Bearer env-token-xyz\r\n"
        + "Content-Length: " + std::to_string(list_body.size()) + "\r\n\r\n" + list_body;
    EXPECT_EQ(status_of(send(ok)), 200);

    const std::string token_file = mcp.token_file();
    mcp.stop();
    EXPECT_FALSE(std::filesystem::exists(token_file));
    std::filesystem::remove_all(tmp);
}
#endif
