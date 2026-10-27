// export_csv_writer.hpp — CSV conversion used by the export_csv example plugin.
//
// Header-only and free of Spectra dependencies so that it can be unit tested
// without loading the plug-in (tests/unit/test_export_csv_plugin.cpp).
//
// Input: the figure JSON handed to an export callback
// (SpectraExportContext::figure_json).  The writer looks for "series" arrays
// at any depth (for example {"series":[...]} or {"axes":[{"series":[...]}]}).
// Each series object may carry:
//
//   "label": "text"          column name (empty/missing -> "Series N")
//   "x":     [numbers...]    x values (missing -> 0, 1, 2, ... for each y)
//   "y":     [numbers...]    y values
//
// Whitespace and key order are irrelevant; unknown keys are ignored.
//
// Output (RFC 4180): one header row followed by numeric rows, with one x,y
// column pair per series.  Series of different lengths are padded with empty
// cells, and JSON null values become empty cells.  Fields containing commas,
// quotes, CR or LF are quoted, with embedded quotes doubled.  Lines end with
// CRLF.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace spectra_examples::csv
{

// ─── Minimal tolerant JSON reader ────────────────────────────────────────────

struct JsonMember;

struct JsonValue
{
    enum class Kind
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Kind                                           kind   = Kind::Null;
    bool                                           bool_v = false;
    double                                         num_v  = 0.0;
    std::string                                    str_v;
    std::vector<JsonValue>                         array_v;
    std::vector<JsonMember>                        object_v;

    // Returns the value for key in an object, or nullptr.
    const JsonValue* find(std::string_view key) const;
};

// Object member.  A named struct rather than std::pair<std::string, JsonValue>:
// std::pair's constructor traits need JsonValue to be complete, which libc++ and
// libstdc++ under clang reject inside the recursive definition.
struct JsonMember
{
    std::string key;
    JsonValue   value;
};

inline const JsonValue* JsonValue::find(std::string_view key) const
{
    if (kind != Kind::Object)
        return nullptr;
    for (const auto& [k, v] : object_v)
    {
        if (k == key)
            return &v;
    }
    return nullptr;
}

class JsonReader
{
   public:
    explicit JsonReader(std::string_view text) : text_(text) {}

    // Parses one complete JSON document.  Returns std::nullopt on syntax errors.
    std::optional<JsonValue> parse_document()
    {
        JsonValue v;
        if (!parse_value(v, 0))
            return std::nullopt;
        skip_ws();
        if (pos_ != text_.size())
            return std::nullopt;
        return v;
    }

   private:
    static constexpr int MAX_DEPTH = 256;

    void skip_ws()
    {
        while (pos_ < text_.size()
               && (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n'
                   || text_[pos_] == '\r'))
            ++pos_;
    }

    bool consume(char c)
    {
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == c)
        {
            ++pos_;
            return true;
        }
        return false;
    }

    bool consume_literal(std::string_view lit)
    {
        if (text_.substr(pos_, lit.size()) != lit)
            return false;
        pos_ += lit.size();
        return true;
    }

    bool parse_value(JsonValue& out, int depth)
    {
        if (depth > MAX_DEPTH)
            return false;
        skip_ws();
        if (pos_ >= text_.size())
            return false;
        const char c = text_[pos_];
        if (c == '{')
            return parse_object(out, depth);
        if (c == '[')
            return parse_array(out, depth);
        if (c == '"')
        {
            out.kind = JsonValue::Kind::String;
            return parse_string(out.str_v);
        }
        if (c == 't' && consume_literal("true"))
        {
            out.kind   = JsonValue::Kind::Bool;
            out.bool_v = true;
            return true;
        }
        if (c == 'f' && consume_literal("false"))
        {
            out.kind = JsonValue::Kind::Bool;
            return true;
        }
        if (c == 'n' && consume_literal("null"))
        {
            out.kind = JsonValue::Kind::Null;
            return true;
        }
        return parse_number(out);
    }

    static bool is_digit(char c) { return c >= '0' && c <= '9'; }

    // Scans a JSON number (-?digits[.digits][(e|E)[+-]digits]) and converts it
    // with the classic "C" locale.  std::from_chars for double is not available
    // in every standard library (e.g. Apple libc++), so it is not used here.
    bool parse_number(JsonValue& out)
    {
        const size_t start  = pos_;
        size_t       p      = pos_;
        auto         digits = [&]()
        {
            const size_t d0 = p;
            while (p < text_.size() && is_digit(text_[p]))
                ++p;
            return p > d0;
        };
        if (p < text_.size() && text_[p] == '-')
            ++p;
        if (!digits())
            return false;
        if (p < text_.size() && text_[p] == '.')
        {
            ++p;
            if (!digits())
                return false;
        }
        if (p < text_.size() && (text_[p] == 'e' || text_[p] == 'E'))
        {
            ++p;
            if (p < text_.size() && (text_[p] == '+' || text_[p] == '-'))
                ++p;
            if (!digits())
                return false;
        }

        std::istringstream in(std::string(text_.substr(start, p - start)));
        in.imbue(std::locale::classic());
        double v = 0.0;
        in >> v;
        if (in.fail())
            return false;
        pos_      = p;
        out.kind  = JsonValue::Kind::Number;
        out.num_v = v;
        return true;
    }

    static void append_utf8(std::string& s, uint32_t cp)
    {
        if (cp < 0x80)
        {
            s += static_cast<char>(cp);
        }
        else if (cp < 0x800)
        {
            s += static_cast<char>(0xC0 | (cp >> 6));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            s += static_cast<char>(0xE0 | (cp >> 12));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            s += static_cast<char>(0xF0 | (cp >> 18));
            s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool parse_hex4(uint32_t& cp)
    {
        if (pos_ + 4 > text_.size())
            return false;
        cp = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char h = text_[pos_++];
            cp <<= 4;
            if (h >= '0' && h <= '9')
                cp |= static_cast<uint32_t>(h - '0');
            else if (h >= 'a' && h <= 'f')
                cp |= static_cast<uint32_t>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F')
                cp |= static_cast<uint32_t>(h - 'A' + 10);
            else
                return false;
        }
        return true;
    }

    bool parse_string(std::string& out)
    {
        if (pos_ >= text_.size() || text_[pos_] != '"')
            return false;
        ++pos_;
        out.clear();
        while (pos_ < text_.size())
        {
            const char c = text_[pos_++];
            if (c == '"')
                return true;
            if (c != '\\')
            {
                out += c;
                continue;
            }
            if (pos_ >= text_.size())
                return false;
            const char e = text_[pos_++];
            switch (e)
            {
                case '"':
                    out += '"';
                    break;
                case '\\':
                    out += '\\';
                    break;
                case '/':
                    out += '/';
                    break;
                case 'b':
                    out += '\b';
                    break;
                case 'f':
                    out += '\f';
                    break;
                case 'n':
                    out += '\n';
                    break;
                case 'r':
                    out += '\r';
                    break;
                case 't':
                    out += '\t';
                    break;
                case 'u':
                {
                    uint32_t cp = 0;
                    if (!parse_hex4(cp))
                        return false;
                    // Combine UTF-16 surrogate pairs.
                    if (cp >= 0xD800 && cp <= 0xDBFF && text_.substr(pos_, 2) == "\\u")
                    {
                        const size_t save = pos_;
                        pos_ += 2;
                        uint32_t lo = 0;
                        if (parse_hex4(lo) && lo >= 0xDC00 && lo <= 0xDFFF)
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else
                            pos_ = save;
                    }
                    append_utf8(out, cp);
                    break;
                }
                default:
                    return false;
            }
        }
        return false;   // unterminated
    }

    bool parse_array(JsonValue& out, int depth)
    {
        ++pos_;   // '['
        out.kind = JsonValue::Kind::Array;
        if (consume(']'))
            return true;
        while (true)
        {
            JsonValue item;
            if (!parse_value(item, depth + 1))
                return false;
            out.array_v.push_back(std::move(item));
            if (consume(','))
                continue;
            return consume(']');
        }
    }

    bool parse_object(JsonValue& out, int depth)
    {
        ++pos_;   // '{'
        out.kind = JsonValue::Kind::Object;
        if (consume('}'))
            return true;
        while (true)
        {
            skip_ws();
            std::string key;
            if (!parse_string(key))
                return false;
            if (!consume(':'))
                return false;
            JsonValue value;
            if (!parse_value(value, depth + 1))
                return false;
            out.object_v.push_back(JsonMember{std::move(key), std::move(value)});
            if (consume(','))
                continue;
            return consume('}');
        }
    }

    std::string_view text_;
    size_t           pos_ = 0;
};

// ─── Series extraction ───────────────────────────────────────────────────────

struct SeriesColumns
{
    std::string                        label;
    std::vector<std::optional<double>> x;
    std::vector<std::optional<double>> y;
};

inline std::vector<std::optional<double>> numeric_array(const JsonValue* v)
{
    std::vector<std::optional<double>> out;
    if (!v || v->kind != JsonValue::Kind::Array)
        return out;
    out.reserve(v->array_v.size());
    for (const auto& item : v->array_v)
    {
        if (item.kind == JsonValue::Kind::Number)
            out.emplace_back(item.num_v);
        else
            out.emplace_back(std::nullopt);
    }
    return out;
}

inline void collect_series(const JsonValue& node, std::vector<SeriesColumns>& out)
{
    if (node.kind == JsonValue::Kind::Array)
    {
        for (const auto& item : node.array_v)
            collect_series(item, out);
        return;
    }
    if (node.kind != JsonValue::Kind::Object)
        return;

    for (const auto& [key, value] : node.object_v)
    {
        if (key == "series" && value.kind == JsonValue::Kind::Array)
        {
            for (const auto& s : value.array_v)
            {
                if (s.kind != JsonValue::Kind::Object)
                    continue;
                SeriesColumns col;
                if (const JsonValue* label = s.find("label");
                    label && label->kind == JsonValue::Kind::String)
                    col.label = label->str_v;
                col.x = numeric_array(s.find("x"));
                col.y = numeric_array(s.find("y"));
                if (!s.find("x"))
                {
                    for (size_t i = 0; i < col.y.size(); ++i)
                        col.x.emplace_back(static_cast<double>(i));
                }
                out.push_back(std::move(col));
            }
        }
        else
        {
            collect_series(value, out);
        }
    }
}

// ─── CSV formatting ──────────────────────────────────────────────────────────

// RFC 4180 field quoting.
inline void append_field(std::string& out, std::string_view field)
{
    const bool needs_quotes = field.find_first_of(",\"\r\n") != std::string_view::npos;
    if (!needs_quotes)
    {
        out += field;
        return;
    }
    out += '"';
    for (char c : field)
    {
        if (c == '"')
            out += '"';
        out += c;
    }
    out += '"';
}

inline void append_number(std::string& out, const std::optional<double>& v)
{
    if (!v)
        return;
    // Shortest decimal (up to 17 significant digits) that round-trips, using the
    // classic "C" locale.  Avoids std::to_chars(double), which older Apple
    // deployment targets do not provide.
    auto format = [](double value, int precision)
    {
        std::ostringstream os;
        os.imbue(std::locale::classic());
        os.precision(precision);
        os << value;
        return os.str();
    };
    int         precision = 1;
    std::string text;
    for (; precision <= 17; ++precision)
    {
        text = format(*v, precision);
        std::istringstream in(text);
        in.imbue(std::locale::classic());
        double back = 0.0;
        in >> back;
        if (!in.fail() && back == *v)
            break;
    }
    // Keep whole-number digits out of exponent form ("20", not "2e+01").
    if (std::isfinite(*v) && *v != 0.0)
    {
        const int int_digits = static_cast<int>(std::floor(std::log10(std::fabs(*v)))) + 1;
        if (int_digits > precision && int_digits <= 17)
            text = format(*v, int_digits);
    }
    out += text;
}

// Converts figure JSON into CSV text.  Returns false if the JSON is malformed.
inline bool figure_json_to_csv(std::string_view json, std::string& csv)
{
    csv.clear();
    JsonReader reader(json);
    auto       doc = reader.parse_document();
    if (!doc)
        return false;

    std::vector<SeriesColumns> series;
    collect_series(*doc, series);
    if (series.empty())
        return true;   // nothing to export: empty file

    for (size_t i = 0; i < series.size(); ++i)
    {
        std::string name =
            series[i].label.empty() ? "Series " + std::to_string(i + 1) : series[i].label;
        if (i > 0)
            csv += ',';
        append_field(csv, name + " x");
        csv += ',';
        append_field(csv, name + " y");
    }
    csv += "\r\n";

    size_t rows = 0;
    for (const auto& s : series)
    {
        rows = std::max(rows, std::max(s.x.size(), s.y.size()));
    }

    for (size_t r = 0; r < rows; ++r)
    {
        for (size_t i = 0; i < series.size(); ++i)
        {
            if (i > 0)
                csv += ',';
            if (r < series[i].x.size())
                append_number(csv, series[i].x[r]);
            csv += ',';
            if (r < series[i].y.size())
                append_number(csv, series[i].y[r]);
        }
        csv += "\r\n";
    }
    return true;
}

}   // namespace spectra_examples::csv
