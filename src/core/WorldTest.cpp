#include "core/WorldTest.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/Sha256.h"
#include "voxel/Chunk.h"
#include "voxel/ChunkCoord.h"
#include "voxel/ChunkRegistry.h"
#include "voxel/VoxelCoords.h"

namespace core {

namespace {

constexpr std::int32_t kWorldTestSeed = 1337;

void AppendInt32(std::vector<std::uint8_t>& buffer, std::int32_t value) {
    buffer.push_back(static_cast<std::uint8_t>(value & 0xFF));
    buffer.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    buffer.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    buffer.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

void AppendUint16(std::vector<std::uint8_t>& buffer, std::uint16_t value) {
    buffer.push_back(static_cast<std::uint8_t>(value & 0xFF));
    buffer.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

bool AppendUniqueChunk(const voxel::ChunkCoord& coord,
                       std::unordered_set<voxel::ChunkCoord, voxel::ChunkCoordHash>& seen,
                       std::vector<voxel::ChunkCoord>& ordered) {
    auto [it, inserted] = seen.insert(coord);
    if (!inserted) {
        return false;
    }
    ordered.push_back(coord);
    return true;
}

} // namespace

WorldTestResult RunWorldTest() {
    WorldTestResult result;
    try {
        voxel::ChunkRegistry registry;
        std::unordered_set<voxel::ChunkCoord, voxel::ChunkCoordHash> seen;
        std::vector<voxel::ChunkCoord> chunkList;

        const std::vector<voxel::ChunkCoord> requiredChunks = {
            {0, 0, 0},
            {1, 0, 0},
            {0, 0, 1},
            {-1, 0, 0},
            {0, 0, -1},
        };

        for (const auto& coord : requiredChunks) {
            AppendUniqueChunk(coord, seen, chunkList);
        }

        const std::vector<voxel::WorldBlockCoord> queryCoords = {
            {0, 0, 0},
            {5, 10, 5},
            {31, 0, 31},
            {32, 0, 0},
            {-1, 0, -1},
        };

        for (const auto& world : queryCoords) {
            AppendUniqueChunk(voxel::WorldToChunkCoord(world, voxel::kChunkSize), seen, chunkList);
        }

        for (const auto& coord : chunkList) {
            auto entry = registry.GetOrCreateEntry(coord);
            std::unique_lock<std::shared_mutex> lock(entry->dataMutex);
            entry->chunk = std::make_unique<voxel::Chunk>();
            voxel::ChunkRegistry::GenerateChunkData(coord, *entry->chunk);
            entry->generationState.store(voxel::GenerationState::Ready, std::memory_order_release);
            entry->dirty.store(false, std::memory_order_release);
        }

        std::vector<std::uint8_t> buffer;
        buffer.reserve(queryCoords.size() * (sizeof(std::int32_t) * 3 + sizeof(std::uint16_t)));

        for (const auto& world : queryCoords) {
            voxel::ChunkCoord chunkCoord = voxel::WorldToChunkCoord(world, voxel::kChunkSize);
            if (!registry.HasChunk(chunkCoord)) {
                result.message = "Missing chunk for query";
                return result;
            }
            auto handle = registry.AcquireChunkRead(chunkCoord);
            if (!handle || !handle->chunk) {
                result.message = "Chunk access failed";
                return result;
            }
            voxel::LocalCoord local = voxel::WorldToLocalCoord(world, voxel::kChunkSize);
            voxel::BlockId block = handle->chunk->Get(local.x, local.y, local.z);

            AppendInt32(buffer, world.x);
            AppendInt32(buffer, world.y);
            AppendInt32(buffer, world.z);
            AppendUint16(buffer, block);
        }

        result.checksum = core::Sha256Hex(buffer);
        result.ok = true;
        std::cout << "[WorldTest] seed=" << kWorldTestSeed << " checksum=" << result.checksum << '\n';
        return result;
    } catch (const std::exception& ex) {
        result.message = ex.what();
        return result;
    } catch (...) {
        result.message = "Unknown exception";
        return result;
    }
}

} // namespace core
