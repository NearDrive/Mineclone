#pragma once

#include <cstddef>
#include <unordered_set>
#include <vector>

#include "core/ThreadSafeQueue.h"
#include "voxel/ChunkCoord.h"
#include "voxel/ChunkJobs.h"

namespace voxel {

class ChunkMesher;
class ChunkRegistry;

struct ChunkStreamingConfig {
    int loadRadius = 10;
    int renderRadius = 8;
    int maxChunkCreatesPerFrame = 3;
    int maxChunkMeshesPerFrame = 2;
    int maxGpuUploadsPerFrame = 3;
    int workerThreads = 2;
    bool enabled = true;
};

struct ChunkStreamingStats {
    ChunkCoord playerChunk{0, 0, 0};
    std::size_t loadedChunks = 0;
    std::size_t generatedChunksReady = 0;
    std::size_t meshedCpuReady = 0;
    std::size_t gpuReadyChunks = 0;
    std::size_t createQueue = 0;
    std::size_t meshQueue = 0;
    std::size_t uploadQueue = 0;
    std::size_t workerThreads = 0;
    int createdThisFrame = 0;
    int meshedThisFrame = 0;
    int uploadedThisFrame = 0;
};

class ChunkStreaming {
public:
    explicit ChunkStreaming(const ChunkStreamingConfig& config = {});

    void SetRenderRadius(int radius);
    void SetLoadRadius(int radius);
    int RenderRadius() const;
    int LoadRadius() const;

    void SetEnabled(bool enabled);
    bool Enabled() const;

    void Tick(const ChunkCoord& playerChunk, ChunkRegistry& registry, const ChunkMesher& mesher);
    void SetWorkerThreads(std::size_t workerThreads);

    core::ThreadSafeQueue<GenerateJob>& GenerateQueue();
    core::ThreadSafeQueue<MeshJob>& MeshQueue();
    core::ThreadSafeQueue<MeshReady>& UploadQueue();

    const ChunkStreamingConfig& Config() const;
    const ChunkStreamingStats& Stats() const;

private:
    void ProcessUploads(ChunkRegistry& registry);
    void BuildDesiredSet(const ChunkCoord& playerChunk);
    void UnloadOutOfRange(ChunkRegistry& registry);
    void EnqueueMissing(ChunkRegistry& registry);

    bool IsDesired(const ChunkCoord& coord) const;
    void UpdateStats(const ChunkRegistry& registry);
    void WarnIfQueuesLarge();

    ChunkStreamingConfig config_;
    ChunkStreamingStats stats_;

    std::vector<ChunkCoord> desiredCoords_;
    std::unordered_set<ChunkCoord, ChunkCoordHash> desiredSet_;
    std::vector<ChunkCoord> unloadList_;

    core::ThreadSafeQueue<GenerateJob> generateQueue_;
    core::ThreadSafeQueue<MeshJob> meshQueue_;
    core::ThreadSafeQueue<MeshReady> uploadQueue_;

    bool warnedGenerateQueue_ = false;
    bool warnedMeshQueue_ = false;
    bool warnedUploadQueue_ = false;
};

} // namespace voxel
