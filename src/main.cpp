#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "Camera.h"
#include "Shader.h"

namespace {
constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;
constexpr float kFov = 60.0f;

Camera gCamera(glm::vec3(0.0f, 0.0f, 3.0f), -90.0f, 0.0f);
bool gFirstMouse = true;
bool gMouseCaptured = true;
float gLastX = static_cast<float>(kWindowWidth) / 2.0f;
float gLastY = static_cast<float>(kWindowHeight) / 2.0f;

void glfwErrorCallback(int error, const char* description) {
    std::cerr << "[GLFW] Error " << error << ": " << description << '\n';
}

void framebufferSizeCallback(GLFWwindow*, int width, int height) {
    glViewport(0, 0, width, height);
}

void mouseCallback(GLFWwindow* window, double xpos, double ypos) {
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

void setMouseCapture(GLFWwindow* window, bool capture) {
    gMouseCaptured = capture;
    if (capture) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        gFirstMouse = true;
    } else {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}

#ifndef NDEBUG
void APIENTRY debugCallback(GLenum source, GLenum type, GLuint id, GLenum severity,
                            GLsizei length, const GLchar* message, const void* userParam) {
    (void)source;
    (void)type;
    (void)id;
    (void)length;
    (void)userParam;

    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) {
        return;
    }

    std::cerr << "[OpenGL] " << message << '\n';
}
#endif

} // namespace

