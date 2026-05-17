#pragma once

#include <cstddef>
#include <cstdint>

namespace spectra::platform
{

// Copy a PNG-encoded image to the system clipboard.
// Returns true on success, false on failure.
bool copy_image_to_clipboard(const uint8_t* png_bytes, size_t size);

}   // namespace spectra::platform
