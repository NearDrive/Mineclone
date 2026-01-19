#pragma once

#include <glm/glm.hpp>

#include "voxel/ChunkRegistry.h"

namespace physics {

constexpr float kVoxelEpsilon = 1e-4f;

struct Aabb {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

enum class Axis {
    X = 0,
    Y = 1,
    Z = 2
};

Aabb MakePlayerAabb(const glm::vec3& feetPosition, float width, float height, float depth);

bool AabbIntersectsSolid(const voxel::ChunkRegistry& registry, const Aabb& aabb, float epsilon = kVoxelEpsilon);

bool FindBlockingVoxelOnAxis(const voxel::ChunkRegistry& registry,
                             const Aabb& aabb,
                             Axis axis,
                             bool positiveDirection,
                             int& hitCoord,
                             float epsilon = kVoxelEpsilon);

} // namespace physics
