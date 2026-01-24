#include "app/AppInput.h"

namespace app {

Camera gCamera({0.0f, 0.0f, 0.0f}, -90.0f, -15.0f);
bool gFirstMouse = true;
bool gMouseCaptured = false;
float gLastX = 0.0f;
float gLastY = 0.0f;

void MouseCallback(GLFWwindow* window, double xpos, double ypos) {
    if (!gMouseCaptured) {
        return;
    }

    if (gFirstMouse) {
        gLastX = static_cast<float>(xpos);
        gLastY = static_cast<float>(ypos);
        gFirstMouse = false;
    }

    float xoffset = static_cast<float>(xpos) - gLastX;
    float yoffset = gLastY - static_cast<float>(ypos);
    gLastX = static_cast<float>(xpos);
    gLastY = static_cast<float>(ypos);

    gCamera.processMouseMovement(xoffset, yoffset);
    (void)window;
}

void SetMouseCapture(GLFWwindow* window, bool capture) {
    gMouseCaptured = capture;
    if (capture) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        gFirstMouse = true;
    } else {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}

} // namespace app
