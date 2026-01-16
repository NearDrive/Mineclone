#include "voxel/WorldGen.h"

namespace voxel {

BlockId SampleFlatWorld(const WorldBlockCoord& coord) {
    if (coord.y < 7) {
        return kBlockStone;
    }
    if (coord.y == 7) {
        return kBlockDirt;
    }
    return kBlockAir;
}

} // namespace voxel
