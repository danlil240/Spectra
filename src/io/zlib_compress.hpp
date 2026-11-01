#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace spectra
{

// zlib (RFC 1950) stream of `data`, e.g. for a PDF /FlateDecode stream, via the
// vendored stb_image_write compressor.  `quality` trades speed for size (stb
// default 8).  Empty only if compression fails (or the input exceeds 2 GiB).
std::vector<uint8_t> zlib_compress(std::span<const uint8_t> data, int quality = 8);

}   // namespace spectra
