#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "Camera.h"
#include "Shader.h"
#include "math/Frustum.h"
#include "voxel/Chunk.h"
#include "voxel/ChunkBounds.h"
#include "voxel/ChunkMesh.h"
#include "voxel/ChunkMesher.h"
#include "voxel/ChunkRegistry.h"
#include "voxel/ChunkStreaming.h"
#include "voxel/VoxelCoords.h"

namespace {
constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;
constexpr float kFov = 60.0f;
constexpr int kRenderRadiusDefault = 8;
constexpr int kRenderRadiusMin = 2;
constexpr int kRenderRadiusMax = 32;
constexpr int kLoadRadiusDefault = 10;
constexpr int kLoadRadiusMin = kRenderRadiusMin;
constexpr int kLoadRadiusMax = 48;

Camera gCamera(glm::vec3(0.0f, 20.0f, 40.0f), -90.0f, -15.0f);
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
void runVoxelSanityChecks() {
    using namespace voxel;

    ChunkRegistry registry;
    const int coordsToTest[] = {-33, -32, -1, 0, 31, 32, 33};

    for (int value : coordsToTest) {
        WorldBlockCoord world{value, value, value};
        ChunkCoord chunk = WorldToChunkCoord(world, kChunkSize);
        LocalCoord local = WorldToLocalCoord(world, kChunkSize);
        WorldBlockCoord roundtrip = ChunkLocalToWorld(chunk, local, kChunkSize);
        assert(world.x == roundtrip.x);
        assert(world.y == roundtrip.y);
        assert(world.z == roundtrip.z);
    }

    ChunkCoord originChunk{0, 0, 0};
    LocalCoord minLocal{0, 0, 0};
    LocalCoord maxLocal{kChunkSize - 1, kChunkSize - 1, kChunkSize - 1};
    WorldBlockCoord worldMin = ChunkLocalToWorld(originChunk, minLocal, kChunkSize);
    WorldBlockCoord worldMax = ChunkLocalToWorld(originChunk, maxLocal, kChunkSize);

    registry.SetBlock(worldMin, kBlockStone);
    registry.SetBlock(worldMax, kBlockDirt);
    assert(registry.GetBlock(worldMin) == kBlockStone);
    assert(registry.GetBlock(worldMax) == kBlockDirt);

    WorldBlockCoord negativeWorld{-1, -1, -1};
    ChunkCoord negativeChunk = WorldToChunkCoord(negativeWorld, kChunkSize);
    LocalCoord negativeLocal = WorldToLocalCoord(negativeWorld, kChunkSize);
    assert(negativeChunk.x == -1);
    assert(negativeChunk.y == -1);
    assert(negativeChunk.z == -1);
    assert(negativeLocal.x == kChunkSize - 1);
    assert(negativeLocal.y == kChunkSize - 1);
    assert(negativeLocal.z == kChunkSize - 1);

    registry.SetBlock(negativeWorld, kBlockDirt);
    assert(registry.GetBlock(negativeWorld) == kBlockDirt);

    WorldBlockCoord surface{0, 7, 0};
    WorldBlockCoord underground{0, 0, 0};
    assert(registry.GetBlock(surface) == kBlockDirt);
    assert(registry.GetBlock(underground) == kBlockStone);

    std::cout << "[Voxel] Sanity OK\n";
}

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
#ifndef NDEBUG
    runVoxelSanityChecks();
#endif

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
    if (!shader.loadFromFiles("shaders/voxel.vert", "shaders/voxel.frag", shaderError)) {
        std::cerr << "[Shader] " << shaderError << '\n';
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    voxel::ChunkRegistry chunkRegistry;
    voxel::ChunkMesher mesher;

    voxel::ChunkStreamingConfig streamingConfig;
    streamingConfig.renderRadius = kRenderRadiusDefault;
    streamingConfig.loadRadius = kLoadRadiusDefault;
    streamingConfig.maxChunkCreatesPerFrame = 3;
    streamingConfig.maxChunkMeshesPerFrame = 2;
    streamingConfig.maxGpuUploadsPerFrame = 3;

    voxel::ChunkStreaming streaming(streamingConfig);

    auto lastTime = std::chrono::high_resolution_clock::now();
    auto fpsTimer = lastTime;
    int frames = 0;
    bool escPressed = false;
    bool clickPressed = false;
    bool decreaseRadiusPressed = false;
    bool increaseRadiusPressed = false;
    bool decreaseLoadRadiusPressed = false;
    bool increaseLoadRadiusPressed = false;
    bool streamingTogglePressed = false;
    bool frustumTogglePressed = false;
    bool distanceTogglePressed = false;
    bool frustumCullingEnabled = true;
    bool distanceCullingEnabled = true;
    std::size_t lastLoadedChunks = 0;
    std::size_t lastDrawnChunks = 0;
    std::size_t lastFrustumCulled = 0;
    std::size_t lastDistanceCulled = 0;
    std::size_t lastDrawCalls = 0;
    std::size_t lastGpuReadyChunks = 0;
    std::size_t lastCreateQueue = 0;
    std::size_t lastMeshQueue = 0;
    std::size_t lastUploadQueue = 0;
    int lastCreates = 0;
    int lastMeshes = 0;
    int lastUploads = 0;

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

        int decreaseState = glfwGetKey(window, GLFW_KEY_LEFT_BRACKET);
        if (decreaseState == GLFW_PRESS && !decreaseRadiusPressed) {
            decreaseRadiusPressed = true;
            int newRadius = std::clamp(streaming.RenderRadius() - 1, kRenderRadiusMin, kRenderRadiusMax);
            streaming.SetRenderRadius(newRadius);
            std::cout << "[Culling] Render radius set to " << streaming.RenderRadius() << " chunks.\n";
        } else if (decreaseState == GLFW_RELEASE) {
            decreaseRadiusPressed = false;
        }

        int increaseState = glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET);
        if (increaseState == GLFW_PRESS && !increaseRadiusPressed) {
            increaseRadiusPressed = true;
            int newRadius = std::clamp(streaming.RenderRadius() + 1, kRenderRadiusMin, kRenderRadiusMax);
            streaming.SetRenderRadius(newRadius);
            std::cout << "[Culling] Render radius set to " << streaming.RenderRadius() << " chunks.\n";
        } else if (increaseState == GLFW_RELEASE) {
            increaseRadiusPressed = false;
        }

