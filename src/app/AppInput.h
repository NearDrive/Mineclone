#pragma once

#include <GLFW/glfw3.h>

#include "Camera.h"

namespace app {

extern Camera gCamera;
extern bool gMouseCaptured;
extern bool gFirstMouse;
extern float gLastX;
extern float gLastY;

void MouseCallback(GLFWwindow* window, double xpos, double ypos);
void SetMouseCapture(GLFWwindow* window, bool capture);

} // namespace app
