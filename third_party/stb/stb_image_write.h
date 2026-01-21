// Minimal stb_image_write-compatible PNG writer for Mineclone render tests.
// Writes RGBA8 PNGs with no compression (stored deflate blocks) and filter type 0.

#ifndef STB_IMAGE_WRITE_H
#define STB_IMAGE_WRITE_H

#ifdef __cplusplus
extern "C" {
#endif

int stbi_write_png(char const* filename, int w, int h, int comp, const void* data, int stride_in_bytes);

#ifdef __cplusplus
}
#endif

#endif // STB_IMAGE_WRITE_H

#ifdef STB_IMAGE_WRITE_IMPLEMENTATION

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

constexpr std::uint8_t kPngSignature[8] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};

std::uint32_t Crc32(const std::uint8_t* data, std::size_t length) {
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            std::uint32_t mask = static_cast<std::uint32_t>(-(crc & 1u));
            crc = (crc >> 1u) ^ (0xedb88320u & mask);
        }
    }
    return crc ^ 0xffffffffu;
}

std::uint32_t Adler32(const std::uint8_t* data, std::size_t length) {
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    constexpr std::uint32_t kMod = 65521u;
    for (std::size_t i = 0; i < length; ++i) {
        a = (a + data[i]) % kMod;
        b = (b + a) % kMod;
    }
    return (b << 16u) | a;
}

void WriteU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
    out.push_back(static_cast<std::uint8_t>(value & 0xffu));
}

void WriteChunk(std::vector<std::uint8_t>& out, const char* type, const std::vector<std::uint8_t>& data) {
    WriteU32(out, static_cast<std::uint32_t>(data.size()));
    std::size_t start = out.size();
    out.push_back(static_cast<std::uint8_t>(type[0]));
    out.push_back(static_cast<std::uint8_t>(type[1]));
    out.push_back(static_cast<std::uint8_t>(type[2]));
    out.push_back(static_cast<std::uint8_t>(type[3]));
    out.insert(out.end(), data.begin(), data.end());
    std::uint32_t crc = Crc32(out.data() + start, out.size() - start);
    WriteU32(out, crc);
}

bool WriteFile(const char* filename, const std::vector<std::uint8_t>& data) {
    std::FILE* file = std::fopen(filename, "wb");
    if (!file) {
        return false;
    }
    bool ok = std::fwrite(data.data(), 1, data.size(), file) == data.size();
    std::fclose(file);
    return ok;
}

} // namespace

int stbi_write_png(char const* filename, int w, int h, int comp, const void* data, int stride_in_bytes) {
    if (!filename || !data || w <= 0 || h <= 0 || comp != 4 || stride_in_bytes <= 0) {
        return 0;
    }

    const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(h) * (static_cast<std::size_t>(w) * 4u + 1u));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0); // filter type 0
        const std::uint8_t* row = bytes + static_cast<std::ptrdiff_t>(y) * stride_in_bytes;
        raw.insert(raw.end(), row, row + static_cast<std::ptrdiff_t>(w) * 4);
    }

    std::vector<std::uint8_t> zlib;
    zlib.push_back(0x78);
    zlib.push_back(0x01);
    std::size_t offset = 0;
    while (offset < raw.size()) {
        std::size_t chunk = std::min<std::size_t>(65535, raw.size() - offset);
        std::uint8_t final = (offset + chunk == raw.size()) ? 1u : 0u;
        zlib.push_back(final);
        std::uint16_t len = static_cast<std::uint16_t>(chunk);
        std::uint16_t nlen = static_cast<std::uint16_t>(~len);
        zlib.push_back(static_cast<std::uint8_t>(len & 0xffu));
        zlib.push_back(static_cast<std::uint8_t>((len >> 8u) & 0xffu));
        zlib.push_back(static_cast<std::uint8_t>(nlen & 0xffu));
        zlib.push_back(static_cast<std::uint8_t>((nlen >> 8u) & 0xffu));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                    raw.begin() + static_cast<std::ptrdiff_t>(offset + chunk));
        offset += chunk;
    }
    std::uint32_t adler = Adler32(raw.data(), raw.size());
    WriteU32(zlib, adler);

    std::vector<std::uint8_t> png;
    png.insert(png.end(), std::begin(kPngSignature), std::end(kPngSignature));

    std::vector<std::uint8_t> ihdr;
    WriteU32(ihdr, static_cast<std::uint32_t>(w));
    WriteU32(ihdr, static_cast<std::uint32_t>(h));
    ihdr.push_back(8); // bit depth
    ihdr.push_back(6); // color type RGBA
    ihdr.push_back(0); // compression
    ihdr.push_back(0); // filter
    ihdr.push_back(0); // interlace
    WriteChunk(png, "IHDR", ihdr);
    WriteChunk(png, "IDAT", zlib);
    WriteChunk(png, "IEND", {});

    return WriteFile(filename, png) ? 1 : 0;
}

#endif // STB_IMAGE_WRITE_IMPLEMENTATION
