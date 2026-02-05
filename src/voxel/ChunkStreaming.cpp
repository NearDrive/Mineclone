#include "voxel/ChunkStreaming.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_set>

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
    ProcessRegionUploads(registry);
    UpdateStats(registry);
    WarnIfQueuesLarge();
    (void)mesher;
}

std::vector<RegionCoord> ChunkStreaming::CollectDrawableRegions(const ChunkCoord& playerChunk) const {
    std::vector<RegionCoord> drawable;
    drawable.reserve(regions_.size());
    std::unordered_set<RegionCoord, RegionCoordHash> emitted;

    for (const auto& [coord, region] : regions_) {
        if (region.mesh.GpuIndexCount() == 0) {
            continue;
        }

        const int minChunkX = coord.x * kRegionSizeChunksX;
        const int minChunkZ = coord.z * kRegionSizeChunksZ;
        const int maxChunkX = minChunkX + (kRegionSizeChunksX - 1);
        const int maxChunkZ = minChunkZ + (kRegionSizeChunksZ - 1);

        const int dx = std::max({playerChunk.x - maxChunkX, 0, minChunkX - playerChunk.x});
        const int dz = std::max({playerChunk.z - maxChunkZ, 0, minChunkZ - playerChunk.z});
        if (std::max(dx, dz) > config_.renderRadius) {
            continue;
        }

        if (emitted.insert(coord).second) {
            drawable.push_back(coord);
        }
    }

    return drawable;
}

bool ChunkStreaming::DrawRegion(const RegionCoord& region) const {
    auto it = regions_.find(region);
    if (it == regions_.end()) {
        return false;
    }
    if (it->second.mesh.GpuIndexCount() == 0) {
        return false;
    }
    it->second.mesh.Draw();
    return true;
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
            entry->cpuMeshReady.store(false, std::memory_order_release);
            meshQueue_.push(MeshJob{coord, entry});
            return true;
        }
    }

    return false;
}

void ChunkStreaming::MarkRegionDirty(const ChunkCoord& coord) {
    MarkRegionDirtyForChunk(coord);
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
        MarkRegionDirtyForChunk(coord);
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

        if (createBudget > 0) {
            GenerationState genExpected = GenerationState::NotScheduled;
            if (entry->generationState.compare_exchange_strong(genExpected, GenerationState::Generating)) {
                bool loaded = false;
                if (storage_) {
                    voxel::Chunk chunk;
                    if (storage_->LoadChunk(coord, chunk)) {
                        std::unique_lock<std::shared_mutex> lock(entry->dataMutex);
                        entry->chunk = std::make_unique<voxel::Chunk>(std::move(chunk));
                        entry->generationState.store(GenerationState::Ready, std::memory_order_release);
                        entry->dirty.store(false, std::memory_order_release);
                        loaded = true;
                    }
                }
                if (!loaded) {
                    entry->generationState.store(GenerationState::Queued, std::memory_order_release);
                    generateQueue_.push(GenerateJob{coord, entry});
                }
                ++stats_.createdThisFrame;
                --createBudget;
            }
        }

        if (meshBudget > 0 &&
            entry->generationState.load(std::memory_order_acquire) == GenerationState::Ready) {
            MeshingState meshExpected = MeshingState::NotScheduled;
            if (entry->meshingState.compare_exchange_strong(meshExpected, MeshingState::Queued)) {
                entry->cpuMeshReady.store(false, std::memory_order_release);
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
    while (stats_.uploadedThisFrame < config_.maxGpuUploadsPerFrame) {
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

        entry->cpuMesh = std::move(*ready.cpuMesh);
        entry->cpuMeshReady.store(true, std::memory_order_release);
        entry->gpuState.store(GpuState::Uploaded, std::memory_order_release);
        MarkRegionDirtyForChunk(ready.coord);
        ++stats_.uploadedThisFrame;
    }
}

void ChunkStreaming::MarkRegionDirtyForChunk(const ChunkCoord& coord) {
    RegionCoord regionCoord = ChunkToRegionCoord(coord);
    RegionMeshEntry& region = regions_[regionCoord];
    region.dirty = true;
}

void ChunkStreaming::BuildDirtyRegionList() {
    dirtyRegions_.clear();
    for (const auto& [coord, region] : regions_) {
        if (region.dirty) {
            dirtyRegions_.push_back(coord);
        }
    }
}

void ChunkStreaming::ProcessRegionUploads(ChunkRegistry& registry) {
    BuildDirtyRegionList();
    for (const RegionCoord& coord : dirtyRegions_) {
        auto regionIt = regions_.find(coord);
        if (regionIt == regions_.end()) {
            continue;
        }
        RegionMeshEntry& region = regionIt->second;

        std::vector<ChunkCoord> chunksToKeep;
        chunksToKeep.reserve(region.chunks.size() + 8);

        for (const ChunkCoord& chunkCoord : desiredCoords_) {
            if (ChunkToRegionCoord(chunkCoord) == coord) {
                chunksToKeep.push_back(chunkCoord);
            }
        }

        region.chunks.clear();
        ChunkMeshCpu merged;
        for (const ChunkCoord& chunkCoord : chunksToKeep) {
            auto entry = registry.TryGetEntry(chunkCoord);
            if (!entry) {
                continue;
            }
            if (!entry->cpuMeshReady.load(std::memory_order_acquire)) {
                continue;
            }
            if (entry->meshingState.load(std::memory_order_acquire) != MeshingState::Ready) {
                continue;
            }

            const std::uint32_t baseVertex = static_cast<std::uint32_t>(merged.vertices.size());
            merged.vertices.insert(merged.vertices.end(), entry->cpuMesh.vertices.begin(), entry->cpuMesh.vertices.end());
            for (std::uint32_t index : entry->cpuMesh.indices) {
                merged.indices.push_back(baseVertex + index);
            }
            region.chunks.insert(chunkCoord);
        }

        region.mesh.Clear();
        if (!merged.indices.empty()) {
            region.mesh.Vertices() = std::move(merged.vertices);
            region.mesh.Indices() = std::move(merged.indices);
            region.mesh.UploadToGpu();
            region.mesh.ClearCpu();
        }

        region.dirty = false;
        if (region.chunks.empty()) {
            region.mesh.DestroyGpu();
        }
    }
}

void ChunkStreaming::UpdateStats(const ChunkRegistry& registry) {
    stats_.loadedChunks = 0;
    stats_.generatedChunksReady = 0;
    stats_.meshedCpuReady = 0;
    stats_.gpuReadyChunks = 0;
    stats_.gpuReadyRegions = 0;

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

    for (const auto& [coord, region] : regions_) {
        (void)coord;
        if (region.mesh.GpuIndexCount() > 0) {
            ++stats_.gpuReadyRegions;
        }
    }

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
