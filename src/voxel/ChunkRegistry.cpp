#include "voxel/ChunkRegistry.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <queue>
#include <shared_mutex>
#include <utility>
#include <vector>

#include "persistence/ChunkStorage.h"
#include "voxel/WorldGen.h"

namespace voxel {

namespace {

bool IsOpaque(BlockId id) {
    switch (id) {
    case kBlockAir:
    case kBlockTorch:
        return false;
    default:
        return true;
    }
}

std::uint8_t EmissiveLevel(BlockId id) {
    switch (id) {
    case kBlockTorch:
        return 14;
    case kBlockLava:
        return kLightMax;
    default:
        return kLightMin;
    }
}

struct LightCoord {
    int x;
    int y;
    int z;
};

struct SunlightVolume {
    int size = kChunkSize + 2;
    int baseX = 0;
    int baseY = 0;
    int baseZ = 0;
    std::vector<std::uint8_t> light;
    std::vector<std::uint8_t> opaque;

    std::size_t Index(int lx, int ly, int lz) const {
        return static_cast<std::size_t>(lx + size * (ly + size * lz));
    }

    bool InBounds(int lx, int ly, int lz) const {
        return lx >= 0 && lx < size && ly >= 0 && ly < size && lz >= 0 && lz < size;
    }
};

struct EmissiveVolume {
    int size = kChunkSize + 2;
    int baseX = 0;
    int baseY = 0;
    int baseZ = 0;
    std::vector<std::uint8_t> light;
    std::vector<std::uint8_t> opaque;

    std::size_t Index(int lx, int ly, int lz) const {
        return static_cast<std::size_t>(lx + size * (ly + size * lz));
    }

