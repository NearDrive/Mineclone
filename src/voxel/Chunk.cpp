#include "voxel/Chunk.h"

#include <cassert>

namespace voxel {

Chunk::Chunk() {
    Fill(kBlockAir);
}

BlockId Chunk::Get(int lx, int ly, int lz) const {
#ifndef NDEBUG
    assert(lx >= 0 && lx < kChunkSize);
    assert(ly >= 0 && ly < kChunkSize);
    assert(lz >= 0 && lz < kChunkSize);
#endif
    return blocks_[ToIndex(lx, ly, lz)];
}

void Chunk::Set(int lx, int ly, int lz, BlockId id) {
#ifndef NDEBUG
    assert(lx >= 0 && lx < kChunkSize);
    assert(ly >= 0 && ly < kChunkSize);
    assert(lz >= 0 && lz < kChunkSize);
#endif
    blocks_[ToIndex(lx, ly, lz)] = id;
}

void Chunk::Fill(BlockId id) {
    blocks_.fill(id);
}

} // namespace voxel
