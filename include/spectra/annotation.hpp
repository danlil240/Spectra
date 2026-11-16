#pragma once

#include <cmath>
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

    /// @brief Background box behind the text (see bbox()).
    struct Box
    {
        float                pad = 4.0f;   ///< Space between text and box edge, in pixels
        std::optional<Color> fill;         ///< Unset: the theme's plot background, 90% opaque
        std::optional<Color> edge;         ///< Unset: the theme's axis border colour at 50%
        float                edge_width = 1.0f;   ///< Border width in pixels; 0 = no border
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

    /// @brief Draw a box behind the text so it stays readable over data.
    const std::optional<Box>& bbox() const { return bbox_; }
    TextAnnotation&           bbox(const Box& b)
    {
        bbox_ = b;
        return *this;
    }
    /// @brief Turn the default background box on or off.
    TextAnnotation& bbox(bool on)
    {
        if (!on)
            bbox_.reset();
        else if (!bbox_)
            bbox_ = Box{};
        return *this;
    }

    /// @brief Outline the glyphs with a halo (default: the theme's plot
    /// background) so text stays readable over data without a box.
    bool            halo() const { return halo_; }
    TextAnnotation& halo(bool on)
    {
        halo_ = on;
        return *this;
    }
    /// @brief Halo colour; unset follows the theme's plot background.
    const std::optional<Color>& halo_color() const { return halo_color_; }
    TextAnnotation&             halo_color(const Color& c)
    {
        halo_       = true;
        halo_color_ = c;
        return *this;
    }
    /// @brief Halo width in pixels (default 2).
    float           halo_width() const { return halo_width_; }
    TextAnnotation& halo_width(float px)
    {
        halo_width_ = px > 0.0f ? px : 0.0f;
        return *this;
    }

    /// @brief Rotation in degrees, counter-clockwise (like matplotlib), about
    /// the anchor point.  The alignment applies along the rotated text.
    float           rotation() const { return rotation_; }
    TextAnnotation& rotation(float degrees)
    {
        rotation_ = std::isfinite(degrees) ? degrees : 0.0f;
        return *this;
    }

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
    std::optional<Box>   bbox_;
    bool                 halo_ = false;
    std::optional<Color> halo_color_;
    float                halo_width_ = 2.0f;
    float                rotation_   = 0.0f;
};

}   // namespace spectra
