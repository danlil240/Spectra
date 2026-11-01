// Single compilation unit for all stb library implementations.
// This avoids ODR violations if the user also includes stb headers.

// Suppress warnings in third-party STB headers (triggered by -Werror in CI)
#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wall"
    #pragma clang diagnostic ignored "-Wextra"
    #pragma clang diagnostic ignored "-Wmissing-field-initializers"
    #pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    #pragma GCC diagnostic ignored "-Wunused-function"
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#if defined(__clang__)
    #pragma clang diagnostic pop
#elif defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

#include <cstdlib>

#include "io/zlib_compress.hpp"

namespace spectra
{

std::vector<uint8_t> zlib_compress(std::span<const uint8_t> data, int quality)
{
    if (data.size() > static_cast<size_t>(INT32_MAX))
        return {};
    int            out_len = 0;
    unsigned char* out     = stbi_zlib_compress(const_cast<unsigned char*>(data.data()),
                                            static_cast<int>(data.size()),
                                            &out_len,
                                            quality);
    if (!out)
        return {};
    std::vector<uint8_t> result(out, out + out_len);
    STBIW_FREE(out);
    return result;
}

}   // namespace spectra
