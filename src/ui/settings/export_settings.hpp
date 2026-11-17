#pragma once

// export_settings.hpp — choices of the Export options dialog (file.export_dialog),
// remembered in settings.json, and the pure helpers that turn them into an
// export (pixel size, layout size, theme).  No ImGui / GPU dependencies so the
// plumbing is unit-testable.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace spectra::ui::settings
{

enum class ExportFormat : uint8_t
{
    Png = 0,
    Svg = 1,
    Pdf = 2,
};

enum class ExportSizeUnit : uint8_t
{
    Pixels = 0,
    Inches = 1,
};

/// Layout pixels per inch: vector exports (SVG / PDF) and the plot layout use
/// CSS pixels (96 per inch, 1 px = 0.75 pt).
inline constexpr float LAYOUT_PX_PER_INCH = 96.0f;
/// Largest PNG edge the dialog produces (matches the daemon's export limit).
inline constexpr uint32_t MAX_EXPORT_PIXELS = 16384;

struct ExportDialogSettings
{
    ExportFormat format = ExportFormat::Png;
    /// "current" (the app's active theme), "light" or "dark".
    std::string theme = "current";
    /// Plot style for the exported figure only: "" keeps the figure's own,
    /// otherwise a built-in style name ("paper", ...).
    std::string    style;
    ExportSizeUnit unit        = ExportSizeUnit::Pixels;
    uint32_t       width_px    = 0;   ///< 0 = the figure's current size
    uint32_t       height_px   = 0;
    float          width_in    = 6.4f;
    float          height_in   = 4.8f;
    float          dpi         = 150.0f;
    float          scale       = 1.0f;   ///< PNG pixel multiplier (HiDPI)
    bool           transparent = false;

    bool operator==(const ExportDialogSettings&) const = default;
};

inline const char* export_format_extension(ExportFormat f)
{
    switch (f)
    {
        case ExportFormat::Svg:
            return "svg";
        case ExportFormat::Pdf:
            return "pdf";
        case ExportFormat::Png:
        default:
            return "png";
    }
}

inline ExportFormat export_format_from_string(std::string_view s)
{
    if (s == "svg")
        return ExportFormat::Svg;
    if (s == "pdf")
        return ExportFormat::Pdf;
    return ExportFormat::Png;
}

/// Clamp out-of-range values (hand-edited settings.json, drag fields) into the
/// ranges the dialog supports.
inline ExportDialogSettings sanitized(ExportDialogSettings s)
{
    auto finite_clamp = [](float v, float lo, float hi, float def)
    { return std::isfinite(v) ? std::clamp(v, lo, hi) : def; };
    s.width_px  = std::min(s.width_px, MAX_EXPORT_PIXELS);
    s.height_px = std::min(s.height_px, MAX_EXPORT_PIXELS);
    s.width_in  = finite_clamp(s.width_in, 0.5f, 100.0f, 6.4f);
    s.height_in = finite_clamp(s.height_in, 0.5f, 100.0f, 4.8f);
    s.dpi       = finite_clamp(s.dpi, 36.0f, 1200.0f, 150.0f);
    s.scale     = finite_clamp(s.scale, 0.25f, 8.0f, 1.0f);
    if (s.theme != "light" && s.theme != "dark")
        s.theme = "current";
    return s;
}

/// Size of one export: the layout the figure is drawn at (CSS px; the size of
/// an SVG / PDF page) and the PNG's pixel size.  pixel = layout x pixel_scale.
struct ResolvedExportSize
{
    uint32_t layout_width  = 0;
    uint32_t layout_height = 0;
    uint32_t pixel_width   = 0;
    uint32_t pixel_height  = 0;
    float    pixel_scale   = 1.0f;
};

/// Resolve the dialog's size choice against a figure of fig_w x fig_h:
///  - pixels: layout = width_px x height_px (0 = the figure's size), PNG = layout x scale;
///  - inches: layout = inches x 96, PNG = inches x dpi x scale (text and lines
///    keep their size relative to the page, like matplotlib's figsize + dpi).
inline ResolvedExportSize resolve_export_size(const ExportDialogSettings& in,
                                              uint32_t                    fig_w,
                                              uint32_t                    fig_h)
{
    const ExportDialogSettings s = sanitized(in);
    ResolvedExportSize         r;
    float                      px_per_layout = s.scale;
    if (s.unit == ExportSizeUnit::Inches)
    {
        r.layout_width  = static_cast<uint32_t>(std::lround(s.width_in * LAYOUT_PX_PER_INCH));
        r.layout_height = static_cast<uint32_t>(std::lround(s.height_in * LAYOUT_PX_PER_INCH));
        px_per_layout   = s.dpi / LAYOUT_PX_PER_INCH * s.scale;
    }
    else
    {
        r.layout_width  = s.width_px > 0 ? s.width_px : fig_w;
        r.layout_height = s.height_px > 0 ? s.height_px : fig_h;
    }
    r.layout_width  = std::clamp(r.layout_width, 1u, MAX_EXPORT_PIXELS);
    r.layout_height = std::clamp(r.layout_height, 1u, MAX_EXPORT_PIXELS);
    // Keep the PNG within the limit by lowering the scale (never the layout).
    const float max_scale =
        static_cast<float>(MAX_EXPORT_PIXELS) / std::max(r.layout_width, r.layout_height);
    r.pixel_scale = std::min(px_per_layout, max_scale);
    r.pixel_width =
        std::max(1u, static_cast<uint32_t>(std::lround(r.layout_width * r.pixel_scale)));
    r.pixel_height =
        std::max(1u, static_cast<uint32_t>(std::lround(r.layout_height * r.pixel_scale)));
    return r;
}

/// Theme name for ExportOptions::theme: "" (the active theme) for "current".
inline std::string export_theme_name(const ExportDialogSettings& s)
{
    return (s.theme == "light" || s.theme == "dark") ? s.theme : std::string();
}

/// Replace (or add) the extension of `path` with the format's.
inline std::string with_export_extension(const std::string& path, ExportFormat f)
{
    const std::string ext   = std::string(".") + export_format_extension(f);
    const size_t      slash = path.find_last_of("/\\");
    const size_t      dot   = path.find_last_of('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        return path.substr(0, dot) + ext;
    return path + ext;
}

}   // namespace spectra::ui::settings
