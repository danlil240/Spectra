#pragma once

#include <span>
#include <spectra/export.hpp>
#include <string>

#include "io/svg_export_internal.hpp"

namespace spectra
{

class Figure;

// One page of a PDF export: a figure (layout already computed) and its chrome colours.
struct PdfPage
{
    const Figure* figure = nullptr;
    SvgStyle      style;
};

// A PDF 1.4 document with one page per entry (null figures are skipped).
// `host` supplies plugin series export, as for SVG.
std::string pdf_export_to_string(std::span<const PdfPage> pages, const SvgExportHost& host);

bool pdf_export_write(const std::string&       path,
                      std::span<const PdfPage> pages,
                      const SvgExportHost&     host);

}   // namespace spectra
