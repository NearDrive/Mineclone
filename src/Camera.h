#ifndef MINECLONE_CAMERA_H
#define MINECLONE_CAMERA_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera {
public:
    enum class Movement {
        Forward,
        Backward,
        Left,
        Right
    };

    Camera(glm::vec3 position, float yaw, float pitch);

    glm::mat4 getViewMatrix() const;

    void processKeyboard(Movement direction, float deltaTime);
    void processMouseMovement(float xoffset, float yoffset, bool constrainPitch = true);

    float getYaw() const { return yaw_; }
    float getPitch() const { return pitch_; }
    const glm::vec3& getPosition() const { return position_; }

    void setMovementSpeed(float speed) { movementSpeed_ = speed; }
    void setMouseSensitivity(float sensitivity) { mouseSensitivity_ = sensitivity; }

private:
    void updateCameraVectors();

    glm::vec3 position_;
    glm::vec3 front_ = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 up_ = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 right_ = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 worldUp_ = glm::vec3(0.0f, 1.0f, 0.0f);

    float yaw_;
    float pitch_;

    float movementSpeed_ = 4.5f;
    float mouseSensitivity_ = 0.1f;
};

#endif
