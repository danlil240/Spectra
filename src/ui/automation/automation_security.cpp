// automation_security.cpp — see automation_security.hpp.

#include "automation_security.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>
#include <vector>

#ifndef _WIN32
    #include <fcntl.h>
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <unistd.h>
    #if defined(__linux__) || defined(__APPLE__)
        #include <sys/random.h>
    #endif
#endif

namespace spectra::automation_security
{

namespace
{
bool is_directory(const std::string& path)
{
    std::error_code ec;
    return !path.empty() && std::filesystem::is_directory(path, ec);
}

std::string strip_trailing_slashes(std::string s)
{
    while (s.size() > 1 && s.back() == '/')
        s.pop_back();
    return s;
}

std::vector<std::string> split_path(const std::string& path)
{
    std::vector<std::string> parts;
    std::string              cur;
    for (char c : path)
    {
        if (c == '/')
        {
            parts.push_back(cur);
            cur.clear();
        }
        else
        {
            cur += c;
        }
    }
    parts.push_back(cur);
    return parts;
}
}   // namespace

bool env_flag_enabled(const char* name)
{
    const char* v = std::getenv(name);
    if (v == nullptr)
        return false;
    std::string s(v);
    std::transform(s.begin(),
                   s.end(),
                   s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s == "1" || s == "true" || s == "yes" || s == "on";
}

bool automation_requested(bool config_flag)
{
    if (config_flag)
        return true;
    if (env_flag_enabled("SPECTRA_AUTOMATION"))
        return true;
    const char* sock = std::getenv("SPECTRA_AUTO_SOCKET");
    return sock != nullptr && sock[0] != '\0';
}

std::string runtime_dir()
{
    if (const char* xdg = std::getenv("XDG_RUNTIME_DIR"); xdg && *xdg && is_directory(xdg))
        return strip_trailing_slashes(xdg);
    if (const char* tmp = std::getenv("TMPDIR"); tmp && *tmp && is_directory(tmp))
        return strip_trailing_slashes(tmp);
    std::error_code ec;
    auto            sys_tmp = std::filesystem::temp_directory_path(ec);
    if (!ec && !sys_tmp.empty())
        return strip_trailing_slashes(sys_tmp.string());
    return "/tmp";
}

std::string generate_token()
{
    std::array<unsigned char, 32> bytes{};
    bool                          filled = false;
#if !defined(_WIN32) && (defined(__linux__) || defined(__APPLE__))
    filled = ::getentropy(bytes.data(), bytes.size()) == 0;
#endif
    if (!filled)
    {
        std::random_device rd;
        for (size_t i = 0; i < bytes.size(); i += 4)
        {
            const uint32_t v = rd();
            for (size_t j = 0; j < 4 && i + j < bytes.size(); ++j)
                bytes[i + j] = static_cast<unsigned char>((v >> (8 * j)) & 0xFF);
        }
    }

    static constexpr const char* kHex = "0123456789abcdef";
    std::string                  out;
    out.reserve(bytes.size() * 2);
    for (unsigned char b : bytes)
    {
        out += kHex[b >> 4];
        out += kHex[b & 0x0F];
    }
    return out;
}

bool constant_time_equals(std::string_view a, std::string_view b)
{
    // Compare over the longer length so timing does not reveal the prefix match length.
    const size_t  n    = std::max(a.size(), b.size());
    unsigned char diff = a.size() == b.size() ? 0 : 1;
    for (size_t i = 0; i < n; ++i)
    {
        const unsigned char ca = i < a.size() ? static_cast<unsigned char>(a[i]) : 0;
        const unsigned char cb = i < b.size() ? static_cast<unsigned char>(b[i]) : 0;
        diff |= static_cast<unsigned char>(ca ^ cb);
    }
    return diff == 0;
}

bool write_private_file(const std::string& path, const std::string& contents)
{
#ifndef _WIN32
    ::unlink(path.c_str());
    const int fd = ::open(path.c_str(),
                          O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                          S_IRUSR | S_IWUSR);
    if (fd < 0)
        return false;
    size_t total = 0;
    bool   ok    = true;
    while (total < contents.size())
    {
        const ssize_t n = ::write(fd, contents.data() + total, contents.size() - total);
        if (n <= 0)
        {
            ok = false;
            break;
        }
        total += static_cast<size_t>(n);
    }
    ::close(fd);
    if (!ok)
        ::unlink(path.c_str());
    return ok;
#else
    (void)path;
    (void)contents;
    return false;
#endif
}

std::string capture_dir()
{
    if (const char* env = std::getenv("SPECTRA_CAPTURE_DIR"); env && *env)
        return strip_trailing_slashes(env);

    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg && *xdg && is_directory(xdg))
        return strip_trailing_slashes(xdg) + "/spectra-captures";

#ifndef _WIN32
    // Shared temp dir: make the name per-user so users cannot squat on each other.
    return runtime_dir() + "/spectra-captures-" + std::to_string(::geteuid());
#else
    return runtime_dir() + "/spectra-captures";
#endif
}

bool ensure_private_dir(const std::string& dir, std::string& error)
{
#ifndef _WIN32
    if (dir.empty() || dir.front() != '/')
    {
        error = "Capture directory must be an absolute path";
        return false;
    }
    if (::mkdir(dir.c_str(), S_IRWXU) != 0 && errno != EEXIST)
    {
        error = "Cannot create capture directory " + dir + ": " + std::strerror(errno);
        return false;
    }
    struct stat st{};
    if (::lstat(dir.c_str(), &st) != 0)
    {
        error = "Cannot stat capture directory " + dir + ": " + std::strerror(errno);
        return false;
    }
    if (!S_ISDIR(st.st_mode))
    {
        error = "Capture directory is not a directory (or is a symlink): " + dir;
        return false;
    }
    if (st.st_uid != ::geteuid())
    {
        error = "Capture directory is not owned by the current user: " + dir;
        return false;
    }
    return true;
#else
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec)
    {
        error = "Cannot create capture directory " + dir + ": " + ec.message();
        return false;
    }
    return true;
#endif
}

