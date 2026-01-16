#include "voxel/ChunkManager.h"

#include "voxel/WorldGen.h"

namespace voxel {

Chunk& ChunkManager::GetOrCreateChunk(const ChunkCoord& coord) {
    auto it = chunks_.find(coord);
    if (it != chunks_.end()) {
        return it->second;
    }

    auto [insertedIt, inserted] = chunks_.emplace(coord, Chunk{});
    if (inserted) {
        GenerateChunk(coord, insertedIt->second);
    }
    return insertedIt->second;
}

const Chunk* ChunkManager::TryGetChunk(const ChunkCoord& coord) const {
    auto it = chunks_.find(coord);
    if (it == chunks_.end()) {
        return nullptr;
    }
    return &it->second;
}

BlockId ChunkManager::GetBlock(const WorldBlockCoord& world) const {
    ChunkCoord chunkCoord = WorldToChunkCoord(world, kChunkSize);
    LocalCoord local = WorldToLocalCoord(world, kChunkSize);
    const Chunk* chunk = TryGetChunk(chunkCoord);
    if (!chunk) {
        return SampleFlatWorld(world);
    }
    return chunk->Get(local.x, local.y, local.z);
}

void ChunkManager::SetBlock(const WorldBlockCoord& world, BlockId id) {
    ChunkCoord chunkCoord = WorldToChunkCoord(world, kChunkSize);
    LocalCoord local = WorldToLocalCoord(world, kChunkSize);
    Chunk& chunk = GetOrCreateChunk(chunkCoord);
    chunk.Set(local.x, local.y, local.z, id);
}

void ChunkManager::GenerateChunk(const ChunkCoord& coord, Chunk& chunk) {
    for (int z = 0; z < kChunkSize; ++z) {
        for (int y = 0; y < kChunkSize; ++y) {
            for (int x = 0; x < kChunkSize; ++x) {
                LocalCoord local{x, y, z};
                WorldBlockCoord world = ChunkLocalToWorld(coord, local, kChunkSize);
                chunk.Set(x, y, z, SampleFlatWorld(world));
            }
        }
    }
}

} // namespace voxel
