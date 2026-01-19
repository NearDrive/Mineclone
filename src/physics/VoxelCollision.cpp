#include "physics/VoxelCollision.h"

#include <cmath>

#include "voxel/BlockId.h"
#include "voxel/VoxelCoords.h"

namespace physics {

namespace {
inline bool IsSolid(voxel::BlockId id) {
    return id != voxel::kBlockAir;
}

inline voxel::WorldBlockCoord MakeCoord(int x, int y, int z) {
    return voxel::WorldBlockCoord{static_cast<std::int32_t>(x),
                                  static_cast<std::int32_t>(y),
                                  static_cast<std::int32_t>(z)};
}
} // namespace

Aabb MakePlayerAabb(const glm::vec3& feetPosition, float width, float height, float depth) {
    const float halfWidth = width * 0.5f;
    const float halfDepth = depth * 0.5f;
    Aabb aabb;
    aabb.min = glm::vec3(feetPosition.x - halfWidth,
                         feetPosition.y,
                         feetPosition.z - halfDepth);
    aabb.max = glm::vec3(feetPosition.x + halfWidth,
                         feetPosition.y + height,
                         feetPosition.z + halfDepth);
    return aabb;
}

bool AabbIntersectsSolid(const voxel::ChunkRegistry& registry, const Aabb& aabb, float epsilon) {
    const int xMin = static_cast<int>(std::floor(aabb.min.x));
    const int yMin = static_cast<int>(std::floor(aabb.min.y));
    const int zMin = static_cast<int>(std::floor(aabb.min.z));
    const int xMax = static_cast<int>(std::floor(aabb.max.x - epsilon));
    const int yMax = static_cast<int>(std::floor(aabb.max.y - epsilon));
    const int zMax = static_cast<int>(std::floor(aabb.max.z - epsilon));

    for (int y = yMin; y <= yMax; ++y) {
        for (int z = zMin; z <= zMax; ++z) {
            for (int x = xMin; x <= xMax; ++x) {
                if (IsSolid(registry.GetBlock(MakeCoord(x, y, z)))) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool FindBlockingVoxelOnAxis(const voxel::ChunkRegistry& registry,
                             const Aabb& aabb,
                             Axis axis,
                             bool positiveDirection,
                             int& hitCoord,
                             float epsilon) {
    const int xMin = static_cast<int>(std::floor(aabb.min.x));
    const int yMin = static_cast<int>(std::floor(aabb.min.y));
    const int zMin = static_cast<int>(std::floor(aabb.min.z));
    const int xMax = static_cast<int>(std::floor(aabb.max.x - epsilon));
    const int yMax = static_cast<int>(std::floor(aabb.max.y - epsilon));
    const int zMax = static_cast<int>(std::floor(aabb.max.z - epsilon));

    if (axis == Axis::X) {
        if (positiveDirection) {
            for (int x = xMin; x <= xMax; ++x) {
                for (int y = yMin; y <= yMax; ++y) {
                    for (int z = zMin; z <= zMax; ++z) {
                        if (IsSolid(registry.GetBlock(MakeCoord(x, y, z)))) {
                            hitCoord = x;
                            return true;
                        }
                    }
                }
            }
        } else {
            for (int x = xMax; x >= xMin; --x) {
                for (int y = yMin; y <= yMax; ++y) {
                    for (int z = zMin; z <= zMax; ++z) {
                        if (IsSolid(registry.GetBlock(MakeCoord(x, y, z)))) {
                            hitCoord = x;
                            return true;
                        }
                    }
                }
            }
        }
    } else if (axis == Axis::Y) {
        if (positiveDirection) {
            for (int y = yMin; y <= yMax; ++y) {
                for (int x = xMin; x <= xMax; ++x) {
                    for (int z = zMin; z <= zMax; ++z) {
                        if (IsSolid(registry.GetBlock(MakeCoord(x, y, z)))) {
                            hitCoord = y;
                            return true;
                        }
                    }
                }
            }
        } else {
            for (int y = yMax; y >= yMin; --y) {
                for (int x = xMin; x <= xMax; ++x) {
                    for (int z = zMin; z <= zMax; ++z) {
                        if (IsSolid(registry.GetBlock(MakeCoord(x, y, z)))) {
                            hitCoord = y;
                            return true;
                        }
                    }
                }
            }
        }
    } else {
        if (positiveDirection) {
            for (int z = zMin; z <= zMax; ++z) {
                for (int x = xMin; x <= xMax; ++x) {
                    for (int y = yMin; y <= yMax; ++y) {
                        if (IsSolid(registry.GetBlock(MakeCoord(x, y, z)))) {
                            hitCoord = z;
                            return true;
                        }
                    }
                }
            }
        } else {
            for (int z = zMax; z >= zMin; --z) {
                for (int x = xMin; x <= xMax; ++x) {
                    for (int y = yMin; y <= yMax; ++y) {
                        if (IsSolid(registry.GetBlock(MakeCoord(x, y, z)))) {
                            hitCoord = z;
                            return true;
                        }
                    }
                }
            }
        }
    }

    return false;
}

} // namespace physics
