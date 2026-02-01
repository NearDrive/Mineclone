#pragma once

#include "voxel/BlockId.h"
#include "voxel/VoxelCoords.h"

namespace voxel {

constexpr int kWorldMinY = -32;
constexpr int kWorldMaxY = 64;

int GetSurfaceHeight(int x, int z);

BlockId SampleFlatWorld(const WorldBlockCoord& coord);

} // namespace voxel
