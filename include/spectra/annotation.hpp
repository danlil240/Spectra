#pragma once

#include <optional>
#include <spectra/color.hpp>
#include <string>
#include <string_view>

namespace spectra
{

/// @brief A text label placed in data coordinates on a 2D Axes, optionally with an arrow.
///
/// Created by Axes::text() and Axes::annotate(); the Axes owns it, so the reference
/// returned by those calls stays valid until Axes::clear_annotations().
/// @code
/// ax.text(2.0f, 0.5f, "peak").color(colors::red).font_size(16.0f);
/// ax.annotate("local max", 3.1f, 0.9f, 4.0f, 1.2f);   // arrow from text to (3.1, 0.9)
/// @endcode
class TextAnnotation
{
   public:
    /// @brief Horizontal placement of the text relative to its anchor point.
    enum class HAlign
    {
        Left,     ///< Text starts at the anchor
        Center,   ///< Text is centred on the anchor
        Right,    ///< Text ends at the anchor
    };

    /// @brief Vertical placement of the text relative to its anchor point.
    enum class VAlign
    {
        Top,      ///< Anchor at the top of the text (text hangs below)
        Middle,   ///< Anchor at the vertical centre of the text
        Bottom,   ///< Anchor at the bottom of the text (text sits above)
    };

    TextAnnotation(float x, float y, std::string_view text) : x_(x), y_(y), text_(text) {}

    /// @brief Text content.
    const std::string& text() const { return text_; }
    TextAnnotation&    text(std::string_view t)
    {
        text_ = std::string(t);
        return *this;
    }

    /// @brief Anchor position in data coordinates.
    float           x() const { return x_; }
    float           y() const { return y_; }
    TextAnnotation& position(float x, float y)
    {
        x_ = x;
        y_ = y;
        return *this;
    }

    /// @brief Text (and arrow) colour. Unset means the theme's primary text colour,
    /// resolved when drawing so annotations follow theme switches.
    const std::optional<Color>& color() const { return color_; }
    TextAnnotation&             color(const Color& c)
    {
        color_ = c;
        return *this;
    }

    /// @brief Font size in pixels; 0 (default) uses the axis-label size.
    /// The renderer snaps to the nearest baked size (tick, label or title).
    float           font_size() const { return font_size_; }
    TextAnnotation& font_size(float px)
    {
        font_size_ = px > 0.0f ? px : 0.0f;
        return *this;
    }

    HAlign          halign() const { return halign_; }
    TextAnnotation& halign(HAlign a)
    {
        halign_ = a;
        return *this;
    }

    VAlign          valign() const { return valign_; }
    TextAnnotation& valign(VAlign a)
    {
        valign_ = a;
        return *this;
    }

    /// @brief Draw an arrow from the text to (x, y) in data coordinates.
    TextAnnotation& arrow_to(float x, float y)
    {
        has_arrow_ = true;
        arrow_x_   = x;
        arrow_y_   = y;
        return *this;
    }
    TextAnnotation& clear_arrow()
    {
        has_arrow_ = false;
        return *this;
    }
    bool  has_arrow() const { return has_arrow_; }
    float arrow_x() const { return arrow_x_; }
    float arrow_y() const { return arrow_y_; }

    bool            visible() const { return visible_; }
    TextAnnotation& visible(bool v)
    {
        visible_ = v;
        return *this;
    }

   private:
    float                x_ = 0.0f;
    float                y_ = 0.0f;
    std::string          text_;
    std::optional<Color> color_;
    float                font_size_ = 0.0f;
    HAlign               halign_    = HAlign::Left;
    VAlign               valign_    = VAlign::Bottom;
    bool                 has_arrow_ = false;
    float                arrow_x_   = 0.0f;
    float                arrow_y_   = 0.0f;
    bool                 visible_   = true;
};

}   // namespace spectra
