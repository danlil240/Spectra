#pragma once

#include <cmath>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace spectra
{

// ─── Expression AST ─────────────────────────────────────────────────────────

struct ExprNode;
using ExprNodePtr = std::unique_ptr<ExprNode>;

struct ExprNode
{
    enum class Kind
    {
        Number,     // literal float
        Variable,   // named variable (x, y, t, s0_x, s1_y, ...)
        UnaryOp,    // -expr
        BinaryOp,   // expr op expr
        FuncCall,   // func(expr) or func(expr, expr)
    };

    Kind        kind;
    float       number = 0.0f;   // Kind::Number
    std::string name;            // Kind::Variable or Kind::FuncCall
    char        op = 0;          // Kind::UnaryOp or Kind::BinaryOp (+, -, *, /, ^, %)
    ExprNodePtr left;            // BinaryOp lhs, UnaryOp operand, FuncCall arg0
    ExprNodePtr right;           // BinaryOp rhs, FuncCall arg1 (optional)

    static ExprNodePtr make_number(float v);
    static ExprNodePtr make_variable(const std::string& name);
    static ExprNodePtr make_unary(char op, ExprNodePtr operand);
    static ExprNodePtr make_binary(char op, ExprNodePtr lhs, ExprNodePtr rhs);
    static ExprNodePtr make_func(const std::string& name,
                                 ExprNodePtr        arg0,
                                 ExprNodePtr        arg1 = nullptr);
};

// ─── Expression parser ──────────────────────────────────────────────────────

// Parses a mathematical expression string into an AST.
// Supported syntax:
//   - Arithmetic: +, -, *, /, ^ (power), % (modulo)
//   - Parentheses: (expr)
//   - Functions: sin, cos, tan, asin, acos, atan, atan2, sinh, cosh, tanh,
//                exp, log, log2, log10, sqrt, cbrt, abs, floor, ceil, round,
//                min, max, pow, sign, clamp
//   - Constants: pi, e, inf, nan
//   - Variables: x, y, t, i, n, s0_x, s0_y, s1_x, s1_y, ... (series data)
class ExpressionParser
{
   public:
    // Parse an expression string. Returns nullptr on error (check error()).
    ExprNodePtr parse(const std::string& expr);

    // Last parse error message (empty if successful)
    const std::string& error() const { return error_; }

   private:
    std::string source_;
    size_t      pos_ = 0;
    std::string error_;

    // Recursive descent parser
    ExprNodePtr parse_expr();
    ExprNodePtr parse_ternary();
    ExprNodePtr parse_additive();
    ExprNodePtr parse_multiplicative();
    ExprNodePtr parse_power();
    ExprNodePtr parse_unary();
    ExprNodePtr parse_primary();

    // Lexer helpers
    void        skip_whitespace();
    bool        match(char c);
    bool        peek(char c) const;
    char        current() const;
    bool        at_end() const;
    void        advance();
    bool        is_ident_char(char c) const;
    std::string read_identifier();
    float       read_number();
};

// ─── Expression evaluator ───────────────────────────────────────────────────

// Variable context for evaluating an expression at a specific data point.
// Provides x, y, t (time/x value), i (index), n (total count),
// and access to other series data via s0_x, s0_y, s1_x, s1_y, etc.
struct ExprContext
{
    float  x = 0.0f;   // current x value
    float  y = 0.0f;   // current y value
    float  t = 0.0f;   // alias for x (time)
    size_t i = 0;      // current index
    size_t n = 0;      // total point count

    // Additional series data: series_data[series_index] = {x_data, y_data}
    struct SeriesRef
    {
        std::span<const float> x;
        std::span<const float> y;
        std::string            label;
    };
    std::vector<SeriesRef> series_data;

    // Custom named variables
    std::unordered_map<std::string, float> custom_vars;
};

// Evaluate a parsed expression with the given context.
// Returns NaN on evaluation error.
float evaluate(const ExprNode& node, const ExprContext& ctx);

// ─── High-level helper ──────────────────────────────────────────────────────

// Parse and validate an expression, returning available variable names.
struct ExpressionInfo
{
    ExprNodePtr              ast;
    std::string              error;
    std::vector<std::string> referenced_variables;
};

ExpressionInfo parse_expression(const std::string& expr);

}   // namespace spectra
