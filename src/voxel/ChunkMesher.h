#pragma once

#include "voxel/Chunk.h"
#include "voxel/ChunkCoord.h"
#include "voxel/ChunkManager.h"
#include "voxel/ChunkMesh.h"

namespace voxel {

class ChunkMesher {
public:
    void BuildMesh(const ChunkCoord& coord, const Chunk& chunk, const ChunkManager& manager,
                   ChunkMesh& mesh) const;
};

} // namespace voxel
