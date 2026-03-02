// ExpressionEditor — implementation.
//
// See expression_editor.hpp for design notes.

#include "expression_editor.hpp"
#include "../expression_plot.hpp"

#include <cstring>
#include <string>

#ifdef SPECTRA_USE_IMGUI
#include <imgui.h>
#endif

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ExpressionEditor::ExpressionEditor(ExpressionPlot* plot)
    : plot_(plot)
{
    std::memset(expr_buf_raw_,    0, sizeof(expr_buf_raw_));
    std::memset(new_var_buf_,     0, sizeof(new_var_buf_));
    std::memset(new_topic_buf_,   0, sizeof(new_topic_buf_));
    std::memset(new_field_buf_,   0, sizeof(new_field_buf_));
    std::memset(new_type_buf_,    0, sizeof(new_type_buf_));
    std::memset(preset_name_buf_, 0, sizeof(preset_name_buf_));

    // Pre-populate from the attached plot.
    if (plot_ && plot_->is_compiled())
    {
        const std::string& e = plot_->expression();
        size_t copy_len = std::min(e.size(), EXPR_BUF_SIZE - 1);
        std::memcpy(expr_buf_raw_, e.c_str(), copy_len);
        expr_buf_ = e;
        last_ok_  = true;
    }
}

// ---------------------------------------------------------------------------
// Public helpers
// ---------------------------------------------------------------------------

void ExpressionEditor::set_pending_expression(const std::string& expr)
{
    size_t copy_len = std::min(expr.size(), EXPR_BUF_SIZE - 1);
    std::memset(expr_buf_raw_, 0, sizeof(expr_buf_raw_));
    std::memcpy(expr_buf_raw_, expr.c_str(), copy_len);
    expr_buf_ = expr;
}

bool ExpressionEditor::has_pending_changes() const
{
    if (!plot_) return !expr_buf_.empty();
    return expr_buf_ != plot_->expression();
}

// ---------------------------------------------------------------------------
// draw() — full panel
// ---------------------------------------------------------------------------

bool ExpressionEditor::draw()
{
#ifndef SPECTRA_USE_IMGUI
    return false;
#else
    bool recompiled = false;

    ImGui::PushID("ExprEditor");

    // Section: Expression input.
    ImGui::SeparatorText("Expression");
    draw_expression_input();
    draw_error_banner();

    // Apply button.
    const bool can_apply = !expr_buf_.empty();
    if (!can_apply) ImGui::BeginDisabled();
    if (ImGui::Button("Apply##expr") || (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter, false)))
    {
        recompiled = try_compile();
        if (recompiled && on_apply_cb_)
            on_apply_cb_(expr_buf_);
    }
    if (!can_apply) ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::TextDisabled("(Ctrl+Enter to apply)");

    // Section: Variable bindings.
    ImGui::Spacing();
    ImGui::SeparatorText("Variable Bindings");
    draw_variable_table();
    draw_variable_add_row();

    // Section: Presets.
    ImGui::Spacing();
    ImGui::SeparatorText("Presets");
    draw_preset_section();

    // Help tooltip button.
    ImGui::Spacing();
    if (ImGui::SmallButton("Syntax Help"))
        ImGui::OpenPopup("##expr_help");
    draw_help_tooltip();

    ImGui::PopID();
    return recompiled;
#endif
}

// ---------------------------------------------------------------------------
// draw_inline() — compact form (expression + error only)
// ---------------------------------------------------------------------------

bool ExpressionEditor::draw_inline()
{
#ifndef SPECTRA_USE_IMGUI
    return false;
#else
    bool recompiled = false;

    ImGui::PushID("ExprEditorInline");

    draw_expression_input();
    draw_error_banner();

    ImGui::SameLine();
    if (ImGui::SmallButton("Apply"))
    {
        recompiled = try_compile();
        if (recompiled && on_apply_cb_)
            on_apply_cb_(expr_buf_);
    }

    ImGui::PopID();
    return recompiled;
#endif
}