        int decreaseLoadState = glfwGetKey(window, GLFW_KEY_COMMA);
        if (decreaseLoadState == GLFW_PRESS && !decreaseLoadRadiusPressed) {
            decreaseLoadRadiusPressed = true;
            int newRadius = std::clamp(streaming.LoadRadius() - 1, kLoadRadiusMin, kLoadRadiusMax);
            streaming.SetLoadRadius(newRadius);
            std::cout << "[Streaming] Load radius set to " << streaming.LoadRadius() << " chunks.\n";
        } else if (decreaseLoadState == GLFW_RELEASE) {
            decreaseLoadRadiusPressed = false;
        }

        int increaseLoadState = glfwGetKey(window, GLFW_KEY_PERIOD);
        if (increaseLoadState == GLFW_PRESS && !increaseLoadRadiusPressed) {
            increaseLoadRadiusPressed = true;
            int newRadius = std::clamp(streaming.LoadRadius() + 1, kLoadRadiusMin, kLoadRadiusMax);
            streaming.SetLoadRadius(newRadius);
            std::cout << "[Streaming] Load radius set to " << streaming.LoadRadius() << " chunks.\n";
        } else if (increaseLoadState == GLFW_RELEASE) {
            increaseLoadRadiusPressed = false;
        }

        int streamingToggleState = glfwGetKey(window, GLFW_KEY_F3);
        if (streamingToggleState == GLFW_PRESS && !streamingTogglePressed) {
            streamingTogglePressed = true;
            streaming.SetEnabled(!streaming.Enabled());
            std::cout << "[Streaming] " << (streaming.Enabled() ? "Enabled" : "Paused") << ".\n";
        } else if (streamingToggleState == GLFW_RELEASE) {
            streamingTogglePressed = false;
        }

        int frustumToggleState = glfwGetKey(window, GLFW_KEY_F1);
        if (frustumToggleState == GLFW_PRESS && !frustumTogglePressed) {
            frustumTogglePressed = true;
            frustumCullingEnabled = !frustumCullingEnabled;
            std::cout << "[Culling] Frustum culling " << (frustumCullingEnabled ? "enabled" : "disabled") << ".\n";
        } else if (frustumToggleState == GLFW_RELEASE) {
            frustumTogglePressed = false;
        }

