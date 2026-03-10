#pragma once

// ExpressionEngine — runtime expression parser and evaluator for C5 (computed topics).
//
// Evaluates mathematical expressions that reference ROS2 field values via
// dollar-sign variables: $topic.field.path
//
// Supported syntax:
//   Numbers       : 3.14, -1, 1e-3
//   Variables     : $imu.linear_acceleration.x
//                   $/ns/topic.field  (topics with namespace prefix)
//   Operators     : + - * / ^ (right-associative power)
//   Unary minus   : -expr
//   Functions     : sqrt abs sin cos tan asin acos atan atan2(y,x)
//                   log log10 exp floor ceil round
//   Parentheses   : ( expr )
//   Constants     : pi e
//
// Typical usage:
//   ExpressionEngine eng;
//   auto result = eng.compile("sqrt($imu.linear_acceleration.x^2 + $imu.linear_acceleration.y^2)");
//   if (!result.ok) { /* result.error contains message */ }
//
//   // Each frame, set variable values then evaluate:
//   eng.set_variable("$imu.linear_acceleration.x", 0.5);
//   eng.set_variable("$imu.linear_acceleration.y", -1.2);
//   double val = eng.evaluate();  // returns NaN on error
//
// Preset format (JSON-like, stored as string):
//   { "name": "...", "expression": "...", "variables": ["$a.b", ...] }
//
// Thread-safety:
//   Not thread-safe.  All methods must be called from the render thread.

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// CompileResult — returned by compile()
// ---------------------------------------------------------------------------

struct CompileResult
{
    bool        ok{false};
    std::string error;           // empty when ok == true
    int         error_col{-1};   // 0-based column in the expression string, -1 if unknown
};

// ---------------------------------------------------------------------------
// ExpressionPreset — a named expression with its variable list
// ---------------------------------------------------------------------------

struct ExpressionPreset
{
    std::string              name;
    std::string              expression;
    std::vector<std::string> variables;   // extracted variable names (e.g. "$imu.acc.x")

    // Serialize to/from a compact JSON string.
    std::string             serialize() const;
    static ExpressionPreset deserialize(const std::string& json);
};

// ---------------------------------------------------------------------------
// ExpressionEngine — main class
// ---------------------------------------------------------------------------

class ExpressionEngine
{
   public:
    ExpressionEngine();
    ~ExpressionEngine();

    // Non-copyable, movable.
    ExpressionEngine(const ExpressionEngine&)            = delete;
    ExpressionEngine& operator=(const ExpressionEngine&) = delete;
    ExpressionEngine(ExpressionEngine&&) noexcept;
    ExpressionEngine& operator=(ExpressionEngine&&) noexcept;

    // ------------------------------------------------------------------
    // Compilation
    // ------------------------------------------------------------------

    // Parse and compile an expression string.
    // Extracts variable names (stored internally) and builds an AST.
    // Returns CompileResult describing success or the first parse error.
    // After a successful compile(), variables() returns the list of
    // $-prefixed identifiers referenced by the expression.
    CompileResult compile(const std::string& expression);

    // True when a valid expression has been compiled.
    bool is_compiled() const { return root_ != nullptr; }

    // The expression string most recently passed to compile().
    const std::string& expression() const { return expression_; }

    // ------------------------------------------------------------------
    // Variables
    // ------------------------------------------------------------------

    // Variable names extracted during compile (e.g. "$imu.acc.x").
    // Populated only when is_compiled() == true.
    const std::vector<std::string>& variables() const { return variables_; }

    // Set the current value of a variable.
    // key must match one of the names in variables() (exact string).
    // Safe to call with unknown keys (no-op).
    void set_variable(const std::string& key, double value);

    // Get the stored value for a variable.  Returns NaN if not found.
    double get_variable(const std::string& key) const;

    // Set all variables from a map at once.
    void set_variables(const std::unordered_map<std::string, double>& vals);

    // Reset all variable values to 0.0.
    void reset_variables();

    // ------------------------------------------------------------------
    // Evaluation
    // ------------------------------------------------------------------

    // Evaluate the compiled expression with the current variable values.
    // Returns NaN if the engine is not compiled or an arithmetic error occurs.
    double evaluate() const;

    // Evaluate with a one-shot variable map (does not persist).
    double evaluate(const std::unordered_map<std::string, double>& vars) const;

    // ------------------------------------------------------------------
    // Presets
    // ------------------------------------------------------------------

    // Save the current expression + variable names as a named preset.
    // Overwrites any existing preset with the same name.
    void save_preset(const std::string& name);

    // Load a preset: calls compile(preset.expression).
    // Returns false if compile fails.
    bool load_preset(const std::string& name);

    // Remove a preset by name.  Returns false if not found.
    bool remove_preset(const std::string& name);

    // All stored presets (snapshot).
    std::vector<ExpressionPreset> presets() const;

    // Serialize all presets to a JSON array string.
    std::string serialize_presets() const;

    // Deserialize presets from a JSON array string (replaces existing).
    void deserialize_presets(const std::string& json);

    // ------------------------------------------------------------------
    // Static utilities
    // ------------------------------------------------------------------

    // Extract all $variable references from an expression string
    // without full compilation.  Useful for validation previews.
    static std::vector<std::string> extract_variables(const std::string& expression);

    // Returns true if `name` is a valid $variable identifier.
    static bool is_valid_variable(const std::string& name);

    // Human-readable description of the supported syntax.
    static const char* syntax_help();

   private:
    // ------------------------------------------------------------------
    // AST node types (forward declarations)
    // ------------------------------------------------------------------
    struct AstNode;
    using AstNodePtr = std::unique_ptr<AstNode>;

    // ------------------------------------------------------------------
    // Parser state
    // ------------------------------------------------------------------
    struct ParseState
    {
        const char* src{nullptr};
        size_t      pos{0};
        size_t      len{0};
        std::string error;
        int         error_col{-1};

        char peek() const;
        char peek_at(size_t offset) const;
        void advance();
        void skip_whitespace();
        bool at_end() const;
    };

    // Recursive-descent parsing methods (each returns nullptr on error).
    AstNodePtr parse_expr(ParseState& ps);
    AstNodePtr parse_additive(ParseState& ps);
    AstNodePtr parse_multiplicative(ParseState& ps);
    AstNodePtr parse_power(ParseState& ps);
    AstNodePtr parse_unary(ParseState& ps);
    AstNodePtr parse_primary(ParseState& ps);
    AstNodePtr parse_number(ParseState& ps);
    AstNodePtr parse_variable(ParseState& ps);
    AstNodePtr parse_function_call(ParseState& ps, const std::string& name);

    // Evaluate a compiled node (const — does not modify state).
    double eval_node(const AstNode& node) const;

    // Collect all variable name references from the compiled AST.
    void collect_variables(const AstNode& node, std::vector<std::string>& out) const;

    // ------------------------------------------------------------------
    // Members
    // ------------------------------------------------------------------
    std::string              expression_;
    AstNodePtr               root_;
    std::vector<std::string> variables_;

    // Current variable values (set_variable).
    mutable std::unordered_map<std::string, double> var_values_;

    // Named presets.
    std::vector<ExpressionPreset> presets_;
};

}   // namespace spectra::adapters::ros2
