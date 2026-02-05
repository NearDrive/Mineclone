#pragma once

#include <cstddef>

#include "voxel/ChunkCoord.h"

namespace voxel {

constexpr int kRegionSizeChunksX = 4;
constexpr int kRegionSizeChunksZ = 4;

struct RegionCoord {
    int x = 0;
    int z = 0;

    bool operator==(const RegionCoord& other) const {
        return x == other.x && z == other.z;
    }
};

struct RegionCoordHash {
    std::size_t operator()(const RegionCoord& coord) const {
        const std::size_t hx = std::hash<int>{}(coord.x);
        const std::size_t hz = std::hash<int>{}(coord.z);
        return hx ^ (hz + 0x9e3779b9u + (hx << 6) + (hx >> 2));
    }
};

inline int FloorDiv(int value, int divisor) {
    int quotient = value / divisor;
    const int remainder = value % divisor;
    if (remainder != 0 && ((remainder > 0) != (divisor > 0))) {
        --quotient;
    }
    return quotient;
}

inline RegionCoord ChunkToRegionCoord(const ChunkCoord& chunk) {
    return RegionCoord{FloorDiv(chunk.x, kRegionSizeChunksX), FloorDiv(chunk.z, kRegionSizeChunksZ)};
}

} // namespace voxel
