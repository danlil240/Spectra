// ExpressionEngine — implementation.
//
// Recursive-descent parser for a simple arithmetic language with ROS2
// field-variable references ($topic.field.path).

#include "expression_engine.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <limits>

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// AST node
// ---------------------------------------------------------------------------

enum class NodeKind
{
    Number,      // literal double
    Variable,    // $topic.field
    Constant,    // named constant: pi, e
    BinOp,       // +, -, *, /, ^
    UnaryMinus,  // -expr
    Function1,   // f(arg)
    Function2,   // f(arg, arg)
};

enum class BinOpKind  { Add, Sub, Mul, Div, Pow };
enum class Func1Kind  { Sqrt, Abs, Sin, Cos, Tan, Asin, Acos, Atan, Log, Log10, Exp, Floor, Ceil, Round };
enum class Func2Kind  { Atan2 };

struct ExpressionEngine::AstNode
{
    NodeKind kind;

    // Number
    double number{0.0};

    // Variable / Constant name
    std::string name;

    // BinOp
    BinOpKind  binop{BinOpKind::Add};

    // Function
    Func1Kind  func1{Func1Kind::Sqrt};
    Func2Kind  func2{Func2Kind::Atan2};

    // Children
    std::unique_ptr<AstNode> left;
    std::unique_ptr<AstNode> right;
};

// ---------------------------------------------------------------------------
// ExpressionPreset serialization
// ---------------------------------------------------------------------------

// Minimal JSON helpers (no external dependency).

static std::string json_escape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s)
    {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else                out += c;
    }
    return out;
}

static std::string json_unescape(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '\\' && i + 1 < s.size())
        {
            ++i;
            switch (s[i])
            {
                case '"':  out += '"'; break;
                case '\\': out += '\\'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                default:   out += s[i]; break;
            }
        }
        else
        {
            out += s[i];
        }
    }
    return out;
}

// Extract the value of a JSON string field named `key` from a flat JSON object string.
static std::string json_get_string(const std::string& json, const std::string& key)
{
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos)
        return {};
    pos += search.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':'))
        ++pos;
    if (pos >= json.size() || json[pos] != '"')
        return {};
    ++pos;  // skip opening quote
    std::string val;
    while (pos < json.size() && json[pos] != '"')
    {
        if (json[pos] == '\\' && pos + 1 < json.size())
        {
            val += '\\';
            val += json[pos + 1];
            pos += 2;
        }
        else
        {
            val += json[pos++];
        }
    }
    return json_unescape(val);
}

// Extract a JSON array of strings for a field named `key`.
static std::vector<std::string> json_get_string_array(const std::string& json, const std::string& key)
{
    std::vector<std::string> out;
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos)
        return out;
    pos += search.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':'))
        ++pos;
    if (pos >= json.size() || json[pos] != '[')
        return out;
    ++pos;  // skip '['
    while (pos < json.size() && json[pos] != ']')
    {
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == ','))
            ++pos;
        if (pos >= json.size() || json[pos] == ']')
            break;
        if (json[pos] == '"')
        {
            ++pos;
            std::string s;
            while (pos < json.size() && json[pos] != '"')
            {
                if (json[pos] == '\\' && pos + 1 < json.size())
                {
                    s += '\\';
                    s += json[pos + 1];
                    pos += 2;
                }
                else
                {
                    s += json[pos++];
                }
            }
            if (pos < json.size())
                ++pos;  // skip closing quote
            out.push_back(json_unescape(s));
        }
        else
        {
            ++pos;
        }
    }
    return out;
}

std::string ExpressionPreset::serialize() const
{
    std::string j = "{\"name\":\"" + json_escape(name)
                  + "\",\"expression\":\"" + json_escape(expression)
                  + "\",\"variables\":[";
    for (size_t i = 0; i < variables.size(); ++i)
    {
        if (i) j += ',';
        j += '"';
        j += json_escape(variables[i]);
        j += '"';
    }
    j += "]}";
    return j;
}

ExpressionPreset ExpressionPreset::deserialize(const std::string& json)
{
    ExpressionPreset p;
    p.name       = json_get_string(json, "name");
    p.expression = json_get_string(json, "expression");
    p.variables  = json_get_string_array(json, "variables");
    return p;
}

// ---------------------------------------------------------------------------
// ParseState helpers
// ---------------------------------------------------------------------------

