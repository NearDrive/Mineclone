#include "voxel/ChunkMesher.h"

#include <cassert>
#include <queue>
#include <vector>

#include "voxel/BlockFaces.h"
#include "voxel/LightData.h"
#include "voxel/WorldGen.h"

namespace voxel {

namespace {

constexpr float kAtlasTileWidth = 0.5f;

float AtlasOffsetForBlock(BlockId id) {
    if (id == kBlockStone) {
        return kAtlasTileWidth;
    }
    return 0.0f;
}

glm::vec2 AtlasUv(BlockId id, const glm::vec2& uv) {
    const float offset = AtlasOffsetForBlock(id);
    return glm::vec2{uv.x * kAtlasTileWidth + offset, uv.y};
}

bool IsOpaque(BlockId id) {
    return id != kBlockAir;
}

struct SunlightVolume {
    int size = kChunkSize + 2;
    int baseX = 0;
    int baseY = 0;
    int baseZ = 0;
    std::vector<std::uint8_t> light;
    std::vector<std::uint8_t> opaque;

    std::size_t Index(int lx, int ly, int lz) const {
        return static_cast<std::size_t>(lx + size * (ly + size * lz));
    }

    bool InBounds(int lx, int ly, int lz) const {
        return lx >= 0 && lx < size && ly >= 0 && ly < size && lz >= 0 && lz < size;
    }
};

SunlightVolume BuildSunlightVolume(const ChunkCoord& coord, const ChunkRegistry& registry) {
    SunlightVolume volume;
    const int volumeSize = volume.size;
    const std::size_t volumeCount = static_cast<std::size_t>(volumeSize * volumeSize * volumeSize);
    volume.light.assign(volumeCount, kLightMin);
    volume.opaque.assign(volumeCount, 0);

    volume.baseX = coord.x * kChunkSize - 1;
    volume.baseY = coord.y * kChunkSize - 1;
    volume.baseZ = coord.z * kChunkSize - 1;

    for (int z = 0; z < volumeSize; ++z) {
        for (int y = 0; y < volumeSize; ++y) {
            for (int x = 0; x < volumeSize; ++x) {
                WorldBlockCoord world{volume.baseX + x, volume.baseY + y, volume.baseZ + z};
                if (IsOpaque(registry.GetBlock(world))) {
                    volume.opaque[volume.Index(x, y, z)] = 1;
                }
            }
        }
    }

    const int minWorldY = volume.baseY;
    const int maxWorldY = volume.baseY + volumeSize - 1;

    for (int z = 0; z < volumeSize; ++z) {
        for (int x = 0; x < volumeSize; ++x) {
            const int worldX = volume.baseX + x;
            const int worldZ = volume.baseZ + z;
            bool blocked = false;
            for (int worldY = kWorldMaxY - 1; worldY >= kWorldMinY; --worldY) {
                BlockId block = registry.GetBlock(WorldBlockCoord{worldX, worldY, worldZ});
                if (IsOpaque(block)) {
                    blocked = true;
                }
                if (worldY < minWorldY || worldY > maxWorldY) {
                    continue;
                }
                const int localY = worldY - volume.baseY;
                const std::size_t idx = volume.Index(x, localY, z);
                if (!blocked && !IsOpaque(block)) {
                    volume.light[idx] = kLightMax;
                } else {
                    volume.light[idx] = kLightMin;
                }
            }
        }
    }

    struct LightCoord {
        int x;
        int y;
        int z;
    };

    std::queue<LightCoord> queue;
    for (int z = 0; z < volumeSize; ++z) {
        for (int y = 0; y < volumeSize; ++y) {
            for (int x = 0; x < volumeSize; ++x) {
                if (volume.light[volume.Index(x, y, z)] > kLightMin) {
                    queue.push({x, y, z});
                }
            }
        }
    }

    const LightCoord offsets[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

    while (!queue.empty()) {
        LightCoord current = queue.front();
        queue.pop();
        const std::uint8_t level = volume.light[volume.Index(current.x, current.y, current.z)];
        if (level <= kLightMin + 1) {
            continue;
        }
        const std::uint8_t nextLevel = static_cast<std::uint8_t>(level - 1);
        for (const LightCoord& offset : offsets) {
            const int nx = current.x + offset.x;
            const int ny = current.y + offset.y;
            const int nz = current.z + offset.z;
            if (!volume.InBounds(nx, ny, nz)) {
                continue;
            }
            const std::size_t nidx = volume.Index(nx, ny, nz);
            if (volume.opaque[nidx] != 0) {
                continue;
            }
            if (nextLevel > volume.light[nidx]) {
                volume.light[nidx] = nextLevel;
                queue.push({nx, ny, nz});
            }
        }
    }

    return volume;
}

} // namespace

void ChunkMesher::BuildMesh(const ChunkCoord& coord, const Chunk& chunk, const ChunkRegistry& registry,
                            ChunkMeshCpu& mesh) const {
    mesh.Clear();

    const std::size_t estimatedFaces = static_cast<std::size_t>(kChunkSize) * kChunkSize * 6;
    mesh.Reserve(estimatedFaces * 4, estimatedFaces * 6);

    auto& vertices = mesh.vertices;
    auto& indices = mesh.indices;

    auto neighborPosX = registry.AcquireChunkRead({coord.x + 1, coord.y, coord.z});
    auto neighborNegX = registry.AcquireChunkRead({coord.x - 1, coord.y, coord.z});
    auto neighborPosY = registry.AcquireChunkRead({coord.x, coord.y + 1, coord.z});
    auto neighborNegY = registry.AcquireChunkRead({coord.x, coord.y - 1, coord.z});
    auto neighborPosZ = registry.AcquireChunkRead({coord.x, coord.y, coord.z + 1});
    auto neighborNegZ = registry.AcquireChunkRead({coord.x, coord.y, coord.z - 1});

    SunlightVolume sunlight = BuildSunlightVolume(coord, registry);

    auto sampleNeighbor = [&](int nx, int ny, int nz) -> BlockId {
        if (nx >= 0 && nx < kChunkSize && ny >= 0 && ny < kChunkSize && nz >= 0 && nz < kChunkSize) {
            return chunk.Get(nx, ny, nz);
        }
        const int outX = nx < 0 || nx >= kChunkSize;
        const int outY = ny < 0 || ny >= kChunkSize;
        const int outZ = nz < 0 || nz >= kChunkSize;
        if ((outX + outY + outZ) != 1) {
            return kBlockAir;
        }
        if (nx < 0) {
            return neighborNegX ? neighborNegX->chunk->Get(nx + kChunkSize, ny, nz) : kBlockAir;
        }
        if (nx >= kChunkSize) {
            return neighborPosX ? neighborPosX->chunk->Get(nx - kChunkSize, ny, nz) : kBlockAir;
        }
        if (ny < 0) {
            return neighborNegY ? neighborNegY->chunk->Get(nx, ny + kChunkSize, nz) : kBlockAir;
        }
        if (ny >= kChunkSize) {
            return neighborPosY ? neighborPosY->chunk->Get(nx, ny - kChunkSize, nz) : kBlockAir;
        }
        if (nz < 0) {
            return neighborNegZ ? neighborNegZ->chunk->Get(nx, ny, nz + kChunkSize) : kBlockAir;
        }
        if (nz >= kChunkSize) {
            return neighborPosZ ? neighborPosZ->chunk->Get(nx, ny, nz - kChunkSize) : kBlockAir;
        }
        return kBlockAir;
    };

    auto sampleSunlight = [&](int nx, int ny, int nz) -> float {
        const int lx = nx + 1;
        const int ly = ny + 1;
        const int lz = nz + 1;
        if (!sunlight.InBounds(lx, ly, lz)) {
            return 0.0f;
        }
        const std::uint8_t level = sunlight.light[sunlight.Index(lx, ly, lz)];
        return static_cast<float>(level) / static_cast<float>(kLightMax);
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

                    const float faceSunlight = sampleSunlight(nx, ny, nz);
                    std::uint32_t baseIndex = static_cast<std::uint32_t>(vertices.size());
                    for (std::size_t i = 0; i < face.vertices.size(); ++i) {
                        const glm::vec3& vertex = face.vertices[i];
                        vertices.push_back({glm::vec3{
                                                static_cast<float>(world.x) + vertex.x,
                                                static_cast<float>(world.y) + vertex.y,
                                                static_cast<float>(world.z) + vertex.z},
                                            face.normal,
                                            AtlasUv(block, face.uvs[i]),
                                            faceSunlight});
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
