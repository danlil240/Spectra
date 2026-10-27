// automation_json.hpp — Shared minimal JSON helpers for automation handlers.
// Extracted from automation_server.cpp so handler groups can parse params
// and build responses without depending on the server class.

#pragma once

#include "core/json_key.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace spectra
{

// ── Escape / parse helpers ───────────────────────────────────────────────────

// Escape a UTF-8 string for embedding inside a JSON string literal. Multi-byte
// UTF-8 is passed through unchanged (valid JSON); every control character below
// U+0020 is escaped so the output is always a valid JSON string body.
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
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
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
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    static constexpr char kHex[] = "0123456789abcdef";
                    out += "\\u00";
                    out += kHex[(static_cast<unsigned char>(c) >> 4) & 0xF];
                    out += kHex[static_cast<unsigned char>(c) & 0xF];
                }
                else
                {
                    out += c;
                }
                break;
        }
    }
    return out;
}

namespace detail
{

inline void json_append_utf8(std::string& out, uint32_t cp)
{
    if (cp < 0x80)
    {
        out += static_cast<char>(cp);
    }
    else if (cp < 0x800)
    {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000)
    {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else
    {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// Parse exactly four hex digits at s[pos..pos+3]. Returns false if any is not hex.
inline bool json_parse_hex4(const std::string& s, size_t pos, uint32_t& value)
{
    if (pos + 4 > s.size())
        return false;
    value = 0;
    for (size_t i = pos; i < pos + 4; ++i)
    {
        const char c = s[i];
        value <<= 4;
        if (c >= '0' && c <= '9')
            value |= static_cast<uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f')
            value |= static_cast<uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            value |= static_cast<uint32_t>(c - 'A' + 10);
        else
            return false;
    }
    return true;
}

}   // namespace detail

// Decode the body of a JSON string literal (the text between the quotes).
//
// Handles \" \\ \/ \b \f \n \r \t and \uXXXX, including UTF-16 surrogate pairs,
// producing UTF-8. Raw (unescaped) UTF-8 is copied through unchanged, so
// "\u2014" and a literal em dash decode to the same bytes.
//
// Malformed input is decoded leniently and never fails the lookup:
//   - an unknown escape (e.g. "\x") or a \u not followed by four hex digits is
//     kept literally, backslash included;
//   - a lone or mis-ordered surrogate (\uD800..\uDFFF without its partner)
//     becomes U+FFFD;
//   - \u0000 becomes U+FFFD so decoded values never carry an embedded NUL into
//     labels, paths or C APIs;
//   - a trailing lone backslash is kept literally.
inline std::string json_unescape(const std::string& body)
{
    constexpr uint32_t kReplacement = 0xFFFD;
    std::string        out;
    out.reserve(body.size());
    size_t i = 0;
    while (i < body.size())
    {
        const char c = body[i];
        if (c != '\\' || i + 1 >= body.size())
        {
            out += c;
            ++i;
            continue;
        }
        const char e = body[i + 1];
        switch (e)
        {
            case '"':
                out += '"';
                break;
            case '\\':
                out += '\\';
                break;
            case '/':
                out += '/';
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u':
            {
                uint32_t cp = 0;
                if (!detail::json_parse_hex4(body, i + 2, cp))
                {
                    // Malformed \u escape: keep the backslash literally and
                    // continue scanning after it.
                    out += '\\';
                    ++i;
                    continue;
                }
                i += 6;
                if (cp >= 0xD800 && cp <= 0xDBFF)
                {
                    uint32_t low = 0;
                    if (i + 1 < body.size() && body[i] == '\\' && body[i + 1] == 'u'
                        && detail::json_parse_hex4(body, i + 2, low) && low >= 0xDC00
                        && low <= 0xDFFF)
                    {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        i += 6;
                    }
                    else
                    {
                        cp = kReplacement;   // high surrogate without a low one
                    }
                }
                else if (cp >= 0xDC00 && cp <= 0xDFFF)
                {
                    cp = kReplacement;   // low surrogate without a preceding high one
                }
                else if (cp == 0)
                {
                    cp = kReplacement;
                }
                detail::json_append_utf8(out, cp);
                continue;
            }
            default:
                // Unknown escape: keep it literally.
                out += '\\';
                out += e;
                break;
        }
        i += 2;
    }
    return out;
}

// Return the value of `key`. String values are returned decoded (see
// json_unescape); other scalar values are returned as their raw token text.
inline std::string json_get_string(const std::string& json, const std::string& key)
{
    std::string search = "\"" + key + "\"";
    auto        pos    = json_find_key(json, key);
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
        return json_unescape(json.substr(start, std::min(end, json.size()) - start));
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
    auto        pos    = json_find_key(json, key);
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
    auto        pos    = json_find_key(json, key);
    if (pos == std::string::npos)
        return "{}";
    pos = json.find('{', pos + search.size());
    if (pos == std::string::npos)
        return "{}";
    int    depth     = 0;
    bool   in_string = false;
    size_t start     = pos;
    for (size_t i = pos; i < json.size(); ++i)
    {
        // Braces inside string values (e.g. a label "a}b" or "\"}") must not
        // end the object early.
        if (in_string)
        {
            if (json[i] == '\\')
                ++i;
            else if (json[i] == '"')
                in_string = false;
            continue;
        }
        if (json[i] == '"')
            in_string = true;
        else if (json[i] == '{')
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
