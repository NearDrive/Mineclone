#include "voxel/ChunkRegistry.h"

#include "voxel/WorldGen.h"

namespace voxel {

ChunkEntry& ChunkRegistry::CreateChunk(const ChunkCoord& coord) {
    auto [it, inserted] = entries_.emplace(coord, ChunkEntry{});
    if (inserted || !it->second.hasChunkData) {
        GenerateChunk(coord, it->second.chunk);
        it->second.hasChunkData = true;
        it->second.hasCpuMesh = false;
        it->second.hasGpuMesh = false;
    }
    return it->second;
}

void ChunkRegistry::RemoveChunk(const ChunkCoord& coord) {
    auto it = entries_.find(coord);
    if (it == entries_.end()) {
        return;
    }

    it->second.mesh.DestroyGpu();
    it->second.mesh.Clear();
    entries_.erase(it);
}

void ChunkRegistry::DestroyAll() {
    for (auto& [coord, entry] : entries_) {
        (void)coord;
        entry.mesh.DestroyGpu();
    }
    entries_.clear();
}

ChunkEntry* ChunkRegistry::TryGetEntry(const ChunkCoord& coord) {
    auto it = entries_.find(coord);
    if (it == entries_.end()) {
        return nullptr;
    }
    return &it->second;
}

const ChunkEntry* ChunkRegistry::TryGetEntry(const ChunkCoord& coord) const {
    auto it = entries_.find(coord);
    if (it == entries_.end()) {
        return nullptr;
    }
    return &it->second;
}

Chunk* ChunkRegistry::TryGetChunk(const ChunkCoord& coord) {
    auto* entry = TryGetEntry(coord);
    if (!entry || !entry->hasChunkData) {
        return nullptr;
    }
    return &entry->chunk;
}

const Chunk* ChunkRegistry::TryGetChunk(const ChunkCoord& coord) const {
    const auto* entry = TryGetEntry(coord);
    if (!entry || !entry->hasChunkData) {
        return nullptr;
    }
    return &entry->chunk;
}

bool ChunkRegistry::HasChunk(const ChunkCoord& coord) const {
    auto it = entries_.find(coord);
    return it != entries_.end() && it->second.hasChunkData;
}

BlockId ChunkRegistry::GetBlock(const WorldBlockCoord& world) const {
    ChunkCoord chunkCoord = WorldToChunkCoord(world, kChunkSize);
    LocalCoord local = WorldToLocalCoord(world, kChunkSize);
    const Chunk* chunk = TryGetChunk(chunkCoord);
    if (!chunk) {
        return SampleFlatWorld(world);
    }
    return chunk->Get(local.x, local.y, local.z);
}

BlockId ChunkRegistry::GetBlockOrAir(const WorldBlockCoord& world) const {
    ChunkCoord chunkCoord = WorldToChunkCoord(world, kChunkSize);
    LocalCoord local = WorldToLocalCoord(world, kChunkSize);
    const Chunk* chunk = TryGetChunk(chunkCoord);
    if (!chunk) {
        return kBlockAir;
    }
    return chunk->Get(local.x, local.y, local.z);
}

void ChunkRegistry::SetBlock(const WorldBlockCoord& world, BlockId id) {
    ChunkCoord chunkCoord = WorldToChunkCoord(world, kChunkSize);
    LocalCoord local = WorldToLocalCoord(world, kChunkSize);
    ChunkEntry& entry = CreateChunk(chunkCoord);
    entry.chunk.Set(local.x, local.y, local.z, id);
}

std::size_t ChunkRegistry::LoadedCount() const {
    return entries_.size();
}

std::size_t ChunkRegistry::GpuReadyCount() const {
    std::size_t ready = 0;
    for (const auto& [coord, entry] : entries_) {
        (void)coord;
        if (entry.hasGpuMesh) {
            ++ready;
        }
    }
    return ready;
}

const std::unordered_map<ChunkCoord, ChunkEntry, ChunkCoordHash>& ChunkRegistry::Entries() const {
    return entries_;
}

void ChunkRegistry::GenerateChunk(const ChunkCoord& coord, Chunk& chunk) {
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
