#include <plotix/export.hpp>

// TODO: SVG export — Phase 2
//
// Planned interface:
//   class SvgExporter {
//   public:
//       static bool write_svg(const std::string& path,
//                             const Figure& figure);
//   };
//
// Implementation will traverse the Figure → Axes → Series hierarchy
// and emit SVG <line>, <circle>, <text> elements directly,
// bypassing the GPU rendering pipeline entirely.
