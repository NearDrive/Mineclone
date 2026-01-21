#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace voxel {

struct ChunkCoord {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;

    bool operator==(const ChunkCoord& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct ChunkCoordHash {
    std::size_t operator()(const ChunkCoord& coord) const {
        std::size_t h1 = std::hash<std::int32_t>{}(coord.x);
        std::size_t h2 = std::hash<std::int32_t>{}(coord.y);
        std::size_t h3 = std::hash<std::int32_t>{}(coord.z);
        std::size_t seed = h1;
        seed ^= h2 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= h3 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

} // namespace voxel
