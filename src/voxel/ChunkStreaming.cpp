#include "voxel/ChunkStreaming.h"

#include <algorithm>
#include <cmath>

#include "voxel/ChunkMesher.h"
#include "voxel/ChunkRegistry.h"

namespace voxel {

ChunkStreaming::ChunkStreaming(const ChunkStreamingConfig& config) : config_(config) {
    if (config_.loadRadius < config_.renderRadius) {
        config_.loadRadius = config_.renderRadius;
    }
}

void ChunkStreaming::SetRenderRadius(int radius) {
    config_.renderRadius = std::max(1, radius);
    if (config_.loadRadius < config_.renderRadius) {
        config_.loadRadius = config_.renderRadius;
    }
}

void ChunkStreaming::SetLoadRadius(int radius) {
    config_.loadRadius = std::max(1, radius);
    if (config_.loadRadius < config_.renderRadius) {
        config_.loadRadius = config_.renderRadius;
    }
}

int ChunkStreaming::RenderRadius() const {
    return config_.renderRadius;
}

int ChunkStreaming::LoadRadius() const {
    return config_.loadRadius;
}

void ChunkStreaming::SetEnabled(bool enabled) {
    config_.enabled = enabled;
}

bool ChunkStreaming::Enabled() const {
    return config_.enabled;
}

void ChunkStreaming::Tick(const ChunkCoord& playerChunk, ChunkRegistry& registry, const ChunkMesher& mesher) {
    stats_.playerChunk = playerChunk;
    stats_.createdThisFrame = 0;
    stats_.meshedThisFrame = 0;
    stats_.uploadedThisFrame = 0;

    if (!config_.enabled) {
        stats_.loadedChunks = registry.LoadedCount();
        stats_.gpuReadyChunks = registry.GpuReadyCount();
        stats_.createQueue = toCreate_.size();
        stats_.meshQueue = toMesh_.size();
        stats_.uploadQueue = toUpload_.size();
        return;
    }

    BuildDesiredSet(playerChunk);

    PruneQueue(toCreate_, scheduledCreate_);
    PruneQueue(toMesh_, scheduledMesh_);
    PruneQueue(toUpload_, scheduledUpload_);

    UnloadOutOfRange(registry);
    EnqueueMissing(registry);

    while (stats_.createdThisFrame < config_.maxChunkCreatesPerFrame && !toCreate_.empty()) {
        ChunkCoord coord = toCreate_.front();
        toCreate_.pop_front();
        scheduledCreate_.erase(coord);

        if (!IsDesired(coord) || registry.HasChunk(coord)) {
            continue;
        }

        registry.CreateChunk(coord);
        ++stats_.createdThisFrame;

        if (scheduledMesh_.insert(coord).second) {
            toMesh_.push_back(coord);
        }
    }

    while (stats_.meshedThisFrame < config_.maxChunkMeshesPerFrame && !toMesh_.empty()) {
        ChunkCoord coord = toMesh_.front();
        toMesh_.pop_front();
        scheduledMesh_.erase(coord);

        if (!IsDesired(coord)) {
            continue;
        }

        ChunkEntry* entry = registry.TryGetEntry(coord);
        if (!entry || !entry->hasChunkData || entry->hasCpuMesh) {
            continue;
        }

        mesher.BuildMesh(coord, entry->chunk, registry, entry->mesh);
        entry->hasCpuMesh = true;
        entry->hasGpuMesh = false;
        ++stats_.meshedThisFrame;

        if (scheduledUpload_.insert(coord).second) {
            toUpload_.push_back(coord);
        }
    }

    while (stats_.uploadedThisFrame < config_.maxGpuUploadsPerFrame && !toUpload_.empty()) {
        ChunkCoord coord = toUpload_.front();
        toUpload_.pop_front();
        scheduledUpload_.erase(coord);

        if (!IsDesired(coord)) {
            continue;
        }

        ChunkEntry* entry = registry.TryGetEntry(coord);
        if (!entry || !entry->hasCpuMesh || entry->hasGpuMesh) {
            continue;
        }

        entry->mesh.UploadToGpu();
        entry->hasGpuMesh = true;
        ++stats_.uploadedThisFrame;
    }

    stats_.loadedChunks = registry.LoadedCount();
    stats_.gpuReadyChunks = registry.GpuReadyCount();
    stats_.createQueue = toCreate_.size();
    stats_.meshQueue = toMesh_.size();
    stats_.uploadQueue = toUpload_.size();
}

const ChunkStreamingConfig& ChunkStreaming::Config() const {
    return config_;
}

const ChunkStreamingStats& ChunkStreaming::Stats() const {
    return stats_;
}

void ChunkStreaming::BuildDesiredSet(const ChunkCoord& playerChunk) {
    const int radius = config_.loadRadius;
    const std::size_t capacity = static_cast<std::size_t>((radius * 2 + 1) * (radius * 2 + 1));
    desiredCoords_.clear();
    desiredCoords_.reserve(capacity);
    desiredSet_.clear();
    desiredSet_.reserve(capacity);

    for (int dz = -radius; dz <= radius; ++dz) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (std::max(std::abs(dx), std::abs(dz)) > radius) {
                continue;
            }
            ChunkCoord coord{playerChunk.x + dx, 0, playerChunk.z + dz};
            desiredCoords_.push_back(coord);
            desiredSet_.insert(coord);
        }
    }
}

void ChunkStreaming::PruneQueue(std::deque<ChunkCoord>& queue,
                               std::unordered_set<ChunkCoord, ChunkCoordHash>& scheduled) {
    if (queue.empty()) {
        return;
    }

    std::deque<ChunkCoord> filtered;
    filtered.resize(0);
    for (const ChunkCoord& coord : queue) {
        if (IsDesired(coord)) {
            filtered.push_back(coord);
        } else {
            scheduled.erase(coord);
        }
    }
    queue.swap(filtered);
}

void ChunkStreaming::UnloadOutOfRange(ChunkRegistry& registry) {
    unloadList_.clear();
    unloadList_.reserve(registry.LoadedCount());

    for (const auto& [coord, entry] : registry.Entries()) {
        (void)entry;
        if (!IsDesired(coord)) {
            unloadList_.push_back(coord);
        }
    }

    for (const ChunkCoord& coord : unloadList_) {
        registry.RemoveChunk(coord);
        scheduledCreate_.erase(coord);
        scheduledMesh_.erase(coord);
        scheduledUpload_.erase(coord);
    }
}

void ChunkStreaming::EnqueueMissing(const ChunkRegistry& registry) {
    for (const ChunkCoord& coord : desiredCoords_) {
        if (!registry.HasChunk(coord) && scheduledCreate_.insert(coord).second) {
            toCreate_.push_back(coord);
        }
    }
}

bool ChunkStreaming::IsDesired(const ChunkCoord& coord) const {
    return desiredSet_.contains(coord);
}

} // namespace voxel