// ---------------------------------------------------------------------------
// Private drawing helpers
// ---------------------------------------------------------------------------

#ifdef SPECTRA_USE_IMGUI

void ExpressionEditor::draw_expression_input()
{
    ImGui::SetNextItemWidth(-1.0f);

    // Detect Ctrl+Enter to trigger apply.
    bool ctrl_enter = ImGui::IsKeyDown(ImGuiKey_ModCtrl)
                   && ImGui::IsKeyPressed(ImGuiKey_Enter, false);

    const ImGuiInputTextFlags flags =
        ImGuiInputTextFlags_EnterReturnsTrue
        | ImGuiInputTextFlags_AllowTabInput;

    bool edited = ImGui::InputTextMultiline(
        "##expr_input",
        expr_buf_raw_,
        EXPR_BUF_SIZE,
        ImVec2(-1.0f, 60.0f),
        flags
    );

    if (edited || ctrl_enter)
        expr_buf_ = expr_buf_raw_;

    // Live-validate while typing (no full compile, just extract vars).
    if (edited)
    {
        ExpressionEngine tmp;
        auto r = tmp.compile(expr_buf_);
        last_ok_        = r.ok;
        last_error_     = r.ok ? "" : r.error;
        last_error_col_ = r.error_col;
    }

    if (ctrl_enter)
    {
        bool ok = try_compile();
        if (ok && on_apply_cb_)
            on_apply_cb_(expr_buf_);
    }
}

void ExpressionEditor::draw_error_banner()
{
    if (last_ok_ || last_error_.empty())
        return;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
    if (last_error_col_ >= 0)
        ImGui::Text("[col %d] %s", last_error_col_, last_error_.c_str());
    else
        ImGui::Text("%s", last_error_.c_str());
    ImGui::PopStyleColor();
}

void ExpressionEditor::draw_variable_table()
{
    if (!plot_)
    {
        ImGui::TextDisabled("No plot attached.");
        return;
    }

    const auto& vars = plot_->engine().variables();
    if (vars.empty())
    {
        ImGui::TextDisabled("No variables in expression.");
        return;
    }

    if (ImGui::BeginTable("##var_table", 4,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
            | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Variable",   ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Topic",      ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Field",      ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Bound",      ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableHeadersRow();

        for (const auto& v : vars)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(v.c_str());

            bool bound = plot_->has_variable(v);

            ImGui::TableSetColumnIndex(1);
            if (bound) ImGui::TextDisabled("—");
            else       ImGui::TextColored(ImVec4(1,0.6f,0,1), "unbound");

            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled("—");

            ImGui::TableSetColumnIndex(3);
            if (bound)
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Yes");
            else
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "No");
        }

        ImGui::EndTable();
    }
}

void ExpressionEditor::draw_variable_add_row()
{
    ImGui::PushID("##var_add");
    ImGui::Text("Add binding:");

    const float col_w = (ImGui::GetContentRegionAvail().x - 80.0f) * 0.25f;

    ImGui::SetNextItemWidth(col_w);
    ImGui::InputText("Var##v", new_var_buf_, FIELD_BUF_SIZE);
    ImGui::SameLine();

    ImGui::SetNextItemWidth(col_w);
    ImGui::InputText("Topic##t", new_topic_buf_, FIELD_BUF_SIZE);
    ImGui::SameLine();

    ImGui::SetNextItemWidth(col_w);
    ImGui::InputText("Field##f", new_field_buf_, FIELD_BUF_SIZE);
    ImGui::SameLine();

    ImGui::SetNextItemWidth(col_w);
    ImGui::InputText("Type##ty", new_type_buf_, FIELD_BUF_SIZE);
    ImGui::SameLine();

    const bool can_add = new_var_buf_[0] != '\0'
                      && new_topic_buf_[0] != '\0'
                      && new_field_buf_[0] != '\0';

    if (!can_add) ImGui::BeginDisabled();
    if (ImGui::SmallButton("Add"))
    {
        VariableBindingRequest req;
        req.var_name   = new_var_buf_;
        req.topic      = new_topic_buf_;
        req.field_path = new_field_buf_;
        req.type_name  = new_type_buf_;

        if (on_binding_cb_)
            on_binding_cb_(req);

        std::memset(new_var_buf_,   0, sizeof(new_var_buf_));
        std::memset(new_topic_buf_, 0, sizeof(new_topic_buf_));
        std::memset(new_field_buf_, 0, sizeof(new_field_buf_));
        std::memset(new_type_buf_,  0, sizeof(new_type_buf_));
    }
    if (!can_add) ImGui::EndDisabled();

    ImGui::PopID();
}

