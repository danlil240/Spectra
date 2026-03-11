#include "expression_eval.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>

#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif

namespace spectra
{

// ─── ExprNode factories ─────────────────────────────────────────────────────

ExprNodePtr ExprNode::make_number(float v)
{
    auto n    = std::make_unique<ExprNode>();
    n->kind   = Kind::Number;
    n->number = v;
    return n;
}

ExprNodePtr ExprNode::make_variable(const std::string& name)
{
    auto n  = std::make_unique<ExprNode>();
    n->kind = Kind::Variable;
    n->name = name;
    return n;
}

ExprNodePtr ExprNode::make_unary(char op, ExprNodePtr operand)
{
    auto n  = std::make_unique<ExprNode>();
    n->kind = Kind::UnaryOp;
    n->op   = op;
    n->left = std::move(operand);
    return n;
}

ExprNodePtr ExprNode::make_binary(char op, ExprNodePtr lhs, ExprNodePtr rhs)
{
    auto n   = std::make_unique<ExprNode>();
    n->kind  = Kind::BinaryOp;
    n->op    = op;
    n->left  = std::move(lhs);
    n->right = std::move(rhs);
    return n;
}

ExprNodePtr ExprNode::make_func(const std::string& name, ExprNodePtr arg0, ExprNodePtr arg1)
{
    auto n   = std::make_unique<ExprNode>();
    n->kind  = Kind::FuncCall;
    n->name  = name;
    n->left  = std::move(arg0);
    n->right = std::move(arg1);
    return n;
}

// ─── Parser ─────────────────────────────────────────────────────────────────

ExprNodePtr ExpressionParser::parse(const std::string& expr)
{
    source_ = expr;
    pos_    = 0;
    error_.clear();

    if (source_.empty())
    {
        error_ = "Empty expression";
        return nullptr;
    }

    auto result = parse_expr();
    if (!result)
        return nullptr;

    skip_whitespace();
    if (!at_end())
    {
        error_ = "Unexpected character at position " + std::to_string(pos_) + ": '"
                 + std::string(1, current()) + "'";
        return nullptr;
    }

    return result;
}

ExprNodePtr ExpressionParser::parse_expr()
{
    return parse_ternary();
}

ExprNodePtr ExpressionParser::parse_ternary()
{
    // Ternary: condition ? true_expr : false_expr
    // Implemented as: if condition > 0 then true_expr else false_expr
    auto node = parse_additive();
    if (!node)
        return nullptr;

    skip_whitespace();
    if (match('?'))
    {
        auto true_expr = parse_additive();
        if (!true_expr)
        {
            error_ = "Expected expression after '?'";
            return nullptr;
        }
        skip_whitespace();
        if (!match(':'))
        {
            error_ = "Expected ':' in ternary expression";
            return nullptr;
        }
        auto false_expr = parse_additive();
        if (!false_expr)
        {
            error_ = "Expected expression after ':'";
            return nullptr;
        }
        // Encode as: __ternary(condition, true_expr) with false stored elsewhere
        // Simpler: use a special function node
        auto ternary          = std::make_unique<ExprNode>();
        ternary->kind         = ExprNode::Kind::FuncCall;
        ternary->name         = "__ternary";
        ternary->left         = std::move(node);   // condition
        ternary->right        = std::make_unique<ExprNode>();
        ternary->right->kind  = ExprNode::Kind::FuncCall;
        ternary->right->name  = "__ternary_pair";
        ternary->right->left  = std::move(true_expr);
        ternary->right->right = std::move(false_expr);
        return ternary;
    }

    return node;
}

ExprNodePtr ExpressionParser::parse_additive()
{
    auto left = parse_multiplicative();
    if (!left)
        return nullptr;

    while (true)
    {
        skip_whitespace();
        if (match('+'))
        {
            auto right = parse_multiplicative();
            if (!right)
            {
                error_ = "Expected expression after '+'";
                return nullptr;
            }
            left = ExprNode::make_binary('+', std::move(left), std::move(right));
        }
        else if (match('-'))
        {
            auto right = parse_multiplicative();
            if (!right)
            {
                error_ = "Expected expression after '-'";
                return nullptr;
            }
            left = ExprNode::make_binary('-', std::move(left), std::move(right));
        }
        else
        {
            break;
        }
    }

    return left;
}

ExprNodePtr ExpressionParser::parse_multiplicative()
{
    auto left = parse_power();
    if (!left)
        return nullptr;

    while (true)
    {
        skip_whitespace();
        if (match('*'))
        {
            auto right = parse_power();
            if (!right)
            {
                error_ = "Expected expression after '*'";
                return nullptr;
            }
            left = ExprNode::make_binary('*', std::move(left), std::move(right));
        }
        else if (match('/'))
        {
            auto right = parse_power();
            if (!right)
            {
                error_ = "Expected expression after '/'";
                return nullptr;
            }
            left = ExprNode::make_binary('/', std::move(left), std::move(right));
        }
        else if (match('%'))
        {
            auto right = parse_power();
            if (!right)
            {
                error_ = "Expected expression after '%'";
                return nullptr;
            }
            left = ExprNode::make_binary('%', std::move(left), std::move(right));
        }
        else
        {
            break;
        }
    }

    return left;
}

ExprNodePtr ExpressionParser::parse_power()
{
    auto base = parse_unary();
    if (!base)
        return nullptr;

    skip_whitespace();
    if (match('^'))
    {
        // Right-associative
        auto exponent = parse_power();
        if (!exponent)
        {
            error_ = "Expected expression after '^'";
            return nullptr;
        }
        return ExprNode::make_binary('^', std::move(base), std::move(exponent));
    }

    return base;
}

ExprNodePtr ExpressionParser::parse_unary()
{
    skip_whitespace();
    if (match('-'))
    {
        auto operand = parse_unary();
        if (!operand)
        {
            error_ = "Expected expression after '-'";
            return nullptr;
        }
        return ExprNode::make_unary('-', std::move(operand));
    }
    if (match('+'))
    {
        return parse_unary();
    }
    return parse_primary();
}

ExprNodePtr ExpressionParser::parse_primary()
{
    skip_whitespace();
    if (at_end())
    {
        error_ = "Unexpected end of expression";
        return nullptr;
    }

    // Parenthesized expression
    if (match('('))
    {
        auto inner = parse_expr();
        if (!inner)
            return nullptr;
        skip_whitespace();
        if (!match(')'))
        {
            error_ = "Missing closing ')'";
            return nullptr;
        }
        return inner;
    }

    // Number literal
    if (std::isdigit(static_cast<unsigned char>(current())) || current() == '.')
    {
        return ExprNode::make_number(read_number());
    }

    // Identifier (variable, constant, or function)
    if (is_ident_char(current()))
    {
        std::string ident = read_identifier();

        // Constants
        if (ident == "pi")
            return ExprNode::make_number(static_cast<float>(M_PI));
        if (ident == "e")
            return ExprNode::make_number(static_cast<float>(M_E));
        if (ident == "inf")
            return ExprNode::make_number(std::numeric_limits<float>::infinity());
        if (ident == "nan")
            return ExprNode::make_number(std::numeric_limits<float>::quiet_NaN());

        // Check if it's a function call
        skip_whitespace();
        if (!at_end() && current() == '(')
        {
            advance();   // consume '('
            auto arg0 = parse_expr();
            if (!arg0)
                return nullptr;

            ExprNodePtr arg1;
            skip_whitespace();
            if (match(','))
            {
                arg1 = parse_expr();
                if (!arg1)
                {
                    error_ = "Expected second argument after ','";
                    return nullptr;
                }

                // Check for third argument (clamp)
                skip_whitespace();
                if (match(','))
                {
                    auto arg2 = parse_expr();
                    if (!arg2)
                    {
                        error_ = "Expected third argument after ','";
                        return nullptr;
                    }
                    skip_whitespace();
                    if (!match(')'))
                    {
                        error_ = "Missing closing ')' in function call";
                        return nullptr;
                    }
                    // For 3-arg functions (clamp), nest: clamp(a, b, c) -> __clamp3(a, __pair(b,
                    // c))
                    auto pair = ExprNode::make_func("__pair", std::move(arg1), std::move(arg2));
                    return ExprNode::make_func(ident, std::move(arg0), std::move(pair));
                }
            }

            skip_whitespace();
            if (!match(')'))
            {
                error_ = "Missing closing ')' in function call";
                return nullptr;
            }

            return ExprNode::make_func(ident, std::move(arg0), std::move(arg1));
        }

        // Variable
        return ExprNode::make_variable(ident);
    }

    error_ = "Unexpected character at position " + std::to_string(pos_) + ": '"
             + std::string(1, current()) + "'";
    return nullptr;
}

// ─── Lexer helpers ──────────────────────────────────────────────────────────

void ExpressionParser::skip_whitespace()
{
    while (pos_ < source_.size()
           && (source_[pos_] == ' ' || source_[pos_] == '\t' || source_[pos_] == '\n'
               || source_[pos_] == '\r'))
    {
        ++pos_;
    }
}

bool ExpressionParser::match(char c)
{
    if (pos_ < source_.size() && source_[pos_] == c)
    {
        ++pos_;
        return true;
    }
    return false;
}

bool ExpressionParser::peek(char c) const
{
    return pos_ < source_.size() && source_[pos_] == c;
}

char ExpressionParser::current() const
{
    return (pos_ < source_.size()) ? source_[pos_] : '\0';
}

bool ExpressionParser::at_end() const
{
    return pos_ >= source_.size();
}

void ExpressionParser::advance()
{
    if (pos_ < source_.size())
        ++pos_;
}

bool ExpressionParser::is_ident_char(char c) const
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::string ExpressionParser::read_identifier()
{
    std::string result;
    while (pos_ < source_.size() && is_ident_char(source_[pos_]))
    {
        result += source_[pos_];
        ++pos_;
    }
    return result;
}

float ExpressionParser::read_number()
{
    std::string num_str;
    bool        has_dot = false;
    bool        has_exp = false;

    while (pos_ < source_.size())
    {
        char c = source_[pos_];
        if (std::isdigit(static_cast<unsigned char>(c)))
        {
            num_str += c;
            ++pos_;
        }
        else if (c == '.' && !has_dot && !has_exp)
        {
            has_dot = true;
            num_str += c;
            ++pos_;
        }
        else if ((c == 'e' || c == 'E') && !has_exp && !num_str.empty())
        {
            has_exp = true;
            num_str += c;
            ++pos_;
            // Optional sign after exponent
            if (pos_ < source_.size() && (source_[pos_] == '+' || source_[pos_] == '-'))
            {
                num_str += source_[pos_];
                ++pos_;
            }
        }
        else
        {
            break;
        }
    }

    try
    {
        return std::stof(num_str);
    }
    catch (...)
    {
        return 0.0f;
    }
}

// ─── Evaluator ──────────────────────────────────────────────────────────────

static float resolve_variable(const std::string& name, const ExprContext& ctx)
{
    // Built-in variables
    if (name == "x" || name == "t")
        return ctx.x;
    if (name == "y")
        return ctx.y;
    if (name == "i")
        return static_cast<float>(ctx.i);
    if (name == "n")
        return static_cast<float>(ctx.n);

    // Series references: s0_x, s0_y, s1_x, s1_y, ...
    if (name.size() >= 4 && name[0] == 's' && std::isdigit(static_cast<unsigned char>(name[1])))
    {
        // Parse series index
        size_t idx_end = 1;
        while (idx_end < name.size() && std::isdigit(static_cast<unsigned char>(name[idx_end])))
            ++idx_end;

        if (idx_end < name.size() && name[idx_end] == '_' && idx_end + 1 < name.size())
        {
            size_t series_idx = std::stoul(name.substr(1, idx_end - 1));
            char   component  = name[idx_end + 1];

            if (series_idx < ctx.series_data.size())
            {
                const auto& sref = ctx.series_data[series_idx];
                if (component == 'x' && ctx.i < sref.x.size())
                    return sref.x[ctx.i];
                if (component == 'y' && ctx.i < sref.y.size())
                    return sref.y[ctx.i];
                if (component == 'n')
                    return static_cast<float>(std::min(sref.x.size(), sref.y.size()));
            }
            return std::numeric_limits<float>::quiet_NaN();
        }
    }

    // Custom variables
    auto it = ctx.custom_vars.find(name);
    if (it != ctx.custom_vars.end())
        return it->second;

    return std::numeric_limits<float>::quiet_NaN();
}

static float call_function(const std::string& name,
                           float              a,
                           float              b,
                           bool               has_b,
                           const ExprNode*    right_node,
                           const ExprContext& ctx)
{
    // Single-argument functions
    if (name == "sin")
        return std::sin(a);
    if (name == "cos")
        return std::cos(a);
    if (name == "tan")
        return std::tan(a);
    if (name == "asin")
        return std::asin(a);
    if (name == "acos")
        return std::acos(a);
    if (name == "atan")
        return has_b ? std::atan2(a, b) : std::atan(a);
    if (name == "atan2")
        return std::atan2(a, b);
    if (name == "sinh")
        return std::sinh(a);
    if (name == "cosh")
        return std::cosh(a);
    if (name == "tanh")
        return std::tanh(a);
    if (name == "exp")
        return std::exp(a);
    if (name == "log" || name == "ln")
        return (a > 0.0f) ? std::log(a) : std::numeric_limits<float>::quiet_NaN();
    if (name == "log2")
        return (a > 0.0f) ? std::log2(a) : std::numeric_limits<float>::quiet_NaN();
    if (name == "log10")
        return (a > 0.0f) ? std::log10(a) : std::numeric_limits<float>::quiet_NaN();
    if (name == "sqrt")
        return (a >= 0.0f) ? std::sqrt(a) : std::numeric_limits<float>::quiet_NaN();
    if (name == "cbrt")
        return std::cbrt(a);
    if (name == "abs")
        return std::abs(a);
    if (name == "floor")
        return std::floor(a);
    if (name == "ceil")
        return std::ceil(a);
    if (name == "round")
        return std::round(a);
    if (name == "sign")
        return (a > 0.0f) ? 1.0f : (a < 0.0f ? -1.0f : 0.0f);
    if (name == "deg")
        return a * (180.0f / static_cast<float>(M_PI));
    if (name == "rad")
        return a * (static_cast<float>(M_PI) / 180.0f);

    // Two-argument functions
    if (name == "min")
        return std::min(a, b);
    if (name == "max")
        return std::max(a, b);
    if (name == "pow")
        return std::pow(a, b);
    if (name == "mod" || name == "fmod")
        return std::fmod(a, b);

    // Three-argument: clamp(value, min, max)
    if (name == "clamp" && right_node && right_node->name == "__pair")
    {
        float lo = evaluate(*right_node->left, ctx);
        float hi = evaluate(*right_node->right, ctx);
        return std::clamp(a, lo, hi);
    }

    return std::numeric_limits<float>::quiet_NaN();
}

float evaluate(const ExprNode& node, const ExprContext& ctx)
{
    switch (node.kind)
    {
        case ExprNode::Kind::Number:
            return node.number;

        case ExprNode::Kind::Variable:
            return resolve_variable(node.name, ctx);

        case ExprNode::Kind::UnaryOp:
        {
            float val = evaluate(*node.left, ctx);
            if (node.op == '-')
                return -val;
            return val;
        }

        case ExprNode::Kind::BinaryOp:
        {
            float l = evaluate(*node.left, ctx);
            float r = evaluate(*node.right, ctx);
            switch (node.op)
            {
                case '+':
                    return l + r;
                case '-':
                    return l - r;
                case '*':
                    return l * r;
                case '/':
                    return (r != 0.0f) ? l / r : std::numeric_limits<float>::quiet_NaN();
                case '^':
                    return std::pow(l, r);
                case '%':
                    return (r != 0.0f) ? std::fmod(l, r) : std::numeric_limits<float>::quiet_NaN();
                default:
                    return std::numeric_limits<float>::quiet_NaN();
            }
        }

        case ExprNode::Kind::FuncCall:
        {
            // Special: ternary
            if (node.name == "__ternary")
            {
                float cond = evaluate(*node.left, ctx);
                if (node.right && node.right->name == "__ternary_pair")
                {
                    if (cond != 0.0f && !std::isnan(cond))
                        return evaluate(*node.right->left, ctx);
                    else
                        return evaluate(*node.right->right, ctx);
                }
                return std::numeric_limits<float>::quiet_NaN();
            }

            float a     = evaluate(*node.left, ctx);
            bool  has_b = node.right != nullptr;
            float b     = has_b ? evaluate(*node.right, ctx) : 0.0f;
            return call_function(node.name, a, b, has_b, node.right.get(), ctx);
        }
    }

    return std::numeric_limits<float>::quiet_NaN();
}

// ─── Collect referenced variables ───────────────────────────────────────────

static void collect_variables(const ExprNode& node, std::vector<std::string>& vars)
{
    switch (node.kind)
    {
        case ExprNode::Kind::Variable:
            if (std::find(vars.begin(), vars.end(), node.name) == vars.end())
                vars.push_back(node.name);
            break;
        case ExprNode::Kind::Number:
            break;
        case ExprNode::Kind::UnaryOp:
            if (node.left)
                collect_variables(*node.left, vars);
            break;
        case ExprNode::Kind::BinaryOp:
        case ExprNode::Kind::FuncCall:
            if (node.left)
                collect_variables(*node.left, vars);
            if (node.right)
                collect_variables(*node.right, vars);
            break;
    }
}

ExpressionInfo parse_expression(const std::string& expr)
{
    ExpressionInfo   info;
    ExpressionParser parser;
    info.ast   = parser.parse(expr);
    info.error = parser.error();

    if (info.ast)
    {
        collect_variables(*info.ast, info.referenced_variables);
    }

    return info;
}

}   // namespace spectra