int main() {
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        std::cerr << "[Init] Failed to initialize GLFW.\n";
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifndef NDEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(kWindowWidth, kWindowHeight, "Mineclone", nullptr, nullptr);
    if (!window) {
        std::cerr << "[Init] Failed to create GLFW window.\n";
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetCursorPosCallback(window, mouseCallback);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::cerr << "[Init] Failed to initialize GLAD.\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    const GLubyte* vendor = glGetString(GL_VENDOR);
    const GLubyte* renderer = glGetString(GL_RENDERER);
    const GLubyte* version = glGetString(GL_VERSION);
    std::cout << "[GPU] Vendor: " << (vendor ? reinterpret_cast<const char*>(vendor) : "Unknown") << '\n';
    std::cout << "[GPU] Renderer: " << (renderer ? reinterpret_cast<const char*>(renderer) : "Unknown") << '\n';
    std::cout << "[GPU] Version: " << (version ? reinterpret_cast<const char*>(version) : "Unknown") << '\n';

#ifndef NDEBUG
    GLint flags = 0;
    glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
    if (flags & GL_CONTEXT_FLAG_DEBUG_BIT) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(debugCallback, nullptr);
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
        std::cout << "[Debug] OpenGL debug output enabled.\n";
    }
#endif

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    setMouseCapture(window, true);

    Shader shader;
    std::string shaderError;
    if (!shader.loadFromFiles("shaders/basic.vert", "shaders/basic.frag", shaderError)) {
        std::cerr << "[Shader] " << shaderError << '\n';
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    float cubeVertices[] = {
        // positions          // colors
        -0.5f, -0.5f, -0.5f,   0.85f, 0.35f, 0.25f,
         0.5f, -0.5f, -0.5f,   0.85f, 0.35f, 0.25f,
         0.5f,  0.5f, -0.5f,   0.85f, 0.35f, 0.25f,
         0.5f,  0.5f, -0.5f,   0.85f, 0.35f, 0.25f,
        -0.5f,  0.5f, -0.5f,   0.85f, 0.35f, 0.25f,
        -0.5f, -0.5f, -0.5f,   0.85f, 0.35f, 0.25f,

        -0.5f, -0.5f,  0.5f,   0.25f, 0.45f, 0.85f,
         0.5f, -0.5f,  0.5f,   0.25f, 0.45f, 0.85f,
         0.5f,  0.5f,  0.5f,   0.25f, 0.45f, 0.85f,
         0.5f,  0.5f,  0.5f,   0.25f, 0.45f, 0.85f,
        -0.5f,  0.5f,  0.5f,   0.25f, 0.45f, 0.85f,
        -0.5f, -0.5f,  0.5f,   0.25f, 0.45f, 0.85f,

        -0.5f,  0.5f,  0.5f,   0.35f, 0.85f, 0.45f,
        -0.5f,  0.5f, -0.5f,   0.35f, 0.85f, 0.45f,
        -0.5f, -0.5f, -0.5f,   0.35f, 0.85f, 0.45f,
        -0.5f, -0.5f, -0.5f,   0.35f, 0.85f, 0.45f,
        -0.5f, -0.5f,  0.5f,   0.35f, 0.85f, 0.45f,
        -0.5f,  0.5f,  0.5f,   0.35f, 0.85f, 0.45f,

         0.5f,  0.5f,  0.5f,   0.85f, 0.85f, 0.25f,
         0.5f,  0.5f, -0.5f,   0.85f, 0.85f, 0.25f,
         0.5f, -0.5f, -0.5f,   0.85f, 0.85f, 0.25f,
         0.5f, -0.5f, -0.5f,   0.85f, 0.85f, 0.25f,
         0.5f, -0.5f,  0.5f,   0.85f, 0.85f, 0.25f,
         0.5f,  0.5f,  0.5f,   0.85f, 0.85f, 0.25f,

        -0.5f, -0.5f, -0.5f,   0.55f, 0.55f, 0.75f,
         0.5f, -0.5f, -0.5f,   0.55f, 0.55f, 0.75f,
         0.5f, -0.5f,  0.5f,   0.55f, 0.55f, 0.75f,
         0.5f, -0.5f,  0.5f,   0.55f, 0.55f, 0.75f,
        -0.5f, -0.5f,  0.5f,   0.55f, 0.55f, 0.75f,
        -0.5f, -0.5f, -0.5f,   0.55f, 0.55f, 0.75f,

        -0.5f,  0.5f, -0.5f,   0.75f, 0.35f, 0.75f,
         0.5f,  0.5f, -0.5f,   0.75f, 0.35f, 0.75f,
         0.5f,  0.5f,  0.5f,   0.75f, 0.35f, 0.75f,
         0.5f,  0.5f,  0.5f,   0.75f, 0.35f, 0.75f,
        -0.5f,  0.5f,  0.5f,   0.75f, 0.35f, 0.75f,
        -0.5f,  0.5f, -0.5f,   0.75f, 0.35f, 0.75f
    };

    GLuint vao = 0;
    GLuint vbo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(0));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    auto lastTime = std::chrono::high_resolution_clock::now();
    auto fpsTimer = lastTime;
    int frames = 0;
    bool escPressed = false;
    bool clickPressed = false;

    while (!glfwWindowShouldClose(window)) {
        auto now = std::chrono::high_resolution_clock::now();
        std::chrono::duration<float> delta = now - lastTime;
        float deltaTime = delta.count();
        lastTime = now;

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
            gCamera.processKeyboard(Camera::Movement::Forward, deltaTime);
        }
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
            gCamera.processKeyboard(Camera::Movement::Backward, deltaTime);
        }
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
            gCamera.processKeyboard(Camera::Movement::Left, deltaTime);
        }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
            gCamera.processKeyboard(Camera::Movement::Right, deltaTime);
        }

        int escState = glfwGetKey(window, GLFW_KEY_ESCAPE);
        if (escState == GLFW_PRESS && !escPressed) {
            escPressed = true;
            if (gMouseCaptured) {
                setMouseCapture(window, false);
            }
        } else if (escState == GLFW_RELEASE) {
            escPressed = false;
        }

        int clickState = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
        if (clickState == GLFW_PRESS && !clickPressed) {
            clickPressed = true;
            if (!gMouseCaptured) {
                setMouseCapture(window, true);
            }
        } else if (clickState == GLFW_RELEASE) {
            clickPressed = false;
        }

        glClearColor(0.08f, 0.10f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        float aspect = width > 0 && height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;

        glm::mat4 projection = glm::perspective(glm::radians(kFov), aspect, 0.1f, 100.0f);
        glm::mat4 view = gCamera.getViewMatrix();
        glm::mat4 model = glm::mat4(1.0f);

        shader.use();
        shader.setMat4("uProjection", projection);
        shader.setMat4("uView", view);
        shader.setMat4("uModel", model);

        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);

        glfwSwapBuffers(window);
        glfwPollEvents();

        frames++;
        std::chrono::duration<float> fpsElapsed = now - fpsTimer;
        if (fpsElapsed.count() >= 0.25f) {
            float fps = static_cast<float>(frames) / fpsElapsed.count();
            std::ostringstream title;
            title << "Mineclone" << " | FPS: " << std::fixed << std::setprecision(1) << fps;
            glfwSetWindowTitle(window, title.str().c_str());
            fpsTimer = now;
            frames = 0;
        }
    }

    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);

    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
