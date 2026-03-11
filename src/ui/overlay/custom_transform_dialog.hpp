#pragma once
#ifdef SPECTRA_USE_IMGUI

    #include <spectra/fwd.hpp>
    #include <string>
    #include <vector>

struct ImFont;

namespace spectra::ui
{

// ─── Custom Transform Dialog ────────────────────────────────────────────────
//
// A popup window that allows users to create custom data transformations
// by writing mathematical formulas. The dialog provides:
//   - A text input for the formula
//   - A list of available variables (series data, x, y, etc.)
//   - A list of available math functions
//   - Live syntax validation with error feedback
//   - Preview of the transformation result
//   - Optional name for saving as a reusable transform

class CustomTransformDialog
{
   public:
    CustomTransformDialog() = default;

    // Open the dialog for a specific figure
    void open(Figure* figure);

    // Close the dialog
    void close();

    // Draw the dialog (call every frame when open)
    void draw();

    // Check if the dialog is open
    bool is_open() const { return open_; }

    // Set fonts (called once after font loading)
    void set_fonts(ImFont* body, ImFont* heading, ImFont* title);

   private:
    void draw_formula_input();
    void draw_variable_reference();
    void draw_function_reference();
    void draw_preview();
    void draw_action_buttons();
    void validate_formula();
    void apply_transform();

    // Collect available variables from the figure's series
    void refresh_series_info();

    bool    open_   = false;
    Figure* figure_ = nullptr;

    // Formula input
    char        formula_buf_[1024] = {};
    char        name_buf_[256]     = {};
    std::string validation_error_;
    bool        formula_valid_   = false;
    bool        formula_changed_ = true;

    // Target configuration
    int  target_axes_   = 0;       // Which axes to apply to (-1 = all)
    int  target_series_ = -1;      // Which series to apply to (-1 = all visible)
    bool apply_to_x_    = false;   // Apply formula to x (default: y)

    // Series information for the variable reference panel
    struct SeriesInfo
    {
        std::string label;
        size_t      point_count  = 0;
        int         axes_index   = 0;
        int         series_index = 0;
    };
    std::vector<SeriesInfo> series_info_;

    // Fonts
    ImFont* font_body_    = nullptr;
    ImFont* font_heading_ = nullptr;
    ImFont* font_title_   = nullptr;
};

}   // namespace spectra::ui

#endif
