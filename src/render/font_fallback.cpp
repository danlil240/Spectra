#include "font_fallback.hpp"

#include <cstdlib>
#include <fstream>
#include <system_error>
#include <filesystem>
#include <utility>

#include <stb_truetype.h>   // declarations only; implemented in text_renderer.cpp

#if defined(__unix__) || defined(__APPLE__)
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <unistd.h>
    #define SPECTRA_FONT_MMAP 1
#endif

namespace spectra
{

// ─── FontFileData ───────────────────────────────────────────────────────────

FontFileData::~FontFileData()
{
    reset();
}

FontFileData::FontFileData(FontFileData&& other) noexcept
{
    *this = std::move(other);
}

FontFileData& FontFileData::operator=(FontFileData&& other) noexcept
{
    if (this != &other)
    {
        reset();
        heap_         = std::move(other.heap_);
        mapped_       = other.mapped_;
        size_         = other.size_;
        data_         = mapped_ ? other.data_ : heap_.data();
        other.data_   = nullptr;
        other.size_   = 0;
        other.mapped_ = false;
    }
    return *this;
}

void FontFileData::reset()
{
#ifdef SPECTRA_FONT_MMAP
    if (mapped_ && data_)
        munmap(const_cast<uint8_t*>(data_), size_);
#endif
    heap_.clear();
    heap_.shrink_to_fit();
    data_   = nullptr;
    size_   = 0;
    mapped_ = false;
}

bool FontFileData::load(const std::string& path)
{
    reset();
#ifdef SPECTRA_FONT_MMAP
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    struct stat st
    {
    };
    if (::fstat(fd, &st) != 0 || st.st_size <= 0 || !S_ISREG(st.st_mode))
    {
        ::close(fd);
        return false;
    }
    void* p = ::mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (p == MAP_FAILED)
        return false;
    data_   = static_cast<const uint8_t*>(p);
    size_   = static_cast<size_t>(st.st_size);
    mapped_ = true;
    return true;
#else
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return false;
    const auto sz = file.tellg();
    if (sz <= 0)
        return false;
    heap_.resize(static_cast<size_t>(sz));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(heap_.data()), sz);
    if (!file)
    {
        heap_.clear();
        return false;
    }
    data_ = heap_.data();
    size_ = heap_.size();
    return true;
#endif
}

bool is_font_signature(const uint8_t* data, size_t size)
{
    if (!data || size < 12)
        return false;
    const uint32_t tag = (static_cast<uint32_t>(data[0]) << 24)
                         | (static_cast<uint32_t>(data[1]) << 16)
                         | (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[3]);
    return tag == 0x00010000u       // TrueType
           || tag == 0x4F54544Fu    // 'OTTO' (OpenType/CFF)
           || tag == 0x74746366u    // 'ttcf' (TrueType Collection)
           || tag == 0x74727565u;   // 'true' (legacy Apple TrueType)
}

// ─── Discovery ──────────────────────────────────────────────────────────────

