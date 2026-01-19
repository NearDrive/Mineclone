#include "game/Player.h"

#include "physics/VoxelCollision.h"

namespace game {

Player::Player(const glm::vec3& spawnPosition)
    : position_(spawnPosition) {}

void Player::SetPosition(const glm::vec3& position) {
    position_ = position;
}

void Player::ResetVelocity() {
    velocity_ = glm::vec3(0.0f);
}

void Player::Update(const voxel::ChunkRegistry& registry,
                    const glm::vec3& desiredDirection,
                    bool jumpPressed,
                    float deltaTime) {
    glm::vec3 horizontal = desiredDirection * kMoveSpeed;
    velocity_.x = horizontal.x;
    velocity_.z = horizontal.z;

    velocity_.y += kGravity * deltaTime;

    if (jumpPressed && grounded_) {
        velocity_.y = kJumpSpeed;
        grounded_ = false;
    }

    grounded_ = false;

    ResolveAxis(registry, deltaTime, 0, HalfWidth(), HalfWidth());
    ResolveAxis(registry, deltaTime, 2, HalfDepth(), HalfDepth());
    ResolveAxis(registry, deltaTime, 1, 0.0f, kHeight);
}

void Player::ResolveAxis(const voxel::ChunkRegistry& registry,
                         float deltaTime,
                         int axisIndex,
                         float halfExtent,
                         float positiveOffset) {
    float velocityAxis = velocity_[axisIndex];
    if (velocityAxis == 0.0f) {
        return;
    }

    glm::vec3 newPosition = position_;
    newPosition[axisIndex] += velocityAxis * deltaTime;

    physics::Aabb aabb = physics::MakePlayerAabb(newPosition, kWidth, kHeight, kDepth);
    if (!physics::AabbIntersectsSolid(registry, aabb)) {
        position_[axisIndex] = newPosition[axisIndex];
        return;
    }

    int hitCoord = 0;
    bool positiveDirection = velocityAxis > 0.0f;
    physics::Axis axis = static_cast<physics::Axis>(axisIndex);
    if (physics::FindBlockingVoxelOnAxis(registry, aabb, axis, positiveDirection, hitCoord)) {
        if (axisIndex == 1) {
            if (positiveDirection) {
                position_.y = static_cast<float>(hitCoord) - positiveOffset - physics::kVoxelEpsilon;
            } else {
                position_.y = static_cast<float>(hitCoord) + 1.0f + physics::kVoxelEpsilon;
                if (velocityAxis < 0.0f) {
                    grounded_ = true;
                }
            }
        } else {
            if (positiveDirection) {
                position_[axisIndex] = static_cast<float>(hitCoord) - halfExtent - physics::kVoxelEpsilon;
            } else {
                position_[axisIndex] = static_cast<float>(hitCoord) + 1.0f + halfExtent + physics::kVoxelEpsilon;
            }
        }
    }

    velocity_[axisIndex] = 0.0f;
}

} // namespace game
