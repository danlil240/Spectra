#include "icons.hpp"

#include <cfloat>
#include <unordered_map>
#include <vector>

#include "imgui.h"

namespace spectra::ui
{

namespace
{

// Convert a PUA codepoint (0xE001-0xE063) to a UTF-8 string.
// All PUA codepoints are in the BMP (U+E000-U+F8FF) so they encode as 3 bytes.
std::string codepoint_to_utf8(uint32_t cp)
{
    std::string s;
    if (cp <= 0x7F)
    {
        s.push_back(static_cast<char>(cp));
    }
    else if (cp <= 0x7FF)
    {
        s.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else
    {
        s.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return s;
}

}  // namespace

IconFont& IconFont::instance()
{
    static IconFont instance;
    return instance;
}

bool IconFont::initialize()
{
    if (initialized_)
        return true;

    build_icon_map();

    // The icon font glyphs are merged into every ImGui font by load_fonts()
    // in imgui_integration.cpp. We just grab the current font (which has icons).
    font_16_ = ImGui::GetFont();
    font_20_ = ImGui::GetFont();
    font_24_ = ImGui::GetFont();
    font_32_ = ImGui::GetFont();

    // Try to find size-specific fonts from the atlas
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    if (atlas)
    {
        for (ImFont* font : atlas->Fonts)
        {
            if (!font)
                continue;
            float sz = font->FontSize;
            if (sz >= 15.5f && sz <= 16.5f)
                font_16_ = font;
            else if (sz >= 19.5f && sz <= 20.5f)
                font_20_ = font;
            else if (sz >= 17.5f && sz <= 18.5f)
                font_24_ = font;  // use 18px as closest to 24
        }
        // font_32_ falls back to the largest available
        if (atlas->Fonts.Size > 0)
        {
            ImFont* largest = atlas->Fonts[0];
            for (ImFont* font : atlas->Fonts)
            {
                if (font && font->FontSize > largest->FontSize)
                    largest = font;
            }
            font_32_ = largest;
        }
    }

    initialized_ = true;
    return true;
}

ImFont* IconFont::get_font(float size) const
{
    if (!initialized_)
        return nullptr;

    if (size <= 16.0f)
        return font_16_;
    if (size <= 20.0f)
        return font_20_;
    if (size <= 24.0f)
        return font_24_;
    return font_32_;
}

void IconFont::draw(Icon icon, float size, const Color& color)
{
    if (!initialized_)
        return;

    ImFont* font = get_font(size);
    if (!font)
        return;

    const char* icon_str = get_icon_string(icon);
    if (!icon_str)
        return;

    ImVec4 imgui_color(color.r, color.g, color.b, color.a);

    ImGui::PushFont(font);
    ImGui::TextColored(imgui_color, "%s", icon_str);
    ImGui::PopFont();
}

void IconFont::draw(Icon icon, float size, const ImVec4& color)
{
    draw(icon, size, Color(color.x, color.y, color.z, color.w));
}

const char* IconFont::get_icon_string(Icon icon) const
{
    // The Icon enum values ARE the PUA codepoints (0xE001-0xE063).
    // Just look up the cached UTF-8 string.
    auto it = codepoint_strings_.find(static_cast<uint32_t>(icon));
    if (it != codepoint_strings_.end())
    {
        return it->second.c_str();
    }
    return "?";
}

float IconFont::get_width(Icon icon, float size) const
{
    if (!initialized_)
        return size;

    ImFont* font = get_font(size);
    if (!font)
        return size;

    const char* icon_str = get_icon_string(icon);
    if (!icon_str)
        return size;

    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, icon_str).x;
}

bool IconFont::has_icon(Icon icon) const
{
    if (icon_map_.empty())
        const_cast<IconFont*>(this)->build_icon_map();
    return icon_map_.find(icon) != icon_map_.end();
}

const std::vector<Icon>& IconFont::get_all_icons() const
{
    if (icon_map_.empty())
        const_cast<IconFont*>(this)->build_icon_map();
    static std::vector<Icon> all_icons;
    if (all_icons.empty())
    {
        for (const auto& [ic, _] : icon_map_)
        {
            all_icons.push_back(ic);
        }
    }
    return all_icons;
}

void IconFont::build_icon_map()
{
    icon_map_.clear();
    codepoint_strings_.clear();

    // The Icon enum values are the PUA codepoints themselves.
    // Build the map for every icon from 0xE001 to 0xE062.
    for (uint32_t cp = 0xE001; cp <= 0xE062; ++cp)
    {
        Icon icon = static_cast<Icon>(cp);
        icon_map_[icon] = cp;
        codepoint_strings_[cp] = codepoint_to_utf8(cp);
    }
}

}  // namespace spectra::ui
