#pragma once

#include <functional>
#include <spectra/export.hpp>
#include <string>

#include "io/vector_sink.hpp"

namespace spectra
{

class CustomSeries;
class Figure;

// Host services an SVG export can use beyond the Figure itself.  The public
// SvgExporter entry points export with none of them.
struct SvgExportHost
{
    // Draws a plugin series through `sink` (data coordinates).  `series_rgba` is
    // the series colour as exported (alpha includes the opacity).  Returns nullptr
    // on success, otherwise why the series was not exported: it is then replaced
    // by an XML comment and a logged warning.  The app binds this to
    // SeriesTypeRegistry::export_vector().  Empty: no plugin series is exported.
    std::function<const char*(const CustomSeries& series,
                              const float*        series_rgba,
                              float               opacity,
                              CustomVectorSink&   sink)>
        custom_series;
};

std::string svg_export_to_string(const Figure&        figure,
                                 const SvgStyle&      style,
                                 const SvgExportHost& host);

bool svg_export_write(const std::string&   path,
                      const Figure&        figure,
                      const SvgStyle&      style,
                      const SvgExportHost& host);

}   // namespace spectra
