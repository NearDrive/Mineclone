#include "math/Frustum.h"

#include <cassert>
#include <cmath>

#include <glm/gtc/matrix_access.hpp>

namespace {
Plane MakePlane(const glm::vec4& coefficients) {
    Plane plane;
    plane.normal = glm::vec3(coefficients);
    plane.d = coefficients.w;
    plane.Normalize();
    return plane;
}
} // namespace

Frustum Frustum::FromMatrix(const glm::mat4& viewProjection) {
    const glm::vec4 row0 = glm::row(viewProjection, 0);
    const glm::vec4 row1 = glm::row(viewProjection, 1);
    const glm::vec4 row2 = glm::row(viewProjection, 2);
    const glm::vec4 row3 = glm::row(viewProjection, 3);

#ifndef NDEBUG
    const glm::vec4 rows[] = {row0, row1, row2, row3};
    for (const glm::vec4& row : rows) {
        assert(std::isfinite(row.x));
        assert(std::isfinite(row.y));
        assert(std::isfinite(row.z));
        assert(std::isfinite(row.w));
    }
#endif

    Frustum frustum;
    frustum.planes_[Left] = MakePlane(row3 + row0);
    frustum.planes_[Right] = MakePlane(row3 - row0);
    frustum.planes_[Bottom] = MakePlane(row3 + row1);
    frustum.planes_[Top] = MakePlane(row3 - row1);
    frustum.planes_[Near] = MakePlane(row3 + row2);
    frustum.planes_[Far] = MakePlane(row3 - row2);

    return frustum;
}

bool Frustum::IntersectsAabb(const glm::vec3& min, const glm::vec3& max) const {
    assert(min.x <= max.x && min.y <= max.y && min.z <= max.z);

    for (const Plane& plane : planes_) {
        glm::vec3 positive{
            plane.normal.x >= 0.0f ? max.x : min.x,
            plane.normal.y >= 0.0f ? max.y : min.y,
            plane.normal.z >= 0.0f ? max.z : min.z};

        if (plane.Distance(positive) < 0.0f) {
            return false;
        }
    }

    return true;
}
