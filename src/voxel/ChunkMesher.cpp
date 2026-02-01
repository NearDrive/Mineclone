#include "voxel/ChunkMesher.h"

#include <cassert>
#include <shared_mutex>
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

struct LightReadHandle {
    std::shared_ptr<const ChunkEntry> entry;
    std::shared_lock<std::shared_mutex> lock;
    const LightChunk* light = nullptr;

    explicit operator bool() const { return light != nullptr; }
};

LightReadHandle AcquireLightRead(const ChunkCoord& coord, const ChunkRegistry& registry) {
    LightReadHandle handle;
    auto entry = registry.TryGetEntry(coord);
    if (!entry || !entry->lightReady.load(std::memory_order_acquire)) {
        return handle;
    }
    handle.entry = entry;
    handle.lock = std::shared_lock<std::shared_mutex>(entry->dataMutex);
    handle.light = &entry->light;
    return handle;
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

    registry.EnsureLightForNeighborhood(coord);

    auto lightEntry = registry.TryGetEntry(coord);
    const LightChunk* currentLight = nullptr;
    if (lightEntry && lightEntry->lightReady.load(std::memory_order_acquire)) {
        currentLight = &lightEntry->light;
    }

    LightReadHandle lightPosX = AcquireLightRead({coord.x + 1, coord.y, coord.z}, registry);
    LightReadHandle lightNegX = AcquireLightRead({coord.x - 1, coord.y, coord.z}, registry);
    LightReadHandle lightPosY = AcquireLightRead({coord.x, coord.y + 1, coord.z}, registry);
    LightReadHandle lightNegY = AcquireLightRead({coord.x, coord.y - 1, coord.z}, registry);
    LightReadHandle lightPosZ = AcquireLightRead({coord.x, coord.y, coord.z + 1}, registry);
    LightReadHandle lightNegZ = AcquireLightRead({coord.x, coord.y, coord.z - 1}, registry);

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

    auto sampleLight = [&](int nx, int ny, int nz, bool sunlight) -> float {
        const LightChunk* light = nullptr;
        int lx = nx;
        int ly = ny;
        int lz = nz;
        if (nx >= 0 && nx < kChunkSize && ny >= 0 && ny < kChunkSize && nz >= 0 && nz < kChunkSize) {
            light = currentLight;
        } else {
            const int outX = nx < 0 || nx >= kChunkSize;
            const int outY = ny < 0 || ny >= kChunkSize;
            const int outZ = nz < 0 || nz >= kChunkSize;
            if ((outX + outY + outZ) != 1) {
                return 0.0f;
            }
            if (nx < 0) {
                light = lightNegX.light;
                lx = nx + kChunkSize;
            } else if (nx >= kChunkSize) {
                light = lightPosX.light;
                lx = nx - kChunkSize;
            } else if (ny < 0) {
                light = lightNegY.light;
                ly = ny + kChunkSize;
            } else if (ny >= kChunkSize) {
                light = lightPosY.light;
                ly = ny - kChunkSize;
            } else if (nz < 0) {
                light = lightNegZ.light;
                lz = nz + kChunkSize;
            } else if (nz >= kChunkSize) {
                light = lightPosZ.light;
                lz = nz - kChunkSize;
            }
        }

        if (!light) {
            return 0.0f;
        }
        const std::uint8_t level = sunlight ? light->Sunlight(lx, ly, lz) : light->Emissive(lx, ly, lz);
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

                    std::uint32_t baseIndex = static_cast<std::uint32_t>(vertices.size());
                    for (std::size_t i = 0; i < face.vertices.size(); ++i) {
                        const glm::vec3& vertex = face.vertices[i];
                        const int sampleX =
                            x + face.neighborOffset.x + (face.neighborOffset.x == 0 ? static_cast<int>(vertex.x) : 0);
                        const int sampleY =
                            y + face.neighborOffset.y + (face.neighborOffset.y == 0 ? static_cast<int>(vertex.y) : 0);
                        const int sampleZ =
                            z + face.neighborOffset.z + (face.neighborOffset.z == 0 ? static_cast<int>(vertex.z) : 0);
                        const float vertexSunlight = sampleLight(sampleX, sampleY, sampleZ, true);
                        const float vertexEmissive = sampleLight(sampleX, sampleY, sampleZ, false);
                        vertices.push_back({glm::vec3{
                                                static_cast<float>(world.x) + vertex.x,
                                                static_cast<float>(world.y) + vertex.y,
                                                static_cast<float>(world.z) + vertex.z},
                                            face.normal,
                                            AtlasUv(block, face.uvs[i]),
                                            vertexSunlight,
                                            vertexEmissive});
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
