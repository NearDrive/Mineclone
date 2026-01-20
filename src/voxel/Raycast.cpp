#include "voxel/Raycast.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

#include "voxel/ChunkRegistry.h"
#include "voxel/VoxelCoords.h"

namespace voxel {

namespace {
double SafeInverse(double value) {
    if (value == 0.0) {
        return std::numeric_limits<double>::infinity();
    }
    return 1.0 / value;
}
} // namespace

RaycastHit RaycastBlocks(const ChunkRegistry& registry, const glm::vec3& origin, const glm::vec3& direction,
                         float maxDistance) {
    RaycastHit result;

    const double dirLen = glm::length(glm::dvec3(direction));
    if (dirLen <= 0.0) {
        return result;
    }

    const glm::dvec3 dir = glm::dvec3(direction) / dirLen;
    const glm::dvec3 originD(origin);
    const double maxDistanceD = static_cast<double>(maxDistance);
    constexpr double kEpsilon = 1e-9;

    glm::ivec3 block = glm::ivec3(glm::floor(originD));
    for (int axis = 0; axis < 3; ++axis) {
        const double axisPos = originD[axis];
        const double axisFrac = axisPos - std::floor(axisPos);
        if (std::abs(axisFrac) <= kEpsilon && dir[axis] < 0.0) {
            block[axis] -= 1;
        }
    }
    glm::ivec3 step{
        dir.x > 0.0 ? 1 : (dir.x < 0.0 ? -1 : 0),
        dir.y > 0.0 ? 1 : (dir.y < 0.0 ? -1 : 0),
        dir.z > 0.0 ? 1 : (dir.z < 0.0 ? -1 : 0)};

    glm::dvec3 tMax;
    glm::dvec3 tDelta;

    const glm::dvec3 invDir(SafeInverse(dir.x), SafeInverse(dir.y), SafeInverse(dir.z));

    auto nextBoundary = [&](int axis) {
        const double originAxis = originD[axis];
        const double blockAxis = static_cast<double>(block[axis]);
        if (dir[axis] > 0.0) {
            return (blockAxis + 1.0f - originAxis) * invDir[axis];
        }
        if (dir[axis] < 0.0) {
            return (originAxis - blockAxis) * -invDir[axis];
        }
        return std::numeric_limits<double>::infinity();
    };

    tMax.x = nextBoundary(0);
    tMax.y = nextBoundary(1);
    tMax.z = nextBoundary(2);

    tDelta.x = std::abs(invDir.x);
    tDelta.y = std::abs(invDir.y);
    tDelta.z = std::abs(invDir.z);

    double t = 0.0;
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

    const int maxSteps = std::min(4096, static_cast<int>(std::ceil(maxDistanceD)) * 3 + 8);
    for (int stepCount = 0; stepCount < maxSteps && t <= maxDistanceD; ++stepCount) {
        const double minT = std::min({tMax.x, tMax.y, tMax.z});
        const bool advanceX = tMax.x <= minT + kEpsilon;
        const bool advanceY = tMax.y <= minT + kEpsilon;
        const bool advanceZ = tMax.z <= minT + kEpsilon;

        if (advanceX) {
            block.x += step.x;
            tMax.x += tDelta.x;
            hitNormal = glm::ivec3(-step.x, 0, 0);
        }
        if (advanceY) {
            block.y += step.y;
            tMax.y += tDelta.y;
            hitNormal = glm::ivec3(0, -step.y, 0);
        }
        if (advanceZ) {
            block.z += step.z;
            tMax.z += tDelta.z;
            hitNormal = glm::ivec3(0, 0, -step.z);
        }

        t = minT;
        if (t > maxDistanceD) {
            break;
        }

        if (isSolid(block)) {
            result.hit = true;
            result.block = block;
            result.normal = hitNormal;
            result.t = static_cast<float>(t);
            return result;
        }
    }

    return result;
}

} // namespace voxel
