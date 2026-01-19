#include "persistence/ChunkStorage.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>

#include "persistence/ChunkFormat.h"
#include "voxel/BlockId.h"

namespace persistence {

namespace {

std::string CoordToString(const voxel::ChunkCoord& coord) {
    std::ostringstream stream;
    stream << "(" << coord.x << "," << coord.y << "," << coord.z << ")";
    return stream.str();
}

bool WriteExact(std::ostream& out, const void* data, std::size_t size) {
    out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    return static_cast<bool>(out);
}

bool ReadExact(std::istream& in, void* data, std::size_t size) {
    in.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(size));
    return static_cast<bool>(in);
}

} // namespace

ChunkStorage::ChunkStorage(std::filesystem::path root) : root_(std::move(root)) {
    EnsureRoot();
}

std::filesystem::path ChunkStorage::DefaultSavePath() {
    return std::filesystem::path("saves") / "world_0";
}

bool ChunkStorage::ChunkFileExists(const voxel::ChunkCoord& coord) const {
    return std::filesystem::exists(ChunkPath(coord));
}

bool ChunkStorage::EnsureRoot() {
    std::error_code error;
    if (std::filesystem::exists(root_, error)) {
        return true;
    }
    if (std::filesystem::create_directories(root_, error)) {
        return true;
    }
    std::cout << "[Storage] Failed to create save folder: " << root_.string()
              << " (" << error.message() << ").\n";
    return false;
}

std::filesystem::path ChunkStorage::ChunkPath(const voxel::ChunkCoord& coord) const {
    std::ostringstream name;
    name << "chunk_" << coord.x << "_" << coord.y << "_" << coord.z << ".bin";
    return root_ / name.str();
}

bool ChunkStorage::LoadChunk(const voxel::ChunkCoord& coord, voxel::Chunk& chunk) {
    const std::filesystem::path path = ChunkPath(coord);
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        if (error) {
            std::cout << "[Storage] Failed to stat chunk file " << path.string()
                      << ": " << error.message() << ".\n";
        }
        return false;
    }

    const std::uint32_t expectedPayload = static_cast<std::uint32_t>(voxel::kChunkVolume * sizeof(voxel::BlockId));
    const std::uintmax_t fileSize = std::filesystem::file_size(path, error);
    if (error) {
        std::cout << "[Storage] Failed to stat chunk file " << path.string() << ": " << error.message() << ".\n";
        return false;
    }

    if (fileSize < kChunkHeaderSize) {
        std::cout << "[Storage] Reject chunk file " << path.string() << ": file too small.\n";
        return false;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cout << "[Storage] Failed to open chunk file " << path.string() << " for read.\n";
        return false;
    }

    ChunkFileHeader header;
    if (!ReadExact(in, header.magic.data(), header.magic.size()) ||
        !ReadExact(in, &header.version, sizeof(header.version)) ||
        !ReadExact(in, &header.cx, sizeof(header.cx)) ||
        !ReadExact(in, &header.cy, sizeof(header.cy)) ||
        !ReadExact(in, &header.cz, sizeof(header.cz)) ||
        !ReadExact(in, &header.chunkSize, sizeof(header.chunkSize)) ||
        !ReadExact(in, &header.blockTypeBytes, sizeof(header.blockTypeBytes)) ||
        !ReadExact(in, &header.payloadBytes, sizeof(header.payloadBytes))) {
        std::cout << "[Storage] Reject chunk file " << path.string() << ": header truncated.\n";
        return false;
    }

    if (header.magic != kChunkMagic) {
        std::cout << "[Storage] Reject chunk file " << path.string() << ": bad magic.\n";
        return false;
    }
    if (header.version != kChunkVersion) {
        std::cout << "[Storage] Reject chunk file " << path.string() << ": version mismatch ("
                  << header.version << ").\n";
        return false;
    }
    if (header.cx != coord.x || header.cy != coord.y || header.cz != coord.z) {
        std::cout << "[Storage] Reject chunk file " << path.string() << ": coord mismatch.\n";
        return false;
    }
    if (header.chunkSize != static_cast<std::uint32_t>(voxel::kChunkSize)) {
        std::cout << "[Storage] Reject chunk file " << path.string() << ": chunk size mismatch.\n";
        return false;
    }
    if (header.blockTypeBytes != sizeof(voxel::BlockId)) {
        std::cout << "[Storage] Reject chunk file " << path.string() << ": block type size mismatch.\n";
        return false;
    }
    if (header.payloadBytes != expectedPayload) {
        std::cout << "[Storage] Reject chunk file " << path.string() << ": payload size mismatch.\n";
        return false;
    }

    const std::uintmax_t expectedSize = kChunkHeaderSize + expectedPayload;
    if (fileSize != expectedSize) {
        std::cout << "[Storage] Reject chunk file " << path.string() << ": file size mismatch.\n";
        return false;
    }

    if (!ReadExact(in, chunk.Data(), expectedPayload)) {
        std::cout << "[Storage] Reject chunk file " << path.string() << ": payload truncated.\n";
        return false;
    }

    std::cout << "[Storage] Loaded chunk " << CoordToString(coord)
              << " (" << header.payloadBytes << " bytes).\n";
    return true;
}

bool ChunkStorage::SaveChunk(const voxel::ChunkCoord& coord, const voxel::Chunk& chunk) {
    if (!EnsureRoot()) {
        return false;
    }

    const std::filesystem::path path = ChunkPath(coord);
    const std::filesystem::path tempPath = path.string() + ".tmp";
    const std::uint32_t payloadBytes = static_cast<std::uint32_t>(voxel::kChunkVolume * sizeof(voxel::BlockId));

    ChunkFileHeader header;
    header.magic = kChunkMagic;
    header.version = kChunkVersion;
    header.cx = coord.x;
    header.cy = coord.y;
    header.cz = coord.z;
    header.chunkSize = static_cast<std::uint32_t>(voxel::kChunkSize);
    header.blockTypeBytes = static_cast<std::uint32_t>(sizeof(voxel::BlockId));
    header.payloadBytes = payloadBytes;

    auto start = std::chrono::steady_clock::now();
    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            std::cout << "[Storage] Failed to open temp file " << tempPath.string() << " for write.\n";
            return false;
        }

        if (!WriteExact(out, header.magic.data(), header.magic.size()) ||
            !WriteExact(out, &header.version, sizeof(header.version)) ||
            !WriteExact(out, &header.cx, sizeof(header.cx)) ||
            !WriteExact(out, &header.cy, sizeof(header.cy)) ||
            !WriteExact(out, &header.cz, sizeof(header.cz)) ||
            !WriteExact(out, &header.chunkSize, sizeof(header.chunkSize)) ||
            !WriteExact(out, &header.blockTypeBytes, sizeof(header.blockTypeBytes)) ||
            !WriteExact(out, &header.payloadBytes, sizeof(header.payloadBytes)) ||
            !WriteExact(out, chunk.Data(), payloadBytes)) {
            std::cout << "[Storage] Failed to write chunk data to " << tempPath.string() << ".\n";
            return false;
        }

        out.flush();
        if (!out) {
            std::cout << "[Storage] Failed to flush chunk file " << tempPath.string() << ".\n";
            return false;
        }
    }

    std::error_code error;
    std::filesystem::rename(tempPath, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(tempPath, path, error);
    }
    if (error) {
        std::cout << "[Storage] Failed to move temp file into place: " << error.message() << ".\n";
        return false;
    }

    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "[Storage] Saved chunk " << CoordToString(coord)
              << " (" << payloadBytes << " bytes, " << elapsed << " ms).\n";
    return true;
}

} // namespace persistence
