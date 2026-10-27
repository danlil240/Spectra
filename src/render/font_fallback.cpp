#include "font_fallback.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

#include <stb_truetype.h>   // declarations only; implemented in text_renderer.cpp

#if defined(__unix__) || defined(__APPLE__)
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <unistd.h>
    #define SPECTRA_FONT_MMAP 1
#endif

#if defined(__unix__) && !defined(__ANDROID__)
    #include <dlfcn.h>
    #define SPECTRA_FONTCONFIG_DLOPEN 1
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

bool is_outline_font(const uint8_t* data, size_t size, int face_index)
{
    if (!is_font_signature(data, size))
        return false;
    const int offset = stbtt_GetFontOffsetForIndex(data, face_index);
    if (offset < 0 || static_cast<size_t>(offset) >= size)
        return false;
    stbtt_fontinfo info{};
    // stbtt_InitFont requires a glyf or CFF table: colour-bitmap-only fonts fail.
    return stbtt_InitFont(&info, data, offset) != 0;
}

// ─── fontconfig (loaded at run time) ────────────────────────────────────────

namespace
{

#ifdef SPECTRA_FONTCONFIG_DLOPEN

bool fontconfig_disabled_by_env()
{
    const char* env = std::getenv("SPECTRA_FONTCONFIG");
    return env
           && (std::strcmp(env, "0") == 0 || std::strcmp(env, "off") == 0
               || std::strcmp(env, "OFF") == 0 || std::strcmp(env, "false") == 0);
}

bool has_outline_font_extension(const std::string& path)
{
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos)
        return false;
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(),
                   ext.end(),
                   ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == "ttf" || ext == "otf" || ext == "ttc" || ext == "otc";
}

// Minimal fontconfig ABI (stable since fontconfig 2.x): opaque handles plus
// the FcFontSet layout and the few enums/objects used below.
struct FcPattern;
struct FcCharSet;
struct FcConfig;
struct FcFontSet
{
    int         nfont;
    int         sfont;
    FcPattern** fonts;
};
using FcBool                   = int;
using FcChar8                  = unsigned char;
constexpr int FC_RESULT_MATCH  = 0;   // FcResultMatch
constexpr int FC_MATCH_PATTERN = 0;   // FcMatchPattern

// RAII handle on libfontconfig plus the entry points the lookups need.  The
// library is opened with RTLD_NODELETE: fontconfig keeps process-wide caches,
// so it stays mapped after the last handle closes.
class Fontconfig
{
   public:
    Fontconfig()
    {
        if (fontconfig_disabled_by_env())
            return;
        lib_ = ::dlopen("libfontconfig.so.1", RTLD_LAZY | RTLD_LOCAL | RTLD_NODELETE);
        if (!lib_)
            lib_ = ::dlopen("libfontconfig.so", RTLD_LAZY | RTLD_LOCAL | RTLD_NODELETE);
        if (!lib_)
            return;
        ok_ = sym(init, "FcInit") && sym(pattern_create, "FcPatternCreate")
              && sym(pattern_destroy, "FcPatternDestroy")
              && sym(pattern_add_string, "FcPatternAddString")
              && sym(pattern_add_charset, "FcPatternAddCharSet")
              && sym(pattern_get_string, "FcPatternGetString")
              && sym(pattern_get_integer, "FcPatternGetInteger")
              && sym(pattern_get_bool, "FcPatternGetBool")
              && sym(pattern_get_charset, "FcPatternGetCharSet")
              && sym(charset_create, "FcCharSetCreate") && sym(charset_destroy, "FcCharSetDestroy")
              && sym(charset_add_char, "FcCharSetAddChar")
              && sym(charset_has_char, "FcCharSetHasChar")
              && sym(config_substitute, "FcConfigSubstitute")
              && sym(default_substitute, "FcDefaultSubstitute") && sym(font_sort, "FcFontSort")
              && sym(fontset_destroy, "FcFontSetDestroy");
        if (ok_)
            ok_ = init() != 0;
    }
    ~Fontconfig()
    {
        if (lib_)
            ::dlclose(lib_);
    }
    Fontconfig(const Fontconfig&)            = delete;
    Fontconfig& operator=(const Fontconfig&) = delete;

