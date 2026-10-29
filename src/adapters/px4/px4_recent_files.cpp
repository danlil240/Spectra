#include "px4_recent_files.hpp"

#include "core/json_key.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

#include "core/config_paths.hpp"

namespace spectra::adapters::px4
{

namespace
{

std::string config_dir()
{
    // Same directory as the main app's settings.json.
    const std::filesystem::path dir = spectra::user_config_dir();
    if (dir.empty())
        return (std::filesystem::path(".") / ".config" / "spectra").string();
    return dir.string();
}

void append_escaped(std::string& out, const std::string& s)
{
    out += '"';
    for (const char ch : s)
    {
        const auto c = static_cast<unsigned char>(ch);
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
                if (c < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                }
                else
                {
                    out += ch;
                }
                break;
        }
    }
    out += '"';
}

// Parses the JSON string starting at json[pos] == '"'.  On success stores the
// unescaped value, advances pos past the closing quote and returns true.
bool parse_string(const std::string& json, size_t& pos, std::string& out)
{
    if (pos >= json.size() || json[pos] != '"')
        return false;
    out.clear();
    for (size_t i = pos + 1; i < json.size(); ++i)
    {
        const char c = json[i];
        if (c == '"')
        {
            pos = i + 1;
            return true;
        }
        if (c != '\\')
        {
            out += c;
            continue;
        }
        if (++i >= json.size())
            return false;
        switch (json[i])
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
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'u':
            {
                if (i + 4 >= json.size())
                    return false;
                const unsigned long cp = std::strtoul(json.substr(i + 1, 4).c_str(), nullptr, 16);
                i += 4;
                // Paths are written byte-for-byte; only control characters are
                // escaped as \u00XX.  Encode anything else as UTF-8.
                if (cp < 0x80)
                {
                    out += static_cast<char>(cp);
                }
                else if (cp < 0x800)
                {
                    out += static_cast<char>(0xC0 | (cp >> 6));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
                else
                {
                    out += static_cast<char>(0xE0 | (cp >> 12));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
                break;
            }
            default:   // \" \\ \/ and anything unknown: take the character as-is
                out += json[i];
                break;
        }
    }
    return false;
}

}   // namespace

Px4RecentFiles::Px4RecentFiles(std::string path, size_t max_entries)
    : path_(std::move(path)), max_entries_(std::max<size_t>(1, max_entries))
{
}

std::string Px4RecentFiles::default_path()
{
    return (std::filesystem::path(config_dir()) / "px4_recent.json").string();
}

std::string Px4RecentFiles::normalize(const std::string& file)
{
    if (file.empty())
        return {};
    std::error_code ec;
    auto            abs = std::filesystem::absolute(std::filesystem::path(file), ec);
    if (ec)
        return file;
    return abs.lexically_normal().string();
}

std::string Px4RecentFiles::serialize(const std::vector<std::string>& entries)
{
    std::string out = "{\n  \"version\": 1,\n  \"files\": [";
    for (size_t i = 0; i < entries.size(); ++i)
    {
        out += i == 0 ? "\n    " : ",\n    ";
        append_escaped(out, entries[i]);
    }
    out += entries.empty() ? "]\n}\n" : "\n  ]\n}\n";
    return out;
}

std::vector<std::string> Px4RecentFiles::deserialize(const std::string& json)
{
    std::vector<std::string> out;
    size_t                   pos = json_find_key(json, "files");
    if (pos == std::string::npos)
        return out;
    pos = json.find('[', pos);
    if (pos == std::string::npos)
        return out;
    ++pos;

    std::string value;
    while (pos < json.size())
    {
        const char c = json[pos];
        if (c == ']')
            break;
        if (c == '"')
        {
            if (!parse_string(json, pos, value))
                break;
            if (!value.empty() && std::find(out.begin(), out.end(), value) == out.end())
                out.push_back(value);
            continue;
        }
        ++pos;   // whitespace, commas
    }
    return out;
}

bool Px4RecentFiles::load()
{
    entries_.clear();
    if (path_.empty())
        return false;
    std::ifstream f(path_, std::ios::binary);
    if (!f.is_open())
        return false;
    const std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    entries_ = deserialize(json);
    if (entries_.size() > max_entries_)
        entries_.resize(max_entries_);
    return true;
}

bool Px4RecentFiles::save() const
{
    if (path_.empty())
        return true;

    const std::filesystem::path p(path_);
    std::error_code             ec;
    if (p.has_parent_path())
        std::filesystem::create_directories(p.parent_path(), ec);

    // Write to a temporary file and rename so a crash never leaves a torn list.
    const std::filesystem::path tmp = p.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f.is_open())
            return false;
        f << serialize(entries_);
        if (!f.good())
            return false;
    }
    std::filesystem::rename(tmp, p, ec);
    if (ec)
    {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

void Px4RecentFiles::push(const std::string& file)
{
    const std::string norm = normalize(file);
    if (norm.empty())
        return;
    entries_.erase(std::remove(entries_.begin(), entries_.end(), norm), entries_.end());
    entries_.insert(entries_.begin(), norm);
    if (entries_.size() > max_entries_)
        entries_.resize(max_entries_);
    save();
}

void Px4RecentFiles::remove(const std::string& file)
{
    const std::string norm = normalize(file);
    const auto        it   = std::remove(entries_.begin(), entries_.end(), norm);
    if (it == entries_.end())
        return;
    entries_.erase(it, entries_.end());
    save();
}

void Px4RecentFiles::clear()
{
    entries_.clear();
    save();
}

}   // namespace spectra::adapters::px4
