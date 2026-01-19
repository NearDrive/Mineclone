#include "voxel/Raycast.h"

#include <cassert>
#include <cmath>
#include <limits>

#include "voxel/ChunkRegistry.h"
#include "voxel/VoxelCoords.h"

namespace voxel {

namespace {
float SafeInverse(float value) {
    if (value == 0.0f) {
        return std::numeric_limits<float>::infinity();
    }
    return 1.0f / value;
}
}

RaycastHit RaycastBlocks(const ChunkRegistry& registry, const glm::vec3& origin, const glm::vec3& direction,
                         float maxDistance) {
    RaycastHit result;

    const float dirLen = glm::length(direction);
    if (dirLen <= 0.0f) {
        return result;
    }

#ifndef NDEBUG
    assert(std::abs(dirLen - 1.0f) < 0.01f);
#endif

    glm::ivec3 block = glm::ivec3(glm::floor(origin));
    glm::ivec3 step{
        direction.x > 0.0f ? 1 : (direction.x < 0.0f ? -1 : 0),
        direction.y > 0.0f ? 1 : (direction.y < 0.0f ? -1 : 0),
        direction.z > 0.0f ? 1 : (direction.z < 0.0f ? -1 : 0)};

    glm::vec3 tMax;
    glm::vec3 tDelta;

    const glm::vec3 invDir = glm::vec3(SafeInverse(direction.x), SafeInverse(direction.y), SafeInverse(direction.z));

    auto nextBoundary = [&](int axis) {
        const float originAxis = origin[axis];
        const float blockAxis = static_cast<float>(block[axis]);
        if (direction[axis] > 0.0f) {
            return (blockAxis + 1.0f - originAxis) * invDir[axis];
        }
        if (direction[axis] < 0.0f) {
            return (originAxis - blockAxis) * -invDir[axis];
        }
        return std::numeric_limits<float>::infinity();
    };

    tMax.x = nextBoundary(0);
    tMax.y = nextBoundary(1);
    tMax.z = nextBoundary(2);

    tDelta.x = std::abs(invDir.x);
    tDelta.y = std::abs(invDir.y);
    tDelta.z = std::abs(invDir.z);

    float t = 0.0f;
    glm::ivec3 hitNormal{0};

    auto isSolid = [&](const glm::ivec3& sample) {
        WorldBlockCoord world{sample.x, sample.y, sample.z};
        return registry.GetBlockOrAir(world) != kBlockAir;
    };

    if (isSolid(block)) {
        result.hit = true;
        result.block = block;
        result.normal = hitNormal;
        result.t = 0.0f;
        return result;
    }

    while (t <= maxDistance) {
        if (tMax.x < tMax.y) {
            if (tMax.x < tMax.z) {
                block.x += step.x;
                t = tMax.x;
                tMax.x += tDelta.x;
                hitNormal = glm::ivec3(-step.x, 0, 0);
            } else {
                block.z += step.z;
                t = tMax.z;
                tMax.z += tDelta.z;
                hitNormal = glm::ivec3(0, 0, -step.z);
            }
        } else {
            if (tMax.y < tMax.z) {
                block.y += step.y;
                t = tMax.y;
                tMax.y += tDelta.y;
                hitNormal = glm::ivec3(0, -step.y, 0);
            } else {
                block.z += step.z;
                t = tMax.z;
                tMax.z += tDelta.z;
                hitNormal = glm::ivec3(0, 0, -step.z);
            }
        }

        if (t > maxDistance) {
            break;
        }

        if (isSolid(block)) {
            result.hit = true;
            result.block = block;
            result.normal = hitNormal;
            result.t = t;
            return result;
        }
    }

    return result;
}

} // namespace voxel
