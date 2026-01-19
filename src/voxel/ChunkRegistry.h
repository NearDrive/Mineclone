#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include "voxel/BlockId.h"
#include "voxel/Chunk.h"
#include "voxel/ChunkCoord.h"
#include "voxel/ChunkJobs.h"
#include "voxel/ChunkMesh.h"
#include "voxel/VoxelCoords.h"

namespace persistence {
class ChunkStorage;
}

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
    std::atomic<bool> dirty{false};
    std::atomic<bool> wanted{true};
    mutable std::shared_mutex dataMutex;
};

struct ChunkReadHandle {
    std::shared_ptr<const ChunkEntry> entry;
    std::shared_lock<std::shared_mutex> lock;
    const Chunk* chunk = nullptr;

    explicit operator bool() const { return chunk != nullptr; }
};

class ChunkRegistry {
public:
    std::shared_ptr<ChunkEntry> GetOrCreateEntry(const ChunkCoord& coord);
    void RemoveChunk(const ChunkCoord& coord);
    void DestroyAll();

    void SetStorage(persistence::ChunkStorage* storage);
    bool SaveChunkIfDirty(const ChunkCoord& coord, persistence::ChunkStorage& storage);
    std::size_t SaveAllDirty(persistence::ChunkStorage& storage);

    std::shared_ptr<ChunkEntry> TryGetEntry(const ChunkCoord& coord);
    std::shared_ptr<const ChunkEntry> TryGetEntry(const ChunkCoord& coord) const;

    bool HasChunk(const ChunkCoord& coord) const;

    ChunkReadHandle AcquireChunkRead(const ChunkCoord& coord) const;

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
    persistence::ChunkStorage* storage_ = nullptr;
};

} // namespace voxel
