#pragma once

#include <array>

#include <glm/vec3.hpp>

#include "voxel/VoxelCoords.h"

namespace voxel {

struct BlockFace {
    WorldBlockCoord neighborOffset;
    glm::vec3 normal;
    std::array<glm::vec3, 4> vertices;
};

inline const std::array<BlockFace, 6> kBlockFaces = {{
    // +X
    {{1, 0, 0},
     {1.0f, 0.0f, 0.0f},
     {glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{1.0f, 1.0f, 0.0f},
      glm::vec3{1.0f, 1.0f, 1.0f}, glm::vec3{1.0f, 0.0f, 1.0f}}},
    // -X
    {{-1, 0, 0},
     {-1.0f, 0.0f, 0.0f},
     {glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 0.0f, 1.0f},
      glm::vec3{0.0f, 1.0f, 1.0f}, glm::vec3{0.0f, 1.0f, 0.0f}}},
    // +Y
    {{0, 1, 0},
     {0.0f, 1.0f, 0.0f},
     {glm::vec3{0.0f, 1.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 1.0f},
      glm::vec3{1.0f, 1.0f, 1.0f}, glm::vec3{1.0f, 1.0f, 0.0f}}},
    // -Y
    {{0, -1, 0},
     {0.0f, -1.0f, 0.0f},
     {glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{1.0f, 0.0f, 0.0f},
      glm::vec3{1.0f, 0.0f, 1.0f}, glm::vec3{0.0f, 0.0f, 1.0f}}},
    // +Z
    {{0, 0, 1},
     {0.0f, 0.0f, 1.0f},
     {glm::vec3{0.0f, 0.0f, 1.0f}, glm::vec3{1.0f, 0.0f, 1.0f},
      glm::vec3{1.0f, 1.0f, 1.0f}, glm::vec3{0.0f, 1.0f, 1.0f}}},
    // -Z
    {{0, 0, -1},
     {0.0f, 0.0f, -1.0f},
     {glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f},
      glm::vec3{1.0f, 1.0f, 0.0f}, glm::vec3{1.0f, 0.0f, 0.0f}}},
}};

} // namespace voxel
