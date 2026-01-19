#include "voxel/BlockEdit.h"

#include <iostream>

#include "voxel/Chunk.h"
#include "voxel/ChunkRegistry.h"
#include "voxel/ChunkStreaming.h"
#include "voxel/VoxelCoords.h"

namespace voxel {

namespace {
void RequestNeighborRemesh(const ChunkCoord& base, const LocalCoord& local,
                           ChunkStreaming& streaming, ChunkRegistry& registry) {
    streaming.RequestRemesh(base, registry);

    if (local.x == 0) {
        streaming.RequestRemesh({base.x - 1, base.y, base.z}, registry);
    } else if (local.x == kChunkSize - 1) {
        streaming.RequestRemesh({base.x + 1, base.y, base.z}, registry);
    }

    if (local.y == 0) {
        streaming.RequestRemesh({base.x, base.y - 1, base.z}, registry);
    } else if (local.y == kChunkSize - 1) {
        streaming.RequestRemesh({base.x, base.y + 1, base.z}, registry);
    }

    if (local.z == 0) {
        streaming.RequestRemesh({base.x, base.y, base.z - 1}, registry);
    } else if (local.z == kChunkSize - 1) {
        streaming.RequestRemesh({base.x, base.y, base.z + 1}, registry);
    }
}
}

bool TrySetBlock(ChunkRegistry& registry, ChunkStreaming& streaming, const WorldBlockCoord& world, BlockId id) {
    const BlockId current = registry.GetBlockOrAir(world);
    if (current == id) {
        return false;
    }

    registry.SetBlock(world, id);

#ifndef NDEBUG
    std::cout << "[Edit] Set block (" << world.x << "," << world.y << "," << world.z << ") = "
              << static_cast<int>(id) << '\n';
#endif

    const ChunkCoord chunkCoord = WorldToChunkCoord(world, kChunkSize);
    const LocalCoord local = WorldToLocalCoord(world, kChunkSize);
    RequestNeighborRemesh(chunkCoord, local, streaming, registry);
    return true;
}

} // namespace voxel
