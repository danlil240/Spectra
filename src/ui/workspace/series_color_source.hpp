#pragma once

#include <cstdint>
#include <spectra/color.hpp>
#include <spectra/series.hpp>

namespace spectra
{

// Where a series colour comes from.  Palette and format-letter colours are
// resolved per theme at draw time, so copies and saved files must keep the
// source, not the RGB that happened to be resolved last.
struct SeriesColorSource
{
    enum class Kind : uint8_t
    {
        Explicit = 0,   // fixed RGB (color())
        Palette  = 1,   // auto colour: data-palette slot `value`
        Letter   = 2,   // format-string letter `value` (r g b c m y k w)
    };

    Kind    kind  = Kind::Explicit;
    int32_t value = 0;
};

inline SeriesColorSource color_source_of(const Series& s)
{
    if (s.has_auto_color())
        return {SeriesColorSource::Kind::Palette, s.auto_color_index()};
    if (format_colors::is_letter(s.color_letter()))
        return {SeriesColorSource::Kind::Letter, static_cast<int32_t>(s.color_letter())};
    return {};
}

// Gives @p s colour @p resolved with source @p src.  For palette / letter
// sources @p resolved is only the provisional colour until the next draw
// re-resolves it for the active theme.  An unknown or invalid source keeps
// @p resolved as an explicit colour.
inline void apply_color_source(Series& s, const SeriesColorSource& src, const Color& resolved)
{
    switch (src.kind)
    {
        case SeriesColorSource::Kind::Palette:
            if (src.value >= 0)
            {
                s.set_auto_color_index(src.value);
                s.resolve_auto_color(resolved);
                return;
            }
            break;
        case SeriesColorSource::Kind::Letter:
            if (src.value > 0 && src.value < 128
                && format_colors::is_letter(static_cast<char>(src.value)))
            {
                s.set_color_letter(static_cast<char>(src.value));
                s.resolve_letter_color(resolved);
                return;
            }
            break;
        case SeriesColorSource::Kind::Explicit:
            break;
    }
    s.color(resolved);
}

// Copies @p from's colour and its source onto @p to.
inline void copy_color_source(Series& to, const Series& from)
{
    apply_color_source(to, color_source_of(from), from.color());
}

}   // namespace spectra
