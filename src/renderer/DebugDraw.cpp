#include <glad/glad.h>

#include "renderer/DebugDraw.h"

#include <algorithm>

#ifndef GL_LINES
#define GL_LINES 0x0001
#endif
#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW 0x88E8
#endif

DebugDraw::DebugDraw() {
    EnsureBuffers();
}

DebugDraw::~DebugDraw() {
    if (vbo_ != 0) {
        glad_glDeleteBuffers(1, &vbo_);
    }
    if (vao_ != 0) {
        glad_glDeleteVertexArrays(1, &vao_);
    }
}

void DebugDraw::EnsureBuffers() {
    if (vao_ != 0 && vbo_ != 0) {
        return;
    }

    glad_glGenVertexArrays(1, &vao_);
    glad_glGenBuffers(1, &vbo_);

    glad_glBindVertexArray(vao_);
    glad_glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glad_glEnableVertexAttribArray(0);
    glad_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), static_cast<void*>(nullptr));

    glad_glBindBuffer(GL_ARRAY_BUFFER, 0);
    glad_glBindVertexArray(0);
}

void DebugDraw::UploadVertices() {
    glad_glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glad_glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices_.size() * sizeof(glm::vec3)),
                      vertices_.data(), GL_DYNAMIC_DRAW);
    glad_glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void DebugDraw::UpdateCube(const glm::vec3& min, const glm::vec3& max) {
    EnsureBuffers();

    const glm::vec3 p000(min.x, min.y, min.z);
    const glm::vec3 p100(max.x, min.y, min.z);
    const glm::vec3 p010(min.x, max.y, min.z);
    const glm::vec3 p110(max.x, max.y, min.z);
    const glm::vec3 p001(min.x, min.y, max.z);
    const glm::vec3 p101(max.x, min.y, max.z);
    const glm::vec3 p011(min.x, max.y, max.z);
    const glm::vec3 p111(max.x, max.y, max.z);

    vertices_ = {
        p000, p100,
        p100, p110,
        p110, p010,
        p010, p000,
        p001, p101,
        p101, p111,
        p111, p011,
        p011, p001,
        p000, p001,
        p100, p101,
        p110, p111,
        p010, p011,
    };

    UploadVertices();
    hasGeometry_ = true;
}

void DebugDraw::UpdateFace(const glm::vec3& min, const glm::vec3& max, const glm::ivec3& normal) {
    EnsureBuffers();

    glm::vec3 a;
    glm::vec3 b;
    glm::vec3 c;
    glm::vec3 d;

    if (normal.x != 0) {
        const float x = normal.x > 0 ? max.x : min.x;
        a = glm::vec3(x, min.y, min.z);
        b = glm::vec3(x, max.y, min.z);
        c = glm::vec3(x, max.y, max.z);
        d = glm::vec3(x, min.y, max.z);
    } else if (normal.y != 0) {
        const float y = normal.y > 0 ? max.y : min.y;
        a = glm::vec3(min.x, y, min.z);
        b = glm::vec3(max.x, y, min.z);
        c = glm::vec3(max.x, y, max.z);
        d = glm::vec3(min.x, y, max.z);
    } else {
        const float z = normal.z > 0 ? max.z : min.z;
        a = glm::vec3(min.x, min.y, z);
        b = glm::vec3(max.x, min.y, z);
        c = glm::vec3(max.x, max.y, z);
        d = glm::vec3(min.x, max.y, z);
    }

    vertices_ = {
        a, b,
        b, c,
        c, d,
        d, a,
    };

    UploadVertices();
    hasGeometry_ = true;
}

void DebugDraw::UpdateCrosshair(float halfWidthNdc, float halfHeightNdc) {
    EnsureBuffers();

    const float clampedHalfWidth = std::max(halfWidthNdc, 0.0f);
    const float clampedHalfHeight = std::max(halfHeightNdc, 0.0f);

    vertices_ = {
        {-clampedHalfWidth, 0.0f, 0.0f},
        {clampedHalfWidth, 0.0f, 0.0f},
        {0.0f, -clampedHalfHeight, 0.0f},
        {0.0f, clampedHalfHeight, 0.0f},
    };

    UploadVertices();
    hasGeometry_ = true;
}

void DebugDraw::Clear() {
    hasGeometry_ = false;
}

void DebugDraw::Draw() const {
    if (!hasGeometry_) {
        return;
    }

    glad_glBindVertexArray(vao_);
    glad_glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertices_.size()));
    glad_glBindVertexArray(0);
}
