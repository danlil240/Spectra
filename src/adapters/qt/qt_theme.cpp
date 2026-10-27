#include "qt_theme.hpp"

#include <algorithm>
#include <cmath>

namespace spectra::adapters::qt
{

namespace
{
int to_channel(float v)
{
    return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
}
}   // namespace

QColor to_qcolor(const ui::Color& c)
{
    return QColor(to_channel(c.r), to_channel(c.g), to_channel(c.b), to_channel(c.a));
}

QPalette palette_from_theme(const ui::ThemeColors& colors)
{
    // Qt chrome is opaque: translucent theme colours (glass rims, borders) are composited
    // over the window background so they look as they do in the Spectra UI.
    auto opaque = [&colors](const ui::Color& c)
    { return to_qcolor(c.composite_over(colors.bg_primary)); };

    const QColor window      = opaque(colors.bg_primary);
    const QColor base        = opaque(colors.bg_secondary);
    const QColor alt_base    = opaque(colors.bg_tertiary);
    const QColor button      = opaque(colors.bg_tertiary);
    const QColor text        = opaque(colors.text_primary);
    const QColor text_dim    = opaque(colors.text_tertiary);
    const QColor highlight   = opaque(colors.accent);
    const QColor on_accent   = opaque(colors.text_inverse);
    const QColor tooltip_bg  = opaque(colors.tooltip_bg);
    const QColor border      = opaque(colors.border_default);
    const QColor border_soft = opaque(colors.border_subtle);

    QPalette p;
    for (QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled})
    {
        const bool   disabled = group == QPalette::Disabled;
        const QColor fg       = disabled ? text_dim : text;

        p.setColor(group, QPalette::Window, window);
        p.setColor(group, QPalette::WindowText, fg);
        p.setColor(group, QPalette::Base, base);
        p.setColor(group, QPalette::AlternateBase, alt_base);
        p.setColor(group, QPalette::Text, fg);
        p.setColor(group, QPalette::PlaceholderText, text_dim);
        p.setColor(group, QPalette::Button, button);
        p.setColor(group, QPalette::ButtonText, fg);
        p.setColor(group, QPalette::BrightText, opaque(colors.error));
        p.setColor(group, QPalette::ToolTipBase, tooltip_bg);
        p.setColor(group, QPalette::ToolTipText, text);
        p.setColor(group, QPalette::Highlight, disabled ? border : highlight);
        p.setColor(group, QPalette::HighlightedText, disabled ? text_dim : on_accent);
        p.setColor(group, QPalette::Link, highlight);
        p.setColor(group, QPalette::LinkVisited, opaque(colors.accent_hover));
        // 3D bevel roles used by styles for frames and separators.
        p.setColor(group, QPalette::Light, border);
        p.setColor(group, QPalette::Midlight, border_soft);
        p.setColor(group, QPalette::Mid, border_soft);
        p.setColor(group, QPalette::Dark, opaque(colors.bg_canvas));
        p.setColor(group, QPalette::Shadow, opaque(colors.bg_canvas));
    }
    return p;
}

}   // namespace spectra::adapters::qt
