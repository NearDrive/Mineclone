#include "voxel/ChunkRegistry.h"

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
        return handle;
    }

    handle.entry = entry;
    handle.lock = std::shared_lock<std::shared_mutex>(entry->dataMutex);
    handle.chunk = entry->chunk.get();
    if (!handle.chunk) {
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
    if (entry->generationState.load(std::memory_order_acquire) != GenerationState::Ready) {
        std::unique_lock<std::shared_mutex> lock(entry->dataMutex);
        if (!entry->chunk) {
            entry->chunk = std::make_unique<Chunk>();
            GenerateChunkData(chunkCoord, *entry->chunk);
        }
        entry->generationState.store(GenerationState::Ready, std::memory_order_release);
    }
    std::unique_lock<std::shared_mutex> lock(entry->dataMutex);
    entry->chunk->Set(local.x, local.y, local.z, id);
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
