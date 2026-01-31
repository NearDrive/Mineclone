#pragma once

#include <vector>

#include <glad/glad.h>
#include <glm/glm.hpp>

class DebugDraw {
public:
    DebugDraw();
    ~DebugDraw();

    DebugDraw(const DebugDraw&) = delete;
    DebugDraw& operator=(const DebugDraw&) = delete;

    void UpdateCube(const glm::vec3& min, const glm::vec3& max);
    void UpdateFace(const glm::vec3& min, const glm::vec3& max, const glm::ivec3& normal);
    void UpdateCrosshair(float halfWidthNdc, float halfHeightNdc);
    void Clear();
    void Draw() const;
    bool HasGeometry() const { return hasGeometry_; }

private:
    void EnsureBuffers();
    void UploadVertices();

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    bool hasGeometry_ = false;
    std::vector<glm::vec3> vertices_{};
};
