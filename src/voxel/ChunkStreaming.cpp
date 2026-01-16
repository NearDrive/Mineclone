#include "voxel/ChunkStreaming.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>

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
        UpdateStats(registry);
        return;
    }

    BuildDesiredSet(playerChunk);

    UnloadOutOfRange(registry);
    EnqueueMissing(registry);
    ProcessUploads(registry);
    UpdateStats(registry);
    WarnIfQueuesLarge();
    (void)mesher;
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

void ChunkStreaming::UnloadOutOfRange(ChunkRegistry& registry) {
    unloadList_.clear();
    unloadList_.reserve(registry.LoadedCount());

    registry.ForEachEntry([&](const ChunkCoord& coord, const std::shared_ptr<ChunkEntry>& entry) {
        (void)entry;
        if (!IsDesired(coord)) {
            unloadList_.push_back(coord);
        }
    });

    for (const ChunkCoord& coord : unloadList_) {
        registry.RemoveChunk(coord);
    }
}

void ChunkStreaming::EnqueueMissing(ChunkRegistry& registry) {
    int createBudget = config_.maxChunkCreatesPerFrame;
    int meshBudget = config_.maxChunkMeshesPerFrame;

    for (const ChunkCoord& coord : desiredCoords_) {
        auto entry = registry.GetOrCreateEntry(coord);
        entry->wanted.store(true);

        if (createBudget > 0) {
            GenerationState genExpected = GenerationState::NotScheduled;
            if (entry->generationState.compare_exchange_strong(genExpected, GenerationState::Queued)) {
                generateQueue_.push(GenerateJob{coord, entry});
                ++stats_.createdThisFrame;
                --createBudget;
            }
        }

        if (meshBudget > 0 &&
            entry->generationState.load(std::memory_order_acquire) == GenerationState::Ready) {
            MeshingState meshExpected = MeshingState::NotScheduled;
            if (entry->meshingState.compare_exchange_strong(meshExpected, MeshingState::Queued)) {
                meshQueue_.push(MeshJob{coord, entry});
                ++stats_.meshedThisFrame;
                --meshBudget;
            }
        }
    }
}

bool ChunkStreaming::IsDesired(const ChunkCoord& coord) const {
    return desiredSet_.contains(coord);
}

void ChunkStreaming::SetWorkerThreads(std::size_t workerThreads) {
    config_.workerThreads = static_cast<int>(workerThreads);
}

core::ThreadSafeQueue<GenerateJob>& ChunkStreaming::GenerateQueue() {
    return generateQueue_;
}

core::ThreadSafeQueue<MeshJob>& ChunkStreaming::MeshQueue() {
    return meshQueue_;
}

core::ThreadSafeQueue<MeshReady>& ChunkStreaming::UploadQueue() {
    return uploadQueue_;
}

void ChunkStreaming::ProcessUploads(ChunkRegistry& registry) {
    while (stats_.uploadedThisFrame < config_.maxGpuUploadsPerFrame) {
        MeshReady ready;
        if (!uploadQueue_.try_pop(ready)) {
            break;
        }

        auto entry = ready.entry.lock();
        if (!entry || !entry->wanted.load()) {
            std::cout << "[Streaming] Dropped mesh upload for unloaded chunk.\n";
            if (entry) {
                entry->gpuState.store(GpuState::NotUploaded, std::memory_order_release);
                entry->meshingState.store(MeshingState::NotScheduled, std::memory_order_release);
            }
            continue;
        }

        if (!IsDesired(ready.coord)) {
            std::cout << "[Streaming] Dropped mesh upload for out-of-range chunk.\n";
            entry->gpuState.store(GpuState::NotUploaded, std::memory_order_release);
            entry->meshingState.store(MeshingState::NotScheduled, std::memory_order_release);
            continue;
        }

        if (entry->gpuState.load(std::memory_order_acquire) != GpuState::UploadQueued) {
            continue;
        }

        entry->mesh.Clear();
        entry->mesh.Vertices() = std::move(ready.cpuMesh->vertices);
        entry->mesh.Indices() = std::move(ready.cpuMesh->indices);
        entry->mesh.UploadToGpu();
        entry->mesh.ClearCpu();
        entry->gpuState.store(GpuState::Uploaded, std::memory_order_release);
        ++stats_.uploadedThisFrame;
    }
}

void ChunkStreaming::UpdateStats(const ChunkRegistry& registry) {
    stats_.loadedChunks = 0;
    stats_.generatedChunksReady = 0;
    stats_.meshedCpuReady = 0;
    stats_.gpuReadyChunks = 0;

    registry.ForEachEntry([&](const ChunkCoord& coord, const std::shared_ptr<ChunkEntry>& entry) {
        (void)coord;
        ++stats_.loadedChunks;
        if (entry->generationState.load(std::memory_order_acquire) == GenerationState::Ready) {
            ++stats_.generatedChunksReady;
        }
        if (entry->meshingState.load(std::memory_order_acquire) == MeshingState::Ready) {
            ++stats_.meshedCpuReady;
        }
        if (entry->gpuState.load(std::memory_order_acquire) == GpuState::Uploaded) {
            ++stats_.gpuReadyChunks;
        }
    });

    stats_.createQueue = generateQueue_.size();
    stats_.meshQueue = meshQueue_.size();
    stats_.uploadQueue = uploadQueue_.size();
    stats_.workerThreads = static_cast<std::size_t>(config_.workerThreads);
}

void ChunkStreaming::WarnIfQueuesLarge() {
    constexpr std::size_t kWarnThreshold = 256;
    const std::size_t createSize = generateQueue_.size();
    const std::size_t meshSize = meshQueue_.size();
    const std::size_t uploadSize = uploadQueue_.size();

    if (createSize > kWarnThreshold) {
        if (!warnedGenerateQueue_) {
            std::cout << "[Streaming] Warning: generate queue size " << createSize << ".\n";
            warnedGenerateQueue_ = true;
        }
    } else {
        warnedGenerateQueue_ = false;
    }

    if (meshSize > kWarnThreshold) {
        if (!warnedMeshQueue_) {
            std::cout << "[Streaming] Warning: mesh queue size " << meshSize << ".\n";
            warnedMeshQueue_ = true;
        }
    } else {
        warnedMeshQueue_ = false;
    }

    if (uploadSize > kWarnThreshold) {
        if (!warnedUploadQueue_) {
            std::cout << "[Streaming] Warning: upload queue size " << uploadSize << ".\n";
            warnedUploadQueue_ = true;
        }
    } else {
        warnedUploadQueue_ = false;
    }
}

} // namespace voxel
