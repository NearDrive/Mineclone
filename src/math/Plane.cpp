#include "math/Plane.h"

#include <cassert>
#include <cmath>

void Plane::Normalize() {
    const float length = glm::length(normal);
    assert(length > 0.0f);
    if (length > 0.0f) {
        normal /= length;
        d /= length;
    }
    assert(std::isfinite(normal.x));
    assert(std::isfinite(normal.y));
    assert(std::isfinite(normal.z));
    assert(std::isfinite(d));
}

float Plane::Distance(const glm::vec3& point) const {
    return glm::dot(normal, point) + d;
}
