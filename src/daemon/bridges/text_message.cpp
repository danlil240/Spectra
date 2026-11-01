#include "text_message.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace spectra::daemon
{

namespace
{

bool is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

std::string_view trim(std::string_view s)
{
    while (!s.empty() && is_ws(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && is_ws(s.back()))
        s.remove_suffix(1);
    return s;
}

// Parse a whole token as a finite double.  Accepts a leading '+'.
bool parse_number(std::string_view s, double& out)
{
    if (!s.empty() && s.front() == '+')
        s.remove_prefix(1);
    if (s.empty())
        return false;
    // strtod rather than <charconv>: Apple libc++ has no floating-point from_chars.
    // strtod also takes leading spaces and hex floats, which from_chars would reject.
    char buf[64];
    if (s.size() >= sizeof(buf) || is_ws(s.front()))
        return false;
    for (char c : s)
        if (c == 'x' || c == 'X')
            return false;
    std::memcpy(buf, s.data(), s.size());
    buf[s.size()] = '\0';
    char*        end = nullptr;
    const double v   = std::strtod(buf, &end);
    if (end != buf + s.size() || !std::isfinite(v))
        return false;
    out = v;
    return true;
}

void append_utf8(std::string& out, uint32_t cp)
{
    if (cp < 0x80)
    {
        out.push_back(static_cast<char>(cp));
    }
    else if (cp < 0x800)
    {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else if (cp < 0x10000)
    {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else
    {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Recursive-descent JSON reader that only keeps numeric / boolean leaves.
class JsonFlattener
{
   public:
    JsonFlattener(std::string_view text, size_t pos, std::vector<FlatField>& out)
        : text_(text), pos_(pos), out_(out)
    {
    }

    bool run(std::string* error)
    {
        std::string path;
        if (!value(path, 0))
        {
            if (error)
                *error = error_.empty() ? "invalid JSON" : error_;
            return false;
        }
        return true;
    }

    size_t pos() const { return pos_; }

   private:
    bool fail(const char* what)
    {
        if (error_.empty())
            error_ = std::string(what) + " at offset " + std::to_string(pos_);
        return false;
    }

    void skip_ws()
    {
        while (pos_ < text_.size() && is_ws(text_[pos_]))
            ++pos_;
    }

    bool eat(char c)
    {
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == c)
        {
            ++pos_;
            return true;
        }
        return false;
    }

    bool literal(std::string_view word)
    {
        if (text_.substr(pos_, word.size()) != word)
            return fail("invalid literal");
        pos_ += word.size();
        return true;
    }

    void emit(const std::string& path, double v)
    {
        if (out_.size() >= MAX_FIELDS_PER_MESSAGE)
            return;
        out_.push_back(FlatField{path, v});
    }

    bool hex4(uint32_t& cp)
    {
        if (pos_ + 4 > text_.size())
            return fail("truncated \\u escape");
        cp = 0;
        for (int i = 0; i < 4; ++i)
        {
            char     c = text_[pos_++];
            uint32_t d = 0;
            if (c >= '0' && c <= '9')
                d = static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f')
                d = static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                d = static_cast<uint32_t>(c - 'A' + 10);
            else
                return fail("bad \\u escape");
            cp = (cp << 4) | d;
        }
        return true;
    }

    // Reads a string (opening quote at pos_).  Decodes escapes into *out when
    // non-null; otherwise just skips it.
    bool string(std::string* out)
    {
        skip_ws();
        if (pos_ >= text_.size() || text_[pos_] != '"')
            return fail("expected string");
        ++pos_;
        while (pos_ < text_.size())
        {
            char c = text_[pos_++];
            if (c == '"')
                return true;
            if (static_cast<unsigned char>(c) < 0x20)
                return fail("control character in string");
            if (c != '\\')
            {
                if (out)
                    out->push_back(c);
                continue;
            }
            if (pos_ >= text_.size())
                break;
            char e       = text_[pos_++];
            char decoded = 0;
            switch (e)
            {
                case '"':
                case '\\':
                case '/':
                    decoded = e;
                    break;
                case 'b':
                    decoded = '\b';
                    break;
                case 'f':
                    decoded = '\f';
                    break;
                case 'n':
                    decoded = '\n';
                    break;
                case 'r':
                    decoded = '\r';
                    break;
                case 't':
                    decoded = '\t';
                    break;
                case 'u':
                {
                    uint32_t cp = 0;
                    if (!hex4(cp))
                        return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 6 <= text_.size()
                        && text_[pos_] == '\\' && text_[pos_ + 1] == 'u')
                    {
                        pos_ += 2;
                        uint32_t lo = 0;
                        if (!hex4(lo))
                            return false;
                        if (lo >= 0xDC00 && lo <= 0xDFFF)
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else
                            cp = 0xFFFD;
                    }
                    else if (cp >= 0xD800 && cp <= 0xDFFF)
                    {
                        cp = 0xFFFD;
                    }
                    if (out)
                        append_utf8(*out, cp);
                    continue;
                }
                default:
                    return fail("bad escape");
            }
            if (out)
                out->push_back(decoded);
        }
        return fail("unterminated string");
    }

    bool number(const std::string& path)
    {
        const size_t start = pos_;
        if (pos_ < text_.size() && text_[pos_] == '-')
            ++pos_;
        auto digits = [this]()
        {
            size_t n = 0;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9')
            {
                ++pos_;
                ++n;
            }
            return n;
        };
        if (digits() == 0)
            return fail("bad number");
        if (pos_ < text_.size() && text_[pos_] == '.')
        {
            ++pos_;
            if (digits() == 0)
                return fail("bad number");
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E'))
        {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-'))
                ++pos_;
            if (digits() == 0)
                return fail("bad number");
        }
        double v = 0.0;
        // Out-of-range (e.g. 1e999) is valid JSON; the field is just skipped.
        if (parse_number(text_.substr(start, pos_ - start), v))
            emit(path, v);
        return true;
    }

    static std::string child_path(const std::string& parent, std::string_view key)
    {
        if (key.empty())
            key = "_";
        if (parent.empty())
            return std::string(key);
        std::string p;
        p.reserve(parent.size() + 1 + key.size());
        p += parent;
        p += '/';
        p += key;
        return p;
    }

    bool value(const std::string& path, size_t depth)
    {
        if (depth > MAX_JSON_DEPTH)
            return fail("nesting too deep");
        skip_ws();
        if (pos_ >= text_.size())
            return fail("unexpected end of input");

        char c = text_[pos_];
        if (c == '{')
        {
            ++pos_;
            if (eat('}'))
                return true;
            do
            {
                std::string key;
                if (!string(&key))
                    return false;
                if (!eat(':'))
                    return fail("expected ':'");
                if (!value(child_path(path, key), depth + 1))
                    return false;
            } while (eat(','));
            if (!eat('}'))
                return fail("expected '}'");
            return true;
        }
        if (c == '[')
        {
            ++pos_;
            if (eat(']'))
                return true;
            size_t index = 0;
            do
            {
                if (!value(child_path(path, std::to_string(index++)), depth + 1))
                    return false;
            } while (eat(','));
            if (!eat(']'))
                return fail("expected ']'");
            return true;
        }
        if (c == '"')
            return string(nullptr);
        if (c == 't')
        {
            if (!literal("true"))
                return false;
            emit(path, 1.0);
            return true;
        }
        if (c == 'f')
        {
            if (!literal("false"))
                return false;
            emit(path, 0.0);
            return true;
        }
        if (c == 'n')
            return literal("null");
        if (c == '-' || (c >= '0' && c <= '9'))
            return number(path);
        return fail("unexpected character");
    }

    std::string_view        text_;
    size_t                  pos_;
    std::vector<FlatField>& out_;
    std::string             error_;
};

}   // namespace

bool flatten_json_value(std::string_view        text,
                        size_t*                 pos,
                        std::vector<FlatField>& out,
                        std::string*            error)
{
    const size_t  start = pos ? *pos : 0;
    const size_t  mark  = out.size();
    JsonFlattener f(text, start, out);
    if (!f.run(error))
    {
        out.resize(mark);
        return false;
    }
    if (pos)
        *pos = f.pos();
    return true;
}

bool flatten_json(std::string_view text, std::vector<FlatField>& out, std::string* error)
{
    size_t       pos  = 0;
    const size_t mark = out.size();
    if (!flatten_json_value(text, &pos, out, error))
        return false;
    if (!trim(text.substr(pos)).empty())
    {
        out.resize(mark);
        if (error)
            *error = "trailing data after JSON value";
        return false;
    }
    return true;
}

bool parse_csv_line(std::string_view line, std::vector<FlatField>& out)
{
    const size_t mark  = out.size();
    size_t       index = 0;
    while (true)
    {
        size_t           comma = line.find(',');
        std::string_view token = trim(line.substr(0, comma));
        if (!token.empty() && out.size() - mark < MAX_FIELDS_PER_MESSAGE)
        {
            double v  = 0.0;
            size_t eq = token.find('=');
            if (eq != std::string_view::npos)
            {
                std::string_view key = trim(token.substr(0, eq));
                if (!key.empty() && parse_number(trim(token.substr(eq + 1)), v))
                    out.push_back(FlatField{std::string(key), v});
            }
            else if (parse_number(token, v))
            {
                out.push_back(FlatField{std::to_string(index), v});
            }
        }
        ++index;
        if (comma == std::string_view::npos)
            break;
        line.remove_prefix(comma + 1);
    }
    return out.size() > mark;
}

size_t decode_text_datagram(std::string_view datagram, std::vector<std::vector<FlatField>>& out)
{
    size_t           errors = 0;
    std::string_view body   = trim(datagram);
    if (body.empty())
        return 0;

    if (body.front() == '{' || body.front() == '[')
    {
        size_t pos = 0;
        while (true)
        {
            while (pos < body.size() && is_ws(body[pos]))
                ++pos;
            if (pos >= body.size())
                break;
            std::vector<FlatField> fields;
            if (!flatten_json_value(body, &pos, fields, nullptr))
            {
                ++errors;
                break;   // cannot resynchronise inside a broken JSON stream
            }
            if (!fields.empty())
                out.push_back(std::move(fields));
        }
        return errors;
    }

    while (!body.empty())
    {
        size_t           nl   = body.find('\n');
        std::string_view line = trim(body.substr(0, nl));
        if (!line.empty())
        {
            std::vector<FlatField> fields;
            if (parse_csv_line(line, fields))
                out.push_back(std::move(fields));
            else
                ++errors;
        }
        if (nl == std::string_view::npos)
            break;
        body.remove_prefix(nl + 1);
    }
    return errors;
}

}   // namespace spectra::daemon
