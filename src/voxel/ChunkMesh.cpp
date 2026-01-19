#include "voxel/ChunkMesh.h"

#include "core/Assert.h"

namespace voxel {

void ChunkMesh::Clear() {
    vertices_.clear();
    indices_.clear();
    gpuIndexCount_ = 0;
}

void ChunkMesh::ClearCpu() {
    vertices_.clear();
    indices_.clear();
}

void ChunkMesh::Reserve(std::size_t vertexCount, std::size_t indexCount) {
    vertices_.reserve(vertexCount);
    indices_.reserve(indexCount);
}

std::vector<VoxelVertex>& ChunkMesh::Vertices() {
    return vertices_;
}

std::vector<std::uint32_t>& ChunkMesh::Indices() {
    return indices_;
}

const std::vector<VoxelVertex>& ChunkMesh::Vertices() const {
    return vertices_;
}

const std::vector<std::uint32_t>& ChunkMesh::Indices() const {
    return indices_;
}

std::size_t ChunkMesh::VertexCount() const {
    return vertices_.size();
}

std::size_t ChunkMesh::IndexCount() const {
    return indices_.size();
}

std::size_t ChunkMesh::GpuIndexCount() const {
    return gpuIndexCount_;
}

void ChunkMesh::UploadToGpu() {
    MC_ASSERT_MAIN_THREAD_GL();
    if (vao_ == 0) {
        glGenVertexArrays(1, &vao_);
    }
    if (vbo_ == 0) {
        glGenBuffers(1, &vbo_);
    }
    if (ebo_ == 0) {
        glGenBuffers(1, &ebo_);
    }

    glBindVertexArray(vao_);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices_.size() * sizeof(VoxelVertex)),
                 vertices_.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices_.size() * sizeof(std::uint32_t)),
                 indices_.data(), GL_STATIC_DRAW);
    gpuIndexCount_ = indices_.size();

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(VoxelVertex), reinterpret_cast<void*>(0));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(VoxelVertex),
                          reinterpret_cast<void*>(sizeof(glm::vec3)));

    glBindVertexArray(0);
}

void ChunkMesh::DestroyGpu() {
    MC_ASSERT_MAIN_THREAD_GL();
    if (ebo_ != 0) {
        glDeleteBuffers(1, &ebo_);
        ebo_ = 0;
    }
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    gpuIndexCount_ = 0;
}

void ChunkMesh::Draw() const {
    MC_ASSERT_MAIN_THREAD_GL();
    if (gpuIndexCount_ == 0 || vao_ == 0) {
        return;
    }

    MC_ASSERT(gpuIndexCount_ % 3 == 0, "Chunk mesh index count must be a multiple of 3.");
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(gpuIndexCount_), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

} // namespace voxel
