#pragma once

#include <cstddef>
#include <deque>
#include <unordered_set>
#include <vector>

#include "voxel/ChunkCoord.h"

namespace voxel {

class ChunkMesher;
class ChunkRegistry;

struct ChunkStreamingConfig {
    int loadRadius = 10;
    int renderRadius = 8;
    int maxChunkCreatesPerFrame = 3;
    int maxChunkMeshesPerFrame = 2;
    int maxGpuUploadsPerFrame = 3;
    bool enabled = true;
};

struct ChunkStreamingStats {
    ChunkCoord playerChunk{0, 0, 0};
    std::size_t loadedChunks = 0;
    std::size_t gpuReadyChunks = 0;
    std::size_t createQueue = 0;
    std::size_t meshQueue = 0;
    std::size_t uploadQueue = 0;
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

    const ChunkStreamingConfig& Config() const;
    const ChunkStreamingStats& Stats() const;

private:
    void BuildDesiredSet(const ChunkCoord& playerChunk);
    void PruneQueue(std::deque<ChunkCoord>& queue,
                    std::unordered_set<ChunkCoord, ChunkCoordHash>& scheduled);
    void UnloadOutOfRange(ChunkRegistry& registry);
    void EnqueueMissing(const ChunkRegistry& registry);

    bool IsDesired(const ChunkCoord& coord) const;

    ChunkStreamingConfig config_;
    ChunkStreamingStats stats_;

    std::vector<ChunkCoord> desiredCoords_;
    std::unordered_set<ChunkCoord, ChunkCoordHash> desiredSet_;
    std::vector<ChunkCoord> unloadList_;

    std::deque<ChunkCoord> toCreate_;
    std::deque<ChunkCoord> toMesh_;
    std::deque<ChunkCoord> toUpload_;

    std::unordered_set<ChunkCoord, ChunkCoordHash> scheduledCreate_;
    std::unordered_set<ChunkCoord, ChunkCoordHash> scheduledMesh_;
    std::unordered_set<ChunkCoord, ChunkCoordHash> scheduledUpload_;
};

} // namespace voxel
