#pragma once

// json_key.hpp — exact key lookup for the hand-rolled JSON readers.
//
// The minimal JSON readers across Spectra used `json.find("\"" + key + "\"")`
// to locate a key.  That matches the quoted text anywhere: inside a string
// *value* ("label": "x" satisfies a lookup of key "x"), inside a longer
// escaped string, or as a key of a nested object that appears before the
// key the caller meant.  json_find_key() only matches real key tokens: a
// complete string literal outside any other string, immediately followed by
// ':' (whitespace allowed), whose raw text equals `key` exactly.  When the
// key occurs at several nesting depths, the shallowest occurrence wins (first
// one on ties), so top-level keys are preferred over same-named nested keys
// while callers that intentionally look up a uniquely named nested key still
// find it.

#include <cstddef>
#include <string_view>

namespace spectra
{

// Return the offset of the opening quote of key token `key`, or npos.
// The result is a drop-in replacement for `json.find("\"" + key + "\"")`:
// the key token spans [pos, pos + key.size() + 2).
inline size_t json_find_key(std::string_view json, std::string_view key)
{
    constexpr size_t npos       = std::string_view::npos;
    size_t           best       = npos;
    int              best_depth = 0;
    int              depth      = 0;
    const size_t     n          = json.size();

    for (size_t i = 0; i < n; ++i)
    {
        const char c = json[i];
        if (c == '{' || c == '[')
        {
            ++depth;
            continue;
        }
        if (c == '}' || c == ']')
        {
            --depth;
            continue;
        }
        if (c != '"')
            continue;

        // Scan the string literal starting at i.
        const size_t start = i;
        size_t       j     = i + 1;
        while (j < n && json[j] != '"')
            j += (json[j] == '\\') ? 2 : 1;
        if (j >= n)
            break;   // unterminated string

        // Is it a key (followed by ':')?
        size_t k = j + 1;
        while (k < n && (json[k] == ' ' || json[k] == '\t' || json[k] == '\n' || json[k] == '\r'))
            ++k;
        const bool is_key = k < n && json[k] == ':';
        if (is_key && json.substr(start + 1, j - start - 1) == key
            && (best == npos || depth < best_depth))
        {
            best       = start;
            best_depth = depth;
        }
        i = j;
    }
    return best;
}

inline bool json_has_key_token(std::string_view json, std::string_view key)
{
    return json_find_key(json, key) != std::string_view::npos;
}

}   // namespace spectra
