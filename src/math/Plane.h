#ifndef MINECLONE_MATH_PLANE_H
#define MINECLONE_MATH_PLANE_H

#include <glm/glm.hpp>

struct Plane {
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float d = 0.0f;

    void Normalize();
    float Distance(const glm::vec3& point) const;
};

#endif
