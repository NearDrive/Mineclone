#include "Camera.h"

Camera::Camera(glm::vec3 position, float yaw, float pitch)
    : position_(position), yaw_(yaw), pitch_(pitch) {
    updateCameraVectors();
}

glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(position_, position_ + front_, up_);
}

void Camera::processKeyboard(Movement direction, float deltaTime) {
    float velocity = movementSpeed_ * deltaTime;
    if (direction == Movement::Forward) {
        position_ += front_ * velocity;
    }
    if (direction == Movement::Backward) {
        position_ -= front_ * velocity;
    }
    if (direction == Movement::Left) {
        position_ -= right_ * velocity;
    }
    if (direction == Movement::Right) {
        position_ += right_ * velocity;
    }
}

void Camera::processMouseMovement(float xoffset, float yoffset, bool constrainPitch) {
    xoffset *= mouseSensitivity_;
    yoffset *= mouseSensitivity_;

    yaw_ += xoffset;
    pitch_ += yoffset;

    if (constrainPitch) {
        if (pitch_ > 89.0f) {
            pitch_ = 89.0f;
        }
        if (pitch_ < -89.0f) {
            pitch_ = -89.0f;
        }
    }

    updateCameraVectors();
}

void Camera::updateCameraVectors() {
    glm::vec3 front;
    front.x = cos(glm::radians(yaw_)) * cos(glm::radians(pitch_));
    front.y = sin(glm::radians(pitch_));
    front.z = sin(glm::radians(yaw_)) * cos(glm::radians(pitch_));
    front_ = glm::normalize(front);

    right_ = glm::normalize(glm::cross(front_, worldUp_));
    up_ = glm::normalize(glm::cross(right_, front_));
}
