#include "mathtext.hpp"

#include <algorithm>
#include <cmath>

namespace spectra::mathtext
{

namespace
{

struct Symbol
{
    std::string_view name;
    std::string_view utf8;
};

// Command -> Unicode.  Greek follows matplotlib (\phi is U+03D5, \varphi U+03C6,
// \epsilon U+03F5, \varepsilon U+03B5).  Glyphs the embedded Inter lacks (marked *) are
// taken from a system fallback font by the GPU renderer.
constexpr Symbol SYMBOLS[] = {
    // Greek small letters
    {"alpha", "α"},
    {"beta", "β"},
    {"gamma", "γ"},
    {"delta", "δ"},
    {"epsilon", "ϵ"},
    {"varepsilon", "ε"},
    {"zeta", "ζ"},
    {"eta", "η"},
    {"theta", "θ"},
    {"vartheta", "ϑ"},
    {"iota", "ι"},
    {"kappa", "κ"},
    {"lambda", "λ"},
    {"mu", "μ"},
    {"nu", "ν"},
    {"xi", "ξ"},
    {"omicron", "ο"},
    {"pi", "π"},
    {"varpi", "ϖ"},
    {"rho", "ρ"},
    {"varrho", "ϱ"},
    {"sigma", "σ"},
    {"varsigma", "ς"},
    {"tau", "τ"},
    {"upsilon", "υ"},
    {"phi", "ϕ"},
    {"varphi", "φ"},
    {"chi", "χ"},
    {"psi", "ψ"},
    {"omega", "ω"},
    // Greek capital letters
    {"Alpha", "Α"},
    {"Beta", "Β"},
    {"Gamma", "Γ"},
    {"Delta", "Δ"},
    {"Epsilon", "Ε"},
    {"Zeta", "Ζ"},
    {"Eta", "Η"},
    {"Theta", "Θ"},
    {"Iota", "Ι"},
    {"Kappa", "Κ"},
    {"Lambda", "Λ"},
    {"Mu", "Μ"},
    {"Nu", "Ν"},
    {"Xi", "Ξ"},
    {"Omicron", "Ο"},
    {"Pi", "Π"},
    {"Rho", "Ρ"},
    {"Sigma", "Σ"},
    {"Tau", "Τ"},
    {"Upsilon", "Υ"},
    {"Phi", "Φ"},
    {"Chi", "Χ"},
    {"Psi", "Ψ"},
    {"Omega", "Ω"},
    // Binary operators and relations
    {"pm", "±"},
    {"mp", "∓"},   // *
    {"times", "×"},
    {"div", "÷"},
    {"cdot", "·"},   // middle dot (Inter lacks U+22C5)
    {"ast", "∗"},    // *
    {"star", "⋆"},   // *
    {"bullet", "•"},
    {"circ", "∘"},   // *
    {"approx", "≈"},
    {"neq", "≠"},
    {"ne", "≠"},
    {"leq", "≤"},
    {"le", "≤"},
    {"geq", "≥"},
    {"ge", "≥"},
    {"ll", "≪"},         // *
    {"gg", "≫"},         // *
    {"equiv", "≡"},      // *
    {"sim", "∼"},        // *
    {"simeq", "≃"},      // *
    {"propto", "∝"},     // *
    {"in", "∈"},         // *
    {"cap", "∩"},        // *
    {"cup", "∪"},        // *
    {"perp", "⊥"},       // *
    {"parallel", "∥"},   // *
    {"wedge", "∧"},      // *
    {"vee", "∨"},        // *
    {"neg", "¬"},
    // Arrows
    {"rightarrow", "→"},
    {"to", "→"},
    {"leftarrow", "←"},
    {"gets", "←"},
    {"uparrow", "↑"},
    {"downarrow", "↓"},
    {"leftrightarrow", "↔"},
    {"Rightarrow", "⇒"},
    {"Leftarrow", "⇐"},
    {"Leftrightarrow", "⇔"},
    // Miscellaneous symbols and large operators
    {"degree", "°"},
    {"infty", "∞"},
    {"partial", "∂"},
    {"nabla", "∇"},   // *
    {"sum", "∑"},
    {"prod", "∏"},
    {"int", "∫"},
    {"sqrt", "√"},
    {"forall", "∀"},   // *
    {"exists", "∃"},   // *
    {"emptyset", "∅"},
    {"hbar", "ℏ"},   // *
    {"ell", "ℓ"},
    {"prime", "′"},
    {"AA", "Å"},
    {"angstrom", "Å"},
    {"ldots", "…"},
    {"dots", "…"},
    {"cdots", "⋯"},    // *
    {"langle", "⟨"},   // *
    {"rangle", "⟩"},   // *
    {"vert", "|"},
    {"|", "‖"},
    // Spacing
    {"quad", " "},
    {"qquad", "  "},
};

// Function names set upright as their own text (\sin -> "sin").
constexpr std::string_view OPERATOR_NAMES[] = {
    "sin",  "cos",  "tan",  "cot", "sec", "csc", "arcsin", "arccos", "arctan", "sinh",
    "cosh", "tanh", "coth", "exp", "log", "ln",  "lg",     "max",    "min",    "sup",
    "inf",  "lim",  "det",  "arg", "deg", "dim", "gcd",    "ker",    "Pr",     "sgn",
};

std::string_view find_operator_name(std::string_view name)
{
    for (std::string_view op : OPERATOR_NAMES)
        if (op == name)
            return op;
    return {};
}

const Symbol* find_symbol(std::string_view name)
{
    for (const Symbol& s : SYMBOLS)
        if (s.name == name)
            return &s;
    return nullptr;
}

bool is_letter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// Byte length of the UTF-8 sequence starting at s[i] (1 for malformed input).
size_t utf8_len(std::string_view s, size_t i)
{
    const auto b0  = static_cast<unsigned char>(s[i]);
    size_t     len = 1;
    if ((b0 & 0xE0u) == 0xC0u)
        len = 2;
    else if ((b0 & 0xF0u) == 0xE0u)
        len = 3;
    else if ((b0 & 0xF8u) == 0xF0u)
        len = 4;
    if (i + len > s.size())
        return 1;
    for (size_t k = 1; k < len; ++k)
        if ((static_cast<unsigned char>(s[i + k]) & 0xC0u) != 0x80u)
            return 1;
    return len;
}

// Byte offsets of the unescaped '$' in `text`.
std::vector<size_t> dollar_positions(std::string_view text)
{
    std::vector<size_t> pos;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '\\' && i + 1 < text.size())
        {
            ++i;   // skip the escaped character (handles "\$" and "\\")
            continue;
        }
        if (text[i] == '$')
            pos.push_back(i);
    }
    return pos;
}

