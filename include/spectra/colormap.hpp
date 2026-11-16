#pragma once

// Colormap: a built-in ColormapType (optionally reversed) or a user lookup
// table.  Every colormapped series (scatter, surface, image / heatmap,
// contour), the colorbar and SVG/PDF export map a value through the same
// 256-entry table, so one t gives one colour everywhere.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <spectra/color.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace spectra
{

class Colormap
{
   public:
    /// Entries of every colormap lookup table.
    static constexpr size_t LUT_SIZE = 256;

    /// ColormapType::None.
    Colormap() = default;
    /// A built-in map; implicit so a ColormapType works wherever a Colormap is taken.
    Colormap(ColormapType type, bool reversed = false) : type_(type), reversed_(reversed) {}

    /// User colormap from colours spread evenly over [0, 1] and linearly
    /// interpolated (like matplotlib's LinearSegmentedColormap.from_list).
    /// One colour gives a flat map; no colours gives ColormapType::None.
    static Colormap from_colors(std::span<const Color> colors);

    /// Parse a matplotlib-style name, case-insensitive: "viridis", "plasma",
    /// "inferno", "magma", "cividis", "turbo", "twilight", "coolwarm", "RdBu",
    /// "PuOr", "jet", "hot", "gray" / "grey" / "grayscale", "none"; a "_r"
    /// suffix reverses the map ("viridis_r").  nullopt for an unknown name.
    static std::optional<Colormap> from_name(std::string_view name);

    ColormapType type() const { return type_; }
    bool         is_none() const { return type_ == ColormapType::None; }
    bool         is_custom() const { return type_ == ColormapType::Custom; }
    bool         is_reversed() const { return reversed_; }

    /// Copy with the direction set (true = high values take the low end).
    Colormap reversed(bool on) const
    {
        Colormap c  = *this;
        c.reversed_ = on;
        return c;
    }

    /// Colour at t (clamped to [0, 1]; NaN reads entry 0): lookup table entry
    /// min(int(t * LUT_SIZE), LUT_SIZE - 1), the same nearest-entry rule as
    /// matplotlib and the GPU shaders.  None (and Custom without colours)
    /// sample as gray.
    Color sample(float t) const;

    /// True when some entry is not fully opaque (only user tables can be).
    /// Colormapped surfaces then draw with the transparent pipeline.
    bool has_transparency() const;

    /// The lookup table as LUT_SIZE RGBA8 entries, reversal applied.  Alpha is
    /// 255 for built-in maps and the colours' alpha for a user table; shaders
    /// multiply it into the series opacity.
    std::array<uint8_t, LUT_SIZE * 4> rgba8() const;

    /// Name accepted by from_name(): "viridis", "RdBu_r", "custom".
    std::string name() const;

    /// Same type, direction and (for Custom) colours.
    bool operator==(const Colormap& other) const;

   private:
    ColormapType type_     = ColormapType::None;
    bool         reversed_ = false;
    // Custom only: LUT_SIZE colours, shared between copies.
    std::shared_ptr<const std::vector<Color>> custom_;
};

/// Display / parse name of a built-in colormap ("viridis", "RdBu", "gray").
std::string_view colormap_name(ColormapType type);

/// The built-in colormaps in picker order (no None / Custom).
std::span<const ColormapType> builtin_colormaps();

}   // namespace spectra
