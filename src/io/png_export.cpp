#include <spectra/export.hpp>

// Suppress warnings in third-party STB headers
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

// stb_image_write header-only (implementation in src/io/stb_impl.cpp)
#include "stb_image_write.h"

#if defined(__clang__)
    #pragma clang diagnostic pop
#elif defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

namespace spectra
{

bool ImageExporter::write_png(const std::string& path,
                              const uint8_t* rgba_data,
                              uint32_t width,
                              uint32_t height)
{
    if (!rgba_data || width == 0 || height == 0)
    {
        return false;
    }

    // stbi_write_png expects: path, w, h, channels, data, stride_in_bytes
    // RGBA = 4 channels, stride = width * 4
    int result = stbi_write_png(path.c_str(),
                                static_cast<int>(width),
                                static_cast<int>(height),
                                4,
                                rgba_data,
                                static_cast<int>(width * 4));
    return result != 0;
}

}  // namespace spectra
