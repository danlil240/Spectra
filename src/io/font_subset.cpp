#include "io/font_subset.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <string_view>

namespace spectra
{

namespace
{

uint16_t rd16(std::span<const uint8_t> d, size_t at)
{
    return static_cast<uint16_t>((d[at] << 8) | d[at + 1]);
}

uint32_t rd32(std::span<const uint8_t> d, size_t at)
{
    return (static_cast<uint32_t>(d[at]) << 24) | (static_cast<uint32_t>(d[at + 1]) << 16)
           | (static_cast<uint32_t>(d[at + 2]) << 8) | static_cast<uint32_t>(d[at + 3]);
}

void put16(std::vector<uint8_t>& o, uint32_t v)
{
    o.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    o.push_back(static_cast<uint8_t>(v & 0xFF));
}

void put32(std::vector<uint8_t>& o, uint32_t v)
{
    put16(o, v >> 16);
    put16(o, v & 0xFFFF);
}

void set32(std::vector<uint8_t>& o, size_t at, uint32_t v)
{
    o[at]     = static_cast<uint8_t>(v >> 24);
    o[at + 1] = static_cast<uint8_t>(v >> 16);
    o[at + 2] = static_cast<uint8_t>(v >> 8);
    o[at + 3] = static_cast<uint8_t>(v);
}

void pad4(std::vector<uint8_t>& o)
{
    while (o.size() % 4 != 0)
        o.push_back(0);
}

uint32_t table_checksum(std::span<const uint8_t> d)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < d.size(); i += 4)
    {
        uint32_t v = 0;
        for (size_t k = 0; k < 4; ++k)
            v = (v << 8) | (i + k < d.size() ? d[i + k] : 0u);
        sum += v;
    }
    return sum;
}

// Largest power of two <= n (n >= 1) and its log2.
std::pair<uint32_t, uint32_t> pow2_floor(uint32_t n)
{
    uint32_t p = 1, l = 0;
    while (p * 2 <= n)
    {
        p *= 2;
        ++l;
    }
    return {p, l};
}

using Tag = std::array<char, 4>;

Tag tag_of(std::string_view s)
{
    return Tag{s[0], s[1], s[2], s[3]};
}

// cmap with a format 4 (BMP) subtable and, for codepoints beyond the BMP, a
// format 12 subtable.
std::vector<uint8_t> build_cmap(std::vector<FontSubsetGlyph> glyphs)
{
    std::sort(glyphs.begin(),
              glyphs.end(),
              [](const FontSubsetGlyph& a, const FontSubsetGlyph& b)
              { return a.codepoint < b.codepoint; });
    glyphs.erase(std::unique(glyphs.begin(),
                             glyphs.end(),
                             [](const FontSubsetGlyph& a, const FontSubsetGlyph& b)
                             { return a.codepoint == b.codepoint; }),
                 glyphs.end());

    struct Seg
    {
        uint32_t start, end;
        uint16_t glyph;   // glyph of `start`; consecutive codepoints map to consecutive glyphs
    };
    std::vector<Seg> bmp, all;
    for (const auto& g : glyphs)
    {
        auto extend = [&](std::vector<Seg>& v)
        {
            if (!v.empty() && v.back().end + 1 == g.codepoint
                && static_cast<uint32_t>(v.back().glyph) + (g.codepoint - v.back().start)
                       == g.glyph)
                v.back().end = g.codepoint;
            else
                v.push_back({g.codepoint, g.codepoint, g.glyph});
        };
        if (g.codepoint < 0xFFFF)
            extend(bmp);
        extend(all);
    }
    const bool need_12 = !glyphs.empty() && glyphs.back().codepoint > 0xFFFF;

    std::vector<uint8_t> f4;
    {
        const uint32_t seg_count = static_cast<uint32_t>(bmp.size()) + 1;   // + 0xFFFF sentinel
        const auto [p, l]        = pow2_floor(seg_count);
        put16(f4, 4);
        put16(f4, 16 + seg_count * 8);   // length
        put16(f4, 0);                    // language
        put16(f4, seg_count * 2);
        put16(f4, p * 2);
        put16(f4, l);
        put16(f4, seg_count * 2 - p * 2);
        for (const auto& s : bmp)
            put16(f4, s.end);
        put16(f4, 0xFFFF);
        put16(f4, 0);   // reservedPad
        for (const auto& s : bmp)
            put16(f4, s.start);
        put16(f4, 0xFFFF);
        for (const auto& s : bmp)
            put16(f4, (static_cast<uint32_t>(s.glyph) - s.start) & 0xFFFF);
        put16(f4, 1);   // 0xFFFF + 1 -> glyph 0
        for (uint32_t i = 0; i < seg_count; ++i)
            put16(f4, 0);   // idRangeOffset
    }
    std::vector<uint8_t> f12;
    if (need_12)
    {
        put16(f12, 12);
        put16(f12, 0);
        put32(f12, 16 + static_cast<uint32_t>(all.size()) * 12);
        put32(f12, 0);
        put32(f12, static_cast<uint32_t>(all.size()));
        for (const auto& s : all)
        {
            put32(f12, s.start);
            put32(f12, s.end);
            put32(f12, s.glyph);
        }
    }

    std::vector<uint8_t> cmap;
    const uint32_t       n_sub = need_12 ? 2 : 1;
    put16(cmap, 0);
    put16(cmap, n_sub);
    const uint32_t f4_off = 4 + 8 * n_sub;
    put16(cmap, 3);
    put16(cmap, 1);
    put32(cmap, f4_off);
    if (need_12)
    {
        put16(cmap, 3);
        put16(cmap, 10);
        put32(cmap, f4_off + static_cast<uint32_t>(f4.size()));
    }
    cmap.insert(cmap.end(), f4.begin(), f4.end());
    cmap.insert(cmap.end(), f12.begin(), f12.end());
    return cmap;
}

}   // namespace

