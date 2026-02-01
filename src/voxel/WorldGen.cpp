#include "voxel/WorldGen.h"

#include <cmath>
#include <cstdint>
#include <limits>

namespace voxel {

namespace {

constexpr std::uint32_t kTerrainSeed = 0x9E3779B9u;

float Fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

std::uint32_t Hash2D(int x, int z) {
    std::uint32_t hx = static_cast<std::uint32_t>(x);
    std::uint32_t hz = static_cast<std::uint32_t>(z);
    std::uint32_t h = kTerrainSeed;
    h ^= hx + 0x9e3779b9u + (h << 6) + (h >> 2);
    h ^= hz + 0x85ebca6bu + (h << 6) + (h >> 2);
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

float RandomValue(int x, int z) {
    constexpr float invMax = 1.0f / static_cast<float>(std::numeric_limits<std::uint32_t>::max());
    return static_cast<float>(Hash2D(x, z)) * invMax;
}

float ValueNoise(float x, float z, float scale) {
    const float xf = x / scale;
    const float zf = z / scale;
    const int x0 = static_cast<int>(std::floor(xf));
    const int z0 = static_cast<int>(std::floor(zf));
    const int x1 = x0 + 1;
    const int z1 = z0 + 1;

    const float tx = Fade(xf - static_cast<float>(x0));
    const float tz = Fade(zf - static_cast<float>(z0));

    const float v00 = RandomValue(x0, z0);
    const float v10 = RandomValue(x1, z0);
    const float v01 = RandomValue(x0, z1);
    const float v11 = RandomValue(x1, z1);

    const float vx0 = Lerp(v00, v10, tx);
    const float vx1 = Lerp(v01, v11, tx);
    return Lerp(vx0, vx1, tz);
}

int SampleHeight(int x, int z) {
    constexpr float baseHeight = 10.0f;
    constexpr float amplitude = 14.0f;
    const float noiseLow = ValueNoise(static_cast<float>(x), static_cast<float>(z), 64.0f);
    const float noiseHigh = ValueNoise(static_cast<float>(x), static_cast<float>(z), 24.0f);
    const float noise = noiseLow * 0.65f + noiseHigh * 0.35f;
    return static_cast<int>(std::round(baseHeight + (noise * 2.0f - 1.0f) * amplitude));
}

} // namespace

int GetSurfaceHeight(int x, int z) {
    return SampleHeight(x, z);
}

BlockId SampleFlatWorld(const WorldBlockCoord& coord) {
    if (coord.y >= kWorldMaxY) {
        return kBlockAir;
    }
    if (coord.y <= kWorldMinY) {
        return kBlockStone;
    }
    const int height = GetSurfaceHeight(coord.x, coord.z);
    if (coord.y > height) {
        return kBlockAir;
    }
    if (coord.y == height) {
        return kBlockDirt;
    }
    if (coord.y >= height - 3) {
        return kBlockDirt;
    }
    return kBlockStone;
}

} // namespace voxel
