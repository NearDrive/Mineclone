#include "voxel/ChunkStreaming.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <mutex>
#include <shared_mutex>

#include "persistence/ChunkStorage.h"
#include "voxel/Chunk.h"

#include "voxel/ChunkMesher.h"
#include "voxel/ChunkRegistry.h"
#include "voxel/VoxelCoords.h"
#include "voxel/WorldGen.h"

namespace voxel {

ChunkStreaming::ChunkStreaming(const ChunkStreamingConfig& config) : config_(config) {
    if (config_.loadRadius < config_.renderRadius) {
        config_.loadRadius = config_.renderRadius;
    }
    config_.verticalRadius = std::max(0, config_.verticalRadius);
    config_.nearDetailRadius = std::max(1, config_.nearDetailRadius);
    config_.midDetailRadius = std::max(config_.nearDetailRadius, config_.midDetailRadius);
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

void ChunkStreaming::SetBudgets(int maxCreatesPerFrame, int maxMeshesPerFrame, int maxUploadsPerFrame) {
    config_.maxChunkCreatesPerFrame = std::max(1, maxCreatesPerFrame);
    config_.maxChunkMeshesPerFrame = std::max(1, maxMeshesPerFrame);
    config_.maxGpuUploadsPerFrame = std::max(1, maxUploadsPerFrame);
}

void ChunkStreaming::SetUploadTimeBudgetMs(int maxUploadMsPerFrame) {
    config_.maxGpuUploadMsPerFrame = std::max(0, maxUploadMsPerFrame);
}

void ChunkStreaming::SetVerticalRadius(int radius) {
    config_.verticalRadius = std::max(0, radius);
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

void ChunkStreaming::SetProfiler(core::Profiler* profiler) {
    profiler_ = profiler;
}

const ChunkStreamingConfig& ChunkStreaming::Config() const {
    return config_;
}

const ChunkStreamingStats& ChunkStreaming::Stats() const {
    return stats_;
}

bool ChunkStreaming::RequestRemesh(const ChunkCoord& coord, ChunkRegistry& registry) {
    auto entry = registry.TryGetEntry(coord);
    if (!entry || !entry->wanted.load()) {
        return false;
    }

    if (entry->generationState.load(std::memory_order_acquire) != GenerationState::Ready) {
        return false;
    }

    MeshingState state = entry->meshingState.load(std::memory_order_acquire);
    while (state == MeshingState::NotScheduled || state == MeshingState::Ready) {
        if (entry->meshingState.compare_exchange_weak(state, MeshingState::Queued)) {
            meshQueue_.push(MeshJob{coord, entry, DetailTierForCoord(coord)});
            return true;
        }
    }

    return false;
}

void ChunkStreaming::BuildDesiredSet(const ChunkCoord& playerChunk) {
    const int radius = config_.loadRadius;
    const int minChunkY = WorldToChunkCoord(WorldBlockCoord{0, kWorldMinY, 0}, kChunkSize).y;
    const int maxChunkY = WorldToChunkCoord(WorldBlockCoord{0, kWorldMaxY, 0}, kChunkSize).y;
    const int clampedPlayerY = std::clamp(playerChunk.y, minChunkY, maxChunkY);
    const int minY = std::max(clampedPlayerY - config_.verticalRadius, minChunkY);
    const int maxY = std::min(clampedPlayerY + config_.verticalRadius, maxChunkY);
    const std::size_t layers = static_cast<std::size_t>(maxY - minY + 1);
    const std::size_t capacity = static_cast<std::size_t>((radius * 2 + 1) * (radius * 2 + 1)) * layers;
    desiredCoords_.clear();
    desiredCoords_.reserve(capacity);
    desiredSet_.clear();
    desiredSet_.reserve(capacity);

    for (int dy = minY; dy <= maxY; ++dy) {
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (std::max(std::abs(dx), std::abs(dz)) > radius) {
                    continue;
                }
                ChunkCoord coord{playerChunk.x + dx, dy, playerChunk.z + dz};
                desiredCoords_.push_back(coord);
                desiredSet_.insert(coord);
            }
        }
    }

    std::sort(desiredCoords_.begin(), desiredCoords_.end(),
              [&](const ChunkCoord& a, const ChunkCoord& b) {
                  const int da =
                      std::max({std::abs(a.x - playerChunk.x), std::abs(a.z - playerChunk.z), std::abs(a.y - playerChunk.y)});
                  const int db =
                      std::max({std::abs(b.x - playerChunk.x), std::abs(b.z - playerChunk.z), std::abs(b.y - playerChunk.y)});
                  return da < db;
              });
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
        if (storage_) {
            registry.SaveChunkIfDirty(coord, *storage_);
        }
        registry.RemoveChunk(coord);
    }
}

