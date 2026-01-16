#ifndef MINECLONE_MATH_FRUSTUM_H
#define MINECLONE_MATH_FRUSTUM_H

#include <array>

#include <glm/glm.hpp>

#include "math/Plane.h"

class Frustum {
public:
    enum PlaneIndex {
        Left = 0,
        Right,
        Bottom,
        Top,
        Near,
        Far
    };

    static Frustum FromMatrix(const glm::mat4& viewProjection);

    bool IntersectsAabb(const glm::vec3& min, const glm::vec3& max) const;

private:
    std::array<Plane, 6> planes_{};
};

#endif
