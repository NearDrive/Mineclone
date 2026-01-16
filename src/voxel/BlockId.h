#pragma once

#include <cstdint>

namespace voxel {

using BlockId = std::uint16_t;

constexpr BlockId kBlockAir = 0;
constexpr BlockId kBlockStone = 1;
constexpr BlockId kBlockDirt = 2;

} // namespace voxel
