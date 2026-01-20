#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <execinfo.h>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unistd.h>

#include "Camera.h"
#include "Shader.h"
#include "core/Assert.h"
#include "core/Cli.h"
#include "core/Profiler.h"
#include "core/Verify.h"
#include "core/WorkerPool.h"
#include "game/Player.h"
#include "math/Frustum.h"
#include "persistence/ChunkStorage.h"
#include "renderer/DebugDraw.h"
#include "voxel/Chunk.h"
#include "voxel/ChunkBounds.h"
#include "voxel/ChunkMesh.h"
#include "voxel/ChunkMesher.h"
#include "voxel/ChunkRegistry.h"
#include "voxel/ChunkStreaming.h"
#include "voxel/BlockEdit.h"
#include "voxel/Raycast.h"
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
constexpr float kReachDistance = 6.0f;
constexpr float kHighlightEpsilon = 0.015f;
constexpr float kMaxDeltaTime = 0.05f;
constexpr int kSmokeTestFrames = 60;
constexpr int kSmokeEditTimeoutMs = 1000;
constexpr int kSmokeMaxDurationMs = 1000;
constexpr float kSmokeDeltaTime = 1.0f / 60.0f;
const glm::vec3 kPlayerSpawn(0.0f, 20.0f, 0.0f);
const glm::vec3 kEyeOffset(0.0f, 1.6f, 0.0f);

Camera gCamera(kPlayerSpawn + kEyeOffset, -90.0f, -15.0f);
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

#if defined(__linux__)
void CrashHandler(int signal) {
    void* frames[64];
    int count = backtrace(frames, static_cast<int>(std::size(frames)));
    std::cerr << "[Crash] Signal " << signal << " received. Backtrace:\n";
    backtrace_symbols_fd(frames, count, STDERR_FILENO);
    std::_Exit(128 + signal);
}

void InstallCrashHandler() {
    std::signal(SIGSEGV, CrashHandler);
    std::signal(SIGABRT, CrashHandler);
}
#endif

} // namespace

