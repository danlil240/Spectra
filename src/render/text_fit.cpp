#include "text_fit.hpp"

#include "core/label_layout.hpp"

namespace spectra
{

FittedText to_fitted_text(FittedLabel&& label)
{
    return FittedText{std::move(label.text), static_cast<FontSize>(label.font), label.truncated};
}

LabelWidthFn to_label_width_fn(const TextWidthFn& measure)
{
    if (!measure)
        return {};
    return [&measure](const std::string& t, LabelFont f)
    { return measure(t, static_cast<FontSize>(f)); };
}

// The fitting rule lives in core/label_layout so the SVG exporter shares it.
FittedText fit_text_to_width(const std::string& text,
                             float              max_width,
                             FontSize           preferred,
                             FontSize           min_size,
                             const TextWidthFn& measure,
                             std::string_view   ellipsis)
{
    return to_fitted_text(fit_label_to_width(text,
                                             max_width,
                                             static_cast<LabelFont>(preferred),
                                             static_cast<LabelFont>(min_size),
                                             to_label_width_fn(measure),
                                             ellipsis));
}

}   // namespace spectra
