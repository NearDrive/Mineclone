#include "voxel/ChunkRegistry.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <shared_mutex>
#include <utility>

#include "persistence/ChunkStorage.h"
#include "voxel/WorldGen.h"

namespace voxel {

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

ChunkReadHandle ChunkRegistry::AcquireChunkRead(const ChunkCoord& coord) const {
    ChunkReadHandle handle;
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
        return handle;
    }

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
    }
    return handle;
}

BlockId ChunkRegistry::GetBlock(const WorldBlockCoord& world) const {
    ChunkCoord chunkCoord = WorldToChunkCoord(world, kChunkSize);
    LocalCoord local = WorldToLocalCoord(world, kChunkSize);
    ChunkReadHandle handle = AcquireChunkRead(chunkCoord);
    if (!handle) {
        return SampleFlatWorld(world);
    }
    return handle.chunk->Get(local.x, local.y, local.z);
}

BlockId ChunkRegistry::GetBlockOrAir(const WorldBlockCoord& world) const {
    ChunkCoord chunkCoord = WorldToChunkCoord(world, kChunkSize);
    LocalCoord local = WorldToLocalCoord(world, kChunkSize);
    ChunkReadHandle handle = AcquireChunkRead(chunkCoord);
    if (!handle) {
        return kBlockAir;
    }
    return handle.chunk->Get(local.x, local.y, local.z);
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
