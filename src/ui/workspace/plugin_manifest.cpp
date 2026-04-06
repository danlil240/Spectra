#include "plugin_manifest.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "spectra/logger.hpp"

namespace spectra
{

// ─── PluginManifest ──────────────────────────────────────────────────────────

bool PluginManifest::is_valid() const
{
    return !name.empty() && !version.empty() && !api_version.empty();
}

std::pair<uint32_t, uint32_t> PluginManifest::parsed_api_version() const
{
    auto dot = api_version.find('.');
    if (dot == std::string::npos)
        return {0, 0};
    try
    {
        uint32_t major = static_cast<uint32_t>(std::stoul(api_version.substr(0, dot)));
        uint32_t minor = static_cast<uint32_t>(std::stoul(api_version.substr(dot + 1)));
        return {major, minor};
    }
    catch (...)
    {
        return {0, 0};
    }
}

bool PluginManifest::is_api_compatible(uint32_t host_major, uint32_t host_minor) const
{
    auto [plugin_major, plugin_minor] = parsed_api_version();
    if (plugin_major != host_major)
        return false;
    return plugin_minor <= host_minor;
}

// ─── Minimal JSON parser helpers ─────────────────────────────────────────────
// Parse simple flat JSON objects (no nesting beyond string arrays).

namespace
{

void skip_ws(const std::string& s, size_t& pos)
{
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r'))
        ++pos;
}

// Parse a quoted JSON string. pos should be AT the opening '"'.
// Returns the string value. pos is advanced past the closing '"'.
std::string parse_json_string(const std::string& s, size_t& pos)
{
    if (pos >= s.size() || s[pos] != '"')
        return {};
    ++pos;
    std::string result;
    while (pos < s.size() && s[pos] != '"')
    {
        if (s[pos] == '\\' && pos + 1 < s.size())
        {
            ++pos;
            switch (s[pos])
            {
                case '"':
                    result += '"';
                    break;
                case '\\':
                    result += '\\';
                    break;
                case '/':
                    result += '/';
                    break;
                case 'n':
                    result += '\n';
                    break;
                case 't':
                    result += '\t';
                    break;
                case 'r':
                    result += '\r';
                    break;
                default:
                    result += s[pos];
                    break;
            }
        }
        else
        {
            result += s[pos];
        }
        ++pos;
    }
    if (pos < s.size())
        ++pos;
    return result;
}

// Parse a JSON array of strings ["a", "b", ...]. pos should be AT '['.
std::vector<std::string> parse_json_string_array(const std::string& s, size_t& pos)
{
    std::vector<std::string> result;
    if (pos >= s.size() || s[pos] != '[')
        return result;
    ++pos;
    skip_ws(s, pos);
    while (pos < s.size() && s[pos] != ']')
    {
        skip_ws(s, pos);
        if (pos < s.size() && s[pos] == '"')
            result.push_back(parse_json_string(s, pos));
        skip_ws(s, pos);
        if (pos < s.size() && s[pos] == ',')
            ++pos;
        skip_ws(s, pos);
    }
    if (pos < s.size())
        ++pos;
    return result;
}

}   // namespace

// ─── load_plugin_manifest ────────────────────────────────────────────────────

PluginManifest load_plugin_manifest(const std::string& json_path)
{
    PluginManifest manifest;

    std::ifstream file(json_path);
    if (!file.is_open())
    {
        SPECTRA_LOG_WARN("plugin", "Could not open plugin manifest: {}", json_path);
        return manifest;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    const std::string json = ss.str();

    size_t pos = 0;
    skip_ws(json, pos);
    if (pos >= json.size() || json[pos] != '{')
    {
        SPECTRA_LOG_WARN("plugin", "Invalid plugin manifest (expected object): {}", json_path);
        return manifest;
    }
    ++pos;

    while (pos < json.size())
    {
        skip_ws(json, pos);
        if (pos >= json.size() || json[pos] == '}')
            break;

        if (json[pos] != '"')
        {
            ++pos;
            continue;
        }

        std::string key = parse_json_string(json, pos);
        skip_ws(json, pos);
        if (pos >= json.size() || json[pos] != ':')
            continue;
        ++pos;
        skip_ws(json, pos);

        if (key == "capabilities" || key == "dependencies")
        {
            auto arr = parse_json_string_array(json, pos);
            if (key == "capabilities")
                manifest.capabilities = std::move(arr);
            else
                manifest.dependencies = std::move(arr);
        }
        else if (json[pos] == '"')
        {
            std::string val = parse_json_string(json, pos);
            if (key == "name")
                manifest.name = val;
            else if (key == "version")
                manifest.version = val;
            else if (key == "api_version")
                manifest.api_version = val;
            else if (key == "author")
                manifest.author = val;
            else if (key == "description")
                manifest.description = val;
            else if (key == "min_spectra_version")
                manifest.min_spectra_version = val;
        }
        else
        {
            // Skip unknown value (number, bool, null, nested object)
            while (pos < json.size() && json[pos] != ',' && json[pos] != '}')
                ++pos;
        }

        skip_ws(json, pos);
        if (pos < json.size() && json[pos] == ',')
            ++pos;
    }

    if (!manifest.is_valid())
    {
        SPECTRA_LOG_WARN("plugin",
                         "Plugin manifest missing required fields (name/version/api_version): {}",
                         json_path);
    }

    return manifest;
}

std::string find_plugin_manifest_path(const std::string& library_path)
{
    std::filesystem::path lib_path(library_path);
    std::filesystem::path manifest_path = lib_path.parent_path() / "plugin.json";
    if (std::filesystem::exists(manifest_path))
        return manifest_path.string();
    return {};
}

}   // namespace spectra
