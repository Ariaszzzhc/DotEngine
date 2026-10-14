#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_HDR
#define STBI_MAX_DIMENSIONS 8192
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>

unsigned char* dotEncodePng(const unsigned char* pixels, int width, int height, int* size) {
  return stbi_write_png_to_mem(pixels, width * 4, width, height, 4, size);
}
