#pragma once

#include <glm/glm.hpp>

#include "voxel/ChunkRegistry.h"

namespace game {

class Player {
public:
    static constexpr float kWidth = 0.6f;
    static constexpr float kDepth = 0.6f;
    static constexpr float kHeight = 1.8f;
    static constexpr float kMoveSpeed = 4.5f;
    static constexpr float kGravity = -20.0f;
    static constexpr float kJumpSpeed = 8.0f;

    explicit Player(const glm::vec3& spawnPosition);

    void Update(const voxel::ChunkRegistry& registry,
                const glm::vec3& desiredDirection,
                bool jumpPressed,
                float deltaTime);

    const glm::vec3& Position() const { return position_; }
    const glm::vec3& Velocity() const { return velocity_; }
    bool Grounded() const { return grounded_; }

    void SetPosition(const glm::vec3& position);
    void ResetVelocity();

private:
    float HalfWidth() const { return kWidth * 0.5f; }
    float HalfDepth() const { return kDepth * 0.5f; }

    void ResolveAxis(const voxel::ChunkRegistry& registry,
                     float deltaTime,
                     int axisIndex,
                     float halfExtent,
                     float positiveOffset);

    glm::vec3 position_;
    glm::vec3 velocity_{0.0f};
    bool grounded_ = false;
};

} // namespace game