    bool InBounds(int lx, int ly, int lz) const {
        return lx >= 0 && lx < size && ly >= 0 && ly < size && lz >= 0 && lz < size;
    }
};

} // namespace

std::shared_ptr<ChunkEntry> ChunkRegistry::GetOrCreateEntry(const ChunkCoord& coord) {
    std::lock_guard<std::mutex> lock(entriesMutex_);
    auto [it, inserted] = entries_.emplace(coord, std::make_shared<ChunkEntry>());
    if (inserted) {
        it->second->wanted.store(true);
    }
    return it->second;
}

void ChunkRegistry::RemoveChunk(const ChunkCoord& coord) {
    std::shared_ptr<ChunkEntry> entry;
    {
        std::lock_guard<std::mutex> lock(entriesMutex_);
        auto it = entries_.find(coord);
        if (it == entries_.end()) {
            return;
        }
        entry = it->second;
        entries_.erase(it);
    }

    entry->wanted.store(false);
    entry->mesh.DestroyGpu();
    entry->mesh.Clear();
    entry->gpuState.store(GpuState::NotUploaded);
}

void ChunkRegistry::DestroyAll() {
    std::unordered_map<ChunkCoord, std::shared_ptr<ChunkEntry>, ChunkCoordHash> entriesCopy;
    {
        std::lock_guard<std::mutex> lock(entriesMutex_);
        entriesCopy.swap(entries_);
    }
    for (auto& [coord, entry] : entriesCopy) {
        (void)coord;
        entry->wanted.store(false);
        entry->mesh.DestroyGpu();
    }
}

void ChunkRegistry::SetStorage(persistence::ChunkStorage* storage) {
    storage_ = storage;
}

bool ChunkRegistry::SaveChunkIfDirty(const ChunkCoord& coord, persistence::ChunkStorage& storage) {
    auto entry = TryGetEntry(coord);
    if (!entry) {
        return false;
    }
    if (!entry->dirty.load(std::memory_order_acquire)) {
        return false;
    }
    if (entry->generationState.load(std::memory_order_acquire) != GenerationState::Ready) {
        return false;
    }

    std::shared_lock<std::shared_mutex> lock(entry->dataMutex);
    if (!entry->chunk) {
        return false;
    }
    if (!entry->dirty.load(std::memory_order_acquire)) {
        return false;
    }
    if (storage.SaveChunk(coord, *entry->chunk)) {
        entry->dirty.store(false, std::memory_order_release);
        return true;
    }
    return false;
}

std::size_t ChunkRegistry::SaveAllDirty(persistence::ChunkStorage& storage) {
    std::size_t saved = 0;
    std::vector<std::pair<ChunkCoord, std::shared_ptr<ChunkEntry>>> entries;
    {
        std::lock_guard<std::mutex> lock(entriesMutex_);
        entries.reserve(entries_.size());
        for (const auto& [coord, entry] : entries_) {
            entries.emplace_back(coord, entry);
        }
    }
    for (const auto& [coord, entry] : entries) {
        if (!entry) {
            continue;
        }
        if (!entry->dirty.load(std::memory_order_acquire)) {
            continue;
        }
        if (entry->generationState.load(std::memory_order_acquire) != GenerationState::Ready) {
            continue;
        }
        std::shared_lock<std::shared_mutex> lock(entry->dataMutex);
        if (!entry->chunk) {
            continue;
        }
        if (!entry->dirty.load(std::memory_order_acquire)) {
            continue;
        }
        if (storage.SaveChunk(coord, *entry->chunk)) {
            entry->dirty.store(false, std::memory_order_release);
            ++saved;
        }
    }
    return saved;
}

std::shared_ptr<ChunkEntry> ChunkRegistry::TryGetEntry(const ChunkCoord& coord) {
    std::lock_guard<std::mutex> lock(entriesMutex_);
    auto it = entries_.find(coord);
    if (it == entries_.end()) {
        return nullptr;
    }
    return it->second;
}

std::shared_ptr<const ChunkEntry> ChunkRegistry::TryGetEntry(const ChunkCoord& coord) const {
    std::lock_guard<std::mutex> lock(entriesMutex_);
    auto it = entries_.find(coord);
    if (it == entries_.end()) {
        return nullptr;
    }
    return it->second;
}

bool ChunkRegistry::HasChunk(const ChunkCoord& coord) const {
    auto entry = TryGetEntry(coord);
    return entry && entry->generationState.load(std::memory_order_acquire) == GenerationState::Ready;
}

std::optional<ChunkReadHandle> ChunkRegistry::AcquireChunkRead(const ChunkCoord& coord) const {
    auto entry = TryGetEntry(coord);
    if (!entry || entry->generationState.load(std::memory_order_acquire) != GenerationState::Ready) {
#ifndef NDEBUG
        static std::atomic<std::int64_t> lastLogMs{0};
        const auto nowMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        std::int64_t previous = lastLogMs.load(std::memory_order_relaxed);
        if (nowMs - previous >= 1000 &&
            lastLogMs.compare_exchange_strong(previous, nowMs, std::memory_order_relaxed)) {
            std::cerr << "[ChunkRegistry] AcquireChunkRead failed for chunk (" << coord.x << ", " << coord.y << ", "
                      << coord.z << "): entry=" << (entry ? "set" : "null");
            if (entry) {
                std::cerr << " state=" << static_cast<int>(entry->generationState.load(std::memory_order_acquire));
            }
            std::cerr << '\n';
        }
#endif
        return std::nullopt;
    }

    ChunkReadHandle handle;
    handle.entry = entry;
    handle.lock = std::shared_lock<std::shared_mutex>(entry->dataMutex);
    handle.chunk = entry->chunk.get();
    if (!handle.chunk) {
#ifndef NDEBUG
        std::cerr << "[ChunkRegistry] AcquireChunkRead failed for chunk (" << coord.x << ", " << coord.y << ", "
                  << coord.z << "): entry=set state="
                  << static_cast<int>(entry->generationState.load(std::memory_order_acquire))
                  << " chunk=null\n";
#endif
        handle.lock.unlock();
        handle.entry.reset();
        return std::nullopt;
    }
    return handle;
}

BlockId ChunkRegistry::GetBlock(const WorldBlockCoord& world) const {
    ChunkCoord chunkCoord = WorldToChunkCoord(world, kChunkSize);
    LocalCoord local = WorldToLocalCoord(world, kChunkSize);
    auto handle = AcquireChunkRead(chunkCoord);
    if (!handle) {
        return SampleFlatWorld(world);
    }
    return handle->chunk->Get(local.x, local.y, local.z);
}

BlockId ChunkRegistry::GetBlockOrAir(const WorldBlockCoord& world) const {
    ChunkCoord chunkCoord = WorldToChunkCoord(world, kChunkSize);
    LocalCoord local = WorldToLocalCoord(world, kChunkSize);
    auto handle = AcquireChunkRead(chunkCoord);
    if (!handle) {
        return kBlockAir;
    }
    return handle->chunk->Get(local.x, local.y, local.z);
}

void ChunkRegistry::SetBlock(const WorldBlockCoord& world, BlockId id) {
    ChunkCoord chunkCoord = WorldToChunkCoord(world, kChunkSize);
    LocalCoord local = WorldToLocalCoord(world, kChunkSize);
    auto entry = GetOrCreateEntry(chunkCoord);
    const bool chunkWasNull = !entry->chunk;
    const auto stateBefore = entry->generationState.load(std::memory_order_acquire);
    std::unique_lock<std::shared_mutex> lock(entry->dataMutex);
    if (entry->generationState.load(std::memory_order_acquire) != GenerationState::Ready || !entry->chunk) {
        if (!entry->chunk) {
            auto chunk = std::make_unique<Chunk>();
            bool loaded = false;
            if (storage_) {
                loaded = storage_->LoadChunk(chunkCoord, *chunk);
            }
            if (!loaded) {
                GenerateChunkData(chunkCoord, *chunk);
            }
            entry->chunk = std::move(chunk);
        }
        entry->generationState.store(GenerationState::Ready, std::memory_order_release);
        entry->dirty.store(false, std::memory_order_release);
    }
    const bool chunkIsNull = !entry->chunk;
    const auto stateAfter = entry->generationState.load(std::memory_order_acquire);
    if (chunkIsNull || stateAfter != GenerationState::Ready) {
#ifndef NDEBUG
        std::cerr << "[ChunkRegistry] SetBlock world(" << world.x << ", " << world.y << ", " << world.z
                  << ") chunk(" << chunkCoord.x << ", " << chunkCoord.y << ", " << chunkCoord.z << ") local("
                  << local.x << ", " << local.y << ", " << local.z << ") state "
                  << static_cast<int>(stateBefore) << "->" << static_cast<int>(stateAfter) << " chunk "
                  << (chunkWasNull ? "null" : "set") << "->" << (chunkIsNull ? "null" : "set") << '\n';
#endif
    }
    entry->chunk->Set(local.x, local.y, local.z, id);
    entry->dirty.store(true, std::memory_order_release);
    entry->lightDirty.store(true, std::memory_order_release);
    entry->lightReady.store(false, std::memory_order_release);
}

void ChunkRegistry::EnsureLightForChunk(const ChunkCoord& coord) {
    auto entry = TryGetEntry(coord);
    if (!entry) {
        return;
    }
    if (entry->generationState.load(std::memory_order_acquire) != GenerationState::Ready) {
        return;
    }
    if (!entry->chunk) {
        return;
    }
    if (!entry->lightDirty.load(std::memory_order_acquire) && entry->lightReady.load(std::memory_order_acquire)) {
        return;
    }
    RebuildLightForChunk(coord);
}

void ChunkRegistry::EnsureLightForNeighborhood(const ChunkCoord& coord) {
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                EnsureLightForChunk({coord.x + dx, coord.y + dy, coord.z + dz});
            }
        }
    }
}

