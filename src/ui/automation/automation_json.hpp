// automation_json.hpp — Shared minimal JSON helpers for automation handlers.
// Extracted from automation_server.cpp so handler groups can parse params
// and build responses without depending on the server class.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace spectra
{

// ── Escape / parse helpers ───────────────────────────────────────────────────

inline std::string json_escape(const std::string& s)
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

inline std::string json_get_string(const std::string& json, const std::string& key)
{
    std::string search = "\"" + key + "\"";
    auto        pos    = json.find(search);
    if (pos == std::string::npos)
        return "";
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos)
        return "";
    pos = json.find_first_not_of(" \t\n\r", pos + 1);
    if (pos == std::string::npos)
        return "";

    if (json[pos] == '"')
    {
        size_t start = pos + 1;
        size_t end   = start;
        while (end < json.size())
        {
            if (json[end] == '\\')
            {
                end += 2;
                continue;
            }
            if (json[end] == '"')
                break;
            ++end;
        }
        return json.substr(start, end - start);
    }
    size_t start = pos;
    size_t end   = json.find_first_of(",}] \t\n\r", start);
    if (end == std::string::npos)
        end = json.size();
    return json.substr(start, end - start);
}

inline double json_get_number(const std::string& json, const std::string& key, double fb = 0.0)
{
    std::string val = json_get_string(json, key);
    if (val.empty())
        return fb;
    try
    {
        return std::stod(val);
    }
    catch (...)
    {
        return fb;
    }
}

inline int json_get_int(const std::string& json, const std::string& key, int fb = 0)
{
    return static_cast<int>(json_get_number(json, key, fb));
}

inline std::vector<float> json_get_float_array(const std::string& json, const std::string& key)
{
    std::string search = "\"" + key + "\"";
    auto        pos    = json.find(search);
    if (pos == std::string::npos)
        return {};
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos)
        return {};
    pos = json.find_first_not_of(" \t\n\r", pos + 1);
    if (pos == std::string::npos || json[pos] != '[')
        return {};
    ++pos;
    std::vector<float> result;
    while (pos < json.size())
    {
        pos = json.find_first_not_of(" \t\n\r,", pos);
        if (pos == std::string::npos || json[pos] == ']')
            break;
        size_t end = json.find_first_of(",]}", pos);
        if (end == std::string::npos)
            break;
        try
        {
            result.push_back(std::stof(json.substr(pos, end - pos)));
        }
        catch (...)
        {
        }
        pos = end;
    }
    return result;
}

inline uint64_t json_get_uint64(const std::string& json, const std::string& key, uint64_t fb = 0)
{
    std::string val = json_get_string(json, key);
    if (val.empty())
        return fb;
    try
    {
        return std::stoull(val);
    }
    catch (...)
    {
        return fb;
    }
}

inline std::string json_get_object(const std::string& json, const std::string& key)
{
    std::string search = "\"" + key + "\"";
    auto        pos    = json.find(search);
    if (pos == std::string::npos)
        return "{}";
    pos = json.find('{', pos + search.size());
    if (pos == std::string::npos)
        return "{}";
    int    depth = 0;
    size_t start = pos;
    for (size_t i = pos; i < json.size(); ++i)
    {
        if (json[i] == '{')
            ++depth;
        else if (json[i] == '}')
        {
            --depth;
            if (depth == 0)
                return json.substr(start, i - start + 1);
        }
    }
    return "{}";
}

// ── Response builders ────────────────────────────────────────────────────────

inline std::string json_ok(uint64_t id, const std::string& result_json = "{}")
{
    return "{\"id\":" + std::to_string(id) + ",\"ok\":true,\"result\":" + result_json + "}";
}

inline std::string json_error(uint64_t id, const std::string& message)
{
    return "{\"id\":" + std::to_string(id) + ",\"ok\":false,\"error\":\"" + json_escape(message)
           + "\"}";
}

}   // namespace spectra