char ExpressionEngine::ParseState::peek() const
{
    if (pos >= len) return '\0';
    return src[pos];
}

char ExpressionEngine::ParseState::peek_at(size_t offset) const
{
    if (pos + offset >= len) return '\0';
    return src[pos + offset];
}

void ExpressionEngine::ParseState::advance()
{
    if (pos < len) ++pos;
}

void ExpressionEngine::ParseState::skip_whitespace()
{
    while (pos < len && std::isspace(static_cast<unsigned char>(src[pos])))
        ++pos;
}

bool ExpressionEngine::ParseState::at_end() const
{
    return pos >= len;
}

// ---------------------------------------------------------------------------
// Constructor / Destructor / Move
// ---------------------------------------------------------------------------

ExpressionEngine::ExpressionEngine()  = default;
ExpressionEngine::~ExpressionEngine() = default;

ExpressionEngine::ExpressionEngine(ExpressionEngine&&) noexcept            = default;
ExpressionEngine& ExpressionEngine::operator=(ExpressionEngine&&) noexcept = default;

// ---------------------------------------------------------------------------
// compile()
// ---------------------------------------------------------------------------

CompileResult ExpressionEngine::compile(const std::string& expression)
{
    expression_ = expression;
    root_.reset();
    variables_.clear();
    var_values_.clear();

    if (expression.empty())
    {
        CompileResult r;
        r.ok    = false;
        r.error = "Expression is empty";
        return r;
    }

    ParseState ps;
    ps.src = expression.c_str();
    ps.len = expression.size();
    ps.pos = 0;

    AstNodePtr node = parse_expr(ps);

    if (!node)
    {
        CompileResult r;
        r.ok        = false;
        r.error     = ps.error.empty() ? "Parse error" : ps.error;
        r.error_col = ps.error_col;
        return r;
    }

    ps.skip_whitespace();
    if (!ps.at_end())
    {
        CompileResult r;
        r.ok        = false;
        r.error     = "Unexpected character '" + std::string(1, ps.peek()) + "'";
        r.error_col = static_cast<int>(ps.pos);
        return r;
    }

    // Collect variable references.
    std::vector<std::string> vars_raw;
    collect_variables(*node, vars_raw);
    // Deduplicate while preserving order.
    std::vector<std::string> vars_dedup;
    for (auto& v : vars_raw)
    {
        if (std::find(vars_dedup.begin(), vars_dedup.end(), v) == vars_dedup.end())
            vars_dedup.push_back(v);
    }
    variables_ = std::move(vars_dedup);
    root_      = std::move(node);

    CompileResult r;
    r.ok = true;
    return r;
}

// ---------------------------------------------------------------------------
// Variable management
// ---------------------------------------------------------------------------

void ExpressionEngine::set_variable(const std::string& key, double value)
{
    var_values_[key] = value;
}

double ExpressionEngine::get_variable(const std::string& key) const
{
    auto it = var_values_.find(key);
    if (it == var_values_.end())
        return std::numeric_limits<double>::quiet_NaN();
    return it->second;
}

void ExpressionEngine::set_variables(const std::unordered_map<std::string, double>& vals)
{
    for (auto& kv : vals)
        var_values_[kv.first] = kv.second;
}

void ExpressionEngine::reset_variables()
{
    for (auto& kv : var_values_)
        kv.second = 0.0;
}

// ---------------------------------------------------------------------------
// Evaluation
// ---------------------------------------------------------------------------

double ExpressionEngine::evaluate() const
{
    if (!root_)
        return std::numeric_limits<double>::quiet_NaN();
    return eval_node(*root_);
}

double ExpressionEngine::evaluate(const std::unordered_map<std::string, double>& vars) const
{
    if (!root_)
        return std::numeric_limits<double>::quiet_NaN();

    // Temporarily overlay the supplied vars on top of var_values_.
    auto saved = var_values_;
    for (auto& kv : vars)
        var_values_[kv.first] = kv.second;
    double result = eval_node(*root_);
    var_values_ = std::move(saved);
    return result;
}