bool resolve_confined_path(const std::string& requested,
                           const std::string& default_name,
                           const std::string& base_dir,
                           std::string&       out,
                           std::string&       error)
{
    const std::string base = strip_trailing_slashes(base_dir);
    if (base.empty() || base.front() != '/')
    {
        error = "Output directory is not an absolute path";
        return false;
    }

    std::string p = requested.empty() ? default_name : requested;
    if (p.find('\0') != std::string::npos || p.find('\\') != std::string::npos)
    {
        error = "Invalid characters in path";
        return false;
    }

    // Reject traversal on the raw input so "/base/../etc" cannot normalize back in.
    for (const auto& part : split_path(p))
    {
        if (part == "..")
        {
            error = "Path traversal ('..') is not allowed";
            return false;
        }
    }

    std::string rel;
    if (p.front() == '/')
    {
        // Absolute: must be inside base_dir.  Compare on normalized components.
        std::vector<std::string> base_parts;
        for (const auto& part : split_path(base))
            if (!part.empty() && part != ".")
                base_parts.push_back(part);
        std::vector<std::string> p_parts;
        for (const auto& part : split_path(p))
            if (!part.empty() && part != ".")
                p_parts.push_back(part);

        if (p_parts.size() <= base_parts.size()
            || !std::equal(base_parts.begin(), base_parts.end(), p_parts.begin()))
        {
            error = "Output path must be inside " + base;
            return false;
        }
        for (size_t i = base_parts.size(); i < p_parts.size(); ++i)
        {
            if (!rel.empty())
                rel += '/';
            rel += p_parts[i];
        }
    }
    else
    {
        for (const auto& part : split_path(p))
        {
            if (part.empty() || part == ".")
                continue;
            if (!rel.empty())
                rel += '/';
            rel += part;
        }
    }

    if (rel.empty() || p.back() == '/')
    {
        error = "Output path must name a file";
        return false;
    }

    out = base + "/" + rel;
    return true;
}

bool resolve_capture_path(const std::string& requested,
                          const std::string& default_name,
                          std::string&       out,
                          std::string&       error)
{
    const std::string base = capture_dir();
    if (!resolve_confined_path(requested, default_name, base, out, error))
        return false;
    if (!ensure_private_dir(base, error))
        return false;

    // Create intermediate sub-directories (inside the private dir) one by one.
    const std::string rel   = out.substr(base.size() + 1);
    const auto        parts = split_path(rel);
    std::string       cur   = base;
    for (size_t i = 0; i + 1 < parts.size(); ++i)
    {
        cur += "/" + parts[i];
        if (!ensure_private_dir(cur, error))
            return false;
    }
    return true;
}

}   // namespace spectra::automation_security
