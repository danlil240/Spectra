#include "text_fit.hpp"

#include <vector>

namespace spectra
{

FittedText fit_text_to_width(const std::string& text,
                             float              max_width,
                             FontSize           preferred,
                             FontSize           min_size,
                             const TextWidthFn& measure,
                             std::string_view   ellipsis)
{
    FittedText result;
    result.text = text;
    result.size = preferred;
    if (text.empty() || !measure)
        return result;

    // Larger enum value == larger font (Tick < Label < Title).
    auto lo = static_cast<int>(min_size);
    auto hi = static_cast<int>(preferred);
    if (lo > hi)
        lo = hi;

    for (int s = hi; s >= lo; --s)
    {
        const auto fs = static_cast<FontSize>(s);
        if (measure(text, fs) <= max_width)
        {
            result.size = fs;
            return result;
        }
    }

    // Ellipsize at the minimum size.
    const auto fs    = static_cast<FontSize>(lo);
    result.size      = fs;
    result.truncated = true;

    // Byte offsets of codepoint boundaries: ends[k] = byte length of the first k codepoints.
    std::vector<size_t> ends;
    ends.push_back(0);
    for (size_t i = 0; i < text.size();)
    {
        uint32_t cp = 0;
        i += TextRenderer::decode_utf8(text, i, cp);
        ends.push_back(i);
    }

    auto candidate = [&](size_t k)
    {
        size_t len = ends[k];
        while (len > 0 && text[len - 1] == ' ')
            --len;
        std::string s = text.substr(0, len);
        s.append(ellipsis);
        return s;
    };

    // Width grows with the prefix length: binary search for the longest fitting prefix.
    // ends.size() - 1 codepoints would be the whole text, which is known not to fit.
    size_t lo_k = 0;
    size_t hi_k = ends.size() - 2;
    if (measure(candidate(0), fs) > max_width)
    {
        result.text.clear();
        return result;
    }
    while (lo_k < hi_k)
    {
        const size_t mid = (lo_k + hi_k + 1) / 2;
        if (measure(candidate(mid), fs) <= max_width)
            lo_k = mid;
        else
            hi_k = mid - 1;
    }
    result.text = candidate(lo_k);
    return result;
}

}   // namespace spectra