std::vector<std::string> split_font_path_list(std::string_view list)
{
#ifdef _WIN32
    constexpr char SEP = ';';
#else
    constexpr char SEP = ':';
#endif
    std::vector<std::string> out;
    size_t                   start = 0;
    while (start <= list.size())
    {
        size_t end = list.find(SEP, start);
        if (end == std::string_view::npos)
            end = list.size();
        if (end > start)
            out.emplace_back(list.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

namespace
{

// Built-in candidates, grouped by purpose.  The first existing file of each
// group is used; groups are searched in order (symbols before CJK so maths
// operators come from a Latin-styled font).
const std::vector<std::vector<const char*>>& candidate_groups()
{
    static const std::vector<std::vector<const char*>> groups = {
        // Symbols / mathematical operators / arrows
        {
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
            "/usr/share/fonts/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/usr/local/share/fonts/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/noto/NotoSansMath-Regular.ttf",
            "/usr/share/fonts/noto/NotoSansMath-Regular.ttf",
            "/usr/share/fonts/google-noto/NotoSansMath-Regular.ttf",
            "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
            "/usr/share/fonts/gnu-free/FreeSans.otf",
            "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
            "/Library/Fonts/Arial Unicode.ttf",
            "/System/Library/Fonts/Apple Symbols.ttf",
            "C:/Windows/Fonts/seguisym.ttf",
            "C:/Windows/Fonts/cambria.ttc",
        },
        // CJK
        {
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/google-noto-sans-cjk-fonts/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf",
            "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
            "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
            "/usr/share/fonts/wenquanyi/wqy-microhei/wqy-microhei.ttc",
            "/usr/share/fonts/wenquanyi/wqy-zenhei/wqy-zenhei.ttc",
            "/usr/share/fonts/wqy-microhei/wqy-microhei.ttc",
            "/usr/share/fonts/wqy-zenhei/wqy-zenhei.ttc",
            "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
            "/usr/share/fonts/google-droid-sans-fonts/DroidSansFallbackFull.ttf",
            "/usr/share/fonts/opentype/ipafont-gothic/ipag.ttf",
            "/System/Library/Fonts/Hiragino Sans GB.ttc",
            "/System/Library/Fonts/STHeiti Light.ttc",
            "C:/Windows/Fonts/msyh.ttc",
            "C:/Windows/Fonts/msgothic.ttc",
            "C:/Windows/Fonts/malgun.ttf",
        },
    };
    return groups;
}

bool is_regular_file(const std::string& path)
{
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

}   // namespace

std::vector<std::string> discover_fallback_fonts()
{
    std::vector<std::string> out;
    auto                     add = [&out](const std::string& p)
    {
        for (const auto& existing : out)
            if (existing == p)
                return;
        out.push_back(p);
    };

    if (const char* env = std::getenv("SPECTRA_FALLBACK_FONTS"))
    {
        for (const auto& p : split_font_path_list(env))
            if (is_regular_file(p))
                add(p);
    }

    for (const auto& group : candidate_groups())
    {
        for (const char* p : group)
        {
            if (is_regular_file(p))
            {
                add(p);
                break;
            }
        }
    }
    return out;
}

bool is_default_ignorable(uint32_t cp)
{
    return cp == 0x00AD                           // soft hyphen
           || cp == 0x034F                        // combining grapheme joiner
           || (cp >= 0x200B && cp <= 0x200F)      // ZW space/joiners, LRM/RLM
           || (cp >= 0x202A && cp <= 0x202E)      // bidi embeddings
           || (cp >= 0x2060 && cp <= 0x206F)      // word joiner, invisible operators
           || (cp >= 0xFE00 && cp <= 0xFE0F)      // variation selectors
           || cp == 0xFEFF                        // BOM / ZWNBSP
           || (cp >= 0xE0000 && cp <= 0xE0FFF);   // tags, VS supplement
}

// ─── FontFallbackChain ──────────────────────────────────────────────────────

struct FontFallbackChain::Entry
{
    bool           attempted = false;
    bool           ok        = false;
    FontFileData   file;
    stbtt_fontinfo info{};
    int            offset = 0;
};

FontFallbackChain::FontFallbackChain() = default;

FontFallbackChain::FontFallbackChain(std::vector<std::string> paths) : paths_(std::move(paths))
{
    entries_.resize(paths_.size());
}

FontFallbackChain::~FontFallbackChain()                                       = default;
FontFallbackChain::FontFallbackChain(FontFallbackChain&&) noexcept            = default;
FontFallbackChain& FontFallbackChain::operator=(FontFallbackChain&&) noexcept = default;

bool FontFallbackChain::ensure_loaded(size_t index)
{
    if (!entries_[index])
        entries_[index] = std::make_unique<Entry>();
    Entry& e = *entries_[index];
    if (e.attempted)
        return e.ok;
    e.attempted = true;
    if (!e.file.load(paths_[index]) || !is_font_signature(e.file.data(), e.file.size()))
    {
        e.file = FontFileData{};
        return false;
    }
    const int offset = stbtt_GetFontOffsetForIndex(e.file.data(), 0);
    if (offset < 0 || static_cast<size_t>(offset) >= e.file.size()
        || !stbtt_InitFont(&e.info, e.file.data(), offset))
    {
        e.file = FontFileData{};
        return false;
    }
    e.offset = offset;
    e.ok     = true;
    return true;
}

int FontFallbackChain::find(uint32_t cp)
{
    for (size_t i = 0; i < paths_.size(); ++i)
    {
        if (!ensure_loaded(i))
            continue;
        if (stbtt_FindGlyphIndex(&entries_[i]->info, static_cast<int>(cp)) != 0)
            return static_cast<int>(i);
    }
    return -1;
}

const uint8_t* FontFallbackChain::font_data(int index) const
{
    if (index < 0 || static_cast<size_t>(index) >= entries_.size() || !entries_[index]
        || !entries_[index]->ok)
        return nullptr;
    return entries_[index]->file.data();
}

int FontFallbackChain::font_offset(int index) const
{
    if (!font_data(index))
        return 0;
    return entries_[index]->offset;
}

const void* FontFallbackChain::font_info(int index) const
{
    if (!font_data(index))
        return nullptr;
    return &entries_[index]->info;
}

size_t FontFallbackChain::loaded_count() const
{
    size_t n = 0;
    for (const auto& e : entries_)
        if (e && e->ok)
            ++n;
    return n;
}

}   // namespace spectra
