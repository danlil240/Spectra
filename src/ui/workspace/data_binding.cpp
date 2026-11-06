#include "data_binding.hpp"

#include <cctype>

namespace spectra
{

namespace
{

bool needs_escape(unsigned char c)
{
    return c < 0x20 || c == 0x7f || c == '%' || c == '#' || c == '?' || c == '&' || c == '=';
}

std::string encode_component(std::string_view s)
{
    static constexpr char HEX[] = "0123456789ABCDEF";
    std::string           out;
    out.reserve(s.size());
    for (char ch : s)
    {
        const auto c = static_cast<unsigned char>(ch);
        if (needs_escape(c))
        {
            out += '%';
            out += HEX[c >> 4];
            out += HEX[c & 0x0f];
        }
        else
        {
            out += ch;
        }
    }
    return out;
}

int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// Percent-decodes @p s.  A '%' not followed by two hex digits is an error.
std::optional<std::string> decode_component(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] != '%')
        {
            out += s[i];
            continue;
        }
        if (i + 2 >= s.size())
            return std::nullopt;
        const int hi = hex_value(s[i + 1]);
        const int lo = hex_value(s[i + 2]);
        if (hi < 0 || lo < 0)
            return std::nullopt;
        out += static_cast<char>((hi << 4) | lo);
        i += 2;
    }
    return out;
}

bool iequals_prefix(std::string_view s, std::string_view prefix)
{
    if (s.size() < prefix.size())
        return false;
    for (size_t i = 0; i < prefix.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(s[i])) != prefix[i])
            return false;
    }
    return true;
}

std::optional<DataBinding> fail(std::string* error, std::string msg)
{
    if (error)
        *error = std::move(msg);
    return std::nullopt;
}

}   // namespace

const char* binding_scheme_name(BindingScheme scheme)
{
    switch (scheme)
    {
        case BindingScheme::Topic:
            return "topic";
        case BindingScheme::Csv:
            return "csv";
        case BindingScheme::Ulog:
            return "ulog";
    }
    return "topic";
}

std::optional<DataBinding> parse_data_binding(std::string_view uri, std::string* error)
{
    if (uri.empty())
        return fail(error, "empty binding");

    const auto sep = uri.find("://");
    if (sep == std::string_view::npos)
        return fail(error, "missing '://' in binding '" + std::string(uri) + "'");
    const std::string_view scheme = uri.substr(0, sep + 3);
    const std::string_view rest   = uri.substr(sep + 3);

    DataBinding b;
    if (iequals_prefix(scheme, "topic://") && scheme.size() == 8)
    {
        b.scheme  = BindingScheme::Topic;
        auto name = decode_component(rest);
        if (!name)
            return fail(error, "bad percent-encoding in topic name");
        if (name->empty())
            return fail(error, "topic binding has no topic name");
        b.source = std::move(*name);
        return b;
    }

    if (iequals_prefix(scheme, "csv://") && scheme.size() == 6)
    {
        b.scheme       = BindingScheme::Csv;
        const auto pos = rest.find('#');
        if (pos == std::string_view::npos)
            return fail(error, "CSV binding needs a column: csv://<file>#<column>");
        std::string_view path  = rest.substr(0, pos);
        std::string_view query = {};
        if (const auto q = path.find('?'); q != std::string_view::npos)
        {
            query = path.substr(q + 1);
            path  = path.substr(0, q);
        }
        auto file   = decode_component(path);
        auto column = decode_component(rest.substr(pos + 1));
        if (!file || !column)
            return fail(error, "bad percent-encoding in CSV binding");
        if (file->empty())
            return fail(error, "CSV binding has no file name");
        if (column->empty())
            return fail(error, "CSV binding has an empty column name");
        b.source = std::move(*file);
        b.field  = std::move(*column);
        // Query: key=value pairs separated by '&'.  Only "x" is defined;
        // unknown keys are ignored so newer files still open.
        while (!query.empty())
        {
            const auto       amp  = query.find('&');
            std::string_view pair = query.substr(0, amp);
            query = amp == std::string_view::npos ? std::string_view{} : query.substr(amp + 1);
            const auto eq = pair.find('=');
            if (eq == std::string_view::npos)
                continue;
            if (pair.substr(0, eq) == "x")
            {
                auto x = decode_component(pair.substr(eq + 1));
                if (!x)
                    return fail(error, "bad percent-encoding in CSV x column");
                b.x_field = std::move(*x);
            }
        }
        return b;
    }

    if (iequals_prefix(scheme, "ulog://") && scheme.size() == 7)
    {
        b.scheme       = BindingScheme::Ulog;
        const auto dot = rest.find('.');
        if (dot == std::string_view::npos)
            return fail(error, "ULog binding needs a field: ulog://<topic>.<field>");
        auto topic = decode_component(rest.substr(0, dot));
        auto field = decode_component(rest.substr(dot + 1));
        if (!topic || !field)
            return fail(error, "bad percent-encoding in ULog binding");
        if (topic->empty() || field->empty())
            return fail(error, "ULog binding needs a topic and a field");
        b.source = std::move(*topic);
        b.field  = std::move(*field);
        return b;
    }

    return fail(error, "unknown binding scheme '" + std::string(uri.substr(0, sep)) + "'");
}

std::string format_data_binding(const DataBinding& b)
{
    std::string out = binding_scheme_name(b.scheme);
    out += "://";
    switch (b.scheme)
    {
        case BindingScheme::Topic:
            out += encode_component(b.source);
            break;
        case BindingScheme::Csv:
            out += encode_component(b.source);
            if (!b.x_field.empty())
            {
                out += "?x=";
                out += encode_component(b.x_field);
            }
            out += '#';
            out += encode_component(b.field);
            break;
        case BindingScheme::Ulog:
        {
            // The first '.' separates topic and field, so a '.' in the topic
            // name is encoded; field paths keep theirs (e.g. "q.0").
            for (char c : encode_component(b.source))
            {
                if (c == '.')
                    out += "%2E";
                else
                    out += c;
            }
            out += '.';
            out += encode_component(b.field);
            break;
        }
    }
    return out;
}

std::string topic_binding_uri(std::string_view topic_name)
{
    DataBinding b;
    b.scheme = BindingScheme::Topic;
    b.source = std::string(topic_name);
    return format_data_binding(b);
}

std::string csv_binding_uri(std::string_view file_name,
                            std::string_view x_column,
                            std::string_view y_column)
{
    DataBinding b;
    b.scheme  = BindingScheme::Csv;
    b.source  = std::string(file_name);
    b.field   = std::string(y_column);
    b.x_field = std::string(x_column);
    return format_data_binding(b);
}

}   // namespace spectra
