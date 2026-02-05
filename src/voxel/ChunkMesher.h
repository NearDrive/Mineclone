#pragma once

#include "voxel/Chunk.h"
#include "voxel/ChunkCoord.h"
#include "voxel/ChunkJobs.h"
#include "voxel/ChunkRegistry.h"

namespace voxel {

class ChunkMesher {
public:
    void SetLightingEnabled(bool enabled) { lightingEnabled_ = enabled; }
    void BuildMesh(const ChunkCoord& coord, const Chunk& chunk, ChunkRegistry& registry,
                   ChunkMeshCpu& mesh, MeshDetailTier detailTier = MeshDetailTier::Near) const;

private:
    bool lightingEnabled_ = true;
};

} // namespace voxel
