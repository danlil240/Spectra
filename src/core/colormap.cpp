#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <spectra/colormap.hpp>

#include "colormap_tables.hpp"

namespace spectra
{

namespace
{

static_assert(Colormap::LUT_SIZE == colormap_tables::LUT_SIZE);

struct BuiltinMap
{
    ColormapType                type;
    std::string_view            name;
    const colormap_tables::Lut* lut;
};

// Picker order: perceptually uniform sequential, then diverging, cyclic and
// the legacy rainbow / heat maps.
constexpr std::array<BuiltinMap, 13> BUILTIN_MAPS = {{
    {ColormapType::Viridis, "viridis", &colormap_tables::VIRIDIS},
    {ColormapType::Plasma, "plasma", &colormap_tables::PLASMA},
    {ColormapType::Inferno, "inferno", &colormap_tables::INFERNO},
    {ColormapType::Magma, "magma", &colormap_tables::MAGMA},
    {ColormapType::Cividis, "cividis", &colormap_tables::CIVIDIS},
    {ColormapType::Grayscale, "gray", &colormap_tables::GRAY},
    {ColormapType::Coolwarm, "coolwarm", &colormap_tables::COOLWARM},
    {ColormapType::RdBu, "RdBu", &colormap_tables::RDBU},
    {ColormapType::PuOr, "PuOr", &colormap_tables::PUOR},
    {ColormapType::Twilight, "twilight", &colormap_tables::TWILIGHT},
    {ColormapType::Turbo, "turbo", &colormap_tables::TURBO},
    {ColormapType::Jet, "jet", &colormap_tables::JET},
    {ColormapType::Hot, "hot", &colormap_tables::HOT},
}};

constexpr std::array<ColormapType, BUILTIN_MAPS.size()> builtin_types()
{
    std::array<ColormapType, BUILTIN_MAPS.size()> types{};
    for (size_t i = 0; i < BUILTIN_MAPS.size(); ++i)
        types[i] = BUILTIN_MAPS[i].type;
    return types;
}

constexpr std::array<ColormapType, BUILTIN_MAPS.size()> BUILTIN_TYPES = builtin_types();

const BuiltinMap* find_builtin(ColormapType type)
{
    for (const auto& m : BUILTIN_MAPS)
        if (m.type == type)
            return &m;
    return nullptr;
}

bool iequals(std::string_view a, std::string_view b)
{
    return a.size() == b.size()
           && std::equal(a.begin(),
                         a.end(),
                         b.begin(),
                         [](char x, char y)
                         {
                             return std::tolower(static_cast<unsigned char>(x))
                                    == std::tolower(static_cast<unsigned char>(y));
                         });
}

size_t lut_entry(float t)
{
    if (!(t > 0.0f))
        return 0;   // also NaN
    const auto i = static_cast<size_t>(std::min(t, 1.0f) * static_cast<float>(Colormap::LUT_SIZE));
    return std::min(i, Colormap::LUT_SIZE - 1);
}

}   // namespace

Colormap Colormap::from_colors(std::span<const Color> colors)
{
    Colormap cm;
    if (colors.empty())
        return cm;
    auto lut = std::make_shared<std::vector<Color>>(LUT_SIZE);
    for (size_t i = 0; i < LUT_SIZE; ++i)
    {
        if (colors.size() == 1)
        {
            (*lut)[i] = colors[0];
            continue;
        }
        // Entry i sits at t = i / (LUT_SIZE - 1), as in matplotlib's from_list.
        const float pos = static_cast<float>(i) / static_cast<float>(LUT_SIZE - 1)
                          * static_cast<float>(colors.size() - 1);
        const size_t k = std::min(static_cast<size_t>(pos), colors.size() - 2);
        const float  u = pos - static_cast<float>(k);
        const Color& a = colors[k];
        const Color& b = colors[k + 1];
        (*lut)[i]      = Color{a.r + (b.r - a.r) * u,
                          a.g + (b.g - a.g) * u,
                          a.b + (b.b - a.b) * u,
                          a.a + (b.a - a.a) * u};
    }
    cm.type_   = ColormapType::Custom;
    cm.custom_ = std::move(lut);
    return cm;
}

std::optional<Colormap> Colormap::from_name(std::string_view name)
{
    bool reversed = false;
    if (name.size() > 2 && iequals(name.substr(name.size() - 2), "_r"))
    {
        reversed = true;
        name.remove_suffix(2);
    }
    if (iequals(name, "none"))
        return Colormap(ColormapType::None, reversed);
    if (iequals(name, "grey") || iequals(name, "grayscale") || iequals(name, "greyscale"))
        return Colormap(ColormapType::Grayscale, reversed);
    for (const auto& m : BUILTIN_MAPS)
        if (iequals(name, m.name))
            return Colormap(m.type, reversed);
    return std::nullopt;
}

Color Colormap::sample(float t) const
{
    size_t i = lut_entry(t);
    if (reversed_)
        i = LUT_SIZE - 1 - i;
    if (type_ == ColormapType::Custom && custom_ && custom_->size() == LUT_SIZE)
        return (*custom_)[i];
    const BuiltinMap* m = find_builtin(type_);
    if (!m)
        m = find_builtin(ColormapType::Grayscale);
    const auto& e = (*m->lut)[i];
    return Color{static_cast<float>(e[0]) / 255.0f,
                 static_cast<float>(e[1]) / 255.0f,
                 static_cast<float>(e[2]) / 255.0f,
                 1.0f};
}

bool Colormap::has_transparency() const
{
    if (type_ != ColormapType::Custom || !custom_)
        return false;
    // The same 8-bit rounding as rgba8(): what the shader sees.
    return std::any_of(custom_->begin(),
                       custom_->end(),
                       [](const Color& c)
                       { return std::lround(std::clamp(c.a, 0.0f, 1.0f) * 255.0f) < 255; });
}

std::array<uint8_t, Colormap::LUT_SIZE * 4> Colormap::rgba8() const
{
    std::array<uint8_t, LUT_SIZE * 4> out{};
    auto                              to8 = [](float c)
    { return static_cast<uint8_t>(std::lround(std::clamp(c, 0.0f, 1.0f) * 255.0f)); };
    for (size_t i = 0; i < LUT_SIZE; ++i)
    {
        // Entry i covers t in [i / n, (i + 1) / n); sample it at its centre.
        const Color c  = sample((static_cast<float>(i) + 0.5f) / static_cast<float>(LUT_SIZE));
        out[i * 4 + 0] = to8(c.r);
        out[i * 4 + 1] = to8(c.g);
        out[i * 4 + 2] = to8(c.b);
        out[i * 4 + 3] = to8(c.a);
    }
    return out;
}

std::string Colormap::name() const
{
    std::string n =
        type_ == ColormapType::Custom ? std::string("custom") : std::string(colormap_name(type_));
    if (reversed_)
        n += "_r";
    return n;
}

bool Colormap::operator==(const Colormap& other) const
{
    if (type_ != other.type_ || reversed_ != other.reversed_)
        return false;
    if (type_ != ColormapType::Custom || custom_ == other.custom_)
        return true;
    if (!custom_ || !other.custom_)
        return false;
    return std::equal(custom_->begin(),
                      custom_->end(),
                      other.custom_->begin(),
                      other.custom_->end(),
                      [](const Color& a, const Color& b)
                      { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; });
}

std::string_view colormap_name(ColormapType type)
{
    if (type == ColormapType::Custom)
        return "custom";
    if (const BuiltinMap* m = find_builtin(type))
        return m->name;
    return "none";
}

std::span<const ColormapType> builtin_colormaps()
{
    return BUILTIN_TYPES;
}

}   // namespace spectra