double ExpressionEngine::eval_node(const AstNode& n) const
{
    switch (n.kind)
    {
        case NodeKind::Number:
            return n.number;

        case NodeKind::Variable:
        {
            auto it = var_values_.find(n.name);
            if (it == var_values_.end())
                return std::numeric_limits<double>::quiet_NaN();
            return it->second;
        }

        case NodeKind::Constant:
        {
            if (n.name == "pi") return M_PI;
            if (n.name == "e")  return M_E;
            return std::numeric_limits<double>::quiet_NaN();
        }

        case NodeKind::UnaryMinus:
            return -eval_node(*n.left);

        case NodeKind::BinOp:
        {
            double lv = eval_node(*n.left);
            double rv = eval_node(*n.right);
            switch (n.binop)
            {
                case BinOpKind::Add: return lv + rv;
                case BinOpKind::Sub: return lv - rv;
                case BinOpKind::Mul: return lv * rv;
                case BinOpKind::Div:
                    if (rv == 0.0) return std::numeric_limits<double>::quiet_NaN();
                    return lv / rv;
                case BinOpKind::Pow: return std::pow(lv, rv);
            }
            break;
        }

        case NodeKind::Function1:
        {
            double a = eval_node(*n.left);
            switch (n.func1)
            {
                case Func1Kind::Sqrt:  return std::sqrt(a);
                case Func1Kind::Abs:   return std::fabs(a);
                case Func1Kind::Sin:   return std::sin(a);
                case Func1Kind::Cos:   return std::cos(a);
                case Func1Kind::Tan:   return std::tan(a);
                case Func1Kind::Asin:  return std::asin(a);
                case Func1Kind::Acos:  return std::acos(a);
                case Func1Kind::Atan:  return std::atan(a);
                case Func1Kind::Log:   return std::log(a);
                case Func1Kind::Log10: return std::log10(a);
                case Func1Kind::Exp:   return std::exp(a);
                case Func1Kind::Floor: return std::floor(a);
                case Func1Kind::Ceil:  return std::ceil(a);
                case Func1Kind::Round: return std::round(a);
            }
            break;
        }

        case NodeKind::Function2:
        {
            double a = eval_node(*n.left);
            double b = eval_node(*n.right);
            switch (n.func2)
            {
                case Func2Kind::Atan2: return std::atan2(a, b);
            }
            break;
        }
    }
    return std::numeric_limits<double>::quiet_NaN();
}

// ---------------------------------------------------------------------------
// collect_variables()
// ---------------------------------------------------------------------------

