#pragma once

#include <cstdint>

#include "voxel/ChunkCoord.h"

namespace voxel {

struct WorldBlockCoord {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
};

struct LocalCoord {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
};

constexpr int floor_div(int a, int b) {
    int q = a / b;
    int r = a % b;
    if ((r != 0) && ((r < 0) != (b < 0))) {
        --q;
    }
    return q;
}

constexpr int floor_mod(int a, int b) {
    int r = a % b;
    if (r < 0) {
        r += (b < 0) ? -b : b;
    }
    return r;
}

inline ChunkCoord WorldToChunkCoord(const WorldBlockCoord& world, int chunkSize) {
    return ChunkCoord{
        floor_div(world.x, chunkSize),
        floor_div(world.y, chunkSize),
        floor_div(world.z, chunkSize)};
}

inline LocalCoord WorldToLocalCoord(const WorldBlockCoord& world, int chunkSize) {
    return LocalCoord{
        floor_mod(world.x, chunkSize),
        floor_mod(world.y, chunkSize),
        floor_mod(world.z, chunkSize)};
}

inline WorldBlockCoord ChunkLocalToWorld(const ChunkCoord& chunk, const LocalCoord& local,
                                         int chunkSize) {
    return WorldBlockCoord{
        chunk.x * chunkSize + local.x,
        chunk.y * chunkSize + local.y,
        chunk.z * chunkSize + local.z};
}

} // namespace voxel
