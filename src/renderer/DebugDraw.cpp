#include "renderer/DebugDraw.h"

DebugDraw::DebugDraw() {
    EnsureBuffers();
}

DebugDraw::~DebugDraw() {
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
    }
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
    }
}

void DebugDraw::EnsureBuffers() {
    if (vao_ != 0 && vbo_ != 0) {
        return;
    }

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices_.size() * sizeof(glm::vec3)), nullptr,
                 GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), static_cast<void*>(nullptr));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
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

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(vertices_.size() * sizeof(glm::vec3)),
                    vertices_.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    hasGeometry_ = true;
}

void DebugDraw::Clear() {
    hasGeometry_ = false;
}

void DebugDraw::Draw() const {
    if (!hasGeometry_) {
        return;
    }

    glBindVertexArray(vao_);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertices_.size()));
    glBindVertexArray(0);
}
