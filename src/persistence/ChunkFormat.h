#pragma once

#include <array>
#include <cstdint>

namespace persistence {

struct ChunkFileHeader {
    std::array<char, 8> magic{};
    std::uint32_t version = 0;
    std::int32_t cx = 0;
    std::int32_t cy = 0;
    std::int32_t cz = 0;
    std::uint32_t chunkSize = 0;
    std::uint32_t blockTypeBytes = 0;
    std::uint32_t payloadBytes = 0;
};

constexpr std::array<char, 8> kChunkMagic = {'M', 'C', 'L', 'C', 'H', 'N', 'K', '\0'};
constexpr std::uint32_t kChunkVersion = 1;
constexpr std::size_t kChunkHeaderSize = 8 + 4 + 4 + 4 + 4 + 4 + 4 + 4;

} // namespace persistence
