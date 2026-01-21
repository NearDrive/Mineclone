#include "core/Sha256.h"

#include <array>
#include <iomanip>
#include <sstream>

namespace core {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

constexpr std::array<std::uint32_t, 8> kInitialState = {
    0x6a09e667u,
    0xbb67ae85u,
    0x3c6ef372u,
    0xa54ff53au,
    0x510e527fu,
    0x9b05688cu,
    0x1f83d9abu,
    0x5be0cd19u
};

std::uint32_t RotateRight(std::uint32_t value, std::uint32_t amount) {
    return (value >> amount) | (value << (32u - amount));
}

std::uint32_t Choose(std::uint32_t e, std::uint32_t f, std::uint32_t g) {
    return (e & f) ^ (~e & g);
}

std::uint32_t Majority(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    return (a & b) ^ (a & c) ^ (b & c);
}

std::uint32_t Sigma0(std::uint32_t x) {
    return RotateRight(x, 2) ^ RotateRight(x, 13) ^ RotateRight(x, 22);
}

std::uint32_t Sigma1(std::uint32_t x) {
    return RotateRight(x, 6) ^ RotateRight(x, 11) ^ RotateRight(x, 25);
}

std::uint32_t Gamma0(std::uint32_t x) {
    return RotateRight(x, 7) ^ RotateRight(x, 18) ^ (x >> 3);
}

std::uint32_t Gamma1(std::uint32_t x) {
    return RotateRight(x, 17) ^ RotateRight(x, 19) ^ (x >> 10);
}

void TransformBlock(const std::uint8_t* block, std::array<std::uint32_t, 8>& state) {
    std::array<std::uint32_t, 64> schedule{};
    for (std::size_t i = 0; i < 16; ++i) {
        const std::size_t offset = i * 4;
        schedule[i] = (static_cast<std::uint32_t>(block[offset]) << 24u) |
                      (static_cast<std::uint32_t>(block[offset + 1]) << 16u) |
                      (static_cast<std::uint32_t>(block[offset + 2]) << 8u) |
                      (static_cast<std::uint32_t>(block[offset + 3]));
    }
    for (std::size_t i = 16; i < 64; ++i) {
        schedule[i] = Gamma1(schedule[i - 2]) + schedule[i - 7] + Gamma0(schedule[i - 15]) + schedule[i - 16];
    }

    std::uint32_t a = state[0];
    std::uint32_t b = state[1];
    std::uint32_t c = state[2];
    std::uint32_t d = state[3];
    std::uint32_t e = state[4];
    std::uint32_t f = state[5];
    std::uint32_t g = state[6];
    std::uint32_t h = state[7];

    for (std::size_t i = 0; i < 64; ++i) {
        std::uint32_t temp1 = h + Sigma1(e) + Choose(e, f, g) + kRoundConstants[i] + schedule[i];
        std::uint32_t temp2 = Sigma0(a) + Majority(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

std::string StateToHex(const std::array<std::uint32_t, 8>& state) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::uint32_t value : state) {
        out << std::setw(8) << value;
    }
    return out.str();
}

} // namespace

std::string Sha256Hex(const std::uint8_t* data, std::size_t size) {
    std::array<std::uint32_t, 8> state = kInitialState;
    std::array<std::uint8_t, 64> block{};
    std::size_t blockSize = 0;
    std::uint64_t totalBits = 0;

    auto processBytes = [&](const std::uint8_t* bytes, std::size_t length) {
        for (std::size_t i = 0; i < length; ++i) {
            block[blockSize++] = bytes[i];
            if (blockSize == block.size()) {
                TransformBlock(block.data(), state);
                totalBits += block.size() * 8u;
                blockSize = 0;
            }
        }
    };

    processBytes(data, size);

    totalBits += blockSize * 8u;
    block[blockSize++] = 0x80u;

    if (blockSize > 56) {
        while (blockSize < block.size()) {
            block[blockSize++] = 0u;
        }
        TransformBlock(block.data(), state);
        blockSize = 0;
    }

    while (blockSize < 56) {
        block[blockSize++] = 0u;
    }

    for (int i = 7; i >= 0; --i) {
        block[blockSize++] = static_cast<std::uint8_t>((totalBits >> (i * 8)) & 0xffu);
    }

    TransformBlock(block.data(), state);

    return StateToHex(state);
}

std::string Sha256Hex(const std::vector<std::uint8_t>& data) {
    return Sha256Hex(data.data(), data.size());
}

} // namespace core