void ChunkRegistry::RebuildLightForChunk(const ChunkCoord& coord) {
    auto entry = TryGetEntry(coord);
    if (!entry) {
        return;
    }
    if (entry->generationState.load(std::memory_order_acquire) != GenerationState::Ready) {
        return;
    }

    std::unique_lock<std::shared_mutex> lock(entry->dataMutex);
    const Chunk* chunk = entry->chunk.get();
    if (!chunk) {
        return;
    }

    const int chunkBaseX = coord.x * kChunkSize;
    const int chunkBaseY = coord.y * kChunkSize;
    const int chunkBaseZ = coord.z * kChunkSize;

    auto sampleBlock = [&](const WorldBlockCoord& world) -> BlockId {
        if (world.x >= chunkBaseX && world.x < chunkBaseX + kChunkSize &&
            world.y >= chunkBaseY && world.y < chunkBaseY + kChunkSize &&
            world.z >= chunkBaseZ && world.z < chunkBaseZ + kChunkSize) {
            const LocalCoord local = WorldToLocalCoord(world, kChunkSize);
            return chunk->Get(local.x, local.y, local.z);
        }
        return GetBlock(world);
    };

    SunlightVolume sunlight;
    const int volumeSize = sunlight.size;
    const std::size_t volumeCount = static_cast<std::size_t>(volumeSize * volumeSize * volumeSize);
    sunlight.light.assign(volumeCount, kLightMin);
    sunlight.opaque.assign(volumeCount, 0);
    sunlight.baseX = chunkBaseX - 1;
    sunlight.baseY = chunkBaseY - 1;
    sunlight.baseZ = chunkBaseZ - 1;

    for (int z = 0; z < volumeSize; ++z) {
        for (int y = 0; y < volumeSize; ++y) {
            for (int x = 0; x < volumeSize; ++x) {
                WorldBlockCoord world{sunlight.baseX + x, sunlight.baseY + y, sunlight.baseZ + z};
                if (IsOpaque(sampleBlock(world))) {
                    sunlight.opaque[sunlight.Index(x, y, z)] = 1;
                }
            }
        }
    }

    const int minWorldY = sunlight.baseY;
    const int maxWorldY = sunlight.baseY + volumeSize - 1;

    for (int z = 0; z < volumeSize; ++z) {
        for (int x = 0; x < volumeSize; ++x) {
            const int worldX = sunlight.baseX + x;
            const int worldZ = sunlight.baseZ + z;
            bool blocked = false;
            for (int worldY = kWorldMaxY - 1; worldY >= kWorldMinY; --worldY) {
                WorldBlockCoord world{worldX, worldY, worldZ};
                BlockId block = sampleBlock(world);
                if (IsOpaque(block)) {
                    blocked = true;
                }
                if (worldY < minWorldY || worldY > maxWorldY) {
                    continue;
                }
                const int localY = worldY - sunlight.baseY;
                const std::size_t idx = sunlight.Index(x, localY, z);
                if (!blocked && !IsOpaque(block)) {
                    sunlight.light[idx] = kLightMax;
                } else {
                    sunlight.light[idx] = kLightMin;
                }
            }
        }
    }

    std::queue<LightCoord> queue;
    for (int z = 0; z < volumeSize; ++z) {
        for (int y = 0; y < volumeSize; ++y) {
            for (int x = 0; x < volumeSize; ++x) {
                if (sunlight.light[sunlight.Index(x, y, z)] > kLightMin) {
                    queue.push({x, y, z});
                }
            }
        }
    }

    const LightCoord offsets[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

    while (!queue.empty()) {
        LightCoord current = queue.front();
        queue.pop();
        const std::uint8_t level = sunlight.light[sunlight.Index(current.x, current.y, current.z)];
        if (level <= kLightMin + 1) {
            continue;
        }
        const std::uint8_t nextLevel = static_cast<std::uint8_t>(level - 1);
        for (const LightCoord& offset : offsets) {
            const int nx = current.x + offset.x;
            const int ny = current.y + offset.y;
            const int nz = current.z + offset.z;
            if (!sunlight.InBounds(nx, ny, nz)) {
                continue;
            }
            const std::size_t nidx = sunlight.Index(nx, ny, nz);
            if (sunlight.opaque[nidx] != 0) {
                continue;
            }
            if (nextLevel > sunlight.light[nidx]) {
                sunlight.light[nidx] = nextLevel;
                queue.push({nx, ny, nz});
            }
        }
    }

    EmissiveVolume emissive;
    emissive.light.assign(volumeCount, kLightMin);
    emissive.opaque.assign(volumeCount, 0);
    emissive.baseX = sunlight.baseX;
    emissive.baseY = sunlight.baseY;
    emissive.baseZ = sunlight.baseZ;

    for (int z = 0; z < volumeSize; ++z) {
        for (int y = 0; y < volumeSize; ++y) {
            for (int x = 0; x < volumeSize; ++x) {
                WorldBlockCoord world{emissive.baseX + x, emissive.baseY + y, emissive.baseZ + z};
                BlockId block = sampleBlock(world);
                const std::size_t idx = emissive.Index(x, y, z);
                if (IsOpaque(block)) {
                    emissive.opaque[idx] = 1;
                }
                const std::uint8_t level = EmissiveLevel(block);
                if (level > kLightMin) {
                    emissive.light[idx] = level;
                    queue.push({x, y, z});
                }
            }
        }
    }

    while (!queue.empty()) {
        LightCoord current = queue.front();
        queue.pop();
        const std::uint8_t level = emissive.light[emissive.Index(current.x, current.y, current.z)];
        if (level <= kLightMin + 1) {
            continue;
        }
        const std::uint8_t nextLevel = static_cast<std::uint8_t>(level - 1);
        for (const LightCoord& offset : offsets) {
            const int nx = current.x + offset.x;
            const int ny = current.y + offset.y;
            const int nz = current.z + offset.z;
            if (!emissive.InBounds(nx, ny, nz)) {
                continue;
            }
            const std::size_t nidx = emissive.Index(nx, ny, nz);
            if (emissive.opaque[nidx] != 0) {
                continue;
            }
            if (nextLevel > emissive.light[nidx]) {
                emissive.light[nidx] = nextLevel;
                queue.push({nx, ny, nz});
            }
        }
    }

    for (int z = 0; z < kChunkSize; ++z) {
        for (int y = 0; y < kChunkSize; ++y) {
            for (int x = 0; x < kChunkSize; ++x) {
                const int vx = x + 1;
                const int vy = y + 1;
                const int vz = z + 1;
                const std::size_t vidx = sunlight.Index(vx, vy, vz);
                entry->light.SetSunlight(x, y, z, sunlight.light[vidx]);
                entry->light.SetEmissive(x, y, z, emissive.light[vidx]);
            }
        }
    }

    entry->lightDirty.store(false, std::memory_order_release);
    entry->lightReady.store(true, std::memory_order_release);
}

void ChunkRegistry::RebuildLightForNeighborhood(const ChunkCoord& coord) {
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                RebuildLightForChunk({coord.x + dx, coord.y + dy, coord.z + dz});
            }
        }
    }
}

