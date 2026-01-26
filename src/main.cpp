#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <vector>

#include "stb_image.h"

#if defined(__linux__)
#include <execinfo.h>
#include <unistd.h>
#endif

#include "Camera.h"
#include "Shader.h"
#include "app/AppInput.h"
#include "app/AppMode.h"
#include "core/Assert.h"
#include "core/Cli.h"
#include "core/Profiler.h"
#include "core/Sha256.h"
#include "core/Verify.h"
#include "core/WorldTest.h"
#include "core/WorkerPool.h"
#include "game/Player.h"
#include "math/Frustum.h"
#include "persistence/ChunkFormat.h"
#include "persistence/ChunkStorage.h"
#include "renderer/DebugDraw.h"
#include "renderer/RenderTest.h"
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
constexpr int kInteractionTestFrames = 240;
constexpr std::uint32_t kInteractionTestSeed = 1337;
constexpr float kInteractionDeltaTime = 1.0f / 60.0f;
constexpr int kInteractionRenderRadius = 3;
constexpr int kInteractionLoadRadius = 4;
constexpr int kInteractionWorkerThreads = 1;
constexpr int kSoakTestFrames = 2000;
constexpr int kSoakTestLongFrames = 10000;

GLuint CreateTextureFromPixels(int width, int height, const std::vector<std::uint8_t>& pixels) {
    if (width <= 0 || height <= 0 || pixels.empty()) {
        return 0;
    }

    GLuint texture = 0;
    glad_glGenTextures(1, &texture);
    glad_glBindTexture(GL_TEXTURE_2D, texture);
    glad_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return texture;
}

std::vector<std::uint8_t> BuildProceduralDirtPixels(int width, int height) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    std::uint32_t state = 0x1234abcd;
    auto nextRandom = [&state]() {
        state = state * 1664525u + 1013904223u;
        return state;
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::uint32_t noiseSeed =
                nextRandom() + static_cast<std::uint32_t>(x * 374761393u) + static_cast<std::uint32_t>(y * 668265263u);
            const int noise = static_cast<int>((noiseSeed >> 24) & 0xFF) % 37 - 18;
            int r = 110 + noise + static_cast<int>((noiseSeed >> 16) & 0xF) - 7;
            int g = 80 + noise;
            int b = 50 + noise + static_cast<int>((noiseSeed >> 12) & 0x7) - 3;
            if (((noiseSeed >> 8) & 0xFF) < 15) {
                r += 20;
                g += 20;
                b += 20;
            }
            r = std::clamp(r, 0, 255);
            g = std::clamp(g, 0, 255);
            b = std::clamp(b, 0, 255);

            const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                       static_cast<std::size_t>(x)) *
                                      4u;
            pixels[index + 0] = static_cast<std::uint8_t>(r);
            pixels[index + 1] = static_cast<std::uint8_t>(g);
            pixels[index + 2] = static_cast<std::uint8_t>(b);
            pixels[index + 3] = 255;
        }
    }
    return pixels;
}

GLuint CreateProceduralDirtTexture(int width, int height) {
    auto pixels = BuildProceduralDirtPixels(width, height);
    return CreateTextureFromPixels(width, height, pixels);
}

GLuint LoadTexture2D(const std::string& path) {
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!data) {
        return 0;
    }

    std::vector<std::uint8_t> pixels(data, data + (width * height * 4));
    stbi_image_free(data);
    return CreateTextureFromPixels(width, height, pixels);
}
constexpr int kSoakSaveInterval = 200;
constexpr int kSoakSaveIntervalLong = 500;
constexpr int kSoakEditStartFrame = 50;
constexpr int kSoakEditInterval = 100;
constexpr float kSoakDeltaTime = 1.0f / 60.0f;
constexpr int kSoakRenderRadius = 4;
constexpr int kSoakLoadRadius = 6;
constexpr int kSoakWorkerThreads = 1;
constexpr int kSoakSyncMaxIterations = 200;
const glm::vec3 kInteractionMoveDir(-2.0f, 0.0f, -2.0f);
const glm::vec3 kPlayerSpawn(0.0f, 20.0f, 0.0f);
const glm::vec3 kEyeOffset(0.0f, 1.6f, 0.0f);

void glfwErrorCallback(int error, const char* description) {
    std::cerr << "[GLFW] Error " << error << ": " << description << '\n';
}

void framebufferSizeCallback(GLFWwindow*, int width, int height) {
    glViewport(0, 0, width, height);
}

void SetCameraAngles(Camera& camera, float yaw, float pitch) {
    const float yawDelta = yaw - camera.getYaw();
    const float pitchDelta = pitch - camera.getPitch();
    camera.processMouseMovement(yawDelta, pitchDelta, true);
}

