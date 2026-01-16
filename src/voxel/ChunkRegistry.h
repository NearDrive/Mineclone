#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "voxel/BlockId.h"
#include "voxel/Chunk.h"
#include "voxel/ChunkCoord.h"
#include "voxel/ChunkJobs.h"
#include "voxel/ChunkMesh.h"
#include "voxel/VoxelCoords.h"

namespace voxel {

enum class GenerationState {
    NotScheduled,
    Queued,
    Generating,
    Ready
};

enum class MeshingState {
    NotScheduled,
    Queued,
    Meshing,
    Ready
};

enum class GpuState {
    NotUploaded,
    UploadQueued,
    Uploaded
};

struct ChunkEntry {
    ChunkMesh mesh;

    std::unique_ptr<Chunk> chunk;
    std::atomic<GenerationState> generationState{GenerationState::NotScheduled};
    std::atomic<MeshingState> meshingState{MeshingState::NotScheduled};
    std::atomic<GpuState> gpuState{GpuState::NotUploaded};
    std::atomic<bool> wanted{true};
    mutable std::mutex dataMutex;
};

class ChunkRegistry {
public:
    std::shared_ptr<ChunkEntry> GetOrCreateEntry(const ChunkCoord& coord);
    void RemoveChunk(const ChunkCoord& coord);
    void DestroyAll();

    std::shared_ptr<ChunkEntry> TryGetEntry(const ChunkCoord& coord);
    std::shared_ptr<const ChunkEntry> TryGetEntry(const ChunkCoord& coord) const;

    Chunk* TryGetChunk(const ChunkCoord& coord);
    const Chunk* TryGetChunk(const ChunkCoord& coord) const;

    bool HasChunk(const ChunkCoord& coord) const;

    BlockId GetBlock(const WorldBlockCoord& world) const;
    BlockId GetBlockOrAir(const WorldBlockCoord& world) const;
    void SetBlock(const WorldBlockCoord& world, BlockId id);

    std::size_t LoadedCount() const;
    std::size_t GpuReadyCount() const;

    void ForEachEntry(const std::function<void(const ChunkCoord&, const std::shared_ptr<ChunkEntry>&)>& fn) const;
    std::vector<std::shared_ptr<ChunkEntry>> EntriesSnapshot() const;

    static void GenerateChunkData(const ChunkCoord& coord, Chunk& chunk);

private:
    mutable std::mutex entriesMutex_;
    std::unordered_map<ChunkCoord, std::shared_ptr<ChunkEntry>, ChunkCoordHash> entries_;
};

} // namespace voxel
