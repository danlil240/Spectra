#include <plotix/export.hpp>

// stb_image_write header-only (implementation in src/io/stb_impl.cpp)
#include "stb_image_write.h"

namespace plotix {

bool ImageExporter::write_png(const std::string& path,
                              const uint8_t* rgba_data,
                              uint32_t width,
                              uint32_t height) {
    if (!rgba_data || width == 0 || height == 0) {
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

} // namespace plotix