std::size_t ChunkRegistry::LoadedCount() const {
    std::lock_guard<std::mutex> lock(entriesMutex_);
    return entries_.size();
}

std::size_t ChunkRegistry::GpuReadyCount() const {
    std::size_t ready = 0;
    std::lock_guard<std::mutex> lock(entriesMutex_);
    for (const auto& [coord, entry] : entries_) {
        (void)coord;
        if (entry->gpuState.load(std::memory_order_acquire) == GpuState::Uploaded) {
            ++ready;
        }
    }
    return ready;
}

void ChunkRegistry::ForEachEntry(
    const std::function<void(const ChunkCoord&, const std::shared_ptr<ChunkEntry>&)>& fn) const {
    std::lock_guard<std::mutex> lock(entriesMutex_);
    for (const auto& [coord, entry] : entries_) {
        fn(coord, entry);
    }
}

std::vector<std::shared_ptr<ChunkEntry>> ChunkRegistry::EntriesSnapshot() const {
    std::vector<std::shared_ptr<ChunkEntry>> entries;
    std::lock_guard<std::mutex> lock(entriesMutex_);
    entries.reserve(entries_.size());
    for (const auto& [coord, entry] : entries_) {
        (void)coord;
        entries.push_back(entry);
    }
    return entries;
}

void ChunkRegistry::GenerateChunkData(const ChunkCoord& coord, Chunk& chunk) {
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
