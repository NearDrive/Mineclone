#pragma once

#include "voxel/Chunk.h"
#include "voxel/ChunkCoord.h"
#include "voxel/ChunkJobs.h"
#include "voxel/ChunkRegistry.h"

namespace voxel {

class ChunkMesher {
public:
    void BuildMesh(const ChunkCoord& coord, const Chunk& chunk, ChunkRegistry& registry,
                   ChunkMeshCpu& mesh) const;
};

} // namespace voxel
