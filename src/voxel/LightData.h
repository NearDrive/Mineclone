#pragma once

#include <array>
#include <cstdint>

#include "voxel/Chunk.h"

namespace voxel {

constexpr std::uint8_t kLightMin = 0;
constexpr std::uint8_t kLightMax = 15;

struct PackedLight {
    std::uint8_t value = 0;

    constexpr std::uint8_t Sunlight() const { return static_cast<std::uint8_t>((value >> 4) & 0x0F); }
    constexpr std::uint8_t Emissive() const { return static_cast<std::uint8_t>(value & 0x0F); }

    constexpr void SetSunlight(std::uint8_t level) {
        value = static_cast<std::uint8_t>((value & 0x0F) | ((level & 0x0F) << 4));
    }

    constexpr void SetEmissive(std::uint8_t level) {
        value = static_cast<std::uint8_t>((value & 0xF0) | (level & 0x0F));
    }
};

class LightChunk {
public:
    LightChunk() = default;

    std::uint8_t Sunlight(int lx, int ly, int lz) const {
        return sunlight_[ToIndex(lx, ly, lz)];
    }

    std::uint8_t Emissive(int lx, int ly, int lz) const {
        return emissive_[ToIndex(lx, ly, lz)];
    }

    void SetSunlight(int lx, int ly, int lz, std::uint8_t level) {
        sunlight_[ToIndex(lx, ly, lz)] = Clamp(level);
    }

    void SetEmissive(int lx, int ly, int lz, std::uint8_t level) {
        emissive_[ToIndex(lx, ly, lz)] = Clamp(level);
    }

    const std::uint8_t* SunlightData() const { return sunlight_.data(); }
    const std::uint8_t* EmissiveData() const { return emissive_.data(); }
    std::uint8_t* SunlightData() { return sunlight_.data(); }
    std::uint8_t* EmissiveData() { return emissive_.data(); }

    static constexpr std::uint8_t Clamp(std::uint8_t level) {
        return static_cast<std::uint8_t>(level > kLightMax ? kLightMax : level);
    }

    static constexpr PackedLight Pack(std::uint8_t sunlight, std::uint8_t emissive) {
        PackedLight packed;
        packed.SetSunlight(Clamp(sunlight));
        packed.SetEmissive(Clamp(emissive));
        return packed;
    }

private:
    static constexpr std::size_t ToIndex(int lx, int ly, int lz) {
        return static_cast<std::size_t>(lx + kChunkSize * (ly + kChunkSize * lz));
    }

    std::array<std::uint8_t, kChunkVolume> sunlight_{};
    std::array<std::uint8_t, kChunkVolume> emissive_{};
};

} // namespace voxel
