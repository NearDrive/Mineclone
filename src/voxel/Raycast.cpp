#include "voxel/Raycast.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

#include "voxel/ChunkRegistry.h"
#include "voxel/VoxelCoords.h"

namespace voxel {

namespace {
double FracPart(double value) {
    return value - std::floor(value);
}

bool NearlyZero(double value, double eps) {
    return std::abs(value) <= eps;
}

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
        const double axisFrac = FracPart(originD[axis]);
        if (NearlyZero(axisFrac, kEpsilon) && dir[axis] < 0.0) {
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
    const double inf = std::numeric_limits<double>::infinity();

    auto intBound = [&](double s, int stepAxis) {
        if (stepAxis > 0) {
            return (std::floor(s) + 1.0 - s);
        }
        if (stepAxis < 0) {
            return (s - std::floor(s));
        }
        return inf;
    };

    tDelta.x = step.x != 0 ? (1.0 / std::abs(dir.x)) : inf;
    tDelta.y = step.y != 0 ? (1.0 / std::abs(dir.y)) : inf;
    tDelta.z = step.z != 0 ? (1.0 / std::abs(dir.z)) : inf;

    tMax.x = step.x != 0 ? intBound(originD.x, step.x) * tDelta.x : inf;
    tMax.y = step.y != 0 ? intBound(originD.y, step.y) * tDelta.y : inf;
    tMax.z = step.z != 0 ? intBound(originD.z, step.z) * tDelta.z : inf;

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

    const int maxSteps = 4096;
    for (int stepCount = 0; stepCount < maxSteps && t <= maxDistanceD + kEpsilon; ++stepCount) {
        const double minT = std::min({tMax.x, tMax.y, tMax.z});
        const bool advanceX = tMax.x <= minT + kEpsilon;
        const bool advanceY = tMax.y <= minT + kEpsilon;
        const bool advanceZ = tMax.z <= minT + kEpsilon;
        hitNormal = glm::ivec3(0);

        if (advanceX) {
            block.x += step.x;
            tMax.x += tDelta.x;
            hitNormal.x = -step.x;
        }
        if (advanceY) {
            block.y += step.y;
            tMax.y += tDelta.y;
            hitNormal.y = -step.y;
        }
        if (advanceZ) {
            block.z += step.z;
            tMax.z += tDelta.z;
            hitNormal.z = -step.z;
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
