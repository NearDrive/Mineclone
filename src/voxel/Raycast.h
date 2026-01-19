#pragma once

#include <glm/glm.hpp>

namespace voxel {

class ChunkRegistry;

struct RaycastHit {
    bool hit = false;
    glm::ivec3 block{0};
    glm::ivec3 normal{0};
    float t = 0.0f;
};

RaycastHit RaycastBlocks(const ChunkRegistry& registry, const glm::vec3& origin, const glm::vec3& direction,
                         float maxDistance);

} // namespace voxel