        int distanceToggleState = glfwGetKey(window, GLFW_KEY_F2);
        if (distanceToggleState == GLFW_PRESS && !distanceTogglePressed) {
            distanceTogglePressed = true;
            distanceCullingEnabled = !distanceCullingEnabled;
            std::cout << "[Culling] Distance culling " << (distanceCullingEnabled ? "enabled" : "disabled") << ".\n";
        } else if (distanceToggleState == GLFW_RELEASE) {
            distanceTogglePressed = false;
        }

        glClearColor(0.08f, 0.10f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        float aspect = width > 0 && height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;

        glm::mat4 projection = glm::perspective(glm::radians(kFov), aspect, 0.1f, 500.0f);
        glm::mat4 view = gCamera.getViewMatrix();
        const Frustum frustum = Frustum::FromMatrix(projection * view);
        glm::vec3 lightDir = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));

        shader.use();
        shader.setMat4("uProjection", projection);
        shader.setMat4("uView", view);
        shader.setVec3("uLightDir", lightDir);

        const glm::vec3 cameraPosition = gCamera.getPosition();
        voxel::WorldBlockCoord cameraBlock{
            static_cast<int>(std::floor(cameraPosition.x)),
            static_cast<int>(std::floor(cameraPosition.y)),
            static_cast<int>(std::floor(cameraPosition.z))};
        const voxel::ChunkCoord cameraChunk = voxel::WorldToChunkCoord(cameraBlock, voxel::kChunkSize);
        streaming.Tick(cameraChunk, chunkRegistry, mesher);

        std::size_t distanceCulled = 0;
        std::size_t frustumCulled = 0;
        std::size_t drawn = 0;

        const int renderRadiusChunks = streaming.RenderRadius();
        for (const auto& [coord, entry] : chunkRegistry.Entries()) {
            if (!entry.hasGpuMesh) {
                continue;
            }

            if (distanceCullingEnabled) {
                const int dx = std::abs(coord.x - cameraChunk.x);
                const int dz = std::abs(coord.z - cameraChunk.z);
                if (std::max(dx, dz) > renderRadiusChunks) {
                    ++distanceCulled;
                    continue;
                }
            }

            if (frustumCullingEnabled) {
                const voxel::ChunkBounds bounds = voxel::GetChunkBounds(coord);
                if (!frustum.IntersectsAabb(bounds.min, bounds.max)) {
                    ++frustumCulled;
                    continue;
                }
            }

            entry.mesh.Draw();
            ++drawn;
        }

        const voxel::ChunkStreamingStats& streamStats = streaming.Stats();
        lastLoadedChunks = streamStats.loadedChunks;
        lastGpuReadyChunks = streamStats.gpuReadyChunks;
        lastCreateQueue = streamStats.createQueue;
        lastMeshQueue = streamStats.meshQueue;
        lastUploadQueue = streamStats.uploadQueue;
        lastCreates = streamStats.createdThisFrame;
        lastMeshes = streamStats.meshedThisFrame;
        lastUploads = streamStats.uploadedThisFrame;
        lastDrawnChunks = drawn;
        lastFrustumCulled = frustumCulled;
        lastDistanceCulled = distanceCulled;
        lastDrawCalls = drawn;

        glfwSwapBuffers(window);
        glfwPollEvents();

        frames++;
        std::chrono::duration<float> fpsElapsed = now - fpsTimer;
        if (fpsElapsed.count() >= 0.25f) {
            float fps = static_cast<float>(frames) / fpsElapsed.count();
            std::ostringstream title;
            title << "Mineclone"
                  << " | FPS: " << std::fixed << std::setprecision(1) << fps
                  << " | PlayerChunk: (" << streamStats.playerChunk.x << "," << streamStats.playerChunk.z << ")"
                  << " | Loaded: " << lastLoadedChunks
                  << " | GPU Ready: " << lastGpuReadyChunks
                  << " | Drawn: " << lastDrawnChunks
                  << " | FrustumCulled: " << lastFrustumCulled
                  << " | DistCulled: " << lastDistanceCulled
                  << " | DrawCalls: " << lastDrawCalls
                  << " | LoadQ: " << lastCreateQueue << "/" << lastMeshQueue << "/" << lastUploadQueue
                  << " | Budgets: " << lastCreates << "/" << lastMeshes << "/" << lastUploads
                  << " | Radii L/R: " << streaming.LoadRadius() << "/" << streaming.RenderRadius();
            glfwSetWindowTitle(window, title.str().c_str());
            fpsTimer = now;
            frames = 0;
        }
    }

    chunkRegistry.DestroyAll();

    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
