#ifdef PLOTIX_USE_IMGUI

#include "command_palette.hpp"
#include "command_registry.hpp"
#include "shortcut_manager.hpp"
#include "design_tokens.hpp"
#include "theme.hpp"
#include "icons.hpp"

#include <imgui.h>
#include <algorithm>
#include <cstring>

namespace plotix {

// ─── Open / Close ────────────────────────────────────────────────────────────

void CommandPalette::open() {
    open_ = true;
    focus_input_ = true;
    search_buf_[0] = '\0';
    last_query_.clear();
    selected_index_ = 0;
    results_.clear();
    update_search();
}

void CommandPalette::close() {
    open_ = false;
    search_buf_[0] = '\0';
    last_query_.clear();
    results_.clear();
}

void CommandPalette::toggle() {
    if (open_) close(); else open();
}

// ─── Search ──────────────────────────────────────────────────────────────────

void CommandPalette::update_search() {
    if (!registry_) return;

    std::string query(search_buf_);
    if (query == last_query_ && !results_.empty()) return;
    last_query_ = query;

    if (query.empty()) {
        // Show recent commands first, then all
        auto recent = registry_->recent_commands(5);
        auto all = registry_->search("", 50);
        results_.clear();

        // Add recent at top (with boosted score)
        for (const auto* cmd : recent) {
            results_.push_back({cmd, 1000});
        }
        // Add remaining (skip duplicates from recent)
        for (const auto& r : all) {
            bool is_recent = false;
            for (const auto* rc : recent) {
                if (rc->id == r.command->id) { is_recent = true; break; }
            }
            if (!is_recent) {
                results_.push_back(r);
            }
        }
    } else {
        results_ = registry_->search(query, 50);
    }

    // Clamp selected index
    if (selected_index_ >= static_cast<int>(results_.size())) {
        selected_index_ = results_.empty() ? 0 : static_cast<int>(results_.size()) - 1;
    }
}

// ─── Keyboard ────────────────────────────────────────────────────────────────

bool CommandPalette::handle_keyboard() {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        close();
        return false;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        selected_index_ = std::max(0, selected_index_ - 1);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
        selected_index_ = std::min(static_cast<int>(results_.size()) - 1, selected_index_ + 1);
        if (selected_index_ < 0) selected_index_ = 0;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
        if (selected_index_ >= 0 && selected_index_ < static_cast<int>(results_.size())) {
            const auto* cmd = results_[selected_index_].command;
            if (cmd && cmd->callback && cmd->enabled) {
                std::string cmd_id = cmd->id;
                close();
                registry_->execute(cmd_id);
                return true;
            }
        }
    }

    return false;
}

// ─── Draw ────────────────────────────────────────────────────────────────────