// Plain text: only "\$" is unescaped.
void append_plain(std::string& out, std::string_view text)
{
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == '$')
        {
            out += '$';
            ++i;
            continue;
        }
        out += text[i];
    }
}

// Recursive-descent parser for one math segment.
class MathParser
{
   public:
    MathParser(std::string_view src, std::vector<Run>& out) : s_(src), out_(out) {}

    // Parses the whole segment at the base size.
    void parse_all()
    {
        while (pos_ < s_.size())
            parse_item(1.0f, 0.0f);
    }

   private:
    std::string_view  s_;
    std::vector<Run>& out_;
    size_t            pos_ = 0;

    void emit(std::string_view text, float scale, float shift)
    {
        if (text.empty())
            return;
        if (!out_.empty())
        {
            Run& last = out_.back();
            if (last.kind == Run::Kind::Text && last.scale == scale && last.shift == shift)
            {
                last.text.append(text);
                return;
            }
        }
        Run r;
        r.text  = std::string(text);
        r.scale = scale;
        r.shift = shift;
        out_.push_back(std::move(r));
    }

    void marker(Run::Kind kind)
    {
        Run r;
        r.kind = kind;
        out_.push_back(std::move(r));
    }

    // Parses a brace group whose '{' was already consumed, up to the matching '}'
    // (or the end of the segment when it is unbalanced).
    void parse_group(float scale, float shift)
    {
        while (pos_ < s_.size())
        {
            if (s_[pos_] == '}')
            {
                ++pos_;
                return;
            }
            parse_item(scale, shift);
        }
    }

    // \text{...}: literal characters (escapes still apply), nested braces kept balanced.
    void parse_text_group(float scale, float shift)
    {
        std::string text;
        int         depth = 0;
        while (pos_ < s_.size())
        {
            const char c = s_[pos_];
            if (c == '\\' && pos_ + 1 < s_.size())
            {
                const char n = s_[pos_ + 1];
                if (n == '$' || n == '{' || n == '}' || n == '_' || n == '^' || n == '#' || n == '%'
                    || n == '&' || n == '\\')
                {
                    text += n;
                    pos_ += 2;
                    continue;
                }
            }
            if (c == '{')
                ++depth;
            else if (c == '}')
            {
                if (depth == 0)
                {
                    ++pos_;
                    break;
                }
                --depth;
            }
            text += c;
            ++pos_;
        }
        emit(text, scale, shift);
    }