void AppendInt32(std::vector<std::uint8_t>& buffer, std::int32_t value) {
    buffer.push_back(static_cast<std::uint8_t>(value & 0xFF));
    buffer.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    buffer.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    buffer.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

void AppendUint32(std::vector<std::uint8_t>& buffer, std::uint32_t value) {
    buffer.push_back(static_cast<std::uint8_t>(value & 0xFF));
    buffer.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    buffer.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    buffer.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

void AppendUint16(std::vector<std::uint8_t>& buffer, std::uint16_t value) {
    buffer.push_back(static_cast<std::uint8_t>(value & 0xFF));
    buffer.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void AppendFloat(std::vector<std::uint8_t>& buffer, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    AppendUint32(buffer, bits);
}

bool IsChunkReady(const voxel::ChunkRegistry& registry, const voxel::ChunkCoord& coord) {
    auto entry = registry.TryGetEntry(coord);
    if (!entry) {
        return false;
    }
    if (entry->generationState.load(std::memory_order_acquire) != voxel::GenerationState::Ready) {
        return false;
    }
    std::shared_lock<std::shared_mutex> lock(entry->dataMutex);
    return entry->chunk != nullptr;
}

void EnsureChunkReady(voxel::ChunkRegistry& registry, const voxel::ChunkCoord& coord) {
    auto entry = registry.GetOrCreateEntry(coord);
    std::unique_lock<std::shared_mutex> lock(entry->dataMutex);
    if (!entry->chunk) {
        entry->chunk = std::make_unique<voxel::Chunk>();
        voxel::ChunkRegistry::GenerateChunkData(coord, *entry->chunk);
    }
    entry->generationState.store(voxel::GenerationState::Ready, std::memory_order_release);
    entry->dirty.store(false, std::memory_order_release);
}

glm::vec2 YawPitchFromDirection(const glm::vec3& direction) {
    glm::vec3 dir = glm::normalize(direction);
    const float yaw = glm::degrees(std::atan2(dir.z, dir.x));
    const float pitch = glm::degrees(std::asin(dir.y));
    return {yaw, pitch};
}

bool SameWorldCoord(const voxel::WorldBlockCoord& a, const voxel::WorldBlockCoord& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

std::filesystem::path BuildSoakStorageRoot(const std::string& modeLabel) {
    std::filesystem::path root = std::filesystem::temp_directory_path() / ("mineclone_" + modeLabel);
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    return root;
}

std::filesystem::path ChunkFilePath(const std::filesystem::path& root, const voxel::ChunkCoord& coord) {
    std::ostringstream name;
    name << "chunk_" << coord.x << "_" << coord.y << "_" << coord.z << ".bin";
    return root / name.str();
}

glm::vec3 SoakCameraPath(int frame, std::uint32_t seed) {
    const float seedOffset = static_cast<float>(seed % 1000) * 0.001f;
    const float t = static_cast<float>(frame) * 0.01f + seedOffset;
    const float x = std::sin(t) * 40.0f - 10.0f;
    const float z = std::cos(t * 0.8f) * 40.0f - 10.0f;
    return {x, 20.0f, z};
}

bool WaitForStreamingIdle(voxel::ChunkStreaming& streaming,
                          voxel::ChunkRegistry& registry,
                          const voxel::ChunkMesher& mesher,
                          core::WorkerPool& workerPool,
                          const voxel::ChunkCoord& playerChunk,
                          int maxIterations) {
    for (int i = 0; i < maxIterations; ++i) {
        streaming.Tick(playerChunk, registry, mesher);
        workerPool.NotifyWork();
        const voxel::ChunkStreamingStats& stats = streaming.Stats();
        if (stats.createQueue == 0 && stats.meshQueue == 0 && stats.uploadQueue == 0 &&
            stats.createdThisFrame == 0 && stats.meshedThisFrame == 0 && stats.uploadedThisFrame == 0) {
            return true;
        }
    }
    return false;
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

struct InteractionRaycastStep {
    int frame = 0;
    glm::vec3 cameraPosition{0.0f};
    glm::ivec3 targetBlock{0};
    voxel::BlockId blockId = voxel::kBlockAir;
};

struct InteractionEditStep {
    int frame = 0;
    voxel::WorldBlockCoord coord{0, 0, 0};
    voxel::BlockId blockId = voxel::kBlockAir;
    voxel::BlockId expectedId = voxel::kBlockAir;
};

struct InteractionTestState {
    bool failed = false;
    std::string failureMessage;
    int frames = 0;
    int edits = 0;
    int raycasts = 0;
    voxel::ChunkStreamingStats stats;
    std::string checksum;
};

struct SoakTestConfig {
    const char* mode = "soak-test";
    int frames = kSoakTestFrames;
    int saveInterval = kSoakSaveInterval;
};

struct SoakRaycastStep {
    int frame = 0;
    glm::vec3 cameraPosition{0.0f};
    glm::ivec3 targetBlock{0};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
    bool useTarget = false;
    bool expectHit = false;
    voxel::BlockId expectedId = voxel::kBlockAir;
};

struct SoakEditStep {
    int frame = 0;
    voxel::WorldBlockCoord coord{0, 0, 0};
    voxel::BlockId blockId = voxel::kBlockAir;
    voxel::BlockId expectedId = voxel::kBlockAir;
};

struct SoakSampleBlock {
    voxel::WorldBlockCoord coord{0, 0, 0};
    voxel::BlockId expectedId = voxel::kBlockAir;
};

struct SoakTestState {
    bool failed = false;
    std::string failureMessage;
    int frames = 0;
    int edits = 0;
    int raycasts = 0;
    int saves = 0;
    int loads = 0;
    voxel::ChunkStreamingStats stats;
    std::string checksum;
    std::uint32_t seed = 0;
    int workerThreads = 0;
    std::filesystem::path storageRoot;
};

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
    const bool interactionTest = options.interactionTest;
    const bool soakTest = options.soakTest;
    const bool soakTestLong = options.soakTestLong;
    const bool runSoakTest = soakTest || soakTestLong;
    const bool renderTest = options.renderTest;
    if (renderTest) {
        renderer::RenderTestOptions renderOptions;
        renderOptions.outputPath = options.renderTestOut;
        renderOptions.width = options.renderTestWidth;
        renderOptions.height = options.renderTestHeight;
        renderOptions.frames = options.renderTestFrames;
        renderOptions.seed = options.renderTestSeed;
        if (options.renderTestCompare) {
            renderOptions.comparePath = options.renderTestComparePath;
        }
#ifndef NDEBUG
        renderOptions.enableGlDebug = !options.noGlDebug;
#endif
        return renderer::RunRenderTest(renderOptions);
    }
    if (options.worldTest) {
        core::WorldTestResult result = core::RunWorldTest();
        if (!result.ok) {
            std::cerr << "[WorldTest] Failed: " << result.message << '\n';
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }
    const bool allowInput = !(smokeTest || interactionTest || runSoakTest);
#ifndef NDEBUG
    const bool enableGlDebug = !options.noGlDebug;
#endif

    SoakTestConfig soakConfig;
    if (soakTestLong) {
        soakConfig.mode = "soak-test-long";
        soakConfig.frames = kSoakTestLongFrames;
        soakConfig.saveInterval = kSoakSaveIntervalLong;
    }

#ifndef NDEBUG
    const bool shouldRunVerify = true;
#else
    const bool shouldRunVerify = smokeTest || interactionTest || runSoakTest;
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
    glfwSetCursorPosCallback(window, app::MouseCallback);

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

    app::SetMouseCapture(window, allowInput || interactionTest || runSoakTest);
    app::gCamera.setPosition(kPlayerSpawn + kEyeOffset);
    if (interactionTest || runSoakTest) {
        app::gCamera.setMouseSensitivity(1.0f);
        if (interactionTest) {
            SetCameraAngles(app::gCamera, -135.0f, -89.0f);
        }
    }

    if (!interactionTest && !runSoakTest) {
        app::AppModeOptions appOptions;
        appOptions.allowInput = allowInput;
        appOptions.smokeTest = smokeTest;
        app::AppMode appMode(window, appOptions);
        if (!appMode.IsInitialized()) {
            std::cerr << appMode.InitError() << '\n';
            glfwDestroyWindow(window);
            glfwTerminate();
            return EXIT_FAILURE;
        }

        while (!glfwWindowShouldClose(window) && !appMode.ShouldExit()) {
            appMode.Tick();
            glfwSwapBuffers(window);
            glfwPollEvents();
        }

        appMode.Shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        if (smokeTest && appMode.SmokeFailed()) {
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    bool smokeFailed = false;
    InteractionTestState interactionState;
    SoakTestState soakState;
    if (runSoakTest) {
        soakState.seed = options.soakTestSeed;
        soakState.workerThreads = kSoakWorkerThreads;
        soakState.storageRoot = BuildSoakStorageRoot(soakConfig.mode);
    }
    GLuint blockTexture = 0;
    {
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

        blockTexture = LoadTexture2D("textures/dirt.png");
        if (blockTexture == 0) {
            blockTexture = CreateProceduralDirtTexture(32, 32);
            if (blockTexture == 0) {
                std::cerr << "[Texture] Failed to load or generate textures/dirt.png\n";
                glfwDestroyWindow(window);
                glfwTerminate();
                return EXIT_FAILURE;
            }
            std::cout << "[Texture] Using procedurally generated dirt texture.\n";
        }

        DebugDraw debugDraw;

        voxel::ChunkRegistry chunkRegistry;
        voxel::ChunkMesher mesher;
        std::filesystem::path storageRoot = persistence::ChunkStorage::DefaultSavePath();
        if (runSoakTest) {
            storageRoot = soakState.storageRoot;
        }
        persistence::ChunkStorage chunkStorage(storageRoot);
        chunkRegistry.SetStorage(&chunkStorage);

        voxel::ChunkStreamingConfig streamingConfig;
        streamingConfig.renderRadius = runSoakTest ? kSoakRenderRadius
                                                  : (interactionTest ? kInteractionRenderRadius : kRenderRadiusDefault);
        streamingConfig.loadRadius = runSoakTest ? kSoakLoadRadius
                                                 : (interactionTest ? kInteractionLoadRadius : kLoadRadiusDefault);
        streamingConfig.maxChunkCreatesPerFrame = 3;
        streamingConfig.maxChunkMeshesPerFrame = 2;
        streamingConfig.maxGpuUploadsPerFrame = 3;
        streamingConfig.workerThreads = runSoakTest
            ? kSoakWorkerThreads
            : (interactionTest ? kInteractionWorkerThreads : (smokeTest ? 0 : 2));

        voxel::ChunkStreaming streaming(streamingConfig);
        streaming.SetStorage(&chunkStorage);
        core::Profiler profiler;
        core::WorkerPool workerPool;
        if (streamingConfig.workerThreads > 0) {
            workerPool.Start(static_cast<std::size_t>(streamingConfig.workerThreads),
                             streaming.GenerateQueue(),
                             streaming.MeshQueue(),
                             streaming.UploadQueue(),
                             chunkRegistry,
                             mesher,
                             &profiler);
        }
        streaming.SetWorkerThreads(workerPool.ThreadCount());
        if (runSoakTest) {
            soakState.workerThreads = static_cast<int>(workerPool.ThreadCount());
        }
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
        int smokeFrames = 0;
        bool smokeChunkEnsured = false;
        std::size_t interactionRaycastIndex = 0;
        std::size_t interactionEditIndex = 0;
        int interactionFrameIndex = 0;
        std::size_t soakRaycastIndex = 0;
        std::size_t soakEditIndex = 0;
        int soakFrameIndex = 0;
        auto lastClampLogTime = lastTime - std::chrono::seconds(1);
        game::Player player(kPlayerSpawn);
        glm::mat4 projection(1.0f);
        glm::mat4 view(1.0f);
        Frustum frustum = Frustum::FromMatrix(glm::mat4(1.0f));
        glm::vec3 lightDir(0.0f, -1.0f, 0.0f);
        glm::vec3 playerPosition = player.Position();
        voxel::ChunkCoord playerChunk{0, 0, 0};

        const std::array<InteractionRaycastStep, 3> kInteractionRaycasts = {{
            {30, {2.5f, 9.6f, 2.5f}, {2, 7, 2}, voxel::kBlockStone},
            {120, {31.5f, 9.6f, 2.5f}, {31, 7, 2}, voxel::kBlockStone},
            {200, {32.5f, 9.6f, -1.5f}, {32, 7, -2}, voxel::kBlockStone},
        }};

        const std::array<InteractionEditStep, 4> kInteractionEdits = {{
            {40, {voxel::kChunkSize - 1, 7, 0}, voxel::kBlockStone, voxel::kBlockStone},
            {80, {voxel::kChunkSize, 7, 0}, voxel::kBlockStone, voxel::kBlockStone},
            {160, {voxel::kChunkSize - 1, 7, 0}, voxel::kBlockDirt, voxel::kBlockDirt},
            {200, {voxel::kChunkSize, 7, 0}, voxel::kBlockDirt, voxel::kBlockDirt},
        }};

        const std::array<voxel::WorldBlockCoord, 6> kSoakEditCoords = {{
            {voxel::kChunkSize - 1, 7, 0},
            {voxel::kChunkSize, 7, 0},
            {-1, 7, -1},
            {-voxel::kChunkSize, 7, -voxel::kChunkSize},
            {0, 7, voxel::kChunkSize - 1},
            {0, 7, voxel::kChunkSize},
        }};

        const std::array<SoakRaycastStep, 4> kSoakRaycasts = {{
            {200, {2.5f, 9.6f, 2.5f}, {2, 7, 2}, {0.0f, 0.0f, -1.0f}, true, true, voxel::kBlockDirt},
            {800, {0.5f, 20.0f, 0.5f}, {0, 7, 0}, {0.0f, -1.0f, 0.0f}, true, true, voxel::kBlockDirt},
            {1200, {33.5f, 9.6f, 1.5f}, {33, 7, 1}, {0.0f, 0.0f, -1.0f}, true, true, voxel::kBlockDirt},
            {1600, {5.0f, 20.0f, 5.0f}, {0, 0, 0}, {0.0f, 1.0f, 0.0f}, false, false, voxel::kBlockAir},
        }};

        std::vector<SoakEditStep> soakEdits;
        std::vector<SoakSampleBlock> soakSamples;
        std::vector<voxel::ChunkCoord> soakTouchedChunks;
        if (runSoakTest) {
            soakEdits.reserve(static_cast<std::size_t>(soakConfig.frames / kSoakEditInterval + 2));
            soakSamples.reserve(kSoakEditCoords.size() + 2);
            std::vector<voxel::BlockId> expected(kSoakEditCoords.size(), voxel::kBlockDirt);
            for (int frame = kSoakEditStartFrame; frame < soakConfig.frames; frame += kSoakEditInterval) {
                std::size_t index = static_cast<std::size_t>((frame - kSoakEditStartFrame) / kSoakEditInterval);
                index %= kSoakEditCoords.size();
                voxel::BlockId next = expected[index] == voxel::kBlockDirt ? voxel::kBlockStone : voxel::kBlockDirt;
                expected[index] = next;
                soakEdits.push_back({frame, kSoakEditCoords[index], next, next});
            }
            for (const auto& coord : kSoakEditCoords) {
                soakSamples.push_back({coord, voxel::kBlockDirt});
            }
            soakSamples.push_back({{0, 7, 0}, voxel::kBlockDirt});
            soakSamples.push_back({{15, 7, 15}, voxel::kBlockDirt});
        }

    while (!glfwWindowShouldClose(window)) {
        core::ScopedTimer frameTimer(&profiler, core::Metric::Frame);
        auto now = std::chrono::steady_clock::now();
        float deltaTime = kSmokeDeltaTime;
        if (interactionTest) {
            deltaTime = kInteractionDeltaTime;
        }
        if (runSoakTest) {
            deltaTime = kSoakDeltaTime;
        }
        if (!smokeTest && !interactionTest && !runSoakTest) {
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
            if (interactionTest) {
                desiredDir = kInteractionMoveDir;
            } else if (runSoakTest) {
                desiredDir = glm::vec3(0.0f);
            } else if (allowInput) {
                int escState = glfwGetKey(window, GLFW_KEY_ESCAPE);
                if (escState == GLFW_PRESS && !escPressed) {
                    escPressed = true;
                    if (app::gMouseCaptured) {
                        app::SetMouseCapture(window, false);
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

                if (app::gMouseCaptured) {
                    float yawRadians = glm::radians(app::gCamera.getYaw());
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
                    if (app::gMouseCaptured) {
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

            if (runSoakTest) {
                const glm::vec3 cameraPosition = SoakCameraPath(soakFrameIndex, soakState.seed);
                const glm::vec3 nextPosition = SoakCameraPath(soakFrameIndex + 1, soakState.seed);
                const glm::vec3 direction = nextPosition - cameraPosition;
                player.SetPosition(cameraPosition - kEyeOffset);
                player.ResetVelocity();
                app::gCamera.setPosition(cameraPosition);
                if (glm::length(direction) > 0.001f) {
                    const glm::vec2 yawPitch = YawPitchFromDirection(direction);
                    SetCameraAngles(app::gCamera, yawPitch.x, yawPitch.y);
                }
            } else {
                player.Update(chunkRegistry, desiredDir, jumpPressed, deltaTime);
                app::gCamera.setPosition(player.Position() + kEyeOffset);
            }

            glClearColor(0.08f, 0.10f, 0.15f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            int width = 0;
            int height = 0;
            glfwGetFramebufferSize(window, &width, &height);
            float aspect = width > 0 && height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;

            if (interactionTest && interactionRaycastIndex < kInteractionRaycasts.size() &&
                interactionFrameIndex == kInteractionRaycasts[interactionRaycastIndex].frame) {
                const InteractionRaycastStep& step = kInteractionRaycasts[interactionRaycastIndex];
                player.SetPosition(step.cameraPosition - kEyeOffset);
                player.ResetVelocity();
                app::gCamera.setPosition(step.cameraPosition);
                voxel::WorldBlockCoord worldTarget{step.targetBlock.x, step.targetBlock.y, step.targetBlock.z};
                voxel::ChunkCoord chunkCoord = voxel::WorldToChunkCoord(worldTarget, voxel::kChunkSize);
                EnsureChunkReady(chunkRegistry, chunkCoord);
                chunkRegistry.SetBlock(worldTarget, step.blockId);
                const glm::vec2 yawPitch = YawPitchFromDirection(
                    glm::vec3(step.targetBlock) + glm::vec3(0.5f) - step.cameraPosition);
                SetCameraAngles(app::gCamera, yawPitch.x, yawPitch.y);
            }

            if (runSoakTest && soakRaycastIndex < kSoakRaycasts.size() &&
                soakFrameIndex == kSoakRaycasts[soakRaycastIndex].frame) {
                voxel::WorldBlockCoord playerBlock{
                    static_cast<int>(std::floor(player.Position().x)),
                    static_cast<int>(std::floor(player.Position().y)),
                    static_cast<int>(std::floor(player.Position().z))};
                voxel::ChunkCoord playerCoord = voxel::WorldToChunkCoord(playerBlock, voxel::kChunkSize);
                if (!WaitForStreamingIdle(streaming, chunkRegistry, mesher, workerPool,
                                          playerCoord, kSoakSyncMaxIterations)) {
                    soakState.failed = true;
                    soakState.failureMessage = "[SoakTest] Streaming did not reach idle state for raycast.";
                }
                const SoakRaycastStep& step = kSoakRaycasts[soakRaycastIndex];
                player.SetPosition(step.cameraPosition - kEyeOffset);
                player.ResetVelocity();
                app::gCamera.setPosition(step.cameraPosition);
                if (step.useTarget) {
                    voxel::WorldBlockCoord worldTarget{step.targetBlock.x, step.targetBlock.y, step.targetBlock.z};
                    voxel::ChunkCoord chunkCoord = voxel::WorldToChunkCoord(worldTarget, voxel::kChunkSize);
                    EnsureChunkReady(chunkRegistry, chunkCoord);
                    const glm::vec2 yawPitch = YawPitchFromDirection(
                        glm::vec3(step.targetBlock) + glm::vec3(0.5f) - step.cameraPosition);
                    SetCameraAngles(app::gCamera, yawPitch.x, yawPitch.y);
                } else {
                    const glm::vec2 yawPitch = YawPitchFromDirection(step.direction);
                    SetCameraAngles(app::gCamera, yawPitch.x, yawPitch.y);
                }
            }

            projection = glm::perspective(glm::radians(kFov), aspect, 0.1f, 500.0f);
            view = app::gCamera.getViewMatrix();
            frustum = Frustum::FromMatrix(projection * view);
            lightDir = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));

            currentHit = {};
            hasTarget = false;
            debugDraw.Clear();
            if (app::gMouseCaptured || interactionTest || runSoakTest) {
                currentHit = voxel::RaycastBlocks(chunkRegistry, app::gCamera.getPosition(), app::gCamera.getFront(),
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
                    if (!app::gMouseCaptured) {
                        app::SetMouseCapture(window, true);
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
                    if (app::gMouseCaptured && hasTarget && currentHit.normal != glm::ivec3(0)) {
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
            shader.setInt("uTexture", 0);
            glad_glActiveTexture(GL_TEXTURE0);
            glad_glBindTexture(GL_TEXTURE_2D, blockTexture);

            playerPosition = player.Position();
            voxel::WorldBlockCoord playerBlock{
                static_cast<int>(std::floor(playerPosition.x)),
                static_cast<int>(std::floor(playerPosition.y)),
                static_cast<int>(std::floor(playerPosition.z))};
            playerChunk = voxel::WorldToChunkCoord(playerBlock, voxel::kChunkSize);
            streaming.Tick(playerChunk, chunkRegistry, mesher);
            workerPool.NotifyWork();

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
                    smokeEditSucceeded = voxel::TrySetBlock(chunkRegistry, streaming, target, voxel::kBlockDirt);
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

            if (interactionTest && !interactionState.failed) {
                if (interactionRaycastIndex < kInteractionRaycasts.size() &&
                    interactionFrameIndex == kInteractionRaycasts[interactionRaycastIndex].frame) {
                    const InteractionRaycastStep& step = kInteractionRaycasts[interactionRaycastIndex];
                    ++interactionState.raycasts;
                    voxel::WorldBlockCoord worldTarget{step.targetBlock.x, step.targetBlock.y, step.targetBlock.z};
                    voxel::ChunkCoord expectedChunk = voxel::WorldToChunkCoord(worldTarget, voxel::kChunkSize);
                    if (!IsChunkReady(chunkRegistry, expectedChunk)) {
                        interactionState.failed = true;
                        interactionState.failureMessage = "[InteractionTest] Raycast chunk not ready.";
                    } else if (!currentHit.hit || currentHit.block != step.targetBlock) {
                        interactionState.failed = true;
                        std::ostringstream message;
                        message << "[InteractionTest] Raycast mismatch at frame " << step.frame
                                << ": hit=" << currentHit.hit
                                << " block=(" << currentHit.block.x << ", " << currentHit.block.y << ", "
                                << currentHit.block.z << ") expected=(" << step.targetBlock.x << ", "
                                << step.targetBlock.y << ", " << step.targetBlock.z << ")";
                        interactionState.failureMessage = message.str();
                    } else {
                        voxel::BlockId id = chunkRegistry.GetBlockOrAir(worldTarget);
                        if (id != step.blockId) {
                            interactionState.failed = true;
                            std::ostringstream message;
                            message << "[InteractionTest] Raycast block id mismatch at frame "
                                    << step.frame << ": got=" << static_cast<int>(id)
                                    << " expected=" << static_cast<int>(step.blockId);
                            interactionState.failureMessage = message.str();
                        }
                    }
                    ++interactionRaycastIndex;
                }

                if (interactionEditIndex < kInteractionEdits.size() &&
                    interactionFrameIndex == kInteractionEdits[interactionEditIndex].frame) {
                    const InteractionEditStep& step = kInteractionEdits[interactionEditIndex];
                    voxel::ChunkCoord chunkCoord = voxel::WorldToChunkCoord(step.coord, voxel::kChunkSize);
                    EnsureChunkReady(chunkRegistry, chunkCoord);
                    bool edited = voxel::TrySetBlock(chunkRegistry, streaming, step.coord, step.blockId);
                    ++interactionState.edits;
                    if (!edited) {
                        interactionState.failed = true;
                        interactionState.failureMessage = "[InteractionTest] SetBlock failed.";
                    } else {
                        voxel::BlockId updated = chunkRegistry.GetBlockOrAir(step.coord);
                        if (updated != step.expectedId) {
                            interactionState.failed = true;
                            std::ostringstream message;
                            message << "[InteractionTest] GetBlockOrAir mismatch at frame " << step.frame
                                    << ": got=" << static_cast<int>(updated)
                                    << " expected=" << static_cast<int>(step.expectedId);
                            interactionState.failureMessage = message.str();
                        }
                    }
                    ++interactionEditIndex;
                }
            }

            if (runSoakTest && !soakState.failed) {
                if (soakRaycastIndex < kSoakRaycasts.size() &&
                    soakFrameIndex == kSoakRaycasts[soakRaycastIndex].frame) {
                    const SoakRaycastStep& step = kSoakRaycasts[soakRaycastIndex];
                    ++soakState.raycasts;
                    if (step.expectHit != currentHit.hit) {
                        soakState.failed = true;
                        std::ostringstream message;
                        message << "[SoakTest] Raycast mismatch at frame " << step.frame
                                << ": hit=" << currentHit.hit << " expected=" << step.expectHit;
                        soakState.failureMessage = message.str();
                    } else if (step.expectHit) {
                        voxel::WorldBlockCoord worldTarget{step.targetBlock.x, step.targetBlock.y, step.targetBlock.z};
                        voxel::ChunkCoord expectedChunk = voxel::WorldToChunkCoord(worldTarget, voxel::kChunkSize);
                        if (!IsChunkReady(chunkRegistry, expectedChunk)) {
                            soakState.failed = true;
                            soakState.failureMessage = "[SoakTest] Raycast chunk not ready.";
                        } else if (currentHit.block != step.targetBlock) {
                            soakState.failed = true;
                            std::ostringstream message;
                            message << "[SoakTest] Raycast block mismatch at frame " << step.frame
                                    << ": got=(" << currentHit.block.x << ", " << currentHit.block.y << ", "
                                    << currentHit.block.z << ") expected=(" << step.targetBlock.x << ", "
                                    << step.targetBlock.y << ", " << step.targetBlock.z << ")";
                            soakState.failureMessage = message.str();
                        } else {
                            voxel::BlockId id = chunkRegistry.GetBlockOrAir(worldTarget);
                            if (id != step.expectedId) {
                                soakState.failed = true;
                                std::ostringstream message;
                                message << "[SoakTest] Raycast block id mismatch at frame " << step.frame
                                        << ": got=" << static_cast<int>(id)
                                        << " expected=" << static_cast<int>(step.expectedId);
                                soakState.failureMessage = message.str();
                            }
                        }
                    }
                    ++soakRaycastIndex;
                }

                if (soakEditIndex < soakEdits.size() &&
                    soakFrameIndex == soakEdits[soakEditIndex].frame) {
                    const SoakEditStep& step = soakEdits[soakEditIndex];
                    voxel::ChunkCoord chunkCoord = voxel::WorldToChunkCoord(step.coord, voxel::kChunkSize);
                    EnsureChunkReady(chunkRegistry, chunkCoord);
                    bool edited = voxel::TrySetBlock(chunkRegistry, streaming, step.coord, step.blockId);
                    ++soakState.edits;
                    if (!edited) {
                        soakState.failed = true;
                        soakState.failureMessage = "[SoakTest] SetBlock failed.";
                    } else {
                        voxel::BlockId updated = chunkRegistry.GetBlockOrAir(step.coord);
                        if (updated != step.expectedId) {
                            soakState.failed = true;
                            std::ostringstream message;
                            message << "[SoakTest] GetBlockOrAir mismatch at frame " << step.frame
                                    << ": got=" << static_cast<int>(updated)
                                    << " expected=" << static_cast<int>(step.expectedId);
                            soakState.failureMessage = message.str();
                        } else {
                            bool found = false;
                            for (const auto& existing : soakTouchedChunks) {
                                if (existing == chunkCoord) {
                                    found = true;
                                    break;
                                }
                            }
                            if (!found) {
                                soakTouchedChunks.push_back(chunkCoord);
                            }
                            for (auto& sample : soakSamples) {
                                if (SameWorldCoord(sample.coord, step.coord)) {
                                    sample.expectedId = step.expectedId;
                                }
                            }
                        }
                    }
                    ++soakEditIndex;
                }

                if (soakFrameIndex > 0 && soakFrameIndex % soakConfig.saveInterval == 0) {
                    voxel::WorldBlockCoord playerBlock{
                        static_cast<int>(std::floor(player.Position().x)),
                        static_cast<int>(std::floor(player.Position().y)),
                        static_cast<int>(std::floor(player.Position().z))};
                    voxel::ChunkCoord playerCoord = voxel::WorldToChunkCoord(playerBlock, voxel::kChunkSize);
                    if (!WaitForStreamingIdle(streaming, chunkRegistry, mesher, workerPool,
                                              playerCoord, kSoakSyncMaxIterations)) {
                        soakState.failed = true;
                        soakState.failureMessage = "[SoakTest] Streaming did not reach idle state for save.";
                    } else {
                        std::size_t saved = chunkRegistry.SaveAllDirty(chunkStorage);
                        if (saved == 0) {
                            soakState.failed = true;
                            soakState.failureMessage = "[SoakTest] Expected dirty chunks for save.";
                        }
                        soakState.saves += static_cast<int>(saved);
                        const std::uintmax_t expectedSize =
                            persistence::kChunkHeaderSize +
                            static_cast<std::uintmax_t>(voxel::kChunkVolume * sizeof(voxel::BlockId));
                        for (const auto& coord : soakTouchedChunks) {
                            std::filesystem::path chunkPath = ChunkFilePath(soakState.storageRoot, coord);
                            std::error_code error;
                            if (!std::filesystem::exists(chunkPath, error)) {
                                soakState.failed = true;
                                soakState.failureMessage = "[SoakTest] Chunk file missing after save.";
                                break;
                            }
                            std::uintmax_t fileSize = std::filesystem::file_size(chunkPath, error);
                            if (error || fileSize != expectedSize) {
                                soakState.failed = true;
                                soakState.failureMessage = "[SoakTest] Chunk file size mismatch.";
                                break;
                            }
                            voxel::Chunk loadedChunk;
                            if (!chunkStorage.LoadChunk(coord, loadedChunk)) {
                                soakState.failed = true;
                                soakState.failureMessage = "[SoakTest] Chunk load failed.";
                                break;
                            }
                            ++soakState.loads;
                            for (const auto& sample : soakSamples) {
                                if (voxel::WorldToChunkCoord(sample.coord, voxel::kChunkSize) != coord) {
                                    continue;
                                }
                                voxel::LocalCoord local = voxel::WorldToLocalCoord(sample.coord, voxel::kChunkSize);
                                voxel::BlockId loadedId = loadedChunk.Get(local.x, local.y, local.z);
                                if (loadedId != sample.expectedId) {
                                    soakState.failed = true;
                                    soakState.failureMessage = "[SoakTest] Chunk load sample mismatch.";
                                    break;
                                }
                            }
                            if (soakState.failed) {
                                break;
                            }
                        }
                    }
                }
            }
        }

        if (interactionTest && interactionState.failed) {
            interactionState.frames = interactionFrameIndex + 1;
            interactionState.stats = streaming.Stats();
        }
        if (runSoakTest && soakState.failed) {
            soakState.frames = soakFrameIndex + 1;
            soakState.stats = streaming.Stats();
        }
        if ((smokeTest && smokeFailed) || (interactionTest && interactionState.failed) ||
            (runSoakTest && soakState.failed)) {
            break;
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
        if (interactionTest) {
            interactionState.frames = interactionFrameIndex + 1;
            interactionState.stats = streaming.Stats();
            if (interactionFrameIndex + 1 >= kInteractionTestFrames) {
                std::cout << "[InteractionTest] Completed " << interactionState.frames << " frames.\n";
                break;
            }
            ++interactionFrameIndex;
        }
        if (runSoakTest) {
            soakState.frames = soakFrameIndex + 1;
            soakState.stats = streaming.Stats();
            if (soakFrameIndex + 1 >= soakConfig.frames) {
                std::cout << "[SoakTest] Completed " << soakState.frames << " frames.\n";
                break;
            }
            ++soakFrameIndex;
        }
        std::chrono::duration<float> fpsElapsed = now - fpsTimer;
        if (!interactionTest && !runSoakTest && fpsElapsed.count() >= 0.25f) {
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

        if (interactionTest) {
            std::vector<std::uint8_t> checksumBuffer;
            checksumBuffer.reserve(256);
            AppendUint32(checksumBuffer, kInteractionTestSeed);
            AppendInt32(checksumBuffer, interactionState.frames);
            AppendInt32(checksumBuffer, interactionState.edits);
            AppendInt32(checksumBuffer, interactionState.raycasts);
            AppendFloat(checksumBuffer, player.Position().x);
            AppendFloat(checksumBuffer, player.Position().y);
            AppendFloat(checksumBuffer, player.Position().z);
            AppendFloat(checksumBuffer, app::gCamera.getPosition().x);
            AppendFloat(checksumBuffer, app::gCamera.getPosition().y);
            AppendFloat(checksumBuffer, app::gCamera.getPosition().z);
            AppendFloat(checksumBuffer, app::gCamera.getYaw());
            AppendFloat(checksumBuffer, app::gCamera.getPitch());

            const std::array<voxel::WorldBlockCoord, 5> samples = {{
                {0, 7, 0},
                {voxel::kChunkSize - 1, 7, 0},
                {voxel::kChunkSize, 7, 0},
                {-1, 7, -1},
                {-33, 7, -33},
            }};
            for (const auto& sample : samples) {
                AppendInt32(checksumBuffer, sample.x);
                AppendInt32(checksumBuffer, sample.y);
                AppendInt32(checksumBuffer, sample.z);
                AppendUint16(checksumBuffer, chunkRegistry.GetBlockOrAir(sample));
            }
            AppendUint32(checksumBuffer, static_cast<std::uint32_t>(interactionState.stats.generatedChunksReady));
            AppendUint32(checksumBuffer, static_cast<std::uint32_t>(interactionState.stats.meshedCpuReady));
            AppendUint32(checksumBuffer, static_cast<std::uint32_t>(interactionState.stats.gpuReadyChunks));

            interactionState.checksum = core::Sha256Hex(checksumBuffer);

            const std::size_t valueWidth = 42;
            std::cout << "+----------------------+------------------------------------------+\n";
            std::cout << "| Metric               | Value                                    |\n";
            std::cout << "+----------------------+------------------------------------------+\n";
            std::cout << "| seed                 | " << std::left << std::setw(valueWidth) << kInteractionTestSeed
                      << "|\n";
            std::cout << "| frames               | " << std::left << std::setw(valueWidth) << interactionState.frames
                      << "|\n";
            std::cout << "| edits                | " << std::left << std::setw(valueWidth) << interactionState.edits << "|\n";
            std::cout << "| raycasts             | " << std::left << std::setw(valueWidth) << interactionState.raycasts
                      << "|\n";
            std::cout << "| chunks_generated     | " << std::left << std::setw(valueWidth)
                      << interactionState.stats.generatedChunksReady << "|\n";
            std::cout << "| chunks_meshed        | " << std::left << std::setw(valueWidth)
                      << interactionState.stats.meshedCpuReady << "|\n";
            std::cout << "| chunks_uploaded      | " << std::left << std::setw(valueWidth)
                      << interactionState.stats.gpuReadyChunks << "|\n";
            std::cout << "| final_checksum_sha256| " << std::left << std::setw(valueWidth)
                      << interactionState.checksum << "|\n";
            std::cout << "+----------------------+------------------------------------------+\n";
        }

        if (runSoakTest) {
            std::vector<std::uint8_t> checksumBuffer;
            checksumBuffer.reserve(512);
            AppendUint32(checksumBuffer, soakState.seed);
            AppendInt32(checksumBuffer, soakState.frames);
            AppendInt32(checksumBuffer, soakState.edits);
            AppendInt32(checksumBuffer, soakState.raycasts);
            AppendInt32(checksumBuffer, soakState.saves);
            AppendInt32(checksumBuffer, soakState.loads);
            AppendFloat(checksumBuffer, player.Position().x);
            AppendFloat(checksumBuffer, player.Position().y);
            AppendFloat(checksumBuffer, player.Position().z);
            AppendFloat(checksumBuffer, app::gCamera.getPosition().x);
            AppendFloat(checksumBuffer, app::gCamera.getPosition().y);
            AppendFloat(checksumBuffer, app::gCamera.getPosition().z);
            AppendFloat(checksumBuffer, app::gCamera.getYaw());
            AppendFloat(checksumBuffer, app::gCamera.getPitch());
            AppendInt32(checksumBuffer, soakState.workerThreads);

            for (const auto& sample : soakSamples) {
                AppendInt32(checksumBuffer, sample.coord.x);
                AppendInt32(checksumBuffer, sample.coord.y);
                AppendInt32(checksumBuffer, sample.coord.z);
                AppendUint16(checksumBuffer, chunkRegistry.GetBlockOrAir(sample.coord));
            }

            AppendUint32(checksumBuffer, static_cast<std::uint32_t>(soakState.stats.generatedChunksReady));
            AppendUint32(checksumBuffer, static_cast<std::uint32_t>(soakState.stats.meshedCpuReady));
            AppendUint32(checksumBuffer, static_cast<std::uint32_t>(soakState.stats.gpuReadyChunks));

            soakState.checksum = core::Sha256Hex(checksumBuffer);

            const std::size_t valueWidth = 42;
            std::cout << "+--------------------------+------------------------------------------+\n";
            std::cout << "| Metric                   | Value                                    |\n";
            std::cout << "+--------------------------+------------------------------------------+\n";
            std::cout << "| mode                     | " << std::left << std::setw(valueWidth) << soakConfig.mode
                      << "|\n";
            std::cout << "| seed                     | " << std::left << std::setw(valueWidth) << soakState.seed
                      << "|\n";
            std::cout << "| frames                   | " << std::left << std::setw(valueWidth) << soakState.frames
                      << "|\n";
            std::cout << "| worker_threads           | " << std::left << std::setw(valueWidth)
                      << soakState.workerThreads << "|\n";
            std::cout << "| raycasts                 | " << std::left << std::setw(valueWidth) << soakState.raycasts
                      << "|\n";
            std::cout << "| edits                    | " << std::left << std::setw(valueWidth) << soakState.edits
                      << "|\n";
            std::cout << "| saves                    | " << std::left << std::setw(valueWidth) << soakState.saves
                      << "|\n";
            std::cout << "| loads                    | " << std::left << std::setw(valueWidth) << soakState.loads
                      << "|\n";
            std::cout << "| chunks_generated         | " << std::left << std::setw(valueWidth)
                      << soakState.stats.generatedChunksReady << "|\n";
            std::cout << "| chunks_meshed            | " << std::left << std::setw(valueWidth)
                      << soakState.stats.meshedCpuReady << "|\n";
            std::cout << "| chunks_uploaded          | " << std::left << std::setw(valueWidth)
                      << soakState.stats.gpuReadyChunks << "|\n";
            std::cout << "| final_checksum_sha256    | " << std::left << std::setw(valueWidth)
                      << soakState.checksum << "|\n";
            std::cout << "+--------------------------+------------------------------------------+\n";
        }

        workerPool.Stop();
        chunkRegistry.SaveAllDirty(chunkStorage);
        chunkRegistry.DestroyAll();
    }

    if (blockTexture != 0) {
        glad_glDeleteTextures(1, &blockTexture);
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    if (smokeTest && smokeFailed) {
        return EXIT_FAILURE;
    }
    if (interactionTest && interactionState.failed) {
        std::cerr << interactionState.failureMessage << '\n';
        return EXIT_FAILURE;
    }
    if (runSoakTest && soakState.failed) {
        std::cerr << soakState.failureMessage << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