std::vector<uint8_t> subset_truetype(std::span<const uint8_t>         font,
                                     std::span<const FontSubsetGlyph> glyphs)
{
    if (font.size() < 12)
        return {};
    const uint32_t version = rd32(font, 0);
    if (version != 0x00010000u && version != 0x74727565u /* 'true' */)
        return {};   // CFF ('OTTO') or a collection
    const uint16_t num_tables = rd16(font, 4);
    if (font.size() < 12 + static_cast<size_t>(num_tables) * 16)
        return {};

    std::map<Tag, std::span<const uint8_t>> tables;
    for (uint16_t i = 0; i < num_tables; ++i)
    {
        const size_t   rec = 12 + static_cast<size_t>(i) * 16;
        const Tag      tag{static_cast<char>(font[rec]),
                      static_cast<char>(font[rec + 1]),
                      static_cast<char>(font[rec + 2]),
                      static_cast<char>(font[rec + 3])};
        const uint32_t off = rd32(font, rec + 8);
        const uint32_t len = rd32(font, rec + 12);
        if (static_cast<uint64_t>(off) + len > font.size())
            return {};
        tables[tag] = font.subspan(off, len);
    }
    auto get = [&](std::string_view t) -> std::span<const uint8_t>
    {
        auto it = tables.find(tag_of(t));
        return it == tables.end() ? std::span<const uint8_t>{} : it->second;
    };

    const auto head = get("head");
    const auto maxp = get("maxp");
    const auto loca = get("loca");
    const auto glyf = get("glyf");
    if (head.size() < 54 || maxp.size() < 6 || get("hhea").size() < 36 || get("hmtx").empty()
        || glyf.empty())
        return {};
    const uint32_t num_glyphs = rd16(maxp, 4);
    const bool     long_loca  = rd16(head, 50) != 0;
    if (num_glyphs == 0 || loca.size() < (num_glyphs + 1) * (long_loca ? 4u : 2u))
        return {};

    std::vector<uint32_t> offsets(num_glyphs + 1);
    for (uint32_t g = 0; g <= num_glyphs; ++g)
    {
        offsets[g] = long_loca ? rd32(loca, g * 4) : 2u * rd16(loca, g * 2);
        if (offsets[g] > glyf.size() || (g > 0 && offsets[g] < offsets[g - 1]))
            return {};
    }
    auto glyph_data = [&](uint32_t g)
    { return glyf.subspan(offsets[g], offsets[g + 1] - offsets[g]); };

    // Glyph closure: .notdef, the requested glyphs and composite components.
    std::vector<bool>     keep(num_glyphs, false);
    std::vector<uint32_t> todo{0};
    for (const auto& g : glyphs)
        if (g.glyph < num_glyphs)
            todo.push_back(g.glyph);
    while (!todo.empty())
    {
        const uint32_t g = todo.back();
        todo.pop_back();
        if (keep[g])
            continue;
        keep[g]      = true;
        const auto d = glyph_data(g);
        if (d.size() < 10 || static_cast<int16_t>(rd16(d, 0)) >= 0)
            continue;
        size_t at = 10;
        for (;;)
        {
            if (at + 4 > d.size())
                return {};
            const uint16_t flags = rd16(d, at);
            const uint16_t comp  = rd16(d, at + 2);
            if (comp < num_glyphs)
                todo.push_back(comp);
            at += 4 + ((flags & 0x0001) ? 4 : 2);   // ARG_1_AND_2_ARE_WORDS
            if (flags & 0x0008)                     // WE_HAVE_A_SCALE
                at += 2;
            else if (flags & 0x0040)   // WE_HAVE_AN_X_AND_Y_SCALE
                at += 4;
            else if (flags & 0x0080)   // WE_HAVE_A_TWO_BY_TWO
                at += 8;
            if (!(flags & 0x0020))   // MORE_COMPONENTS
                break;
        }
    }

    std::vector<uint8_t>  new_glyf;
    std::vector<uint32_t> new_offsets(num_glyphs + 1, 0);
    for (uint32_t g = 0; g < num_glyphs; ++g)
    {
        new_offsets[g] = static_cast<uint32_t>(new_glyf.size());
        if (keep[g])
        {
            const auto d = glyph_data(g);
            new_glyf.insert(new_glyf.end(), d.begin(), d.end());
            pad4(new_glyf);
        }
    }
    new_offsets[num_glyphs] = static_cast<uint32_t>(new_glyf.size());
    if (new_glyf.empty())
        new_glyf.assign(4, 0);   // some parsers reject a zero-length glyf

    const bool           short_loca = new_offsets[num_glyphs] < 0x20000u;
    std::vector<uint8_t> new_loca;
    for (uint32_t off : new_offsets)
    {
        if (short_loca)
            put16(new_loca, off / 2);
        else
            put32(new_loca, off);
    }

    std::vector<uint8_t> new_head(head.begin(), head.end());
    set32(new_head, 8, 0);   // checkSumAdjustment, patched below
    new_head[50] = 0;
    new_head[51] = short_loca ? 0 : 1;

    std::vector<uint8_t> new_post;
    {
        const auto post = get("post");
        if (post.size() >= 32)
            new_post.assign(post.begin(), post.begin() + 32);
        else
            new_post.assign(32, 0);
        set32(new_post, 0, 0x00030000u);   // format 3: no glyph names
    }

    std::map<Tag, std::vector<uint8_t>> out_tables;
    out_tables[tag_of("head")] = std::move(new_head);
    out_tables[tag_of("loca")] = std::move(new_loca);
    out_tables[tag_of("glyf")] = std::move(new_glyf);
    out_tables[tag_of("post")] = std::move(new_post);
    out_tables[tag_of("cmap")] = build_cmap({glyphs.begin(), glyphs.end()});
    for (const char* t : {"hhea", "hmtx", "maxp", "OS/2", "name", "cvt ", "fpgm", "prep", "gasp"})
    {
        const auto d = get(t);
        if (!d.empty())
            out_tables[tag_of(t)].assign(d.begin(), d.end());
    }

    // std::map orders the tags bytewise, as the table directory requires.
    const auto n      = static_cast<uint32_t>(out_tables.size());
    const auto [p, l] = pow2_floor(n);
    std::vector<uint8_t> out;
    put32(out, 0x00010000u);
    put16(out, n);
    put16(out, p * 16);
    put16(out, l);
    put16(out, n * 16 - p * 16);
    size_t       data_at     = 12 + static_cast<size_t>(n) * 16;
    size_t       head_offset = 0;
    const size_t dir_at      = out.size();
    out.resize(data_at, 0);
    size_t rec = dir_at;
    for (auto& [tag, data] : out_tables)
    {
        if (tag == tag_of("head"))
            head_offset = out.size();
        std::memcpy(&out[rec], tag.data(), 4);
        set32(out, rec + 4, table_checksum(data));
        set32(out, rec + 8, static_cast<uint32_t>(out.size()));
        set32(out, rec + 12, static_cast<uint32_t>(data.size()));
        out.insert(out.end(), data.begin(), data.end());
        pad4(out);
        rec += 16;
    }
    set32(out, head_offset + 8, 0xB1B0AFBAu - table_checksum(out));
    return out;
}

std::string base64_encode(std::span<const uint8_t> data)
{
    static constexpr char abc[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 3 <= data.size(); i += 3)
    {
        const uint32_t v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out += abc[(v >> 18) & 63];
        out += abc[(v >> 12) & 63];
        out += abc[(v >> 6) & 63];
        out += abc[v & 63];
    }
    if (i < data.size())
    {
        uint32_t v = data[i] << 16;
        if (i + 1 < data.size())
            v |= data[i + 1] << 8;
        out += abc[(v >> 18) & 63];
        out += abc[(v >> 12) & 63];
        out += i + 1 < data.size() ? abc[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

}   // namespace spectra
