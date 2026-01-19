#pragma once

#include <array>

#include <glad/glad.h>
#include <glm/glm.hpp>

class DebugDraw {
public:
    DebugDraw();
    ~DebugDraw();

    DebugDraw(const DebugDraw&) = delete;
    DebugDraw& operator=(const DebugDraw&) = delete;

    void UpdateCube(const glm::vec3& min, const glm::vec3& max);
    void Clear();
    void Draw() const;
    bool HasGeometry() const { return hasGeometry_; }

private:
    void EnsureBuffers();

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    bool hasGeometry_ = false;
    std::array<glm::vec3, 24> vertices_{};
};
