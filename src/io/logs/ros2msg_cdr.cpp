#include "ros2msg_cdr.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstring>

namespace spectra::logs::ros2msg
{

namespace
{

constexpr int MAX_DEPTH = 32;

std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

bool is_separator(std::string_view line)
{
    line = trim(line);
    return line.size() >= 3
           && std::all_of(line.begin(), line.end(), [](char c) { return c == '='; });
}

bool parse_primitive(std::string_view base, Primitive& out)
{
    struct Entry
    {
        std::string_view name;
        Primitive        type;
    };
    static constexpr Entry TABLE[] = {
        {"bool", Primitive::Bool},
        {"byte", Primitive::Byte},
        {"char", Primitive::Char},
        {"int8", Primitive::Int8},
        {"uint8", Primitive::UInt8},
        {"int16", Primitive::Int16},
        {"uint16", Primitive::UInt16},
        {"int32", Primitive::Int32},
        {"uint32", Primitive::UInt32},
        {"int64", Primitive::Int64},
        {"uint64", Primitive::UInt64},
        {"float32", Primitive::Float32},
        {"float64", Primitive::Float64},
        {"string", Primitive::String},
        {"wstring", Primitive::WString},
    };
    // Bounded strings: "string<=10".
    if (auto le = base.find("<="); le != std::string_view::npos)
        base = base.substr(0, le);
    for (const auto& e : TABLE)
    {
        if (e.name == base)
        {
            out = e.type;
            return true;
        }
    }
    return false;
}

std::string package_of(std::string_view type)
{
    auto slash = type.find('/');
    return slash == std::string_view::npos ? std::string() : std::string(type.substr(0, slash));
}

// Parses one field line ("float64[3] x", "geometry_msgs/Point p", "int32 v 5").
// Returns false for blank lines and constants; sets *error on malformed lines.
bool parse_field_line(std::string_view raw,
                      std::string_view package,
                      FieldDef&        out,
                      std::string&     error)
{
    if (auto hash = raw.find('#'); hash != std::string_view::npos)
        raw = raw.substr(0, hash);
    std::string_view line = trim(raw);
    if (line.empty())
        return false;

    size_t ws = 0;
    while (ws < line.size() && !std::isspace(static_cast<unsigned char>(line[ws])))
        ++ws;
    std::string_view type = line.substr(0, ws);
    std::string_view rest = trim(line.substr(ws));
    if (rest.empty())
    {
        error = "missing field name in \"" + std::string(line) + "\"";
        return false;
    }

    size_t name_end = 0;
    while (name_end < rest.size() && !std::isspace(static_cast<unsigned char>(rest[name_end]))
           && rest[name_end] != '=')
        ++name_end;
    std::string_view name  = rest.substr(0, name_end);
    std::string_view after = trim(rest.substr(name_end));
    if (!after.empty() && after.front() == '=')
        return false;   // constant: "uint8 FOO=1"

    out           = FieldDef{};
    out.name      = std::string(name);
    out.type_name = std::string(type);

    std::string_view base = type;
    if (auto lb = type.find('['); lb != std::string_view::npos)
    {
        auto rb = type.find(']', lb);
        if (rb == std::string_view::npos)
        {
            error = "unterminated array in \"" + std::string(type) + "\"";
            return false;
        }
        base                  = type.substr(0, lb);
        std::string_view size = type.substr(lb + 1, rb - lb - 1);
        if (size.empty() || size.rfind("<=", 0) == 0)
        {
            out.array = ArrayKind::Sequence;
        }
        else
        {
            uint32_t n   = 0;
            auto     res = std::from_chars(size.data(), size.data() + size.size(), n);
            if (res.ec != std::errc() || res.ptr != size.data() + size.size())
            {
                error = "bad array size in \"" + std::string(type) + "\"";
                return false;
            }
            out.array      = ArrayKind::Fixed;
            out.array_size = n;
        }
    }

    if (parse_primitive(base, out.type))
        return true;

    out.type = Primitive::Message;
    if (base == "Header")
        out.msg_type = "std_msgs/Header";
    else if (base == "time")
        out.msg_type = "builtin_interfaces/Time";
    else if (base == "duration")
        out.msg_type = "builtin_interfaces/Duration";
    else if (base.find('/') != std::string_view::npos)
        out.msg_type = normalize_type_name(base);
    else
        out.msg_type = std::string(package) + "/" + std::string(base);
    return true;
}

// ─── CDR cursor ─────────────────────────────────────────────────────────────

class Cursor
{
   public:
    Cursor(const std::byte* data, size_t size, bool little) : data_(data), size_(size)
    {
        swap_ = little != (std::endian::native == std::endian::little);
    }

    bool align(size_t n)
    {
        n                = std::min<size_t>(n, 8);
        const size_t pad = (n - pos_ % n) % n;
        if (pos_ + pad > size_)
            return false;
        pos_ += pad;
        return true;
    }

    bool skip(size_t n)
    {
        if (n > size_ - pos_)
            return false;
        pos_ += n;
        return true;
    }

