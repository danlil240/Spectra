#pragma once

// Shortening a figure tab title to a pixel width.  Pure logic, so it is unit
// tested without ImGui (the caller passes the text measure).

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <string_view>

namespace spectra::ui
{

// Byte length of the longest UTF-8 prefix of `s` that is at most `n` bytes.
inline size_t utf8_floor(std::string_view s, size_t n)
{
    n = std::min(n, s.size());
    while (n > 0 && n < s.size() && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80)
        --n;
    return n;
}

// `title` if it fits in `max_w`, otherwise a middle-elided form "head…tail".
// A trailing number ("Figure 12") is kept whole when possible, since that is
// what tells compressed default-named tabs apart; the rest of the room goes
// to the head, and when even "…12" is too wide the bare number is shown.
// Returns "…" alone (or "") when nothing more fits.
inline std::string fit_tab_title(std::string_view                              title,
                                 float                                         max_w,
                                 const std::function<float(std::string_view)>& measure)
{
    if (measure(title) <= max_w)
        return std::string(title);
    static constexpr std::string_view ELL = "\xE2\x80\xA6";   // …
    if (measure(ELL) > max_w)
        return {};

    // Trailing run of digits (the number of a default "Figure N" title).
    size_t num_begin = title.size();
    while (num_begin > 0 && std::isdigit(static_cast<unsigned char>(title[num_begin - 1])))
        --num_begin;
    const std::string_view number = title.substr(num_begin);

    auto compose = [&](size_t head_len, std::string_view tail)
    {
        std::string out(title.substr(0, head_len));
        while (!out.empty() && out.back() == ' ')
            out.pop_back();
        out += ELL;
        out += tail;
        return out;
    };

    if (!number.empty() && measure(compose(0, number)) <= max_w)
    {
        // Keep the whole number, grow the head as far as it fits.
        size_t best = 0;
        for (size_t h = 1; h <= num_begin; ++h)
        {
            const size_t hl = utf8_floor(title, h);
            if (hl != h)
                continue;
            if (measure(compose(hl, number)) > max_w)
                break;
            best = hl;
        }
        return compose(best, number);
    }

    // The number alone still tells tabs apart better than "Fi…".
    if (!number.empty() && measure(number) <= max_w)
        return std::string(number);

    // No number (or it does not fit): plain end ellipsis on the head.
    size_t best = 0;
    for (size_t h = 1; h <= title.size(); ++h)
    {
        const size_t hl = utf8_floor(title, h);
        if (hl != h)
            continue;
        if (measure(compose(hl, {})) > max_w)
            break;
        best = hl;
    }
    return compose(best, {});
}

}   // namespace spectra::ui
