#include "voxel/ChunkStreaming.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
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
    stats_.uploadedBytesThisFrame = 0;
    stats_.regionsUploadedThisFrame = 0;
    stats_.regionUploadedIndicesThisFrame = 0;
    stats_.regionDeferredThisFrame = 0;

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

    std::sort(desiredCoords_.begin(), desiredCoords_.end(), [&](const ChunkCoord& a, const ChunkCoord& b) {
        const int adx = a.x - playerChunk.x;
        const int ady = a.y - clampedPlayerY;
        const int adz = a.z - playerChunk.z;
        const int bdx = b.x - playerChunk.x;
        const int bdy = b.y - clampedPlayerY;
        const int bdz = b.z - playerChunk.z;

        const int aDist2 = adx * adx + ady * ady + adz * adz;
        const int bDist2 = bdx * bdx + bdy * bdy + bdz * bdz;
        if (aDist2 != bDist2) {
            return aDist2 < bDist2;
        }
        if (a.y != b.y) {
            return a.y < b.y;
        }
        if (a.z != b.z) {
            return a.z < b.z;
        }
        return a.x < b.x;
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
        MarkRegionDirtyForChunk(coord);
        if (storage_) {
            auto entry = registry.TryGetEntry(coord);
            if (entry && entry->dirty.load(std::memory_order_acquire) &&
                entry->generationState.load(std::memory_order_acquire) == GenerationState::Ready) {
                std::shared_lock<std::shared_mutex> lock(entry->dataMutex);
                if (entry->chunk && entry->dirty.load(std::memory_order_acquire)) {
                    auto chunkCopy = std::make_shared<const Chunk>(*entry->chunk);
                    saveQueue_.push(SaveJob{coord, chunkCopy});
                    entry->dirty.store(false, std::memory_order_release);
                }
            }
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
                entry->generationState.store(GenerationState::Queued, std::memory_order_release);
                generateQueue_.push(GenerateJob{coord, entry});
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

core::ThreadSafeQueue<SaveJob>& ChunkStreaming::SaveQueue() {
    return saveQueue_;
}

void ChunkStreaming::ProcessUploads(ChunkRegistry& registry) {
    core::ScopedTimer uploadTimer(profiler_, core::Metric::Upload);
    const auto distanceSq = [&](const ChunkCoord& coord) {
        const int dx = coord.x - stats_.playerChunk.x;
        const int dy = coord.y - stats_.playerChunk.y;
        const int dz = coord.z - stats_.playerChunk.z;
        return dx * dx + dy * dy + dz * dz;
    };

    const auto encodeUploadPriority = [&](const MeshReady& ready) {
        const std::size_t meshBytes = ready.cpuMesh ? ready.cpuMesh->ByteSize() : 0;
        const std::uint64_t dist2 = static_cast<std::uint64_t>(distanceSq(ready.coord));
        const std::uint64_t cappedBytes =
            std::min<std::uint64_t>(meshBytes, static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()));
        return (dist2 << 32) | cappedBytes;
    };

    const auto enqueueDeferred = [&](MeshReady&& ready) {
        const std::uint64_t priority = encodeUploadPriority(ready);
        deferredUploads_.push(DeferredUpload{
            std::move(ready),
            priority,
            deferredUploadSequence_++,
        });
    };

    MeshReady popped;
    int remainingSortBudget = std::max(0, config_.maxGpuUploadsPerFrame);
    while (remainingSortBudget > 0 && uploadQueue_.try_pop(popped)) {
        enqueueDeferred(std::move(popped));
        --remainingSortBudget;
    }

    std::size_t uploadedBytes = 0;
    while (stats_.uploadedThisFrame < config_.maxGpuUploadsPerFrame && !deferredUploads_.empty()) {
        DeferredUpload deferred = deferredUploads_.top();
        deferredUploads_.pop();
        MeshReady& ready = deferred.ready;

        if (!ready.cpuMesh) {
            continue;
        }

        auto entry = registry.TryGetEntry(ready.coord);
        if (!entry) {
            continue;
        }
        auto queuedEntry = ready.entry.lock();
        if (queuedEntry && queuedEntry.get() != entry.get()) {
            continue;
        }
        if (!entry->wanted.load()) {
            entry->gpuState.store(GpuState::NotUploaded, std::memory_order_release);
            entry->meshingState.store(MeshingState::NotScheduled, std::memory_order_release);
            continue;
        }

        if (!IsDesired(ready.coord)) {
            entry->gpuState.store(GpuState::NotUploaded, std::memory_order_release);
            entry->meshingState.store(MeshingState::NotScheduled, std::memory_order_release);
            continue;
        }

        if (entry->gpuState.load(std::memory_order_acquire) != GpuState::UploadQueued) {
            continue;
        }

        const std::size_t meshBytes = ready.cpuMesh->ByteSize();
        const bool hasBudget = uploadedBytes + meshBytes <= config_.maxGpuUploadBytesPerFrame;
        if (!hasBudget && stats_.uploadedThisFrame > 0) {
            enqueueDeferred(std::move(ready));
            continue;
        }

        entry->cpuMesh = std::move(*ready.cpuMesh);
        entry->cpuMeshReady.store(true, std::memory_order_release);
        entry->gpuState.store(GpuState::Uploaded, std::memory_order_release);
        MarkRegionDirtyForChunk(ready.coord);
        ++stats_.uploadedThisFrame;
        uploadedBytes += meshBytes;
    }

    stats_.uploadedBytesThisFrame = uploadedBytes;
}

void ChunkStreaming::QueueDirtyRegion(const RegionCoord& coord) {
    if (dirtyRegionSet_.insert(coord).second) {
        dirtyRegions_.push_back(coord);
    }
}

void ChunkStreaming::MarkRegionDirtyForChunk(const ChunkCoord& coord) {
    RegionCoord regionCoord = ChunkToRegionCoord(coord);
    RegionMeshEntry& region = regions_[regionCoord];
    region.dirty = true;
    region.dirtyChunks.insert(coord);
    QueueDirtyRegion(regionCoord);
}

void ChunkStreaming::EraseChunkFromRegionMesh(RegionMeshEntry& region, const ChunkCoord& coord) {
    auto spanIt = region.chunkSpans.find(coord);
    if (spanIt == region.chunkSpans.end()) {
        return;
    }

    RegionChunkSpan span = spanIt->second;
    const std::size_t vertexEnd = span.vertexStart + span.vertexCount;
    const std::size_t indexEnd = span.indexStart + span.indexCount;
    region.mesh.Vertices().erase(region.mesh.Vertices().begin() + static_cast<std::ptrdiff_t>(span.vertexStart),
                                 region.mesh.Vertices().begin() + static_cast<std::ptrdiff_t>(vertexEnd));
    region.mesh.Indices().erase(region.mesh.Indices().begin() + static_cast<std::ptrdiff_t>(span.indexStart),
                                region.mesh.Indices().begin() + static_cast<std::ptrdiff_t>(indexEnd));

    for (auto& [otherCoord, otherSpan] : region.chunkSpans) {
        if (otherCoord == coord) {
            continue;
        }
        if (otherSpan.vertexStart > span.vertexStart) {
            otherSpan.vertexStart -= span.vertexCount;
        }
        if (otherSpan.indexStart > span.indexStart) {
            otherSpan.indexStart -= span.indexCount;
            for (std::size_t i = 0; i < otherSpan.indexCount; ++i) {
                region.mesh.Indices()[otherSpan.indexStart + i] -= static_cast<std::uint32_t>(span.vertexCount);
            }
        }
    }

    region.chunkSpans.erase(spanIt);
    region.chunks.erase(coord);
}

void ChunkStreaming::AppendChunkToRegionMesh(RegionMeshEntry& region, const ChunkCoord& coord, const ChunkMeshCpu& cpuMesh) {
    if (cpuMesh.indices.empty() || cpuMesh.vertices.empty()) {
        return;
    }

    RegionChunkSpan span;
    span.vertexStart = region.mesh.Vertices().size();
    span.vertexCount = cpuMesh.vertices.size();
    span.indexStart = region.mesh.Indices().size();
    span.indexCount = cpuMesh.indices.size();

    region.mesh.Vertices().insert(region.mesh.Vertices().end(), cpuMesh.vertices.begin(), cpuMesh.vertices.end());
    const std::uint32_t baseVertex = static_cast<std::uint32_t>(span.vertexStart);
    for (std::uint32_t index : cpuMesh.indices) {
        region.mesh.Indices().push_back(baseVertex + index);
    }

    region.chunkSpans[coord] = span;
    region.chunks.insert(coord);
}

void ChunkStreaming::ProcessRegionUploads(ChunkRegistry& registry) {
    std::vector<RegionCoord> budgeted;
    budgeted.reserve(dirtyRegions_.size());
    while (!dirtyRegions_.empty()) {
        budgeted.push_back(dirtyRegions_.front());
        dirtyRegionSet_.erase(dirtyRegions_.front());
        dirtyRegions_.pop_front();
    }

    std::sort(budgeted.begin(), budgeted.end(), [&](const RegionCoord& a, const RegionCoord& b) {
        const int aMinChunkX = a.x * kRegionSizeChunksX;
        const int aMinChunkZ = a.z * kRegionSizeChunksZ;
        const int aMaxChunkX = aMinChunkX + (kRegionSizeChunksX - 1);
        const int aMaxChunkZ = aMinChunkZ + (kRegionSizeChunksZ - 1);
        const int bMinChunkX = b.x * kRegionSizeChunksX;
        const int bMinChunkZ = b.z * kRegionSizeChunksZ;
        const int bMaxChunkX = bMinChunkX + (kRegionSizeChunksX - 1);
        const int bMaxChunkZ = bMinChunkZ + (kRegionSizeChunksZ - 1);

        const int adx = std::max({stats_.playerChunk.x - aMaxChunkX, 0, aMinChunkX - stats_.playerChunk.x});
        const int adz = std::max({stats_.playerChunk.z - aMaxChunkZ, 0, aMinChunkZ - stats_.playerChunk.z});
        const int bdx = std::max({stats_.playerChunk.x - bMaxChunkX, 0, bMinChunkX - stats_.playerChunk.x});
        const int bdz = std::max({stats_.playerChunk.z - bMaxChunkZ, 0, bMinChunkZ - stats_.playerChunk.z});

        const int aChebyshev = std::max(adx, adz);
        const int bChebyshev = std::max(bdx, bdz);
        if (aChebyshev != bChebyshev) {
            return aChebyshev < bChebyshev;
        }

        const int aManhattan = adx + adz;
        const int bManhattan = bdx + bdz;
        if (aManhattan != bManhattan) {
            return aManhattan < bManhattan;
        }

        if (a.x != b.x) {
            return a.x < b.x;
        }
        return a.z < b.z;
    });

    const int maxRegions = std::max(0, config_.maxRegionUploadsPerFrame);
    const std::size_t maxIndices = config_.maxRegionUploadIndicesPerFrame;
    std::size_t uploadedIndices = 0;

    for (const RegionCoord& coord : budgeted) {
        auto regionIt = regions_.find(coord);
        if (regionIt == regions_.end()) {
            continue;
        }
        RegionMeshEntry& region = regionIt->second;

        if (stats_.regionsUploadedThisFrame >= maxRegions) {
            ++stats_.regionDeferredThisFrame;
            QueueDirtyRegion(coord);
            continue;
        }

        std::vector<ChunkCoord> dirtyChunks(region.dirtyChunks.begin(), region.dirtyChunks.end());
        std::sort(dirtyChunks.begin(), dirtyChunks.end(), [](const ChunkCoord& a, const ChunkCoord& b) {
            if (a.y != b.y) return a.y < b.y;
            if (a.x != b.x) return a.x < b.x;
            return a.z < b.z;
        });

        std::size_t regionDeltaIndices = 0;
        for (const ChunkCoord& chunkCoord : dirtyChunks) {
            EraseChunkFromRegionMesh(region, chunkCoord);

            if (!desiredSet_.contains(chunkCoord)) {
                continue;
            }

            auto entry = registry.TryGetEntry(chunkCoord);
            if (!entry) {
                continue;
            }
            if (!entry->cpuMeshReady.load(std::memory_order_acquire) ||
                entry->meshingState.load(std::memory_order_acquire) != MeshingState::Ready) {
                continue;
            }

            regionDeltaIndices += entry->cpuMesh.indices.size();
            AppendChunkToRegionMesh(region, chunkCoord, entry->cpuMesh);
        }

        const bool fitsIndexBudget = maxIndices == 0 || uploadedIndices + regionDeltaIndices <= maxIndices;
        if (!fitsIndexBudget && stats_.regionsUploadedThisFrame > 0) {
            ++stats_.regionDeferredThisFrame;
            QueueDirtyRegion(coord);
            continue;
        }

        region.mesh.UploadToGpu();
        region.mesh.ClearCpu();
        region.dirty = false;
        region.dirtyChunks.clear();

        if (region.chunks.empty()) {
            region.mesh.DestroyGpu();
        }

        ++stats_.regionsUploadedThisFrame;
        uploadedIndices += regionDeltaIndices;
    }

    stats_.regionUploadedIndicesThisFrame = uploadedIndices;
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