void ExpressionEditor::draw_preset_section()
{
    if (!plot_)
    {
        ImGui::TextDisabled("No plot attached.");
        return;
    }

    // Save current as preset.
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputText("Preset name##pn", preset_name_buf_, PRESET_NAME_SIZE);
    ImGui::SameLine();

    const bool can_save = preset_name_buf_[0] != '\0' && plot_->is_compiled();
    if (!can_save) ImGui::BeginDisabled();
    if (ImGui::SmallButton("Save"))
    {
        plot_->save_preset(preset_name_buf_);
        std::memset(preset_name_buf_, 0, sizeof(preset_name_buf_));
    }
    if (!can_save) ImGui::EndDisabled();

    ImGui::Spacing();

    // Scrollable preset list.
    const auto presets = plot_->presets();
    if (presets.empty())
    {
        ImGui::TextDisabled("No presets saved.");
        return;
    }

    ImGui::BeginChild("##preset_list", ImVec2(0, 120.0f), true);
    for (const auto& p : presets)
    {
        ImGui::PushID(p.name.c_str());

        bool selected = false;
        if (ImGui::Selectable(p.name.c_str(), &selected,
                              ImGuiSelectableFlags_None,
                              ImVec2(0, 0)))
        {
            plot_->load_preset(p.name);
            set_pending_expression(plot_->expression());
            last_ok_    = plot_->is_compiled();
            last_error_ = plot_->compile_error();
        }

        // Show expression in tooltip.
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(p.expression.c_str());
            ImGui::EndTooltip();
        }

        ImGui::SameLine();
        if (ImGui::SmallButton("x##del"))
            plot_->remove_preset(p.name);

        ImGui::PopID();
    }
    ImGui::EndChild();
}

void ExpressionEditor::draw_help_tooltip()
{
    if (ImGui::BeginPopup("##expr_help"))
    {
        ImGui::TextUnformatted(ExpressionEngine::syntax_help());
        ImGui::EndPopup();
    }
}

#endif  // SPECTRA_USE_IMGUI

// ---------------------------------------------------------------------------
// try_compile() — compile expr_buf_ into the attached plot.
// ---------------------------------------------------------------------------

bool ExpressionEditor::try_compile()
{
    expr_buf_ = expr_buf_raw_;

    if (!plot_)
    {
        // Standalone compile for validation.
        ExpressionEngine tmp;
        auto r = tmp.compile(expr_buf_);
        last_ok_        = r.ok;
        last_error_     = r.ok ? "" : r.error;
        last_error_col_ = r.error_col;
        return last_ok_;
    }

    bool ok = plot_->set_expression(expr_buf_);
    last_ok_        = ok;
    last_error_     = ok ? "" : plot_->compile_error();
    last_error_col_ = -1;  // ExpressionPlot does not expose column yet

    // Re-query error detail from engine for column info.
    if (!ok)
    {
        ExpressionEngine tmp;
        auto r = tmp.compile(expr_buf_);
        last_error_col_ = r.error_col;
    }

    return ok;
}

}   // namespace spectra::adapters::ros2