    // One "atom" (the operand of _ or ^, or of \mathrm without braces).  Returns false
    // when there is none (end of segment or a closing brace).
    bool parse_atom(float scale, float shift)
    {
        if (pos_ >= s_.size() || s_[pos_] == '}')
            return false;
        if (s_[pos_] == '{')
        {
            ++pos_;
            parse_group(scale, shift);
            return true;
        }
        if (s_[pos_] == '\\')
        {
            parse_command(scale, shift);
            return true;
        }
        parse_char(scale, shift);
        return true;
    }

    void parse_char(float scale, float shift)
    {
        const char c = s_[pos_];
        if (c == '-')
        {
            emit("−", scale, shift);
            ++pos_;
            return;
        }
        const size_t n = utf8_len(s_, pos_);
        emit(s_.substr(pos_, n), scale, shift);
        pos_ += n;
    }

    // At a backslash.
    void parse_command(float scale, float shift)
    {
        const size_t start = pos_;
        ++pos_;   // '\'
        if (pos_ >= s_.size())
        {
            emit("\\", scale, shift);
            return;
        }
        if (!is_letter(s_[pos_]))
        {
            const char c = s_[pos_];
            ++pos_;
            switch (c)
            {
                case '$':
                case '{':
                case '}':
                case '_':
                case '^':
                case '#':
                case '%':
                case '&':
                case '\\':
                    emit(std::string_view(&c, 1), scale, shift);
                    return;
                case ',':
                    emit(" ", scale, shift);   // thin space
                    return;
                case ':':
                case ';':
                case ' ':
                    emit(" ", scale, shift);
                    return;
                case '!':
                    return;   // negative thin space: approximated by nothing
                case '|':
                    emit("‖", scale, shift);
                    return;
                default:
                    // Unknown: keep literally.
                    pos_ = start + 1;
                    emit("\\", scale, shift);
                    return;
            }
        }
        size_t end = pos_;
        while (end < s_.size() && is_letter(s_[end]))
            ++end;
        const std::string_view name = s_.substr(pos_, end - pos_);
        pos_                        = end;

        if (name == "mathrm" || name == "text" || name == "textrm" || name == "mathregular")
        {
            while (pos_ < s_.size() && s_[pos_] == ' ')
                ++pos_;
            if (name != "mathrm" && name != "mathregular" && pos_ < s_.size() && s_[pos_] == '{')
            {
                ++pos_;
                parse_text_group(scale, shift);
                return;
            }
            parse_atom(scale, shift);
            return;
        }
        if (const Symbol* sym = find_symbol(name))
        {
            emit(sym->utf8, scale, shift);
            return;
        }
        if (const std::string_view op = find_operator_name(name); !op.empty())
        {
            emit(op, scale, shift);
            return;
        }
        // Unknown command: keep it literally.
        emit(s_.substr(start, end - start), scale, shift);
    }

    // Parses one script operand after '_' (sub) or '^' (sup) at the parent's size.
    bool parse_script(bool sup, float scale, float shift)
    {
        const float script_scale = std::max(scale * SCRIPT_SCALE, MIN_SCRIPT_SCALE);
        const float script_shift =
            sup ? shift + SUPERSCRIPT_RISE * scale : shift - SUBSCRIPT_DROP * scale;
        return parse_atom(script_scale, script_shift);
    }

    // One item of a sequence: a character, command, group or script.
    void parse_item(float scale, float shift)
    {
        const char c = s_[pos_];
        if (c == '}')
        {
            ++pos_;   // stray closing brace (a group consumes its own): dropped
            return;
        }
        if (c == '{')
        {
            ++pos_;
            parse_group(scale, shift);
            return;
        }
        if (c == '\\')
        {
            parse_command(scale, shift);
            return;
        }
        if (c == '_' || c == '^')
        {
            const bool   sup   = c == '^';
            const size_t op    = pos_;
            const size_t begin = out_.size();
            ++pos_;
            marker(Run::Kind::StackBegin);
            if (!parse_script(sup, scale, shift))
            {
                // Nothing to attach: keep the operator literally.
                out_.resize(begin);
                pos_ = op + 1;
                emit(std::string_view(&c, 1), scale, shift);
                return;
            }
            const char other = sup ? '_' : '^';
            if (pos_ < s_.size() && s_[pos_] == other)
            {
                const size_t mid = out_.size();
                ++pos_;
                marker(Run::Kind::StackRewind);
                if (parse_script(!sup, scale, shift))
                {
                    marker(Run::Kind::StackEnd);
                    return;
                }
                out_.resize(mid);
                emit(std::string_view(&other, 1), scale, shift);
            }
            // A single script needs no stack: drop the StackBegin marker.
            out_.erase(out_.begin() + static_cast<std::ptrdiff_t>(begin));
            return;
        }
        parse_char(scale, shift);
    }
};

}   // namespace