    bool ok() const { return ok_; }

    // Best outline font for cp (sans-serif preferred), skipping colour fonts.
    bool find(uint32_t cp, SystemFontMatch& out) const
    {
        if (!ok_)
            return false;
        FcPattern* pat = pattern_create();
        FcCharSet* cs  = charset_create();
        if (!pat || !cs)
        {
            if (pat)
                pattern_destroy(pat);
            if (cs)
                charset_destroy(cs);
            return false;
        }
        charset_add_char(cs, cp);
        pattern_add_string(pat, "family", reinterpret_cast<const FcChar8*>("sans-serif"));
        pattern_add_charset(pat, "charset", cs);
        config_substitute(nullptr, pat, FC_MATCH_PATTERN);
        default_substitute(pat);
        int        result = 0;
        FcFontSet* set    = font_sort(nullptr, pat, 1, nullptr, &result);
        bool       found  = false;
        if (set)
        {
            for (int i = 0; i < set->nfont && !found; ++i)
            {
                FcPattern* f      = set->fonts[i];
                FcCharSet* fcs    = nullptr;
                FcChar8*   file   = nullptr;
                int        index  = 0;
                FcBool     colour = 0;
                if (pattern_get_charset(f, "charset", 0, &fcs) != FC_RESULT_MATCH || !fcs
                    || !charset_has_char(fcs, cp))
                    continue;
                if (pattern_get_bool(f, "color", 0, &colour) == FC_RESULT_MATCH && colour)
                    continue;   // bitmap colour emoji: stb_truetype cannot draw it
                if (pattern_get_string(f, "file", 0, &file) != FC_RESULT_MATCH || !file)
                    continue;
                std::string path(reinterpret_cast<const char*>(file));
                if (!has_outline_font_extension(path))
                    continue;   // Type 1 / PCF / bitmap fonts
                if (pattern_get_integer(f, "index", 0, &index) != FC_RESULT_MATCH)
                    index = 0;
                out.path       = std::move(path);
                out.face_index = index & 0xFFFF;   // high bits select a variable instance
                found          = true;
            }
            fontset_destroy(set);
        }
        pattern_destroy(pat);
        charset_destroy(cs);
        return found;
    }

   private:
    template <typename Fn>
    bool sym(Fn& fn, const char* name)
    {
        fn = reinterpret_cast<Fn>(::dlsym(lib_, name));
        return fn != nullptr;
    }

    void* lib_ = nullptr;
    bool  ok_  = false;

    FcBool (*init)()                                                            = nullptr;
    FcPattern* (*pattern_create)()                                              = nullptr;
    void (*pattern_destroy)(FcPattern*)                                         = nullptr;
    FcBool (*pattern_add_string)(FcPattern*, const char*, const FcChar8*)       = nullptr;
    FcBool (*pattern_add_charset)(FcPattern*, const char*, const FcCharSet*)    = nullptr;
    int (*pattern_get_string)(const FcPattern*, const char*, int, FcChar8**)    = nullptr;
    int (*pattern_get_integer)(const FcPattern*, const char*, int, int*)        = nullptr;
    int (*pattern_get_bool)(const FcPattern*, const char*, int, FcBool*)        = nullptr;
    int (*pattern_get_charset)(const FcPattern*, const char*, int, FcCharSet**) = nullptr;
    FcCharSet* (*charset_create)()                                              = nullptr;
    void (*charset_destroy)(FcCharSet*)                                         = nullptr;
    FcBool (*charset_add_char)(FcCharSet*, uint32_t)                            = nullptr;
    FcBool (*charset_has_char)(const FcCharSet*, uint32_t)                      = nullptr;
    FcBool (*config_substitute)(FcConfig*, FcPattern*, int)                     = nullptr;
    void (*default_substitute)(FcPattern*)                                      = nullptr;
    FcFontSet* (*font_sort)(FcConfig*, FcPattern*, FcBool, FcCharSet**, int*)   = nullptr;
    void (*fontset_destroy)(FcFontSet*)                                         = nullptr;
};