void ChunkStreaming::EnqueueMissing(ChunkRegistry& registry) {
    const int layerCount = config_.verticalRadius * 2 + 1;
    int createBudget = config_.maxChunkCreatesPerFrame * layerCount;
    int meshBudget = config_.maxChunkMeshesPerFrame * layerCount;

    for (const ChunkCoord& coord : desiredCoords_) {
        auto entry = registry.GetOrCreateEntry(coord);
        entry->wanted.store(true);

        if (createBudget > 0 && generateQueue_.size() < config_.maxGenerateQueueSize) {
            GenerationState genExpected = GenerationState::NotScheduled;
            if (entry->generationState.compare_exchange_strong(genExpected, GenerationState::Queued)) {
                generateQueue_.push(GenerateJob{coord, entry});
                ++stats_.createdThisFrame;
                --createBudget;
            }
        }

        if (meshBudget > 0 &&
            entry->generationState.load(std::memory_order_acquire) == GenerationState::Ready &&
            meshQueue_.size() < config_.maxMeshQueueSize) {
            MeshingState meshExpected = MeshingState::NotScheduled;
            if (entry->meshingState.compare_exchange_strong(meshExpected, MeshingState::Queued)) {
                meshQueue_.push(MeshJob{coord, entry, DetailTierForCoord(coord)});
                ++stats_.meshedThisFrame;
                --meshBudget;
            }
        }
    }
}


MeshDetailTier ChunkStreaming::DetailTierForCoord(const ChunkCoord& coord) const {
    const int dx = std::abs(coord.x - stats_.playerChunk.x);
    const int dz = std::abs(coord.z - stats_.playerChunk.z);
    const int distance = std::max(dx, dz);
    if (distance <= config_.nearDetailRadius) {
        return MeshDetailTier::Near;
    }
    if (distance <= config_.midDetailRadius) {
        return MeshDetailTier::Mid;
    }
    return MeshDetailTier::Far;
}

bool ChunkStreaming::IsDesired(const ChunkCoord& coord) const {
    return desiredSet_.contains(coord);
}

void ChunkStreaming::SetWorkerThreads(std::size_t workerThreads) {
    config_.workerThreads = static_cast<int>(workerThreads);
}

void ChunkStreaming::SetStorage(persistence::ChunkStorage* storage) {
    storage_ = storage;
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
    core::ScopedTimer uploadTimer(profiler_, core::Metric::Upload);
    const auto startTime = std::chrono::steady_clock::now();
    while (stats_.uploadedThisFrame < config_.maxGpuUploadsPerFrame) {
        if (config_.maxGpuUploadMsPerFrame > 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime);
            if (elapsed.count() >= config_.maxGpuUploadMsPerFrame) {
                break;
            }
        }
        MeshReady ready;
        if (!uploadQueue_.try_pop(ready)) {
            break;
        }

        auto entry = registry.TryGetEntry(ready.coord);
        if (!entry) {
#ifndef NDEBUG
            std::cout << "[Streaming] Dropped mesh upload for missing chunk (" << ready.coord.x << ", "
                      << ready.coord.y << ", " << ready.coord.z << ").\n";
#endif
            continue;
        }
        auto queuedEntry = ready.entry.lock();
        if (queuedEntry && queuedEntry.get() != entry.get()) {
#ifndef NDEBUG
            std::cout << "[Streaming] Dropped mesh upload for stale chunk (" << ready.coord.x << ", "
                      << ready.coord.y << ", " << ready.coord.z << ").\n";
#endif
            continue;
        }
        if (!entry->wanted.load()) {
#ifndef NDEBUG
            std::cout << "[Streaming] Dropped mesh upload for unloaded chunk (" << ready.coord.x << ", "
                      << ready.coord.y << ", " << ready.coord.z << ").\n";
#endif
            entry->gpuState.store(GpuState::NotUploaded, std::memory_order_release);
            entry->meshingState.store(MeshingState::NotScheduled, std::memory_order_release);
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