bool is_math_text(std::string_view text)
{
    if (!has_markup(text))
        return false;
    const size_t n = dollar_positions(text).size();
    return n > 0 && n % 2 == 0;
}

std::vector<Run> parse(std::string_view text)
{
    std::vector<Run> runs;
    if (!has_markup(text))
    {
        if (!text.empty())
            runs.push_back(Run{Run::Kind::Text, std::string(text)});
        return runs;
    }
    const std::vector<size_t> dollars = dollar_positions(text);
    if (dollars.empty() || dollars.size() % 2 != 0)
    {
        Run r;
        append_plain(r.text, text);
        if (!r.text.empty())
            runs.push_back(std::move(r));
        return runs;
    }

    auto plain = [&](std::string_view seg)
    {
        std::string s;
        append_plain(s, seg);
        if (s.empty())
            return;
        if (!runs.empty() && runs.back().kind == Run::Kind::Text && runs.back().scale == 1.0f
            && runs.back().shift == 0.0f)
            runs.back().text += s;
        else
            runs.push_back(Run{Run::Kind::Text, std::move(s)});
    };

    size_t cursor = 0;
    for (size_t k = 0; k < dollars.size(); k += 2)
    {
        plain(text.substr(cursor, dollars[k] - cursor));
        const size_t begin = dollars[k] + 1;
        const size_t end   = dollars[k + 1];
        MathParser(text.substr(begin, end - begin), runs).parse_all();
        cursor = end + 1;
    }
    plain(text.substr(cursor));
    return runs;
}

std::string to_plain_text(std::string_view text)
{
    std::string out;
    for (const Run& r : parse(text))
        out += r.text;
    return out;
}

float layout(std::vector<Run>& runs, const AdvanceFn& advance)
{
    struct Stack
    {
        float start     = 0.0f;
        float first_end = 0.0f;
    };
    std::vector<Stack> stack;
    float              x     = 0.0f;
    float              max_x = 0.0f;
    for (Run& r : runs)
    {
        r.x = x;
        switch (r.kind)
        {
            case Run::Kind::Text:
                if (advance && !r.text.empty())
                    x += advance(r.text, r.scale);
                break;
            case Run::Kind::StackBegin:
                stack.push_back({x, x});
                break;
            case Run::Kind::StackRewind:
                if (!stack.empty())
                {
                    stack.back().first_end = x;
                    x                      = stack.back().start;
                }
                break;
            case Run::Kind::StackEnd:
                if (!stack.empty())
                {
                    x = std::max(x, stack.back().first_end);
                    stack.pop_back();
                }
                break;
        }
        max_x = std::max(max_x, x);
    }
    return max_x;
}

Overhang overhang(const std::vector<Run>& runs, float font_px, float ascent_px, float descent_px)
{
    Overhang o;
    for (const Run& r : runs)
    {
        if (r.kind != Run::Kind::Text || r.text.empty())
            continue;
        const float rise = r.shift * font_px;
        o.above          = std::max(o.above, rise + r.scale * ascent_px - ascent_px);
        o.below          = std::max(o.below, -rise + r.scale * descent_px - descent_px);
    }
    return o;
}

void close_truncated(std::string& prefix)
{
    if (dollar_positions(prefix).size() % 2 == 0)
        return;   // the cut is outside math: nothing is left open
    // Drop a command name cut short ("\alp"), then dangling operators.
    const size_t bs = prefix.rfind('\\');
    if (bs != std::string::npos && bs + 1 < prefix.size())
    {
        const std::string_view tail = std::string_view(prefix).substr(bs + 1);
        if (std::all_of(tail.begin(), tail.end(), is_letter) && !find_symbol(tail)
            && find_operator_name(tail).empty())
            prefix.resize(bs);
    }
    while (!prefix.empty())
    {
        const char c = prefix.back();
        if (c != '\\' && c != '_' && c != '^' && c != '{' && c != ' ')
            break;
        // Keep an escaped character ("\_") intact.
        if (prefix.size() >= 2 && prefix[prefix.size() - 2] == '\\' && c != '\\')
            break;
        prefix.pop_back();
    }
    const size_t n = prefix.size();
    if (n > 0 && prefix[n - 1] == '$' && (n < 2 || prefix[n - 2] != '\\'))
        prefix.pop_back();   // empty math segment: drop the opening '$'
    else
        prefix += '$';
}

}   // namespace spectra::mathtext
