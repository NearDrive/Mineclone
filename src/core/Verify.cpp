#include "core/Verify.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <shared_mutex>

#include "core/WorkerPool.h"
#include "persistence/ChunkStorage.h"
#include "voxel/BlockEdit.h"
#include "voxel/Chunk.h"
#include "voxel/ChunkMesher.h"
#include "voxel/ChunkRegistry.h"
#include "voxel/ChunkStreaming.h"
#include "voxel/Raycast.h"
#include "voxel/VoxelCoords.h"

namespace core {

namespace {

struct VerifyState {
    bool ok = true;
    std::string message;
};

void Require(bool condition, const std::string& message, VerifyState& state) {
    if (!condition && state.ok) {
        state.ok = false;
        state.message = message;
    }
}

void CheckVoxelCoords(VerifyState& state) {
    using namespace voxel;
    constexpr std::array<int, 8> coordsToTest = {-33, -32, -1, 0, 1, 31, 32, 33};
    for (int value : coordsToTest) {
        WorldBlockCoord world{value, value, value};
        ChunkCoord chunk = WorldToChunkCoord(world, kChunkSize);
        LocalCoord local = WorldToLocalCoord(world, kChunkSize);
        WorldBlockCoord roundtrip = ChunkLocalToWorld(chunk, local, kChunkSize);
        Require(world.x == roundtrip.x && world.y == roundtrip.y && world.z == roundtrip.z,
                "Voxel coord roundtrip failed for value " + std::to_string(value), state);
    }
}

void CheckChunkIndexing(VerifyState& state) {
    using namespace voxel;
    Chunk chunk;
    chunk.Fill(kBlockAir);

    struct Sample {
        int x;
        int y;
        int z;
        BlockId id;
    };
    std::array<Sample, 5> samples = {{
        {0, 0, 0, kBlockStone},
        {kChunkSize - 1, 0, 0, kBlockDirt},
        {0, 1, 0, static_cast<BlockId>(3)},
        {0, 0, 1, static_cast<BlockId>(4)},
        {kChunkSize - 1, kChunkSize - 1, kChunkSize - 1, static_cast<BlockId>(5)},
    }};

    for (const auto& sample : samples) {
        chunk.Set(sample.x, sample.y, sample.z, sample.id);
        std::size_t index = static_cast<std::size_t>(sample.x +
                                                     kChunkSize * (sample.y + kChunkSize * sample.z));
        Require(chunk.Data()[index] == sample.id, "Chunk linear index mapping failed.", state);
    }
}

void CheckRegistryReadOnly(VerifyState& state) {
    using namespace voxel;
    ChunkRegistry registry;
    WorldBlockCoord world{10, 5, -2};
    (void)registry.GetBlock(world);
    Require(registry.LoadedCount() == 0, "ChunkRegistry GetBlock should not create chunks.", state);
}

void CheckRaycast(VerifyState& state) {
    using namespace voxel;
    ChunkRegistry registry;
    ChunkCoord coord{0, 0, 0};
    auto entry = registry.GetOrCreateEntry(coord);
    {
        std::unique_lock<std::shared_mutex> lock(entry->dataMutex);
        entry->chunk = std::make_unique<Chunk>();
        entry->generationState.store(GenerationState::Ready, std::memory_order_release);
        entry->dirty.store(false, std::memory_order_release);
    }
    registry.SetBlock({0, 0, 0}, kBlockStone);
    Require(registry.GetBlockOrAir({0, 0, 0}) == kBlockStone,
            "SetBlock then GetBlockOrAir mismatch in CheckRaycast", state);
    const glm::vec3 origin(0.5f, 2.5f, 0.5f);
    const glm::vec3 dir(0.0f, -1.0f, 0.0f);
    RaycastHit hit = RaycastBlocks(registry, origin, dir, 10.0f);
    if (!(hit.hit && hit.block == glm::ivec3(0, 0, 0))) {
        Require(false,
                "Raycast expected (0,0,0) but got hit=" + std::to_string(hit.hit) + " block=(" +
                    std::to_string(hit.block.x) + "," + std::to_string(hit.block.y) + "," +
                    std::to_string(hit.block.z) + ") t=" + std::to_string(hit.t),
                state);
    }

    const glm::ivec3 edgeBlock(kChunkSize - 1, 0, 0);
    registry.SetBlock({edgeBlock.x, edgeBlock.y, edgeBlock.z}, kBlockStone);
    const glm::vec3 edgeOrigin(static_cast<float>(kChunkSize), 0.5f, 0.5f);
    const glm::vec3 edgeDir(-1.0f, 0.0f, 0.0f);
    RaycastHit edgeHit = RaycastBlocks(registry, edgeOrigin, edgeDir, 2.0f);
    Require(edgeHit.hit && edgeHit.block == edgeBlock, "Raycast did not hit expected chunk-edge block.", state);
}

void CheckEditNeighborRemesh(VerifyState& state) {
    using namespace voxel;
    ChunkRegistry registry;
    ChunkStreaming streaming;

    ChunkCoord base{0, 0, 0};
    ChunkCoord neighbor{1, 0, 0};
    auto baseEntry = registry.GetOrCreateEntry(base);
    auto neighborEntry = registry.GetOrCreateEntry(neighbor);

    baseEntry->chunk = std::make_unique<Chunk>();
    baseEntry->generationState.store(GenerationState::Ready, std::memory_order_release);
    baseEntry->meshingState.store(MeshingState::NotScheduled, std::memory_order_release);

    neighborEntry->chunk = std::make_unique<Chunk>();
    neighborEntry->generationState.store(GenerationState::Ready, std::memory_order_release);
    neighborEntry->meshingState.store(MeshingState::NotScheduled, std::memory_order_release);

    WorldBlockCoord world{kChunkSize - 1, 0, 0};
    bool edited = TrySetBlock(registry, streaming, world, kBlockDirt);
    Require(edited, "Expected block edit to succeed.", state);
    Require(baseEntry->meshingState.load(std::memory_order_acquire) == MeshingState::Queued,
            "Base chunk not queued for remesh after border edit.", state);
    Require(neighborEntry->meshingState.load(std::memory_order_acquire) == MeshingState::Queued,
            "Neighbor chunk not queued for remesh after border edit.", state);
    Require(streaming.MeshQueue().size() == 2, "Expected two remesh jobs queued.", state);
}

void CheckPersistence(VerifyState& state, const VerifyOptions& options) {
    if (!options.enablePersistence) {
        return;
    }

    using namespace voxel;
    std::filesystem::path root = options.persistenceRoot;
    if (root.empty()) {
        root = std::filesystem::temp_directory_path() / "mineclone_verify";
    }
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    persistence::ChunkStorage storage(root);
    ChunkCoord coord{2, 0, -1};
    Chunk saved;
    saved.Fill(kBlockStone);
    saved.Set(1, 2, 3, kBlockDirt);

    Require(storage.SaveChunk(coord, saved), "Failed to save chunk in persistence check.", state);
    Chunk loaded;
    Require(storage.LoadChunk(coord, loaded), "Failed to load chunk in persistence check.", state);
    const BlockId* savedData = saved.Data();
    const BlockId* loadedData = loaded.Data();
    Require(std::equal(savedData, savedData + kChunkVolume, loadedData),
            "Chunk persistence data mismatch.", state);
}

void CheckJobScheduling(VerifyState& state) {
    using namespace voxel;
    ChunkRegistry registry;
    ChunkStreaming streaming;
    ChunkCoord coord{0, 0, 0};
    auto entry = registry.GetOrCreateEntry(coord);
    entry->chunk = std::make_unique<Chunk>();
    entry->generationState.store(GenerationState::Ready, std::memory_order_release);
    entry->meshingState.store(MeshingState::NotScheduled, std::memory_order_release);

    bool first = streaming.RequestRemesh(coord, registry);
    bool second = streaming.RequestRemesh(coord, registry);
    Require(first, "First remesh request should succeed.", state);
    Require(!second, "Second remesh request should be rejected.", state);
    Require(streaming.MeshQueue().size() == 1, "Remesh queue should only contain one job.", state);
}

void CheckWorkerPoolShutdown(VerifyState& state) {
    using namespace voxel;
    core::Profiler profiler;
    ChunkRegistry registry;
    ChunkMesher mesher;
    ChunkStreaming streaming;
    core::WorkerPool pool;
    pool.Start(1, streaming.GenerateQueue(), streaming.MeshQueue(), streaming.UploadQueue(), registry, mesher, &profiler);
    pool.Stop();
    Require(pool.ThreadCount() == 0, "Worker pool threads did not stop.", state);
}

} // namespace

VerifyResult RunAll(const VerifyOptions& options) {
    VerifyState state;
    CheckVoxelCoords(state);
    CheckChunkIndexing(state);
    CheckRegistryReadOnly(state);
    CheckRaycast(state);
    CheckEditNeighborRemesh(state);
    CheckJobScheduling(state);
    CheckPersistence(state, options);
    CheckWorkerPoolShutdown(state);

    if (state.ok) {
        std::cout << "[Verify] All checks passed.\n";
    } else {
        std::cout << "[Verify] Failed: " << state.message << '\n';
    }

    return VerifyResult{state.ok, state.message};
}

} // namespace core
