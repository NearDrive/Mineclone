#pragma once

#include <cstdint>

namespace voxel {

using BlockId = std::uint16_t;

constexpr BlockId kBlockAir = 0;
constexpr BlockId kBlockStone = 1;
constexpr BlockId kBlockDirt = 2;
constexpr BlockId kBlockTorch = 3;
constexpr BlockId kBlockLava = 4;

} // namespace voxel