int main(int argc, char** argv) {
    core::InitMainThread();
#if defined(__linux__)
    InstallCrashHandler();
#endif

    core::CliOptions options;
    std::string cliError;
    if (!core::ParseCli(argc, argv, options, cliError)) {
        std::cerr << "[CLI] " << cliError << '\n' << core::Usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (options.help) {
        std::cout << core::Usage(argv[0]);
        return EXIT_SUCCESS;
    }

    const bool smokeTest = options.smokeTest;
    const bool allowInput = !smokeTest;
#ifndef NDEBUG
    const bool enableGlDebug = !options.noGlDebug;
#endif

#ifndef NDEBUG
    const bool shouldRunVerify = true;
#else
    const bool shouldRunVerify = smokeTest;
#endif

    if (shouldRunVerify) {
        core::VerifyOptions verifyOptions;
        verifyOptions.enablePersistence = true;
        verifyOptions.persistenceRoot =
            std::filesystem::temp_directory_path() / "mineclone_verify";
        core::VerifyResult result = core::RunAll(verifyOptions);
        if (!result.ok) {
            return EXIT_FAILURE;
        }
    }

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        std::cerr << "[Init] Failed to initialize GLFW.\n";
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifndef NDEBUG
    if (enableGlDebug) {
        glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
    }
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
    if (enableGlDebug) {
        GLint flags = 0;
        glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
        if (flags & GL_CONTEXT_FLAG_DEBUG_BIT) {
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(debugCallback, nullptr);
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
            std::cout << "[Debug] OpenGL debug output enabled.\n";
        }
    }
#endif

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    setMouseCapture(window, allowInput);

    Shader shader;
    std::string shaderError;
    if (!shader.loadFromFiles("shaders/voxel.vert", "shaders/voxel.frag", shaderError)) {
        std::cerr << "[Shader] " << shaderError << '\n';
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    Shader debugShader;
    if (!debugShader.loadFromFiles("shaders/debug_line.vert", "shaders/debug_line.frag", shaderError)) {
        std::cerr << "[Shader] " << shaderError << '\n';
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    DebugDraw debugDraw;

    voxel::ChunkRegistry chunkRegistry;
    voxel::ChunkMesher mesher;
    persistence::ChunkStorage chunkStorage;
    chunkRegistry.SetStorage(&chunkStorage);

    voxel::ChunkStreamingConfig streamingConfig;
    streamingConfig.renderRadius = kRenderRadiusDefault;
    streamingConfig.loadRadius = kLoadRadiusDefault;
    streamingConfig.maxChunkCreatesPerFrame = 3;
    streamingConfig.maxChunkMeshesPerFrame = 2;
    streamingConfig.maxGpuUploadsPerFrame = 3;
    streamingConfig.workerThreads = smokeTest ? 0 : 2;

    voxel::ChunkStreaming streaming(streamingConfig);
    streaming.SetStorage(&chunkStorage);
    core::Profiler profiler;
    core::WorkerPool workerPool;
    workerPool.Start(static_cast<std::size_t>(streamingConfig.workerThreads),
                     streaming.GenerateQueue(),
                     streaming.MeshQueue(),
                     streaming.UploadQueue(),
                     chunkRegistry,
                     mesher,
                     &profiler);
    streaming.SetWorkerThreads(workerPool.ThreadCount());
    streaming.SetProfiler(&profiler);

    auto lastTime = std::chrono::steady_clock::now();
    const auto smokeStartTime = lastTime;
    auto fpsTimer = lastTime;
    int frames = 0;
    bool escPressed = false;
    bool leftClickPressed = false;
    bool rightClickPressed = false;
    bool decreaseRadiusPressed = false;
    bool increaseRadiusPressed = false;
    bool decreaseLoadRadiusPressed = false;
    bool increaseLoadRadiusPressed = false;
    bool streamingTogglePressed = false;
    bool savePressed = false;
    bool statsTogglePressed = false;
    bool statsPrintTogglePressed = false;
    bool frustumTogglePressed = false;
    bool distanceTogglePressed = false;
    bool frustumCullingEnabled = true;
    bool distanceCullingEnabled = true;
    bool statsTitleEnabled = true;
    bool statsPrintEnabled = false;
    auto lastStatsPrint = lastTime - std::chrono::seconds(5);
    std::size_t lastLoadedChunks = 0;
    std::size_t lastDrawnChunks = 0;
    std::size_t lastFrustumCulled = 0;
    std::size_t lastDistanceCulled = 0;
    std::size_t lastDrawCalls = 0;
    std::size_t lastGpuReadyChunks = 0;
    std::size_t lastGeneratedChunks = 0;
    std::size_t lastMeshedChunks = 0;
    std::size_t lastWorkerThreads = 0;
    std::size_t lastCreateQueue = 0;
    std::size_t lastMeshQueue = 0;
    std::size_t lastUploadQueue = 0;
    int lastCreates = 0;
    int lastMeshes = 0;
    int lastUploads = 0;
    voxel::RaycastHit currentHit;
    bool hasTarget = false;
    bool spacePressed = false;
#ifndef NDEBUG
    bool resetPressed = false;
#endif
    bool smokeEditRequested = false;
    bool smokeEditSucceeded = false;
    bool smokeFailed = false;
    int smokeFrames = 0;
    bool smokeChunkEnsured = false;
    auto lastClampLogTime = lastTime - std::chrono::seconds(1);
    game::Player player(kPlayerSpawn);
    glm::mat4 projection(1.0f);
    glm::mat4 view(1.0f);
    Frustum frustum = Frustum::FromMatrix(glm::mat4(1.0f));
    glm::vec3 lightDir(0.0f, -1.0f, 0.0f);
    glm::vec3 playerPosition = player.Position();
    voxel::ChunkCoord playerChunk{0, 0, 0};

    while (!glfwWindowShouldClose(window)) {
        core::ScopedTimer frameTimer(&profiler, core::Metric::Frame);
        auto now = std::chrono::steady_clock::now();
        float deltaTime = kSmokeDeltaTime;
        if (!smokeTest) {
            std::chrono::duration<float> delta = now - lastTime;
            deltaTime = delta.count();
            if (deltaTime > kMaxDeltaTime) {
                std::chrono::duration<float> clampElapsed = now - lastClampLogTime;
                if (clampElapsed.count() >= 1.0f) {
                    std::cout << "[Timing] Delta time clamped from " << deltaTime << " to " << kMaxDeltaTime << '\n';
                    lastClampLogTime = now;
                }
                deltaTime = kMaxDeltaTime;
            }
        }
        lastTime = now;
        glm::vec3 desiredDir(0.0f);

        {
            core::ScopedTimer updateTimer(&profiler, core::Metric::Update);

            bool jumpPressed = false;
            if (allowInput) {
                int escState = glfwGetKey(window, GLFW_KEY_ESCAPE);
                if (escState == GLFW_PRESS && !escPressed) {
                    escPressed = true;
                    if (gMouseCaptured) {
                        setMouseCapture(window, false);
                    }
                } else if (escState == GLFW_RELEASE) {
                    escPressed = false;
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

                int statsToggleState = glfwGetKey(window, GLFW_KEY_F3);
                if (statsToggleState == GLFW_PRESS && !statsTogglePressed) {
                    statsTogglePressed = true;
                    statsTitleEnabled = !statsTitleEnabled;
                    std::cout << "[Stats] Title " << (statsTitleEnabled ? "enabled" : "disabled") << ".\n";
                } else if (statsToggleState == GLFW_RELEASE) {
                    statsTogglePressed = false;
                }

                int statsPrintState = glfwGetKey(window, GLFW_KEY_F4);
                if (statsPrintState == GLFW_PRESS && !statsPrintTogglePressed) {
                    statsPrintTogglePressed = true;
                    statsPrintEnabled = !statsPrintEnabled;
                    std::cout << "[Stats] Stdout " << (statsPrintEnabled ? "enabled" : "disabled") << ".\n";
                } else if (statsPrintState == GLFW_RELEASE) {
                    statsPrintTogglePressed = false;
                }

                int saveState = glfwGetKey(window, GLFW_KEY_F5);
                if (saveState == GLFW_PRESS && !savePressed) {
                    savePressed = true;
                    std::size_t saved = chunkRegistry.SaveAllDirty(chunkStorage);
                    std::cout << "[Storage] Forced save of " << saved << " dirty chunk(s).\n";
                } else if (saveState == GLFW_RELEASE) {
                    savePressed = false;
                }

                int streamingToggleState = glfwGetKey(window, GLFW_KEY_F6);
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
                    std::cout << "[Culling] Frustum culling " << (frustumCullingEnabled ? "enabled" : "disabled")
                              << ".\n";
                } else if (frustumToggleState == GLFW_RELEASE) {
                    frustumTogglePressed = false;
                }

                int distanceToggleState = glfwGetKey(window, GLFW_KEY_F2);
                if (distanceToggleState == GLFW_PRESS && !distanceTogglePressed) {
                    distanceTogglePressed = true;
                    distanceCullingEnabled = !distanceCullingEnabled;
                    std::cout << "[Culling] Distance culling " << (distanceCullingEnabled ? "enabled" : "disabled")
                              << ".\n";
                } else if (distanceToggleState == GLFW_RELEASE) {
                    distanceTogglePressed = false;
                }

                if (gMouseCaptured) {
                    float yawRadians = glm::radians(gCamera.getYaw());
                    glm::vec3 forward(std::cos(yawRadians), 0.0f, std::sin(yawRadians));
                    glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));

                    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
                        desiredDir += forward;
                    }
                    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
                        desiredDir -= forward;
                    }
                    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
                        desiredDir -= right;
                    }
                    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
                        desiredDir += right;
                    }
                }

                if (glm::length(desiredDir) > 0.0f) {
                    desiredDir = glm::normalize(desiredDir);
                }

                int spaceState = glfwGetKey(window, GLFW_KEY_SPACE);
                if (spaceState == GLFW_PRESS && !spacePressed) {
                    spacePressed = true;
                    if (gMouseCaptured) {
                        jumpPressed = true;
                    }
                } else if (spaceState == GLFW_RELEASE) {
                    spacePressed = false;
                }

#ifndef NDEBUG
                int resetState = glfwGetKey(window, GLFW_KEY_R);
                if (resetState == GLFW_PRESS && !resetPressed) {
                    resetPressed = true;
                    player.SetPosition(kPlayerSpawn);
                    player.ResetVelocity();
                    std::cout << "[Debug] Player reset to spawn.\n";
                } else if (resetState == GLFW_RELEASE) {
                    resetPressed = false;
                }
#endif
            }

            player.Update(chunkRegistry, desiredDir, jumpPressed, deltaTime);
            gCamera.setPosition(player.Position() + kEyeOffset);

            glClearColor(0.08f, 0.10f, 0.15f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            int width = 0;
            int height = 0;
            glfwGetFramebufferSize(window, &width, &height);
            float aspect = width > 0 && height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;

            projection = glm::perspective(glm::radians(kFov), aspect, 0.1f, 500.0f);
            view = gCamera.getViewMatrix();
            frustum = Frustum::FromMatrix(projection * view);
            lightDir = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));

            currentHit = {};
            hasTarget = false;
            debugDraw.Clear();
            if (gMouseCaptured) {
                currentHit = voxel::RaycastBlocks(chunkRegistry, gCamera.getPosition(), gCamera.getFront(),
                                                  kReachDistance);
                if (currentHit.hit) {
                    hasTarget = true;
                    const glm::vec3 min = glm::vec3(currentHit.block) - glm::vec3(kHighlightEpsilon);
                    const glm::vec3 max = glm::vec3(currentHit.block) + glm::vec3(1.0f + kHighlightEpsilon);
                    debugDraw.UpdateCube(min, max);
                }
            }

            if (allowInput) {
                int leftState = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
                if (leftState == GLFW_PRESS && !leftClickPressed) {
                    leftClickPressed = true;
                    if (!gMouseCaptured) {
                        setMouseCapture(window, true);
                    } else if (hasTarget) {
                        voxel::WorldBlockCoord target{currentHit.block.x, currentHit.block.y, currentHit.block.z};
                        voxel::TrySetBlock(chunkRegistry, streaming, target, voxel::kBlockAir);
                    }
                } else if (leftState == GLFW_RELEASE) {
                    leftClickPressed = false;
                }

                int rightState = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT);
                if (rightState == GLFW_PRESS && !rightClickPressed) {
                    rightClickPressed = true;
                    if (gMouseCaptured && hasTarget && currentHit.normal != glm::ivec3(0)) {
                        glm::ivec3 placeBlock = currentHit.block + currentHit.normal;
                        voxel::WorldBlockCoord target{placeBlock.x, placeBlock.y, placeBlock.z};
                        if (chunkRegistry.GetBlockOrAir(target) == voxel::kBlockAir) {
                            voxel::TrySetBlock(chunkRegistry, streaming, target, voxel::kBlockDirt);
                        }
                    }
                } else if (rightState == GLFW_RELEASE) {
                    rightClickPressed = false;
                }
            }

            shader.use();
            shader.setMat4("uProjection", projection);
            shader.setMat4("uView", view);
            shader.setVec3("uLightDir", lightDir);

            playerPosition = player.Position();
            voxel::WorldBlockCoord playerBlock{
                static_cast<int>(std::floor(playerPosition.x)),
                static_cast<int>(std::floor(playerPosition.y)),
                static_cast<int>(std::floor(playerPosition.z))};
            playerChunk = voxel::WorldToChunkCoord(playerBlock, voxel::kChunkSize);
            streaming.Tick(playerChunk, chunkRegistry, mesher);

            if (smokeTest && !smokeEditRequested) {
                voxel::WorldBlockCoord target{voxel::kChunkSize - 1, 1, 0};
                voxel::ChunkCoord targetChunk = voxel::WorldToChunkCoord(target, voxel::kChunkSize);
                auto entry = chunkRegistry.TryGetEntry(targetChunk);
                const bool ready = entry &&
                                   entry->generationState.load(std::memory_order_acquire) ==
                                       voxel::GenerationState::Ready &&
                                   entry->chunk;
                const auto smokeElapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - smokeStartTime);

                if (!ready && !smokeChunkEnsured) {
                    auto ensureEntry = chunkRegistry.GetOrCreateEntry(targetChunk);
                    std::unique_lock<std::shared_mutex> lock(ensureEntry->dataMutex);
                    if (!ensureEntry->chunk) {
                        ensureEntry->chunk = std::make_unique<voxel::Chunk>();
                    }
                    ensureEntry->generationState.store(voxel::GenerationState::Ready, std::memory_order_release);
                    ensureEntry->dirty.store(false, std::memory_order_release);
                    smokeChunkEnsured = true;
                }

                if (ready || smokeChunkEnsured) {
                    smokeEditRequested = true;
                    smokeEditSucceeded = voxel::TrySetBlock(chunkRegistry, streaming, target, voxel::kBlockAir);
                    if (!smokeEditSucceeded) {
                        std::cerr << "[Smoke] Block edit failed.\n";
                        smokeFailed = true;
                    }
                } else if (smokeElapsed.count() >= kSmokeEditTimeoutMs) {
                    std::cerr << "[Smoke] Block edit precondition failed for chunk (" << targetChunk.x << ", "
                              << targetChunk.y << ", " << targetChunk.z << "): entry="
                              << (entry ? "set" : "null");
                    if (entry) {
                        std::cerr << " state="
                                  << static_cast<int>(entry->generationState.load(std::memory_order_acquire))
                                  << " chunk=" << (entry->chunk ? "set" : "null");
                    }
                    std::cerr << '\n';
                    smokeFailed = true;
                }
            }
        }

        std::size_t distanceCulled = 0;
        std::size_t frustumCulled = 0;
        std::size_t drawn = 0;

        {
            core::ScopedTimer renderTimer(&profiler, core::Metric::Render);
            const int renderRadiusChunks = streaming.RenderRadius();

            chunkRegistry.ForEachEntry([&](const voxel::ChunkCoord& coord,
                                           const std::shared_ptr<voxel::ChunkEntry>& entry) {
                if (entry->gpuState.load(std::memory_order_acquire) != voxel::GpuState::Uploaded) {
                    return;
                }

                if (distanceCullingEnabled) {
                    const int dx = std::abs(coord.x - playerChunk.x);
                    const int dz = std::abs(coord.z - playerChunk.z);
                    if (std::max(dx, dz) > renderRadiusChunks) {
                        ++distanceCulled;
                        return;
                    }
                }

                if (frustumCullingEnabled) {
                    const voxel::ChunkBounds bounds = voxel::GetChunkBounds(coord);
                    if (!frustum.IntersectsAabb(bounds.min, bounds.max)) {
                        ++frustumCulled;
                        return;
                    }
                }

                entry->mesh.Draw();
                ++drawn;
            });

            if (debugDraw.HasGeometry()) {
                debugShader.use();
                debugShader.setMat4("uProjection", projection);
                debugShader.setMat4("uView", view);
                debugShader.setVec3("uColor", glm::vec3(1.0f, 0.95f, 0.2f));
                // glLineWidth not available in current GLAD; default line width used.
                debugDraw.Draw();
            }
        }

        const voxel::ChunkStreamingStats& streamStats = streaming.Stats();
        lastLoadedChunks = streamStats.loadedChunks;
        lastGeneratedChunks = streamStats.generatedChunksReady;
        lastMeshedChunks = streamStats.meshedCpuReady;
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
        lastWorkerThreads = streamStats.workerThreads;

        glfwSwapBuffers(window);
        glfwPollEvents();

        frames++;
        if (smokeTest) {
            ++smokeFrames;
            const auto smokeElapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - smokeStartTime);
            if (smokeFailed) {
                break;
            }
            if (smokeElapsed.count() >= kSmokeMaxDurationMs || smokeFrames >= kSmokeTestFrames) {
                if (!smokeEditRequested || !smokeEditSucceeded) {
                    std::cerr << "[Smoke] Deterministic edit did not complete.\n";
                    smokeFailed = true;
                }
                std::cout << "[Smoke] Completed " << smokeFrames << " frames. "
                          << "Loaded=" << lastLoadedChunks
                          << " GPU=" << lastGpuReadyChunks << '\n';
                break;
            }
        }
        std::chrono::duration<float> fpsElapsed = now - fpsTimer;
        if (fpsElapsed.count() >= 0.25f) {
            float fps = static_cast<float>(frames) / fpsElapsed.count();
            auto round1 = [](float value) { return std::round(value * 10.0f) / 10.0f; };
            std::ostringstream title;
            core::ProfilerSnapshot snapshot = profiler.CollectSnapshot();
            const auto metricIndex = [](core::Metric metric) {
                return static_cast<std::size_t>(metric);
            };
            auto ms = [&](core::Metric metric) {
                return snapshot.emaMs[metricIndex(metric)];
            };

            title << "Mineclone"
                  << " | FPS: " << std::fixed << std::setprecision(1) << fps;

            if (statsTitleEnabled) {
                title << " | frame " << ms(core::Metric::Frame)
                      << "ms | upd " << ms(core::Metric::Update)
                      << "ms | up " << ms(core::Metric::Upload)
                      << "ms | rnd " << ms(core::Metric::Render) << "ms";

                const double genMs = snapshot.avgMs[metricIndex(core::Metric::Generate)];
                const double meshMs = snapshot.avgMs[metricIndex(core::Metric::Mesh)];
                const std::int64_t genCount = snapshot.counts[metricIndex(core::Metric::Generate)];
                const std::int64_t meshCount = snapshot.counts[metricIndex(core::Metric::Mesh)];

                title << " | gen " << std::setprecision(2) << genMs << "ms/job (" << genCount << ")"
                      << " | mesh " << meshMs << "ms/job (" << meshCount << ")"
                      << " | Loaded: " << lastLoadedChunks
                      << " | GPU: " << lastGpuReadyChunks
                      << " | Q: " << lastCreateQueue << "/" << lastMeshQueue << "/" << lastUploadQueue
                      << " | Drawn: " << lastDrawnChunks;
            }

            if (!statsTitleEnabled) {
                title << " | Pos: (" << round1(player.Position().x) << "," << round1(player.Position().y) << ","
                      << round1(player.Position().z) << ")";
            }
            glfwSetWindowTitle(window, title.str().c_str());

            if (statsPrintEnabled) {
                std::chrono::duration<double> printElapsed = now - lastStatsPrint;
                if (printElapsed.count() >= 5.0) {
                    std::ostringstream perfLine;
                    perfLine << "[Perf] fps " << std::fixed << std::setprecision(1) << fps
                             << " frame " << ms(core::Metric::Frame) << "ms"
                             << " upd " << ms(core::Metric::Update) << "ms"
                             << " up " << ms(core::Metric::Upload) << "ms"
                             << " rnd " << ms(core::Metric::Render) << "ms"
                             << " gen " << std::setprecision(2) << snapshot.avgMs[metricIndex(core::Metric::Generate)]
                             << "ms/job (" << snapshot.counts[metricIndex(core::Metric::Generate)] << ")"
                             << " mesh " << snapshot.avgMs[metricIndex(core::Metric::Mesh)]
                             << "ms/job (" << snapshot.counts[metricIndex(core::Metric::Mesh)] << ")"
                             << " loaded " << lastLoadedChunks
                             << " gpu " << lastGpuReadyChunks
                             << " q " << lastCreateQueue << "/" << lastMeshQueue << "/" << lastUploadQueue;
                    std::cout << perfLine.str() << '\n';
                    lastStatsPrint = now;
                }
            }

            fpsTimer = now;
            frames = 0;
        }
    }

    workerPool.Stop();
    chunkRegistry.SaveAllDirty(chunkStorage);
    chunkRegistry.DestroyAll();

    glfwDestroyWindow(window);
    glfwTerminate();
    if (smokeTest && smokeFailed) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
