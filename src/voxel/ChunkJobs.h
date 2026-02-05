#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "voxel/ChunkCoord.h"
#include "voxel/ChunkMesh.h"

namespace voxel {

struct Chunk;
struct ChunkEntry;

struct ChunkMeshCpu {
    std::size_t ByteSize() const {
        return vertices.size() * sizeof(VoxelVertex) + indices.size() * sizeof(std::uint32_t);
    }

    void Clear() {
        vertices.clear();
        indices.clear();
    }

    void Reserve(std::size_t vertexCount, std::size_t indexCount) {
        vertices.reserve(vertexCount);
        indices.reserve(indexCount);
    }

    std::vector<VoxelVertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct GenerateJob {
    ChunkCoord coord;
    std::weak_ptr<ChunkEntry> entry;
};

struct MeshJob {
    ChunkCoord coord;
    std::weak_ptr<ChunkEntry> entry;
};

struct MeshReady {
    ChunkCoord coord;
    std::weak_ptr<ChunkEntry> entry;
    std::shared_ptr<ChunkMeshCpu> cpuMesh;
};

struct SaveJob {
    ChunkCoord coord;
    std::shared_ptr<const Chunk> chunk;
};

} // namespace voxel