    template <typename T>
    bool read(T& v)
    {
        if (!align(sizeof(T)) || sizeof(T) > size_ - pos_)
            return false;
        unsigned char bytes[sizeof(T)];
        std::memcpy(bytes, data_ + pos_, sizeof(T));
        if (swap_)
            std::reverse(bytes, bytes + sizeof(T));
        std::memcpy(&v, bytes, sizeof(T));
        pos_ += sizeof(T);
        return true;
    }

   private:
    const std::byte* data_;
    size_t           size_;
    size_t           pos_  = 0;
    bool             swap_ = false;
};

size_t primitive_size(Primitive t)
{
    switch (t)
    {
        case Primitive::Bool:
        case Primitive::Byte:
        case Primitive::Char:
        case Primitive::Int8:
        case Primitive::UInt8:
            return 1;
        case Primitive::Int16:
        case Primitive::UInt16:
            return 2;
        case Primitive::Int32:
        case Primitive::UInt32:
        case Primitive::Float32:
            return 4;
        case Primitive::Int64:
        case Primitive::UInt64:
        case Primitive::Float64:
            return 8;
        case Primitive::String:
        case Primitive::WString:
        case Primitive::Message:
            break;
    }
    return 0;
}

bool read_number(Cursor& c, Primitive t, double& v)
{
    switch (t)
    {
        case Primitive::Bool:
        case Primitive::Byte:
        case Primitive::Char:
        case Primitive::UInt8:
        {
            uint8_t x = 0;
            if (!c.read(x))
                return false;
            v = x;
            return true;
        }
        case Primitive::Int8:
        {
            int8_t x = 0;
            if (!c.read(x))
                return false;
            v = x;
            return true;
        }
        case Primitive::Int16:
        {
            int16_t x = 0;
            if (!c.read(x))
                return false;
            v = x;
            return true;
        }
        case Primitive::UInt16:
        {
            uint16_t x = 0;
            if (!c.read(x))
                return false;
            v = x;
            return true;
        }
        case Primitive::Int32:
        {
            int32_t x = 0;
            if (!c.read(x))
                return false;
            v = x;
            return true;
        }
        case Primitive::UInt32:
        {
            uint32_t x = 0;
            if (!c.read(x))
                return false;
            v = x;
            return true;
        }
        case Primitive::Int64:
        {
            int64_t x = 0;
            if (!c.read(x))
                return false;
            v = static_cast<double>(x);
            return true;
        }
        case Primitive::UInt64:
        {
            uint64_t x = 0;
            if (!c.read(x))
                return false;
            v = static_cast<double>(x);
            return true;
        }
        case Primitive::Float32:
        {
            float x = 0;
            if (!c.read(x))
                return false;
            v = x;
            return true;
        }
        case Primitive::Float64:
            return c.read(v);
        case Primitive::String:
        case Primitive::WString:
        case Primitive::Message:
            break;
    }
    return false;
}

class Decoder
{
   public:
    Decoder(const Schema& schema, Cursor& cursor, const LeafFn& on_leaf, const DecodeOptions& opt)
        : schema_(schema), cursor_(cursor), on_leaf_(on_leaf), opt_(opt)
    {
    }

    bool message(const MessageDef& def, bool emit, int depth)
    {
        if (depth > MAX_DEPTH)
            return fail("message nesting too deep");
        for (const auto& f : def.fields)
        {
            const size_t mark = path_.size();
            if (!path_.empty())
                path_ += '.';
            path_ += f.name;
            const bool ok = field(f, emit, depth);
            path_.resize(mark);
            if (!ok)
                return false;
        }
        return true;
    }

    const std::string& error() const { return error_; }

   private:
    bool fail(std::string msg)
    {
        if (error_.empty())
            error_ = std::move(msg);
        return false;
    }

    bool field(const FieldDef& f, bool emit, int depth)
    {
        if (f.type == Primitive::WString)
            return fail("wstring field \"" + f.name + "\" is not supported");

        const MessageDef* nested = nullptr;
        if (f.type == Primitive::Message)
        {
            nested = schema_.find(f.msg_type);
            if (!nested)
                return fail("unknown message type " + f.msg_type);
        }

        if (f.array == ArrayKind::None)
            return element(f, nested, emit, depth);

        uint32_t count = f.array_size;
        if (f.array == ArrayKind::Sequence && !cursor_.read(count))
            return fail("truncated sequence length");

        const bool emit_items = emit && count <= opt_.max_array_elements;
        // Fast path: skipped arrays of fixed-size primitives.
        if (!emit_items && primitive_size(f.type) > 0)
        {
            const size_t sz = primitive_size(f.type);
            if (count > 0 && (!cursor_.align(sz) || !cursor_.skip(sz * count)))
                return fail("truncated array");
            return true;
        }

        const size_t mark = path_.size();
        for (uint32_t i = 0; i < count; ++i)
        {
            path_ += '[';
            path_ += std::to_string(i);
            path_ += ']';
            const bool ok = element(f, nested, emit_items, depth);
            path_.resize(mark);
            if (!ok)
                return false;
        }
        return true;
    }