#else

class Fontconfig
{
   public:
    bool ok() const { return false; }
    bool find(uint32_t, SystemFontMatch&) const { return false; }
};

#endif

}   // namespace

bool system_font_lookup_available()
{
    return Fontconfig().ok();
}

bool find_system_font_for(uint32_t cp, SystemFontMatch& out)
{
    return Fontconfig().find(cp, out);
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
        // Emoji, monochrome outlines only (colour bitmap fonts cannot be drawn)
        {
            "/usr/share/fonts/truetype/noto/NotoEmoji-Regular.ttf",
            "/usr/share/fonts/noto/NotoEmoji-Regular.ttf",
            "/usr/share/fonts/google-noto-emoji/NotoEmoji-Regular.ttf",
            "/usr/share/fonts/google-noto-emoji-fonts/NotoEmoji-Regular.ttf",
            "/usr/share/fonts/truetype/ancient-scripts/Symbola_hint.ttf",
            "/usr/share/fonts/gdouros-symbola/Symbola.ttf",
            "/usr/share/fonts/TTF/Symbola.ttf",
            "/usr/share/fonts/opentype/unifont/unifont_upper.otf",
            "C:/Windows/Fonts/seguiemj.ttf",
        },
    };
    return groups;
}

// A codepoint representative of each candidate group, used to ask fontconfig
// for that group's font: ∮ (contour integral), 中, 😀.
constexpr uint32_t GROUP_PROBE_CODEPOINTS[] = {0x222E, 0x4E2D, 0x1F600};

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

    const Fontconfig fc;
    const auto&      groups = candidate_groups();
    for (size_t g = 0; g < groups.size(); ++g)
    {
        // Prefer the system's configured choice; fall back to the fixed list.
        SystemFontMatch m;
        if (g < std::size(GROUP_PROBE_CODEPOINTS) && fc.find(GROUP_PROBE_CODEPOINTS[g], m)
            && m.face_index == 0 && is_regular_file(m.path))
        {
            add(m.path);
            continue;
        }
        for (const char* p : groups[g])
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
    faces_.assign(paths_.size(), 0);
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
    const int offset = stbtt_GetFontOffsetForIndex(e.file.data(), faces_[index]);
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

    // No font in the chain has it: ask the system (fontconfig) once per codepoint.
    if (!system_lookup_ || system_misses_.count(cp) != 0)
        return -1;
    SystemFontMatch m;
    if (system_lookup_(cp, m) && !m.path.empty())
    {
        const size_t before = paths_.size();
        const int    idx    = add_font(m.path, m.face_index);
        // An already present font was searched above: it lacks the glyph.
        if (static_cast<size_t>(idx) >= before && ensure_loaded(static_cast<size_t>(idx))
            && stbtt_FindGlyphIndex(&entries_[static_cast<size_t>(idx)]->info, static_cast<int>(cp))
                   != 0)
            return idx;
    }
    system_misses_.insert(cp);
    return -1;
}

int FontFallbackChain::add_font(std::string path, int face_index)
{
    for (size_t i = 0; i < paths_.size(); ++i)
        if (paths_[i] == path && faces_[i] == face_index)
            return static_cast<int>(i);
    paths_.push_back(std::move(path));
    faces_.push_back(face_index);
    entries_.emplace_back();
    return static_cast<int>(paths_.size() - 1);
}

void FontFallbackChain::set_system_lookup(SystemFontLookup lookup)
{
    system_lookup_ = std::move(lookup);
    system_misses_.clear();
}

int FontFallbackChain::face_index(int index) const
{
    if (!font_data(index))
        return 0;
    return faces_[static_cast<size_t>(index)];
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
