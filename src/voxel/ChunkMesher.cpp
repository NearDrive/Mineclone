#include "voxel/ChunkMesher.h"

#include <cassert>

#include "voxel/BlockFaces.h"

namespace voxel {

void ChunkMesher::BuildMesh(const ChunkCoord& coord, const Chunk& chunk, const ChunkRegistry& registry,
                            ChunkMesh& mesh) const {
    mesh.Clear();

    const std::size_t estimatedFaces = static_cast<std::size_t>(kChunkSize) * kChunkSize * 6;
    mesh.Reserve(estimatedFaces * 4, estimatedFaces * 6);

    auto& vertices = mesh.Vertices();
    auto& indices = mesh.Indices();

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
                    WorldBlockCoord neighbor{
                        world.x + face.neighborOffset.x,
                        world.y + face.neighborOffset.y,
                        world.z + face.neighborOffset.z};

                    if (registry.GetBlockOrAir(neighbor) != kBlockAir) {
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
