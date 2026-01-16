#pragma once

#include <unordered_map>

#include "voxel/BlockId.h"
#include "voxel/Chunk.h"
#include "voxel/ChunkCoord.h"
#include "voxel/VoxelCoords.h"

namespace voxel {

class ChunkManager {
public:
    Chunk& GetOrCreateChunk(const ChunkCoord& coord);
    const Chunk* TryGetChunk(const ChunkCoord& coord) const;

    BlockId GetBlock(const WorldBlockCoord& world) const;
    BlockId GetBlockOrAir(const WorldBlockCoord& world) const;
    void SetBlock(const WorldBlockCoord& world, BlockId id);

private:
    void GenerateChunk(const ChunkCoord& coord, Chunk& chunk);

    std::unordered_map<ChunkCoord, Chunk, ChunkCoordHash> chunks_;
};

} // namespace voxel
