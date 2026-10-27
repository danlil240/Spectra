#pragma once

// Fallback-font discovery and lookup for glyphs the primary (Inter) font lacks.
//
// Spectra embeds only Inter (Latin, Greek, Cyrillic, common punctuation and a
// few maths symbols).  Maths operators such as ∮ ∇ ∈, CJK text and emoji are
// taken from fonts already installed on the system instead of embedding
// several megabytes of extra font data.
//
// On Linux (and other systems with fontconfig), fontconfig is loaded at run
// time (dlopen, no build dependency) and asked for a font covering each group
// (symbols, CJK, emoji) and, lazily, for any codepoint no font in the chain
// has.  Without fontconfig, or when SPECTRA_FONTCONFIG=0, well-known system
// font paths are used instead (DejaVu Sans / Noto Sans Math / FreeSans for
// symbols, Noto Sans CJK / WenQuanYi / IPA / Droid Sans Fallback for CJK,
// Noto Emoji / Symbola for emoji, plus the macOS and Windows equivalents).
// SPECTRA_FALLBACK_FONTS (a ':'-separated list on POSIX, ';' on Windows) adds
// user fonts ahead of the discovered ones.
//
// Emoji are drawn monochrome: colour-only emoji fonts (CBDT/sbix bitmaps such
// as Noto Color Emoji or Apple Color Emoji) have no outlines stb_truetype can
// rasterise and are skipped.  True colour emoji are not supported.
//
// Font files are memory-mapped on POSIX, so only the pages that are actually
// read (cmap lookups, rasterised outlines) become resident.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace spectra
{

// Read-only, move-only view of a font file (mmap on POSIX, heap copy elsewhere).
class FontFileData
{
   public:
    FontFileData() = default;
    ~FontFileData();
    FontFileData(FontFileData&& other) noexcept;
    FontFileData& operator=(FontFileData&& other) noexcept;
    FontFileData(const FontFileData&)            = delete;
    FontFileData& operator=(const FontFileData&) = delete;

    // Map/read the file.  Returns false (and stays empty) on failure.
    bool load(const std::string& path);

    const uint8_t* data() const { return data_; }
    size_t         size() const { return size_; }
    bool           empty() const { return size_ == 0; }

   private:
    void reset();

    const uint8_t*       data_   = nullptr;
    size_t               size_   = 0;
    bool                 mapped_ = false;
    std::vector<uint8_t> heap_;
};

// True for TrueType / OpenType / TrueType-collection signatures.
bool is_font_signature(const uint8_t* data, size_t size);

// True when face `face_index` of the font has glyph outlines (glyf or CFF)
// that stb_truetype / ImGui can rasterise.  False for colour-bitmap-only fonts
// (CBDT / sbix emoji fonts) and for anything that is not a font.
bool is_outline_font(const uint8_t* data, size_t size, int face_index = 0);

// A font file (and face within a collection) chosen by the system font
// configuration.
struct SystemFontMatch
{
    std::string path;
    int         face_index = 0;
};

// Looks up a font that has a glyph for a codepoint.  Returns false when none.
using SystemFontLookup = std::function<bool(uint32_t cp, SystemFontMatch& out)>;

// True when fontconfig could be loaded and is not disabled (SPECTRA_FONTCONFIG=0).
bool system_font_lookup_available();

// Ask fontconfig for an outline (non-colour) font that has a glyph for cp,
// preferring sans-serif faces.  Returns false when fontconfig is unavailable
// or disabled, or no installed font has the glyph.  Never throws.
bool find_system_font_for(uint32_t cp, SystemFontMatch& out);

// Ordered list of existing fallback font files: entries from
// SPECTRA_FALLBACK_FONTS first, then at most one font per group (symbols/maths,
// CJK, emoji): fontconfig's choice when available, else the first existing
// built-in candidate.  Missing files are skipped.  Never throws.
std::vector<std::string> discover_fallback_fonts();

// Split a SPECTRA_FALLBACK_FONTS-style list into paths (empty items dropped).
std::vector<std::string> split_font_path_list(std::string_view list);

// Characters that are invisible by definition (zero-width spaces/joiners,
// variation selectors, BOM, ...).  When no font has them they are skipped
// rather than drawn as a missing-glyph box.
bool is_default_ignorable(uint32_t cp);

// Lazily loaded chain of fallback fonts.  A font file is mapped and parsed the
// first time a lookup reaches it, so a chain whose fonts are never needed costs
// nothing beyond the path strings.
class FontFallbackChain
{
   public:
    FontFallbackChain();
    explicit FontFallbackChain(std::vector<std::string> paths);
    ~FontFallbackChain();
    FontFallbackChain(FontFallbackChain&&) noexcept;
    FontFallbackChain& operator=(FontFallbackChain&&) noexcept;
    FontFallbackChain(const FontFallbackChain&)            = delete;
    FontFallbackChain& operator=(const FontFallbackChain&) = delete;

    // Index (into paths()) of the first font that contains cp, or -1.  When
    // no font in the chain has it and a system lookup is set, the font that
    // lookup names is appended to the chain (once) and used.
    int find(uint32_t cp);

    // Append a font (face `face_index` of a collection) to the end of the chain.
    // Returns its index; an already present path/face returns the existing index.
    int add_font(std::string path, int face_index = 0);

    // Per-codepoint system lookup consulted by find() for codepoints no font
    // in the chain has (e.g. find_system_font_for).  Empty disables it.
    void set_system_lookup(SystemFontLookup lookup);

    // Font bytes / face offset / face index for a font returned by find().
    const uint8_t* font_data(int index) const;
    int            font_offset(int index) const;
    int            face_index(int index) const;
    // Opaque pointer to the parsed stbtt_fontinfo for a font returned by find().
    const void* font_info(int index) const;

    const std::vector<std::string>& paths() const { return paths_; }
    // Number of font files that have been mapped so far (for tests/diagnostics).
    size_t loaded_count() const;

   private:
    struct Entry;
    bool ensure_loaded(size_t index);

    std::vector<std::string>            paths_;
    std::vector<int>                    faces_;
    std::vector<std::unique_ptr<Entry>> entries_;
    SystemFontLookup                    system_lookup_;
    std::unordered_set<uint32_t>        system_misses_;   // cps the lookup had no font for
};

}   // namespace spectra
