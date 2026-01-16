#ifndef MINECLONE_VOXEL_CHUNK_BOUNDS_H
#define MINECLONE_VOXEL_CHUNK_BOUNDS_H

#include <cassert>

#include <glm/glm.hpp>

#include "voxel/Chunk.h"
#include "voxel/ChunkCoord.h"

namespace voxel {

struct ChunkBounds {
    glm::vec3 min;
    glm::vec3 max;
};

inline ChunkBounds GetChunkBounds(const ChunkCoord& coord) {
    const glm::vec3 origin = glm::vec3(coord.x, coord.y, coord.z) * static_cast<float>(kChunkSize);
    ChunkBounds bounds{origin, origin + glm::vec3(static_cast<float>(kChunkSize))};
    assert(bounds.min.x <= bounds.max.x);
    assert(bounds.min.y <= bounds.max.y);
    assert(bounds.min.z <= bounds.max.z);
    return bounds;
}

} // namespace voxel

#endif
