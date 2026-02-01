#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glad/glad.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace voxel {

struct VoxelVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    float sunlight = 0.0f;
    float emissive = 0.0f;
};

class ChunkMesh {
public:
    void Clear();
    void ClearCpu();
    void Reserve(std::size_t vertexCount, std::size_t indexCount);

    std::vector<VoxelVertex>& Vertices();
    std::vector<std::uint32_t>& Indices();
    const std::vector<VoxelVertex>& Vertices() const;
    const std::vector<std::uint32_t>& Indices() const;

    std::size_t VertexCount() const;
    std::size_t IndexCount() const;
    std::size_t GpuIndexCount() const;

    void UploadToGpu();
    void DestroyGpu();
    void Draw() const;

private:
    std::vector<VoxelVertex> vertices_;
    std::vector<std::uint32_t> indices_;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    std::size_t gpuIndexCount_ = 0;
};

} // namespace voxel
