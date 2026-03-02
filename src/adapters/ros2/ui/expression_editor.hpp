#pragma once

// ExpressionEditor — ImGui panel for editing computed/derived field expressions (C5).
//
// Provides:
//   - Multi-line text input for the expression string with syntax-highlighted error
//   - Inline error feedback (red banner + column marker)
//   - Variable binding table: map "$name" → (topic, field_path)
//   - Preset save/load/delete with scrollable list
//   - Syntax reference help tooltip
//
// Typical usage (inside a docked ImGui window):
//   ExpressionEditor editor(plot);
//   editor.draw();   // call once per frame inside any ImGui window
//
// All ImGui code is gated on SPECTRA_USE_IMGUI.  Without ImGui the class
// still compiles but draw() is a no-op.
//
// Thread-safety:
//   Not thread-safe — must be called from the render thread.

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "../expression_engine.hpp"

// Forward-declare ExpressionPlot to avoid pulling in heavy headers here.
namespace spectra::adapters::ros2 { class ExpressionPlot; }

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// VariableBindingRequest — issued by the editor when the user wants to add
// a new variable binding (topic + field pair).  The application shell should
// handle this by calling ExpressionPlot::add_variable().
// ---------------------------------------------------------------------------

struct VariableBindingRequest
{
    std::string var_name;     // e.g. "$ax"
    std::string topic;        // e.g. "/imu"
    std::string field_path;   // e.g. "linear_acceleration.x"
    std::string type_name;    // optional; empty = auto-detect
};

// ---------------------------------------------------------------------------
// ExpressionEditor
// ---------------------------------------------------------------------------

class ExpressionEditor
{
public:
    // Construct with a pointer to the ExpressionPlot being edited.
    // `plot` must outlive the editor.
    explicit ExpressionEditor(ExpressionPlot* plot = nullptr);
    ~ExpressionEditor() = default;

    // Non-copyable, non-movable.
    ExpressionEditor(const ExpressionEditor&)            = delete;
    ExpressionEditor& operator=(const ExpressionEditor&) = delete;
    ExpressionEditor(ExpressionEditor&&)                 = delete;
    ExpressionEditor& operator=(ExpressionEditor&&)      = delete;

    // Swap the active plot.
    void set_plot(ExpressionPlot* plot) { plot_ = plot; }
    ExpressionPlot* plot() const { return plot_; }

    // ------------------------------------------------------------------
    // Drawing (ImGui)
    // ------------------------------------------------------------------

    // Draw the full editor panel inside the caller's current ImGui window.
    // Call once per frame.
    // Returns true if the expression was recompiled this frame.
    bool draw();

    // Draw only the expression input + compile/error line (compact form).
    bool draw_inline();

    // ------------------------------------------------------------------
    // Callbacks
    // ------------------------------------------------------------------

    // Called when the user clicks "Apply" or presses Ctrl+Enter.
    using ApplyCallback = std::function<void(const std::string& expression)>;
    void set_on_apply(ApplyCallback cb) { on_apply_cb_ = std::move(cb); }

    // Called when the user fills in the variable binding form and clicks "Add".
    using BindingCallback = std::function<void(const VariableBindingRequest&)>;
    void set_on_binding(BindingCallback cb) { on_binding_cb_ = std::move(cb); }

    // ------------------------------------------------------------------
    // State
    // ------------------------------------------------------------------

    // Current text in the expression input buffer.
    const std::string& pending_expression() const { return expr_buf_; }

    // Force the input buffer to a specific string (e.g. after loading preset).
    void set_pending_expression(const std::string& expr);

    // Whether the editor has unsaved (not yet applied) changes.
    bool has_pending_changes() const;

    // The last error message from the most recent compile attempt (empty = ok).
    const std::string& last_error() const { return last_error_; }
    int last_error_col() const { return last_error_col_; }

private:
    // ------------------------------------------------------------------
    // Private drawing helpers
    // ------------------------------------------------------------------

#ifdef SPECTRA_USE_IMGUI
    void draw_expression_input();
    void draw_error_banner();
    void draw_variable_table();
    void draw_variable_add_row();
    void draw_preset_section();
    void draw_help_tooltip();
#endif

    // Try to compile expr_buf_ into the attached plot.
    // Returns true on success.
    bool try_compile();

    // ------------------------------------------------------------------
    // Members
    // ------------------------------------------------------------------
    ExpressionPlot* plot_{nullptr};

    // Expression input buffer.
    static constexpr size_t EXPR_BUF_SIZE = 1024;
    char   expr_buf_raw_[EXPR_BUF_SIZE]{};
    std::string expr_buf_;          // mirrors expr_buf_raw_ after edits

    // Variable-add form buffers.
    static constexpr size_t FIELD_BUF_SIZE = 256;
    char new_var_buf_[FIELD_BUF_SIZE]{};
    char new_topic_buf_[FIELD_BUF_SIZE]{};
    char new_field_buf_[FIELD_BUF_SIZE]{};
    char new_type_buf_[FIELD_BUF_SIZE]{};

    // Preset save form.
    static constexpr size_t PRESET_NAME_SIZE = 128;
    char preset_name_buf_[PRESET_NAME_SIZE]{};

    // Last compile result.
    std::string last_error_;
    int         last_error_col_{-1};
    bool        last_ok_{false};

    ApplyCallback   on_apply_cb_;
    BindingCallback on_binding_cb_;
};

}   // namespace spectra::adapters::ros2
