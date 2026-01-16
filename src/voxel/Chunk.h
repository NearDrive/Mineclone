#pragma once

#include <array>
#include <cstddef>

#include "voxel/BlockId.h"

namespace voxel {

constexpr int kChunkSize = 32;
constexpr int kChunkVolume = kChunkSize * kChunkSize * kChunkSize;

class Chunk {
public:
    Chunk();

    BlockId Get(int lx, int ly, int lz) const;
    void Set(int lx, int ly, int lz, BlockId id);

    void Fill(BlockId id);

private:
    static constexpr std::size_t ToIndex(int lx, int ly, int lz) {
        return static_cast<std::size_t>(lx + kChunkSize * (ly + kChunkSize * lz));
    }

    std::array<BlockId, kChunkVolume> blocks_{};
};

} // namespace voxel
