// Minimal stb_image-compatible PNG loader for Mineclone render tests.
// Supports only RGBA8 PNGs with no compression (stored deflate blocks) and filter type 0.
// This is intentionally small to keep dependencies minimal for deterministic tests.

#ifndef STB_IMAGE_H
#define STB_IMAGE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char stbi_uc;

stbi_uc* stbi_load(char const* filename, int* x, int* y, int* comp, int req_comp);
void stbi_image_free(void* retval_from_stbi_load);

#ifdef __cplusplus
}
#endif

#endif // STB_IMAGE_H

#ifdef STB_IMAGE_IMPLEMENTATION

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

constexpr std::uint8_t kPngSignature[8] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};

std::uint32_t ReadU32(const std::uint8_t* data) {
    return (static_cast<std::uint32_t>(data[0]) << 24u) |
           (static_cast<std::uint32_t>(data[1]) << 16u) |
           (static_cast<std::uint32_t>(data[2]) << 8u) |
           static_cast<std::uint32_t>(data[3]);
}

bool ReadFile(const char* filename, std::vector<std::uint8_t>& data) {
    std::FILE* file = std::fopen(filename, "rb");
    if (!file) {
        return false;
    }
    std::fseek(file, 0, SEEK_END);
    long size = std::ftell(file);
    if (size <= 0) {
        std::fclose(file);
        return false;
    }
    std::fseek(file, 0, SEEK_SET);
    data.resize(static_cast<std::size_t>(size));
    if (std::fread(data.data(), 1, data.size(), file) != data.size()) {
        std::fclose(file);
        return false;
    }
    std::fclose(file);
    return true;
}

bool InflateStored(const std::vector<std::uint8_t>& input, std::vector<std::uint8_t>& output) {
    if (input.size() < 6) {
        return false;
    }
    std::size_t offset = 2; // skip zlib header
    while (offset + 5 <= input.size()) {
        std::uint8_t header = input[offset++];
        std::uint8_t bfinal = header & 0x1;
        std::uint8_t btype = (header >> 1) & 0x3;
        if (btype != 0) {
            return false;
        }
        if (offset + 4 > input.size()) {
            return false;
        }
        std::uint16_t len = static_cast<std::uint16_t>(input[offset] | (input[offset + 1] << 8u));
        std::uint16_t nlen = static_cast<std::uint16_t>(input[offset + 2] | (input[offset + 3] << 8u));
        offset += 4;
        if (static_cast<std::uint16_t>(len ^ 0xFFFFu) != nlen) {
            return false;
        }
        if (offset + len > input.size()) {
            return false;
        }
        output.insert(output.end(), input.begin() + static_cast<std::ptrdiff_t>(offset),
                      input.begin() + static_cast<std::ptrdiff_t>(offset + len));
        offset += len;
        if (bfinal) {
            return true;
        }
    }
    return false;
}

} // namespace

stbi_uc* stbi_load(char const* filename, int* x, int* y, int* comp, int req_comp) {
    (void)req_comp;
    std::vector<std::uint8_t> fileData;
    if (!ReadFile(filename, fileData)) {
        return nullptr;
    }
    if (fileData.size() < 8 || std::memcmp(fileData.data(), kPngSignature, 8) != 0) {
        return nullptr;
    }
    std::size_t offset = 8;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> idatData;

    while (offset + 8 <= fileData.size()) {
        std::uint32_t length = ReadU32(&fileData[offset]);
        offset += 4;
        if (offset + 4 > fileData.size()) {
            return nullptr;
        }
        const std::uint8_t* type = &fileData[offset];
        offset += 4;
        if (offset + length + 4 > fileData.size()) {
            return nullptr;
        }
        if (std::memcmp(type, "IHDR", 4) == 0) {
            if (length < 13) {
                return nullptr;
            }
            width = ReadU32(&fileData[offset]);
            height = ReadU32(&fileData[offset + 4]);
            std::uint8_t bitDepth = fileData[offset + 8];
            std::uint8_t colorType = fileData[offset + 9];
            if (bitDepth != 8 || colorType != 6) {
                return nullptr;
            }
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            idatData.insert(idatData.end(), fileData.begin() + static_cast<std::ptrdiff_t>(offset),
                            fileData.begin() + static_cast<std::ptrdiff_t>(offset + length));
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }
        offset += length + 4; // skip data and CRC
    }

    if (width == 0 || height == 0 || idatData.empty()) {
        return nullptr;
    }

    std::vector<std::uint8_t> inflated;
    if (!InflateStored(idatData, inflated)) {
        return nullptr;
    }

    const std::size_t stride = static_cast<std::size_t>(width) * 4u;
    const std::size_t expected = (stride + 1) * static_cast<std::size_t>(height);
    if (inflated.size() < expected) {
        return nullptr;
    }

    std::vector<std::uint8_t> pixels;
    pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    std::size_t srcOffset = 0;
    std::size_t dstOffset = 0;
    for (std::uint32_t row = 0; row < height; ++row) {
        std::uint8_t filter = inflated[srcOffset++];
        if (filter != 0) {
            return nullptr;
        }
        std::memcpy(pixels.data() + static_cast<std::ptrdiff_t>(dstOffset),
                    inflated.data() + static_cast<std::ptrdiff_t>(srcOffset),
                    stride);
        srcOffset += stride;
        dstOffset += stride;
    }

    stbi_uc* result = static_cast<stbi_uc*>(std::malloc(pixels.size()));
    if (!result) {
        return nullptr;
    }
    std::memcpy(result, pixels.data(), pixels.size());
    if (x) {
        *x = static_cast<int>(width);
    }
    if (y) {
        *y = static_cast<int>(height);
    }
    if (comp) {
        *comp = 4;
    }
    return result;
}

void stbi_image_free(void* retval_from_stbi_load) {
    std::free(retval_from_stbi_load);
}

#endif // STB_IMAGE_IMPLEMENTATION
