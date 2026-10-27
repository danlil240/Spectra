#pragma once

// Fallback-font discovery and lookup for glyphs the primary (Inter) font lacks.
//
// Spectra embeds only Inter (Latin, Greek, Cyrillic, common punctuation and a
// few maths symbols).  Maths operators such as ∮ ∇ ∈ and CJK text are taken
// from fonts already installed on the system instead of embedding several
// megabytes of extra font data.  Candidates are well-known system font paths
// (DejaVu Sans / Noto Sans Math / FreeSans for symbols, Noto Sans CJK /
// WenQuanYi / IPA / Droid Sans Fallback for CJK, plus the macOS and Windows
// equivalents).  SPECTRA_FALLBACK_FONTS (a ':'-separated list on POSIX, ';' on
// Windows) adds user fonts ahead of the built-in candidates.
//
// Font files are memory-mapped on POSIX, so only the pages that are actually
// read (cmap lookups, rasterised outlines) become resident.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
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

// Ordered list of existing fallback font files: entries from
// SPECTRA_FALLBACK_FONTS first, then at most one built-in candidate per group
// (symbols/maths, then CJK).  Missing files are skipped.  Never throws.
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

    // Index (into paths()) of the first font that contains cp, or -1.
    int find(uint32_t cp);

    // Font bytes / face offset for a font returned by find().
    const uint8_t* font_data(int index) const;
    int            font_offset(int index) const;
    // Opaque pointer to the parsed stbtt_fontinfo for a font returned by find().
    const void* font_info(int index) const;

    const std::vector<std::string>& paths() const { return paths_; }
    // Number of font files that have been mapped so far (for tests/diagnostics).
    size_t loaded_count() const;

   private:
    struct Entry;
    bool ensure_loaded(size_t index);

    std::vector<std::string>            paths_;
    std::vector<std::unique_ptr<Entry>> entries_;
};

}   // namespace spectra
