#pragma once

#include <cstddef>
#include <unordered_map>

#include "voxel/BlockId.h"
#include "voxel/Chunk.h"
#include "voxel/ChunkCoord.h"
#include "voxel/ChunkMesh.h"
#include "voxel/VoxelCoords.h"

namespace voxel {

struct ChunkEntry {
    Chunk chunk;
    ChunkMesh mesh;
    bool hasChunkData = false;
    bool hasCpuMesh = false;
    bool hasGpuMesh = false;
};

class ChunkRegistry {
public:
    ChunkEntry& CreateChunk(const ChunkCoord& coord);
    void RemoveChunk(const ChunkCoord& coord);
    void DestroyAll();

    ChunkEntry* TryGetEntry(const ChunkCoord& coord);
    const ChunkEntry* TryGetEntry(const ChunkCoord& coord) const;

    Chunk* TryGetChunk(const ChunkCoord& coord);
    const Chunk* TryGetChunk(const ChunkCoord& coord) const;

    bool HasChunk(const ChunkCoord& coord) const;

    BlockId GetBlock(const WorldBlockCoord& world) const;
    BlockId GetBlockOrAir(const WorldBlockCoord& world) const;
    void SetBlock(const WorldBlockCoord& world, BlockId id);

    std::size_t LoadedCount() const;
    std::size_t GpuReadyCount() const;

    const std::unordered_map<ChunkCoord, ChunkEntry, ChunkCoordHash>& Entries() const;

private:
    void GenerateChunk(const ChunkCoord& coord, Chunk& chunk);

    std::unordered_map<ChunkCoord, ChunkEntry, ChunkCoordHash> entries_;
};

} // namespace voxel
