#include "voxel/ChunkMesher.h"

#include <cassert>

#include "voxel/BlockFaces.h"

namespace voxel {

void ChunkMesher::BuildMesh(const ChunkCoord& coord, const Chunk& chunk, const ChunkRegistry& registry,
                            ChunkMeshCpu& mesh) const {
    mesh.Clear();

    const std::size_t estimatedFaces = static_cast<std::size_t>(kChunkSize) * kChunkSize * 6;
    mesh.Reserve(estimatedFaces * 4, estimatedFaces * 6);

    auto& vertices = mesh.vertices;
    auto& indices = mesh.indices;

    const Chunk* neighborPosX = registry.TryGetChunk({coord.x + 1, coord.y, coord.z});
    const Chunk* neighborNegX = registry.TryGetChunk({coord.x - 1, coord.y, coord.z});
    const Chunk* neighborPosY = registry.TryGetChunk({coord.x, coord.y + 1, coord.z});
    const Chunk* neighborNegY = registry.TryGetChunk({coord.x, coord.y - 1, coord.z});
    const Chunk* neighborPosZ = registry.TryGetChunk({coord.x, coord.y, coord.z + 1});
    const Chunk* neighborNegZ = registry.TryGetChunk({coord.x, coord.y, coord.z - 1});

    auto sampleNeighbor = [&](int nx, int ny, int nz) -> BlockId {
        if (nx >= 0 && nx < kChunkSize && ny >= 0 && ny < kChunkSize && nz >= 0 && nz < kChunkSize) {
            return chunk.Get(nx, ny, nz);
        }
        if (nx < 0) {
            return neighborNegX ? neighborNegX->Get(nx + kChunkSize, ny, nz) : kBlockAir;
        }
        if (nx >= kChunkSize) {
            return neighborPosX ? neighborPosX->Get(nx - kChunkSize, ny, nz) : kBlockAir;
        }
        if (ny < 0) {
            return neighborNegY ? neighborNegY->Get(nx, ny + kChunkSize, nz) : kBlockAir;
        }
        if (ny >= kChunkSize) {
            return neighborPosY ? neighborPosY->Get(nx, ny - kChunkSize, nz) : kBlockAir;
        }
        if (nz < 0) {
            return neighborNegZ ? neighborNegZ->Get(nx, ny, nz + kChunkSize) : kBlockAir;
        }
        if (nz >= kChunkSize) {
            return neighborPosZ ? neighborPosZ->Get(nx, ny, nz - kChunkSize) : kBlockAir;
        }
        return kBlockAir;
    };

    for (int z = 0; z < kChunkSize; ++z) {
        for (int y = 0; y < kChunkSize; ++y) {
            for (int x = 0; x < kChunkSize; ++x) {
                BlockId block = chunk.Get(x, y, z);
                if (block == kBlockAir) {
                    continue;
                }

                LocalCoord local{x, y, z};
                WorldBlockCoord world = ChunkLocalToWorld(coord, local, kChunkSize);

                for (const BlockFace& face : kBlockFaces) {
                    const int nx = x + face.neighborOffset.x;
                    const int ny = y + face.neighborOffset.y;
                    const int nz = z + face.neighborOffset.z;

                    if (sampleNeighbor(nx, ny, nz) != kBlockAir) {
                        continue;
                    }

                    std::uint32_t baseIndex = static_cast<std::uint32_t>(vertices.size());
                    for (const glm::vec3& vertex : face.vertices) {
                        vertices.push_back({glm::vec3{
                                                static_cast<float>(world.x) + vertex.x,
                                                static_cast<float>(world.y) + vertex.y,
                                                static_cast<float>(world.z) + vertex.z},
                                            face.normal});
                    }

                    indices.push_back(baseIndex + 0);
                    indices.push_back(baseIndex + 1);
                    indices.push_back(baseIndex + 2);
                    indices.push_back(baseIndex + 0);
                    indices.push_back(baseIndex + 2);
                    indices.push_back(baseIndex + 3);

#ifndef NDEBUG
                    assert(indices.back() < vertices.size());
#endif
                }
            }
        }
    }
}

} // namespace voxel
