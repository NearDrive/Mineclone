#include <glad/glad.h>

#include "voxel/ChunkMesh.h"

#include "core/Assert.h"

#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#endif
#ifndef GL_UNSIGNED_INT
#define GL_UNSIGNED_INT 0x1405
#endif

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
        glad_glGenVertexArrays(1, &vao_);
    }
    if (vbo_ == 0) {
        glad_glGenBuffers(1, &vbo_);
    }
    if (ebo_ == 0) {
        glad_glGenBuffers(1, &ebo_);
    }

    glad_glBindVertexArray(vao_);

    glad_glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glad_glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices_.size() * sizeof(VoxelVertex)),
                      vertices_.data(), GL_STATIC_DRAW);

    glad_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glad_glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices_.size() * sizeof(std::uint32_t)),
                      indices_.data(), GL_STATIC_DRAW);
    gpuIndexCount_ = indices_.size();

    glad_glEnableVertexAttribArray(0);
    glad_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(VoxelVertex), reinterpret_cast<void*>(0));

    glad_glEnableVertexAttribArray(1);
    glad_glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(VoxelVertex),
                               reinterpret_cast<void*>(sizeof(glm::vec3)));

    glad_glBindVertexArray(0);
}

void ChunkMesh::DestroyGpu() {
    MC_ASSERT_MAIN_THREAD_GL();
    if (ebo_ != 0) {
        glad_glDeleteBuffers(1, &ebo_);
        ebo_ = 0;
    }
    if (vbo_ != 0) {
        glad_glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (vao_ != 0) {
        glad_glDeleteVertexArrays(1, &vao_);
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
    glad_glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(gpuIndexCount_), GL_UNSIGNED_INT, nullptr);
    glad_glBindVertexArray(0);
}

} // namespace voxel
