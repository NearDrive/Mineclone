#pragma once

#include <filesystem>

#include "voxel/Chunk.h"
#include "voxel/ChunkCoord.h"

namespace persistence {

class ChunkStorage {
public:
    explicit ChunkStorage(std::filesystem::path root = DefaultSavePath());

    static std::filesystem::path DefaultSavePath();

    bool LoadChunk(const voxel::ChunkCoord& coord, voxel::Chunk& chunk);
    bool SaveChunk(const voxel::ChunkCoord& coord, const voxel::Chunk& chunk);

    bool ChunkFileExists(const voxel::ChunkCoord& coord) const;

private:
    std::filesystem::path ChunkPath(const voxel::ChunkCoord& coord) const;
    bool EnsureRoot();

    std::filesystem::path root_;
};

} // namespace persistence