bool CommandPalette::draw(float window_width, float window_height) {
    if (!open_) {
        opacity_ = 0.0f;
        scale_ = 0.98f;
        return false;
    }

    // Animate open
    float dt = ImGui::GetIO().DeltaTime;
    opacity_ = std::min(1.0f, opacity_ + dt * ANIM_SPEED);
    scale_ = scale_ + (1.0f - scale_) * std::min(1.0f, dt * ANIM_SPEED);

    const auto& colors = ui::theme();

    // Overlay background
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(window_width, window_height));
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
        ImVec4(colors.bg_overlay.r, colors.bg_overlay.g, colors.bg_overlay.b,
               colors.bg_overlay.a * opacity_ * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    ImGuiWindowFlags overlay_flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

    if (ImGui::Begin("##palette_overlay", nullptr, overlay_flags)) {
        // Click on overlay to close
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) {
            close();
            ImGui::End();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor();
            return false;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    // Palette window
    float palette_w = PALETTE_WIDTH * scale_;
    float palette_x = (window_width - palette_w) * 0.5f;
    float palette_y = window_height * 0.2f;  // 20% from top

    // Calculate height based on results
    float results_height = std::min(
        static_cast<float>(results_.size()) * RESULT_ITEM_HEIGHT,
        PALETTE_MAX_HEIGHT - INPUT_HEIGHT - ui::tokens::SPACE_2);
    float palette_h = INPUT_HEIGHT + results_height + ui::tokens::SPACE_2;

    ImGui::SetNextWindowPos(ImVec2(palette_x, palette_y));
    ImGui::SetNextWindowSize(ImVec2(palette_w, palette_h));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::tokens::RADIUS_LG);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui::tokens::SPACE_2, ui::tokens::SPACE_2));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
        ImVec4(colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b,
               opacity_));
    ImGui::PushStyleColor(ImGuiCol_Border,
        ImVec4(colors.border_default.r, colors.border_default.g, colors.border_default.b,
               opacity_ * 0.8f));

    // Shadow effect via a slightly larger background rect
    ImDrawList* bg_draw = ImGui::GetBackgroundDrawList();
    float shadow_offset = 8.0f;
    bg_draw->AddRectFilled(
        ImVec2(palette_x - 2, palette_y - 2),
        ImVec2(palette_x + palette_w + 2, palette_y + palette_h + shadow_offset),
        IM_COL32(0, 0, 0, static_cast<int>(60 * opacity_)),
        ui::tokens::RADIUS_LG + 2);

    ImGuiWindowFlags palette_flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;

    bool executed = false;

    if (ImGui::Begin("##command_palette", nullptr, palette_flags)) {
        // Search input
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::tokens::RADIUS_SM);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ui::tokens::SPACE_3, ui::tokens::SPACE_2));
        ImGui::PushStyleColor(ImGuiCol_FrameBg,
            ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImVec4(colors.text_primary.r, colors.text_primary.g, colors.text_primary.b, 1.0f));

        ImGui::SetNextItemWidth(palette_w - ui::tokens::SPACE_4);

        if (focus_input_) {
            ImGui::SetKeyboardFocusHere();
            focus_input_ = false;
        }

        if (font_body_) ImGui::PushFont(font_body_);

        bool input_changed = ImGui::InputTextWithHint(
            "##palette_search", "Type a command...",
            search_buf_, sizeof(search_buf_),
            ImGuiInputTextFlags_AutoSelectAll);

        if (font_body_) ImGui::PopFont();

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);

        if (input_changed) {
            update_search();
            selected_index_ = 0;
        }

        // Handle keyboard navigation
        executed = handle_keyboard();

        // Separator
        ImGui::PushStyleColor(ImGuiCol_Separator,
            ImVec4(colors.border_subtle.r, colors.border_subtle.g, colors.border_subtle.b, 0.5f));
        ImGui::Separator();
        ImGui::PopStyleColor();

        // Results list
        if (!results_.empty()) {
            ImGui::BeginChild("##palette_results", ImVec2(0, results_height), false,
                              ImGuiWindowFlags_NoScrollbar);

            std::string current_category;

            for (int i = 0; i < static_cast<int>(results_.size()); ++i) {
                const auto& result = results_[i];
                if (!result.command) continue;

                // Category header
                if (result.command->category != current_category) {
                    current_category = result.command->category;
                    if (font_heading_) ImGui::PushFont(font_heading_);
                    ImGui::PushStyleColor(ImGuiCol_Text,
                        ImVec4(colors.text_tertiary.r, colors.text_tertiary.g,
                               colors.text_tertiary.b, 0.8f));
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ui::tokens::SPACE_1);
                    ImGui::TextUnformatted(current_category.c_str());
                    ImGui::PopStyleColor();
                    if (font_heading_) ImGui::PopFont();
                }

                // Result item
                bool is_selected = (i == selected_index_);
                ImVec2 item_pos = ImGui::GetCursorScreenPos();
                float item_w = ImGui::GetContentRegionAvail().x;

                // Highlight selected item
                if (is_selected) {
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        item_pos,
                        ImVec2(item_pos.x + item_w, item_pos.y + RESULT_ITEM_HEIGHT),
                        IM_COL32(
                            static_cast<int>(colors.accent_muted.r * 255),
                            static_cast<int>(colors.accent_muted.g * 255),
                            static_cast<int>(colors.accent_muted.b * 255),
                            80),
                        ui::tokens::RADIUS_SM);
                }

                // Invisible button for click handling
                ImGui::PushID(i);
                if (ImGui::InvisibleButton("##item", ImVec2(item_w, RESULT_ITEM_HEIGHT))) {
                    if (result.command->callback && result.command->enabled) {
                        std::string cmd_id = result.command->id;
                        close();
                        registry_->execute(cmd_id);
                        executed = true;
                    }
                }

                if (ImGui::IsItemHovered()) {
                    selected_index_ = i;
                }

                // Draw item content over the invisible button
                ImVec2 text_pos(item_pos.x + ui::tokens::SPACE_3,
                                item_pos.y + (RESULT_ITEM_HEIGHT - ImGui::GetTextLineHeight()) * 0.5f);

                if (font_body_) ImGui::PushFont(font_body_);

                // Command label
                ImGui::GetWindowDrawList()->AddText(
                    text_pos,
                    IM_COL32(
                        static_cast<int>(colors.text_primary.r * 255),
                        static_cast<int>(colors.text_primary.g * 255),
                        static_cast<int>(colors.text_primary.b * 255),
                        result.command->enabled ? 255 : 128),
                    result.command->label.c_str());

                // Shortcut badge on the right
                if (!result.command->shortcut.empty()) {
                    ImVec2 shortcut_size = ImGui::CalcTextSize(result.command->shortcut.c_str());
                    float badge_x = item_pos.x + item_w - shortcut_size.x - ui::tokens::SPACE_4;
                    float badge_y = text_pos.y;

                    // Badge background
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        ImVec2(badge_x - ui::tokens::SPACE_1, badge_y - 2),
                        ImVec2(badge_x + shortcut_size.x + ui::tokens::SPACE_1,
                               badge_y + shortcut_size.y + 2),
                        IM_COL32(
                            static_cast<int>(colors.bg_tertiary.r * 255),
                            static_cast<int>(colors.bg_tertiary.g * 255),
                            static_cast<int>(colors.bg_tertiary.b * 255),
                            200),
                        ui::tokens::RADIUS_SM);

                    ImGui::GetWindowDrawList()->AddText(
                        ImVec2(badge_x, badge_y),
                        IM_COL32(
                            static_cast<int>(colors.text_secondary.r * 255),
                            static_cast<int>(colors.text_secondary.g * 255),
                            static_cast<int>(colors.text_secondary.b * 255),
                            200),
                        result.command->shortcut.c_str());
                }

                if (font_body_) ImGui::PopFont();

                ImGui::PopID();
            }

            // Scroll selected item into view
            if (selected_index_ >= 0 && selected_index_ < static_cast<int>(results_.size())) {
                float scroll_y = ImGui::GetScrollY();
                float item_top = selected_index_ * RESULT_ITEM_HEIGHT;
                float item_bottom = item_top + RESULT_ITEM_HEIGHT;
                float visible_top = scroll_y;
                float visible_bottom = scroll_y + results_height;

                if (item_top < visible_top) {
                    ImGui::SetScrollY(item_top);
                } else if (item_bottom > visible_bottom) {
                    ImGui::SetScrollY(item_bottom - results_height);
                }
            }

            ImGui::EndChild();
        } else {
            // No results
            if (font_body_) ImGui::PushFont(font_body_);
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImVec4(colors.text_tertiary.r, colors.text_tertiary.g,
                       colors.text_tertiary.b, 0.6f));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ui::tokens::SPACE_4);
            ImGui::SetCursorPosX(
                (palette_w - ImGui::CalcTextSize("No matching commands").x) * 0.5f);
            ImGui::TextUnformatted("No matching commands");
            ImGui::PopStyleColor();
            if (font_body_) ImGui::PopFont();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    return executed;
}

} // namespace plotix

#endif // PLOTIX_USE_IMGUI
