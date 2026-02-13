#pragma once

#include <cstddef>
#include <cstdint>

namespace plotix {
namespace embedded {

// Embedded MSDF font atlas PNG data
// In a real build, this would contain a pre-baked atlas (e.g., Roboto/Noto).
// For now, a minimal placeholder atlas is provided.
extern const uint8_t font_atlas_png[];
extern const size_t  font_atlas_png_size;

// Embedded glyph metrics as JSON string
extern const char font_atlas_metrics_json[];

} // namespace embedded
} // namespace plotix