    bool element(const FieldDef& f, const MessageDef* nested, bool emit, int depth)
    {
        if (nested)
            return message(*nested, emit, depth + 1);

        if (f.type == Primitive::String)
        {
            uint32_t len = 0;
            if (!cursor_.read(len) || !cursor_.skip(len))
                return fail("truncated string");
            return true;
        }

        double v = 0.0;
        if (!read_number(cursor_, f.type, v))
            return fail("truncated value for \"" + f.name + "\"");
        if (emit && on_leaf_)
            on_leaf_(path_, v, f);
        return true;
    }

    const Schema&        schema_;
    Cursor&              cursor_;
    const LeafFn&        on_leaf_;
    const DecodeOptions& opt_;
    std::string          path_;
    std::string          error_;
};

}   // namespace

std::string normalize_type_name(std::string_view name)
{
    name = trim(name);
    std::string out(name);
    for (std::string_view mid : {"/msg/", "/srv/", "/action/"})
    {
        if (auto pos = out.find(mid); pos != std::string::npos)
        {
            out.erase(pos, mid.size() - 1);
            break;
        }
    }
    return out;
}

bool Schema::parse(std::string_view root_type, std::string_view text, std::string* error)
{
    defs_.clear();
    by_name_.clear();

    auto set_error = [error](std::string msg)
    {
        if (error)
            *error = std::move(msg);
        return false;
    };

    defs_.push_back(MessageDef{normalize_type_name(root_type), {}});
    MessageDef* current     = &defs_.back();
    bool        want_header = false;

    size_t start = 0;
    while (start <= text.size())
    {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos)
            end = text.size();
        std::string_view line = text.substr(start, end - start);
        start                 = end + 1;

        if (is_separator(line))
        {
            want_header = true;
            continue;
        }
        if (want_header)
        {
            std::string_view t = trim(line);
            if (t.empty())
                continue;
            if (t.rfind("MSG:", 0) != 0)
                return set_error("expected \"MSG: <type>\" after separator");
            defs_.push_back(MessageDef{normalize_type_name(t.substr(4)), {}});
            current     = &defs_.back();
            want_header = false;
            continue;
        }

        FieldDef    f;
        std::string line_error;
        if (parse_field_line(line, package_of(current->name), f, line_error))
            current->fields.push_back(std::move(f));
        else if (!line_error.empty())
            return set_error(current->name + ": " + line_error);
    }

    for (size_t i = 0; i < defs_.size(); ++i)
        by_name_.emplace(defs_[i].name, i);

    // ROS 1 style builtins, when the schema does not define them.
    for (const char* builtin : {"builtin_interfaces/Time", "builtin_interfaces/Duration"})
    {
        if (by_name_.count(builtin))
            continue;
        MessageDef def{builtin, {}};
        FieldDef   sec;
        sec.name      = "sec";
        sec.type      = Primitive::Int32;
        sec.type_name = "int32";
        FieldDef nsec;
        nsec.name      = "nanosec";
        nsec.type      = Primitive::UInt32;
        nsec.type_name = "uint32";
        def.fields     = {sec, nsec};
        by_name_.emplace(def.name, defs_.size());
        defs_.push_back(std::move(def));
    }
    return true;
}

const MessageDef* Schema::root() const
{
    return defs_.empty() ? nullptr : &defs_.front();
}

const MessageDef* Schema::find(std::string_view name) const
{
    const std::string norm = normalize_type_name(name);
    if (auto it = by_name_.find(norm); it != by_name_.end())
        return &defs_[it->second];

    // Unique match on the short name, for schemas that qualify differently.
    const auto        slash      = norm.rfind('/');
    const std::string short_name = slash == std::string::npos ? norm : norm.substr(slash + 1);
    const MessageDef* match      = nullptr;
    for (const auto& d : defs_)
    {
        const auto s = d.name.rfind('/');
        if ((s == std::string::npos ? d.name : d.name.substr(s + 1)) == short_name)
        {
            if (match)
                return nullptr;   // ambiguous
            match = &d;
        }
    }
    return match;
}

bool decode_cdr(const Schema&              schema,
                std::span<const std::byte> data,
                const LeafFn&              on_leaf,
                const DecodeOptions&       options,
                std::string*               error)
{
    auto set_error = [error](std::string msg)
    {
        if (error)
            *error = std::move(msg);
        return false;
    };

    const MessageDef* root = schema.root();
    if (!root)
        return set_error("empty schema");
    if (data.size() < 4)
        return set_error("message shorter than the CDR header");

    // Encapsulation id, big endian: 0x0000 CDR_BE, 0x0001 CDR_LE.
    const unsigned id =
        (std::to_integer<unsigned>(data[0]) << 8) | std::to_integer<unsigned>(data[1]);
    if (id > 0x0001)
    {
        char hex[8];
        std::snprintf(hex, sizeof(hex), "%04x", id);
        return set_error("unsupported CDR encapsulation 0x" + std::string(hex)
                         + " (only XCDR1 is supported)");
    }

    Cursor  cursor(data.data() + 4, data.size() - 4, id == 0x0001);
    Decoder decoder(schema, cursor, on_leaf, options);
    if (!decoder.message(*root, true, 0))
        return set_error(decoder.error());
    return true;
}

}   // namespace spectra::logs::ros2msg