void ExpressionEngine::collect_variables(const AstNode& n,
                                         std::vector<std::string>& out) const
{
    if (n.kind == NodeKind::Variable)
    {
        out.push_back(n.name);
        return;
    }
    if (n.left)  collect_variables(*n.left,  out);
    if (n.right) collect_variables(*n.right, out);
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------

void ExpressionEngine::save_preset(const std::string& name)
{
    ExpressionPreset p;
    p.name       = name;
    p.expression = expression_;
    p.variables  = variables_;

    for (auto& existing : presets_)
    {
        if (existing.name == name)
        {
            existing = p;
            return;
        }
    }
    presets_.push_back(std::move(p));
}

bool ExpressionEngine::load_preset(const std::string& name)
{
    for (const auto& p : presets_)
    {
        if (p.name == name)
        {
            auto r = compile(p.expression);
            return r.ok;
        }
    }
    return false;
}

bool ExpressionEngine::remove_preset(const std::string& name)
{
    auto it = std::find_if(presets_.begin(), presets_.end(),
                           [&name](const ExpressionPreset& p){ return p.name == name; });
    if (it == presets_.end())
        return false;
    presets_.erase(it);
    return true;
}

std::vector<ExpressionPreset> ExpressionEngine::presets() const
{
    return presets_;
}

std::string ExpressionEngine::serialize_presets() const
{
    std::string j = "[";
    for (size_t i = 0; i < presets_.size(); ++i)
    {
        if (i) j += ',';
        j += presets_[i].serialize();
    }
    j += "]";
    return j;
}

void ExpressionEngine::deserialize_presets(const std::string& json)
{
    presets_.clear();
    // Find array bounds.
    size_t start = json.find('[');
    if (start == std::string::npos) return;
    ++start;

    // Walk through top-level objects.
    size_t pos = start;
    while (pos < json.size())
    {
        while (pos < json.size() && json[pos] != '{' && json[pos] != ']')
            ++pos;
        if (pos >= json.size() || json[pos] == ']')
            break;
        // Find matching '}'
        int depth = 0;
        size_t obj_start = pos;
        size_t obj_end   = pos;
        while (pos < json.size())
        {
            if      (json[pos] == '{') ++depth;
            else if (json[pos] == '}') { --depth; if (depth == 0) { obj_end = pos; ++pos; break; } }
            ++pos;
        }
        std::string obj = json.substr(obj_start, obj_end - obj_start + 1);
        ExpressionPreset p = ExpressionPreset::deserialize(obj);
        if (!p.name.empty())
            presets_.push_back(std::move(p));
    }
}

// ---------------------------------------------------------------------------
// Static utilities
// ---------------------------------------------------------------------------

std::vector<std::string> ExpressionEngine::extract_variables(const std::string& expression)
{
    std::vector<std::string> out;
    const size_t len = expression.size();
    size_t i = 0;
    while (i < len)
    {
        if (expression[i] == '$')
        {
            size_t start = i;
            ++i;
            // Variable: letters, digits, '_', '.', '/'
            while (i < len && (std::isalnum(static_cast<unsigned char>(expression[i]))
                                || expression[i] == '_'
                                || expression[i] == '.'
                                || expression[i] == '/'))
            {
                ++i;
            }
            std::string v = expression.substr(start, i - start);
            if (v.size() > 1)
                out.push_back(v);
        }
        else
        {
            ++i;
        }
    }
    // Deduplicate.
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

bool ExpressionEngine::is_valid_variable(const std::string& name)
{
    if (name.empty() || name[0] != '$') return false;
    if (name.size() < 2)               return false;
    for (size_t i = 1; i < name.size(); ++i)
    {
        char c = name[i];
        if (!std::isalnum(static_cast<unsigned char>(c))
            && c != '_' && c != '.' && c != '/')
            return false;
    }
    return true;
}

const char* ExpressionEngine::syntax_help()
{
    return
        "Operators:  + - * / ^ (power)\n"
        "Variables:  $topic.field.path  (e.g. $imu.linear_acceleration.x)\n"
        "Constants:  pi  e\n"
        "Functions:  sqrt  abs  sin  cos  tan  asin  acos  atan\n"
        "            atan2(y,x)  log  log10  exp  floor  ceil  round\n"
        "Grouping:   ( expr )\n"
        "\n"
        "Example:  sqrt($imu.acc.x^2 + $imu.acc.y^2 + $imu.acc.z^2)";
}

// ---------------------------------------------------------------------------
// Recursive-descent parser
// ---------------------------------------------------------------------------
// Grammar:
//   expr         → additive
//   additive     → multiplicative (('+' | '-') multiplicative)*
//   multiplicative → power (('*' | '/') power)*
//   power        → unary ('^' power)?      (right-associative)
//   unary        → '-' unary | primary
//   primary      → number | variable | constant | func_call | '(' expr ')'
//   func_call    → name '(' expr (',' expr)? ')'
//   variable     → '$' (alnum | '_' | '.' | '/')+
//   number       → digit+ ('.' digit*)? (('e'|'E') ['+'|'-'] digit+)?
// ---------------------------------------------------------------------------

ExpressionEngine::AstNodePtr ExpressionEngine::parse_expr(ParseState& ps)
{
    return parse_additive(ps);
}

ExpressionEngine::AstNodePtr ExpressionEngine::parse_additive(ParseState& ps)
{
    auto lhs = parse_multiplicative(ps);
    if (!lhs) return nullptr;

    ps.skip_whitespace();
    while (!ps.at_end() && (ps.peek() == '+' || ps.peek() == '-'))
    {
        char op = ps.peek();
        ps.advance();
        ps.skip_whitespace();
        auto rhs = parse_multiplicative(ps);
        if (!rhs) return nullptr;

        auto node      = std::make_unique<AstNode>();
        node->kind     = NodeKind::BinOp;
        node->binop    = (op == '+') ? BinOpKind::Add : BinOpKind::Sub;
        node->left     = std::move(lhs);
        node->right    = std::move(rhs);
        lhs            = std::move(node);
        ps.skip_whitespace();
    }
    return lhs;
}

ExpressionEngine::AstNodePtr ExpressionEngine::parse_multiplicative(ParseState& ps)
{
    auto lhs = parse_power(ps);
    if (!lhs) return nullptr;

    ps.skip_whitespace();
    while (!ps.at_end() && (ps.peek() == '*' || ps.peek() == '/'))
    {
        char op = ps.peek();
        ps.advance();
        ps.skip_whitespace();
        auto rhs = parse_power(ps);
        if (!rhs) return nullptr;

        auto node   = std::make_unique<AstNode>();
        node->kind  = NodeKind::BinOp;
        node->binop = (op == '*') ? BinOpKind::Mul : BinOpKind::Div;
        node->left  = std::move(lhs);
        node->right = std::move(rhs);
        lhs         = std::move(node);
        ps.skip_whitespace();
    }
    return lhs;
}

ExpressionEngine::AstNodePtr ExpressionEngine::parse_power(ParseState& ps)
{
    auto base = parse_unary(ps);
    if (!base) return nullptr;

    ps.skip_whitespace();
    if (!ps.at_end() && ps.peek() == '^')
    {
        ps.advance();
        ps.skip_whitespace();
        // Right-associative: recurse into parse_power.
        auto exp = parse_power(ps);
        if (!exp) return nullptr;

        auto node   = std::make_unique<AstNode>();
        node->kind  = NodeKind::BinOp;
        node->binop = BinOpKind::Pow;
        node->left  = std::move(base);
        node->right = std::move(exp);
        return node;
    }
    return base;
}

ExpressionEngine::AstNodePtr ExpressionEngine::parse_unary(ParseState& ps)
{
    ps.skip_whitespace();
    if (!ps.at_end() && ps.peek() == '-')
    {
        ps.advance();
        ps.skip_whitespace();
        auto operand = parse_unary(ps);
        if (!operand) return nullptr;

        auto node  = std::make_unique<AstNode>();
        node->kind = NodeKind::UnaryMinus;
        node->left = std::move(operand);
        return node;
    }
    return parse_primary(ps);
}

ExpressionEngine::AstNodePtr ExpressionEngine::parse_primary(ParseState& ps)
{
    ps.skip_whitespace();
    if (ps.at_end())
    {
        ps.error     = "Unexpected end of expression";
        ps.error_col = static_cast<int>(ps.pos);
        return nullptr;
    }

    const char c = ps.peek();

    // Parenthesised sub-expression.
    if (c == '(')
    {
        ps.advance();
        auto inner = parse_expr(ps);
        if (!inner) return nullptr;
        ps.skip_whitespace();
        if (ps.at_end() || ps.peek() != ')')
        {
            ps.error     = "Expected ')'";
            ps.error_col = static_cast<int>(ps.pos);
            return nullptr;
        }
        ps.advance();
        return inner;
    }

    // Variable reference.
    if (c == '$')
        return parse_variable(ps);

    // Numeric literal.
    if (std::isdigit(static_cast<unsigned char>(c)) || c == '.')
        return parse_number(ps);

    // Identifier: named constant or function call.
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
    {
        size_t start = ps.pos;
        while (!ps.at_end() && (std::isalnum(static_cast<unsigned char>(ps.peek()))
                                 || ps.peek() == '_'))
        {
            ps.advance();
        }
        std::string ident = std::string(ps.src + start, ps.pos - start);
        ps.skip_whitespace();

        // Named constant?
        if (ident == "pi" || ident == "e")
        {
            auto node  = std::make_unique<AstNode>();
            node->kind = NodeKind::Constant;
            node->name = ident;
            return node;
        }

        // Function call?
        if (!ps.at_end() && ps.peek() == '(')
            return parse_function_call(ps, ident);

        ps.error     = "Unknown identifier '" + ident + "'";
        ps.error_col = static_cast<int>(start);
        return nullptr;
    }

    ps.error     = std::string("Unexpected character '") + c + "'";
    ps.error_col = static_cast<int>(ps.pos);
    return nullptr;
}

ExpressionEngine::AstNodePtr ExpressionEngine::parse_number(ParseState& ps)
{
    size_t start = ps.pos;

    // Integer part.
    while (!ps.at_end() && std::isdigit(static_cast<unsigned char>(ps.peek())))
        ps.advance();

    // Decimal part.
    if (!ps.at_end() && ps.peek() == '.')
    {
        ps.advance();
        while (!ps.at_end() && std::isdigit(static_cast<unsigned char>(ps.peek())))
            ps.advance();
    }

    // Exponent part.
    if (!ps.at_end() && (ps.peek() == 'e' || ps.peek() == 'E'))
    {
        ps.advance();
        if (!ps.at_end() && (ps.peek() == '+' || ps.peek() == '-'))
            ps.advance();
        if (ps.at_end() || !std::isdigit(static_cast<unsigned char>(ps.peek())))
        {
            ps.error     = "Expected digits in exponent";
            ps.error_col = static_cast<int>(ps.pos);
            return nullptr;
        }
        while (!ps.at_end() && std::isdigit(static_cast<unsigned char>(ps.peek())))
            ps.advance();
    }

    std::string num_str(ps.src + start, ps.pos - start);
    if (num_str.empty() || num_str == ".")
    {
        ps.error     = "Invalid number";
        ps.error_col = static_cast<int>(start);
        return nullptr;
    }

    double val = 0.0;
    try { val = std::stod(num_str); }
    catch (...) {
        ps.error     = "Number out of range: " + num_str;
        ps.error_col = static_cast<int>(start);
        return nullptr;
    }

    auto node   = std::make_unique<AstNode>();
    node->kind  = NodeKind::Number;
    node->number = val;
    return node;
}

ExpressionEngine::AstNodePtr ExpressionEngine::parse_variable(ParseState& ps)
{
    size_t start = ps.pos;
    ps.advance();  // consume '$'

    // Variable body: alnum, '_', '.', '/'
    size_t body_start = ps.pos;
    while (!ps.at_end() && (std::isalnum(static_cast<unsigned char>(ps.peek()))
                             || ps.peek() == '_'
                             || ps.peek() == '.'
                             || ps.peek() == '/'))
    {
        ps.advance();
    }

    if (ps.pos == body_start)
    {
        ps.error     = "Empty variable name after '$'";
        ps.error_col = static_cast<int>(start);
        return nullptr;
    }

    std::string var_name(ps.src + start, ps.pos - start);  // includes '$'
    auto node  = std::make_unique<AstNode>();
    node->kind = NodeKind::Variable;
    node->name = std::move(var_name);
    return node;
}

ExpressionEngine::AstNodePtr ExpressionEngine::parse_function_call(ParseState& ps,
                                                                    const std::string& name)
{
    // pos is at '('
    ps.advance();  // consume '('
    ps.skip_whitespace();

    auto arg1 = parse_expr(ps);
    if (!arg1) return nullptr;
    ps.skip_whitespace();

    // Two-argument functions.
    if (name == "atan2")
    {
        if (ps.at_end() || ps.peek() != ',')
        {
            ps.error     = "atan2 requires two arguments";
            ps.error_col = static_cast<int>(ps.pos);
            return nullptr;
        }
        ps.advance();  // consume ','
        ps.skip_whitespace();
        auto arg2 = parse_expr(ps);
        if (!arg2) return nullptr;
        ps.skip_whitespace();
        if (ps.at_end() || ps.peek() != ')')
        {
            ps.error     = "Expected ')' after atan2 arguments";
            ps.error_col = static_cast<int>(ps.pos);
            return nullptr;
        }
        ps.advance();

        auto node   = std::make_unique<AstNode>();
        node->kind  = NodeKind::Function2;
        node->func2 = Func2Kind::Atan2;
        node->left  = std::move(arg1);
        node->right = std::move(arg2);
        return node;
    }

    // One-argument functions.
    if (ps.at_end() || ps.peek() != ')')
    {
        ps.error     = "Expected ')' after function argument";
        ps.error_col = static_cast<int>(ps.pos);
        return nullptr;
    }
    ps.advance();

    // Map name to Func1Kind.
    Func1Kind fk;
    if      (name == "sqrt")  fk = Func1Kind::Sqrt;
    else if (name == "abs")   fk = Func1Kind::Abs;
    else if (name == "sin")   fk = Func1Kind::Sin;
    else if (name == "cos")   fk = Func1Kind::Cos;
    else if (name == "tan")   fk = Func1Kind::Tan;
    else if (name == "asin")  fk = Func1Kind::Asin;
    else if (name == "acos")  fk = Func1Kind::Acos;
    else if (name == "atan")  fk = Func1Kind::Atan;
    else if (name == "log")   fk = Func1Kind::Log;
    else if (name == "log10") fk = Func1Kind::Log10;
    else if (name == "exp")   fk = Func1Kind::Exp;
    else if (name == "floor") fk = Func1Kind::Floor;
    else if (name == "ceil")  fk = Func1Kind::Ceil;
    else if (name == "round") fk = Func1Kind::Round;
    else
    {
        ps.error = "Unknown function '" + name + "'";
        return nullptr;
    }

    auto node   = std::make_unique<AstNode>();
    node->kind  = NodeKind::Function1;
    node->func1 = fk;
    node->left  = std::move(arg1);
    return node;
}

}   // namespace spectra::adapters::ros2
