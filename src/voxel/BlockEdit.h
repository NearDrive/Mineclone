#pragma once

#include "voxel/BlockId.h"
#include "voxel/VoxelCoords.h"

namespace voxel {

class ChunkRegistry;
class ChunkStreaming;

bool TrySetBlock(ChunkRegistry& registry, ChunkStreaming& streaming, const WorldBlockCoord& world, BlockId id);

} // namespace voxel
