#pragma once

// Pure text matching for the Settings search box (no ImGui dependency,
// unit-tested).  A query matches a section when every whitespace-separated
// term occurs, case-insensitively, in the section's title or keywords.

#include <cctype>
#include <string>
#include <string_view>

namespace spectra::ui::settings
{

inline std::string settings_lower(std::string_view s)
{
    std::string out(s);
    for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// True when `query` is empty or blank, or every term of it occurs in `title`
// or `keywords` (case-insensitive substring match).
inline bool settings_query_matches(std::string_view query,
                                   std::string_view title,
                                   std::string_view keywords)
{
    const std::string haystack = settings_lower(title) + ' ' + settings_lower(keywords);
    size_t            i        = 0;
    while (i < query.size())
    {
        while (i < query.size() && std::isspace(static_cast<unsigned char>(query[i])))
            ++i;
        size_t j = i;
        while (j < query.size() && !std::isspace(static_cast<unsigned char>(query[j])))
            ++j;
        if (j > i && haystack.find(settings_lower(query.substr(i, j - i))) == std::string::npos)
            return false;
        i = j;
    }
    return true;
}

// True when the query has at least one non-space character.
inline bool settings_query_active(std::string_view query)
{
    for (char c : query)
        if (!std::isspace(static_cast<unsigned char>(c)))
            return true;
    return false;
}

}   // namespace spectra::ui::settings
