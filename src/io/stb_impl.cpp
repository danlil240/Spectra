// Single compilation unit for all stb library implementations.
// This avoids ODR violations if the user also includes stb headers.

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
