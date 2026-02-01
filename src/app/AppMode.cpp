#include "app/AppMode.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "stb_image.h"

#include "app/AppInput.h"
#include "app/GameState.h"
#include "app/MenuModel.h"
#include "core/Profiler.h"
#include "core/WorkerPool.h"
#include "game/Player.h"
#include "math/Frustum.h"
#include "persistence/ChunkStorage.h"
#include "renderer/DebugDraw.h"
#include "Shader.h"
#include "voxel/BlockEdit.h"
#include "voxel/ChunkMesher.h"
#include "voxel/ChunkRegistry.h"
#include "voxel/ChunkStreaming.h"
#include "voxel/ChunkBounds.h"
#include "voxel/Raycast.h"
#include "voxel/VoxelCoords.h"
#include "voxel/WorldGen.h"

namespace app {

namespace {
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
constexpr float kSmokeDeltaTime = 1.0f / 60.0f;
constexpr int kWorkerThreadsDefault = 2;
constexpr int kSmokeMenuWorldFrames = 60;
constexpr std::string_view kWorldPrefix = "world_";
const glm::vec3 kPlayerSpawn = []() {
    const int surfaceHeight = voxel::GetSurfaceHeight(0, 0);
    const int spawnY = std::clamp(surfaceHeight + 2, voxel::kWorldMinY + 2, voxel::kWorldMaxY - 2);
    return glm::vec3(0.0f, static_cast<float>(spawnY), 0.0f);
}();
const glm::vec3 kEyeOffset(0.0f, 1.6f, 0.0f);

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

struct TexturePixels {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
    bool IsValid() const { return width > 0 && height > 0 && !pixels.empty(); }
};

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

std::vector<std::uint8_t> BuildProceduralStonePixels(int width, int height) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    std::uint32_t state = 0x7f4a7c15u;
    auto nextRandom = [&state]() {
        state = state * 1103515245u + 12345u;
        return state;
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::uint32_t noiseSeed =
                nextRandom() + static_cast<std::uint32_t>(x * 2654435761u) + static_cast<std::uint32_t>(y * 1013904223u);
            const int noise = static_cast<int>((noiseSeed >> 24) & 0xFF) % 25 - 12;
            int shade = 130 + noise;
            shade = std::clamp(shade, 80, 200);
            const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                       static_cast<std::size_t>(x)) *
                                      4u;
            pixels[index + 0] = static_cast<std::uint8_t>(shade);
            pixels[index + 1] = static_cast<std::uint8_t>(shade);
            pixels[index + 2] = static_cast<std::uint8_t>(shade);
            pixels[index + 3] = 255;
        }
    }
    return pixels;
}

TexturePixels LoadTexturePixels(const std::string& path) {
    TexturePixels result;
    int channels = 0;
    stbi_uc* data = stbi_load(path.c_str(), &result.width, &result.height, &channels, 4);
    if (!data) {
        return result;
    }
    result.pixels.assign(data, data + (result.width * result.height * 4));
    stbi_image_free(data);
    return result;
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

GLuint CreateBlockAtlasTexture() {
    constexpr int kFallbackSize = 32;
    TexturePixels dirt = LoadTexturePixels("textures/dirt.png");
    if (!dirt.IsValid()) {
        dirt.width = kFallbackSize;
        dirt.height = kFallbackSize;
        dirt.pixels = BuildProceduralDirtPixels(kFallbackSize, kFallbackSize);
        std::cout << "[Texture] Using procedurally generated dirt texture.\n";
    }

    TexturePixels stone = LoadTexturePixels("textures/stone.png");
    if (!stone.IsValid()) {
        stone.width = kFallbackSize;
        stone.height = kFallbackSize;
        stone.pixels = BuildProceduralStonePixels(kFallbackSize, kFallbackSize);
        std::cout << "[Texture] Using procedurally generated stone texture.\n";
    }

    if (dirt.width != stone.width || dirt.height != stone.height) {
        std::cout << "[Texture] Mismatched block texture sizes, falling back to procedural 32x32 atlas.\n";
        dirt.width = kFallbackSize;
        dirt.height = kFallbackSize;
        dirt.pixels = BuildProceduralDirtPixels(kFallbackSize, kFallbackSize);
        stone.width = kFallbackSize;
        stone.height = kFallbackSize;
        stone.pixels = BuildProceduralStonePixels(kFallbackSize, kFallbackSize);
    }

    const int atlasWidth = dirt.width * 2;
    const int atlasHeight = dirt.height;
    std::vector<std::uint8_t> atlas(static_cast<std::size_t>(atlasWidth) * atlasHeight * 4u);

    auto blit = [&](const TexturePixels& src, int dstX) {
        for (int y = 0; y < src.height; ++y) {
            for (int x = 0; x < src.width; ++x) {
                const std::size_t srcIndex =
                    (static_cast<std::size_t>(y) * src.width + static_cast<std::size_t>(x)) * 4u;
                const std::size_t dstIndex =
                    (static_cast<std::size_t>(y) * atlasWidth + static_cast<std::size_t>(x + dstX)) * 4u;
                atlas[dstIndex + 0] = src.pixels[srcIndex + 0];
                atlas[dstIndex + 1] = src.pixels[srcIndex + 1];
                atlas[dstIndex + 2] = src.pixels[srcIndex + 2];
                atlas[dstIndex + 3] = src.pixels[srcIndex + 3];
            }
        }
    };

    blit(dirt, 0);
    blit(stone, dirt.width);
    return CreateTextureFromPixels(atlasWidth, atlasHeight, atlas);
}

const char* BlockLabel(voxel::BlockId id) {
    switch (id) {
    case voxel::kBlockStone:
        return "Stone";
    case voxel::kBlockDirt:
        return "Dirt";
    case voxel::kBlockAir:
    default:
        return "Air";
    }
}

GLuint CreateProceduralDirtTexture(int width, int height) {
    auto pixels = BuildProceduralDirtPixels(width, height);
    return CreateTextureFromPixels(width, height, pixels);
}

std::string FormatWorldId(std::time_t timestamp) {
    std::tm localTime{};
#if defined(_WIN32)
    localtime_s(&localTime, &timestamp);
#else
    localtime_r(&timestamp, &localTime);
#endif
    std::ostringstream out;
    out << kWorldPrefix << std::put_time(&localTime, "%Y%m%d_%H%M%S");
    return out.str();
}

bool WorldHasChunkFiles(const std::filesystem::path& root) {
    std::error_code error;
    if (!std::filesystem::exists(root, error)) {
        return false;
    }
    for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
        if (entry.is_regular_file() && entry.path().extension() == ".bin") {
            return true;
        }
    }
    return false;
}

} // namespace

struct AppMode::WorldRuntime {
    explicit WorldRuntime(const std::filesystem::path& storageRoot, int workerThreads)
        : chunkStorage(storageRoot),
          streaming(BuildStreamingConfig(workerThreads)),
          player(kPlayerSpawn) {
        chunkRegistry.SetStorage(&chunkStorage);
        streaming.SetStorage(&chunkStorage);
        streaming.SetProfiler(&profiler);
        StartWorkers(workerThreads);
    }

    static voxel::ChunkStreamingConfig BuildStreamingConfig(int workerThreads) {
        voxel::ChunkStreamingConfig config;
        config.renderRadius = kRenderRadiusDefault;
        config.loadRadius = kLoadRadiusDefault;
        config.maxChunkCreatesPerFrame = 3;
        config.maxChunkMeshesPerFrame = 2;
        config.maxGpuUploadsPerFrame = 3;
        config.workerThreads = workerThreads;
        return config;
    }

    void StartWorkers(int workerThreads) {
        if (workerThreads <= 0) {
            workerThreads = 1;
        }
        workerThreadsTarget = workerThreads;
        workerPool.Start(static_cast<std::size_t>(workerThreads),
                         streaming.GenerateQueue(),
                         streaming.MeshQueue(),
                         streaming.UploadQueue(),
                         chunkRegistry,
                         mesher,
                         &profiler);
        streaming.SetWorkerThreads(workerPool.ThreadCount());
    }

    void StopWorkers() {
        workerPool.Stop();
    }

    persistence::ChunkStorage chunkStorage;
    voxel::ChunkRegistry chunkRegistry;
    voxel::ChunkMesher mesher;
    voxel::ChunkStreaming streaming;
    core::Profiler profiler;
    core::WorkerPool workerPool;
    game::Player player;
    DebugDraw debugDraw;
    DebugDraw crosshairDraw;

    glm::mat4 projection{1.0f};
    glm::mat4 view{1.0f};
    Frustum frustum = Frustum::FromMatrix(glm::mat4(1.0f));
    glm::vec3 lightDir{0.0f, -1.0f, 0.0f};

    voxel::RaycastHit currentHit{};
    bool hasTarget = false;

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
    bool spacePressed = false;
#ifndef NDEBUG
    bool resetPressed = false;
#endif
    bool frustumCullingEnabled = true;
    bool distanceCullingEnabled = true;
    bool statsTitleEnabled = true;
    bool statsPrintEnabled = false;

    std::chrono::steady_clock::time_point fpsTimer{};
    std::chrono::steady_clock::time_point lastStatsPrint{};
    std::chrono::steady_clock::time_point lastClampLogTime{};

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
    int frames = 0;

    int workerThreadsTarget = kWorkerThreadsDefault;
};

std::string StateLabel(GameState state) {
    switch (state) {
        case GameState::MainMenu:
            return "MainMenu";
        case GameState::Playing:
            return "Playing";
        case GameState::PauseMenu:
            return "PauseMenu";
        case GameState::Exiting:
            return "Exiting";
    }
    return "Unknown";
}

AppMode::~AppMode() = default;

AppMode::AppMode(GLFWwindow* window, const AppModeOptions& options)
    : window_(window), options_(options) {
    lastTime_ = std::chrono::steady_clock::now();
    lastClampLogTime_ = lastTime_ - std::chrono::seconds(1);
    lastTitleUpdate_ = lastTime_ - std::chrono::seconds(1);
    InitializeShaders();
    if (initialized_) {
        InitializeTextures();
    }
    if (initialized_) {
        SetState(GameState::MainMenu);
    }
}

bool AppMode::IsInitialized() const {
    return initialized_;
}

const std::string& AppMode::InitError() const {
    return initError_;
}

void AppMode::InitializeShaders() {
    Shader shader;
    std::string shaderError;
    if (!shader.loadFromFiles("shaders/voxel.vert", "shaders/voxel.frag", shaderError)) {
        initError_ = "[Shader] " + shaderError;
        initialized_ = false;
        return;
    }

    Shader debugShader;
    if (!debugShader.loadFromFiles("shaders/debug_line.vert", "shaders/debug_line.frag", shaderError)) {
        initError_ = "[Shader] " + shaderError;
        initialized_ = false;
        return;
    }

    shader_ = std::move(shader);
    debugShader_ = std::move(debugShader);
    initialized_ = true;
}

void AppMode::InitializeTextures() {
    blockTexture_ = CreateBlockAtlasTexture();
    if (blockTexture_ == 0) {
        initError_ = "[Texture] Failed to create block atlas texture.";
        initialized_ = false;
    }
}

void AppMode::Tick() {
    if (!initialized_) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    float deltaTime = kSmokeDeltaTime;
    if (!options_.smokeTest) {
        std::chrono::duration<float> delta = now - lastTime_;
        deltaTime = delta.count();
        if (deltaTime > kMaxDeltaTime) {
            std::chrono::duration<float> clampElapsed = now - lastClampLogTime_;
            if (clampElapsed.count() >= 1.0f) {
                std::cout << "[Timing] Delta time clamped from " << deltaTime << " to " << kMaxDeltaTime << '\n';
                lastClampLogTime_ = now;
            }
            deltaTime = kMaxDeltaTime;
        }
    }
    lastTime_ = now;

    if (options_.smokeTest && !smokeCompleted_ && !smokeFailed_) {
        AdvanceSmokeTest();
    }

    HandleMenuInput();
    if (state_ == GameState::Playing) {
        HandlePlayingInput();
    }

    if (state_ == GameState::Playing) {
        if (world_) {
            TickWorld(deltaTime, now, options_.allowInput, true, true);
        }
    } else if (state_ == GameState::PauseMenu) {
        if (world_) {
            TickWorld(deltaTime, now, false, false, false);
        } else {
            glClearColor(0.08f, 0.10f, 0.15f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }
        UpdateMenuTitle(false);
    } else if (state_ == GameState::MainMenu) {
        glClearColor(0.08f, 0.10f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        UpdateMenuTitle(false);
    }
}

void AppMode::Shutdown() {
    if (world_) {
        StopWorldAndReturnToMenu();
    }
    if (blockTexture_ != 0) {
        glad_glDeleteTextures(1, &blockTexture_);
        blockTexture_ = 0;
    }
}

bool AppMode::ShouldExit() const {
    return shouldExit_ || state_ == GameState::Exiting;
}

bool AppMode::SmokeCompleted() const {
    return smokeCompleted_;
}

bool AppMode::SmokeFailed() const {
    return smokeFailed_;
}

void AppMode::SetState(GameState state) {
    if (state_ == state) {
        return;
    }
    state_ = state;
    std::cout << "[State] " << StateLabel(state_) << '\n';
    if (state_ == GameState::Playing) {
        SetMouseCapture(window_, options_.allowInput);
        loadMissing_ = false;
    } else {
        SetMouseCapture(window_, false);
    }
    if (state_ == GameState::MainMenu && !menuHintPrinted_) {
        std::cout << "[Menu] Press 1 for New, 2 for Load, 3 to Exit.\n";
        menuHintPrinted_ = true;
    }
    UpdateMenuTitle(true);
}

void AppMode::HandleMenuInput() {
    if (state_ != GameState::MainMenu && state_ != GameState::PauseMenu) {
        return;
    }

    const int key1State = glfwGetKey(window_, GLFW_KEY_1);
    if (key1State == GLFW_PRESS && !key1Pressed_) {
        key1Pressed_ = true;
        if (state_ == GameState::MainMenu) {
            StartNewWorld(GenerateNewWorldId());
        } else if (state_ == GameState::PauseMenu) {
            SetState(GameState::Playing);
        }
    } else if (key1State == GLFW_RELEASE) {
        key1Pressed_ = false;
    }

    const int key2State = glfwGetKey(window_, GLFW_KEY_2);
    if (key2State == GLFW_PRESS && !key2Pressed_) {
        key2Pressed_ = true;
        if (state_ == GameState::MainMenu) {
            StartLoadedWorld();
        } else if (state_ == GameState::PauseMenu) {
            if (!SaveWorld()) {
                std::cout << "[Storage] Save failed or no world loaded.\n";
            }
        }
    } else if (key2State == GLFW_RELEASE) {
        key2Pressed_ = false;
    }

    const int key3State = glfwGetKey(window_, GLFW_KEY_3);
    if (key3State == GLFW_PRESS && !key3Pressed_) {
        key3Pressed_ = true;
        if (state_ == GameState::MainMenu) {
            SetState(GameState::Exiting);
            shouldExit_ = true;
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        } else if (state_ == GameState::PauseMenu) {
            StopWorldAndReturnToMenu();
        }
    } else if (key3State == GLFW_RELEASE) {
        key3Pressed_ = false;
    }
}

void AppMode::HandlePlayingInput() {
    int escState = glfwGetKey(window_, GLFW_KEY_ESCAPE);
    if (escState == GLFW_PRESS && !escPressed_) {
        escPressed_ = true;
        SetState(GameState::PauseMenu);
    } else if (escState == GLFW_RELEASE) {
        escPressed_ = false;
    }
}

void AppMode::UpdateMenuTitle(bool force) {
    const auto now = std::chrono::steady_clock::now();
    if (!force) {
        std::chrono::duration<double> elapsed = now - lastTitleUpdate_;
        if (elapsed.count() < 0.25) {
            return;
        }
    }

    if (state_ == GameState::MainMenu) {
        const std::string_view title = loadMissing_ ? MenuModel::kMainMenuMissingTitle
                                                    : MenuModel::kMainMenuTitle;
        glfwSetWindowTitle(window_, std::string(title).c_str());
    } else if (state_ == GameState::PauseMenu) {
        glfwSetWindowTitle(window_, std::string(MenuModel::kPauseMenuTitle).c_str());
    }

    lastTitleUpdate_ = now;
}

void AppMode::StartNewWorld(const std::string& worldId) {
    if (world_) {
        StopWorldAndReturnToMenu();
    }

    worldId_ = worldId;
    const std::filesystem::path storageRoot =
        persistence::ChunkStorage::DefaultSavePath().parent_path() / worldId_;

    int workerThreads = options_.smokeTest ? 0 : kWorkerThreadsDefault;
    world_ = std::make_unique<WorldRuntime>(storageRoot, workerThreads);
    world_->fpsTimer = std::chrono::steady_clock::now();
    world_->lastStatsPrint = world_->fpsTimer - std::chrono::seconds(5);
    world_->lastClampLogTime = world_->fpsTimer - std::chrono::seconds(1);

    gCamera.setPosition(kPlayerSpawn + kEyeOffset);
    loadMissing_ = false;
    SetState(GameState::Playing);
}

void AppMode::StartLoadedWorld() {
    const std::optional<std::string> latestWorld = FindLatestWorldId();
    if (!latestWorld) {
        loadMissing_ = true;
        std::cout << "[Menu] No saved world found in saves/.\n";
        UpdateMenuTitle(true);
        return;
    }
    StartNewWorld(*latestWorld);
}

void AppMode::StopWorldAndReturnToMenu() {
    if (!world_) {
        SetState(GameState::MainMenu);
        return;
    }

    world_->StopWorkers();
    const std::size_t saved = world_->chunkRegistry.SaveAllDirty(world_->chunkStorage);
    if (saved > 0) {
        std::cout << "[Storage] Saved " << saved << " dirty chunk(s).\n";
    }
    world_->chunkRegistry.DestroyAll();
    world_.reset();
    SetState(GameState::MainMenu);
}

bool AppMode::SaveWorld() {
    if (!world_) {
        return false;
    }

    const bool wasEnabled = world_->streaming.Enabled();
    world_->streaming.SetEnabled(false);
    world_->StopWorkers();
    const std::size_t saved = world_->chunkRegistry.SaveAllDirty(world_->chunkStorage);
    std::cout << "[Storage] Saved " << saved << " dirty chunk(s).\n";
    world_->StartWorkers(world_->workerThreadsTarget);
    world_->streaming.SetEnabled(wasEnabled);
    return true;
}

bool AppMode::WorldExists(const std::string& worldId) const {
    const std::filesystem::path root = persistence::ChunkStorage::DefaultSavePath().parent_path() / worldId;
    return WorldHasChunkFiles(root);
}

std::string AppMode::GenerateNewWorldId() const {
    const auto now = std::chrono::system_clock::now();
    std::time_t timestamp = std::chrono::system_clock::to_time_t(now);
    std::string candidate = FormatWorldId(timestamp);
    if (!WorldExists(candidate)) {
        return candidate;
    }
    int suffix = 1;
    while (true) {
        std::ostringstream out;
        out << candidate << "_" << suffix;
        std::string attempt = out.str();
        if (!WorldExists(attempt)) {
            return attempt;
        }
        ++suffix;
    }
}

std::optional<std::string> AppMode::FindLatestWorldId() const {
    const std::filesystem::path savesRoot = persistence::ChunkStorage::DefaultSavePath().parent_path();
    std::error_code error;
    if (!std::filesystem::exists(savesRoot, error)) {
        return std::nullopt;
    }

    std::optional<std::string> latestId;
    std::filesystem::file_time_type latestTime{};

    for (const auto& entry : std::filesystem::directory_iterator(savesRoot, error)) {
        if (!entry.is_directory()) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (name.rfind(kWorldPrefix, 0) != 0) {
            continue;
        }
        if (!WorldHasChunkFiles(entry.path())) {
            continue;
        }
        std::error_code timeError;
        const auto writeTime = std::filesystem::last_write_time(entry.path(), timeError);
        if (timeError) {
            continue;
        }
        if (!latestId || writeTime > latestTime) {
            latestId = name;
            latestTime = writeTime;
        }
    }

    return latestId;
}

void AppMode::TickWorld(float deltaTime, const std::chrono::steady_clock::time_point& now,
                        bool allowInput, bool updateStreaming, bool updateTitle) {
    if (!world_) {
        return;
    }

    glm::vec3 desiredDir(0.0f);
    bool jumpPressed = false;

    if (allowInput && updateStreaming) {
        int decreaseState = glfwGetKey(window_, GLFW_KEY_LEFT_BRACKET);
        if (decreaseState == GLFW_PRESS && !world_->decreaseRadiusPressed) {
            world_->decreaseRadiusPressed = true;
            int newRadius = std::clamp(world_->streaming.RenderRadius() - 1, kRenderRadiusMin, kRenderRadiusMax);
            world_->streaming.SetRenderRadius(newRadius);
            std::cout << "[Culling] Render radius set to " << world_->streaming.RenderRadius() << " chunks.\n";
        } else if (decreaseState == GLFW_RELEASE) {
            world_->decreaseRadiusPressed = false;
        }

        int increaseState = glfwGetKey(window_, GLFW_KEY_RIGHT_BRACKET);
        if (increaseState == GLFW_PRESS && !world_->increaseRadiusPressed) {
            world_->increaseRadiusPressed = true;
            int newRadius = std::clamp(world_->streaming.RenderRadius() + 1, kRenderRadiusMin, kRenderRadiusMax);
            world_->streaming.SetRenderRadius(newRadius);
            std::cout << "[Culling] Render radius set to " << world_->streaming.RenderRadius() << " chunks.\n";
        } else if (increaseState == GLFW_RELEASE) {
            world_->increaseRadiusPressed = false;
        }

        int decreaseLoadState = glfwGetKey(window_, GLFW_KEY_COMMA);
        if (decreaseLoadState == GLFW_PRESS && !world_->decreaseLoadRadiusPressed) {
            world_->decreaseLoadRadiusPressed = true;
            int newRadius = std::clamp(world_->streaming.LoadRadius() - 1, kLoadRadiusMin, kLoadRadiusMax);
            world_->streaming.SetLoadRadius(newRadius);
            std::cout << "[Streaming] Load radius set to " << world_->streaming.LoadRadius() << " chunks.\n";
        } else if (decreaseLoadState == GLFW_RELEASE) {
            world_->decreaseLoadRadiusPressed = false;
        }

        int increaseLoadState = glfwGetKey(window_, GLFW_KEY_PERIOD);
        if (increaseLoadState == GLFW_PRESS && !world_->increaseLoadRadiusPressed) {
            world_->increaseLoadRadiusPressed = true;
            int newRadius = std::clamp(world_->streaming.LoadRadius() + 1, kLoadRadiusMin, kLoadRadiusMax);
            world_->streaming.SetLoadRadius(newRadius);
            std::cout << "[Streaming] Load radius set to " << world_->streaming.LoadRadius() << " chunks.\n";
        } else if (increaseLoadState == GLFW_RELEASE) {
            world_->increaseLoadRadiusPressed = false;
        }

        int statsToggleState = glfwGetKey(window_, GLFW_KEY_F3);
        if (statsToggleState == GLFW_PRESS && !world_->statsTogglePressed) {
            world_->statsTogglePressed = true;
            world_->statsTitleEnabled = !world_->statsTitleEnabled;
            std::cout << "[Stats] Title " << (world_->statsTitleEnabled ? "enabled" : "disabled") << ".\n";
        } else if (statsToggleState == GLFW_RELEASE) {
            world_->statsTogglePressed = false;
        }

        int statsPrintState = glfwGetKey(window_, GLFW_KEY_F4);
        if (statsPrintState == GLFW_PRESS && !world_->statsPrintTogglePressed) {
            world_->statsPrintTogglePressed = true;
            world_->statsPrintEnabled = !world_->statsPrintEnabled;
            std::cout << "[Stats] Stdout " << (world_->statsPrintEnabled ? "enabled" : "disabled") << ".\n";
        } else if (statsPrintState == GLFW_RELEASE) {
            world_->statsPrintTogglePressed = false;
        }

        int saveState = glfwGetKey(window_, GLFW_KEY_F5);
        if (saveState == GLFW_PRESS && !world_->savePressed) {
            world_->savePressed = true;
            std::size_t saved = world_->chunkRegistry.SaveAllDirty(world_->chunkStorage);
            std::cout << "[Storage] Forced save of " << saved << " dirty chunk(s).\n";
        } else if (saveState == GLFW_RELEASE) {
            world_->savePressed = false;
        }

        int streamingToggleState = glfwGetKey(window_, GLFW_KEY_F6);
        if (streamingToggleState == GLFW_PRESS && !world_->streamingTogglePressed) {
            world_->streamingTogglePressed = true;
            world_->streaming.SetEnabled(!world_->streaming.Enabled());
            std::cout << "[Streaming] " << (world_->streaming.Enabled() ? "Enabled" : "Paused") << ".\n";
        } else if (streamingToggleState == GLFW_RELEASE) {
            world_->streamingTogglePressed = false;
        }

        int frustumToggleState = glfwGetKey(window_, GLFW_KEY_F1);
        if (frustumToggleState == GLFW_PRESS && !world_->frustumTogglePressed) {
            world_->frustumTogglePressed = true;
            world_->frustumCullingEnabled = !world_->frustumCullingEnabled;
            std::cout << "[Culling] Frustum culling " << (world_->frustumCullingEnabled ? "enabled" : "disabled")
                      << ".\n";
        } else if (frustumToggleState == GLFW_RELEASE) {
            world_->frustumTogglePressed = false;
        }

        int distanceToggleState = glfwGetKey(window_, GLFW_KEY_F2);
        if (distanceToggleState == GLFW_PRESS && !world_->distanceTogglePressed) {
            world_->distanceTogglePressed = true;
            world_->distanceCullingEnabled = !world_->distanceCullingEnabled;
            std::cout << "[Culling] Distance culling " << (world_->distanceCullingEnabled ? "enabled" : "disabled")
                      << ".\n";
        } else if (distanceToggleState == GLFW_RELEASE) {
            world_->distanceTogglePressed = false;
        }

        if (gMouseCaptured) {
            float yawRadians = glm::radians(gCamera.getYaw());
            glm::vec3 forward(std::cos(yawRadians), 0.0f, std::sin(yawRadians));
            glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));

            if (glfwGetKey(window_, GLFW_KEY_W) == GLFW_PRESS) {
                desiredDir += forward;
            }
            if (glfwGetKey(window_, GLFW_KEY_S) == GLFW_PRESS) {
                desiredDir -= forward;
            }
            if (glfwGetKey(window_, GLFW_KEY_A) == GLFW_PRESS) {
                desiredDir -= right;
            }
            if (glfwGetKey(window_, GLFW_KEY_D) == GLFW_PRESS) {
                desiredDir += right;
            }
        }

        if (glm::length(desiredDir) > 0.0f) {
            desiredDir = glm::normalize(desiredDir);
        }

        int spaceState = glfwGetKey(window_, GLFW_KEY_SPACE);
        if (spaceState == GLFW_PRESS && !world_->spacePressed) {
            world_->spacePressed = true;
            if (gMouseCaptured) {
                jumpPressed = true;
            }
        } else if (spaceState == GLFW_RELEASE) {
            world_->spacePressed = false;
        }

#ifndef NDEBUG
        int resetState = glfwGetKey(window_, GLFW_KEY_R);
        if (resetState == GLFW_PRESS && !world_->resetPressed) {
            world_->resetPressed = true;
            world_->player.SetPosition(kPlayerSpawn);
            world_->player.ResetVelocity();
            std::cout << "[Debug] Player reset to spawn.\n";
        } else if (resetState == GLFW_RELEASE) {
            world_->resetPressed = false;
        }
#endif
    }

    if (updateStreaming) {
        world_->player.Update(world_->chunkRegistry, desiredDir, jumpPressed, deltaTime);
        gCamera.setPosition(world_->player.Position() + kEyeOffset);
    }

    glClearColor(0.08f, 0.10f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    float aspect = width > 0 && height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;

    constexpr float kCrosshairHalfSizePx = 6.0f;
    if (width > 0 && height > 0) {
        const float halfWidthNdc = kCrosshairHalfSizePx / (0.5f * static_cast<float>(width));
        const float halfHeightNdc = kCrosshairHalfSizePx / (0.5f * static_cast<float>(height));
        world_->crosshairDraw.UpdateCrosshair(halfWidthNdc, halfHeightNdc);
    } else {
        world_->crosshairDraw.Clear();
    }

    world_->projection = glm::perspective(glm::radians(kFov), aspect, 0.1f, 500.0f);
    world_->view = gCamera.getViewMatrix();
    world_->frustum = Frustum::FromMatrix(world_->projection * world_->view);
    world_->lightDir = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));

    world_->currentHit = {};
    world_->hasTarget = false;
    world_->debugDraw.Clear();
    if (gMouseCaptured && updateStreaming) {
        world_->currentHit = voxel::RaycastBlocks(world_->chunkRegistry, gCamera.getPosition(), gCamera.getFront(),
                                                  kReachDistance);
        if (world_->currentHit.hit) {
            world_->hasTarget = true;
            const glm::vec3 min = glm::vec3(world_->currentHit.block) - glm::vec3(kHighlightEpsilon);
            const glm::vec3 max = glm::vec3(world_->currentHit.block) + glm::vec3(1.0f + kHighlightEpsilon);
            if (world_->currentHit.normal == glm::ivec3(0)) {
                world_->debugDraw.UpdateCube(min, max);
            } else {
                world_->debugDraw.UpdateFace(min, max, world_->currentHit.normal);
            }
        }
    }

    if (allowInput && updateStreaming) {
        int leftState = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT);
        if (leftState == GLFW_PRESS && !world_->leftClickPressed) {
            world_->leftClickPressed = true;
            if (!gMouseCaptured) {
                SetMouseCapture(window_, true);
            } else if (world_->hasTarget) {
                voxel::WorldBlockCoord target{world_->currentHit.block.x, world_->currentHit.block.y,
                                              world_->currentHit.block.z};
                voxel::TrySetBlock(world_->chunkRegistry, world_->streaming, target, voxel::kBlockAir);
            }
        } else if (leftState == GLFW_RELEASE) {
            world_->leftClickPressed = false;
        }

        int rightState = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT);
        if (rightState == GLFW_PRESS && !world_->rightClickPressed) {
            world_->rightClickPressed = true;
            if (gMouseCaptured && world_->hasTarget && world_->currentHit.normal != glm::ivec3(0)) {
                glm::ivec3 placeBlock = world_->currentHit.block + world_->currentHit.normal;
                voxel::WorldBlockCoord target{placeBlock.x, placeBlock.y, placeBlock.z};
                if (world_->chunkRegistry.GetBlockOrAir(target) == voxel::kBlockAir) {
                    voxel::TrySetBlock(world_->chunkRegistry, world_->streaming, target, voxel::kBlockDirt);
                }
            }
        } else if (rightState == GLFW_RELEASE) {
            world_->rightClickPressed = false;
        }
    }

    shader_.use();
    shader_.setMat4("uProjection", world_->projection);
    shader_.setMat4("uView", world_->view);
    shader_.setVec3("uLightDir", world_->lightDir);
    shader_.setInt("uTexture", 0);
    glad_glActiveTexture(GL_TEXTURE0);
    glad_glBindTexture(GL_TEXTURE_2D, blockTexture_);

    glm::vec3 playerPosition = world_->player.Position();
    voxel::WorldBlockCoord playerBlock{
        static_cast<int>(std::floor(playerPosition.x)),
        static_cast<int>(std::floor(playerPosition.y)),
        static_cast<int>(std::floor(playerPosition.z))};
    voxel::ChunkCoord playerChunk = voxel::WorldToChunkCoord(playerBlock, voxel::kChunkSize);

    if (updateStreaming) {
        world_->streaming.Tick(playerChunk, world_->chunkRegistry, world_->mesher);
        world_->workerPool.NotifyWork();
    }

    std::size_t distanceCulled = 0;
    std::size_t frustumCulled = 0;
    std::size_t drawn = 0;

    const int renderRadiusChunks = world_->streaming.RenderRadius();

    world_->chunkRegistry.ForEachEntry([&](const voxel::ChunkCoord& coord,
                                           const std::shared_ptr<voxel::ChunkEntry>& entry) {
        if (entry->gpuState.load(std::memory_order_acquire) != voxel::GpuState::Uploaded) {
            return;
        }

        if (world_->distanceCullingEnabled) {
            const int dx = std::abs(coord.x - playerChunk.x);
            const int dz = std::abs(coord.z - playerChunk.z);
            if (std::max(dx, dz) > renderRadiusChunks) {
                ++distanceCulled;
                return;
            }
        }

        if (world_->frustumCullingEnabled) {
            const voxel::ChunkBounds bounds = voxel::GetChunkBounds(coord);
            if (!world_->frustum.IntersectsAabb(bounds.min, bounds.max)) {
                ++frustumCulled;
                return;
            }
        }

        entry->mesh.Draw();
        ++drawn;
    });

    if (world_->debugDraw.HasGeometry()) {
        debugShader_.use();
        debugShader_.setMat4("uProjection", world_->projection);
        debugShader_.setMat4("uView", world_->view);
        debugShader_.setVec3("uColor", glm::vec3(1.0f, 0.95f, 0.2f));
        world_->debugDraw.Draw();
    }

    if (world_->crosshairDraw.HasGeometry()) {
        glDisable(GL_DEPTH_TEST);
        debugShader_.use();
        debugShader_.setMat4("uProjection", glm::mat4(1.0f));
        debugShader_.setMat4("uView", glm::mat4(1.0f));
        debugShader_.setVec3("uColor", glm::vec3(1.0f));
        world_->crosshairDraw.Draw();
        glEnable(GL_DEPTH_TEST);
    }

    const voxel::ChunkStreamingStats& streamStats = world_->streaming.Stats();
    world_->lastLoadedChunks = streamStats.loadedChunks;
    world_->lastGeneratedChunks = streamStats.generatedChunksReady;
    world_->lastMeshedChunks = streamStats.meshedCpuReady;
    world_->lastGpuReadyChunks = streamStats.gpuReadyChunks;
    world_->lastCreateQueue = streamStats.createQueue;
    world_->lastMeshQueue = streamStats.meshQueue;
    world_->lastUploadQueue = streamStats.uploadQueue;
    world_->lastCreates = streamStats.createdThisFrame;
    world_->lastMeshes = streamStats.meshedThisFrame;
    world_->lastUploads = streamStats.uploadedThisFrame;
    world_->lastDrawnChunks = drawn;
    world_->lastFrustumCulled = frustumCulled;
    world_->lastDistanceCulled = distanceCulled;
    world_->lastDrawCalls = drawn;
    world_->lastWorkerThreads = streamStats.workerThreads;

    if (updateTitle) {
        world_->frames++;
        std::chrono::duration<float> fpsElapsed = now - world_->fpsTimer;
        if (fpsElapsed.count() >= 0.25f) {
            float fps = static_cast<float>(world_->frames) / fpsElapsed.count();
            auto round1 = [](float value) { return std::round(value * 10.0f) / 10.0f; };
            std::ostringstream title;
            core::ProfilerSnapshot snapshot = world_->profiler.CollectSnapshot();
            const auto metricIndex = [](core::Metric metric) {
                return static_cast<std::size_t>(metric);
            };
            auto ms = [&](core::Metric metric) {
                return snapshot.emaMs[metricIndex(metric)];
            };

            title << "Mineclone"
                  << " | FPS: " << std::fixed << std::setprecision(1) << fps;

            const char* targetLabel = "None";
            if (world_->currentHit.hit) {
                voxel::WorldBlockCoord hitBlock{world_->currentHit.block.x, world_->currentHit.block.y,
                                                world_->currentHit.block.z};
                targetLabel = BlockLabel(world_->chunkRegistry.GetBlockOrAir(hitBlock));
            }

            if (world_->statsTitleEnabled) {
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
                      << " | Loaded: " << world_->lastLoadedChunks
                      << " | GPU: " << world_->lastGpuReadyChunks
                      << " | Q: " << world_->lastCreateQueue << "/" << world_->lastMeshQueue << "/"
                      << world_->lastUploadQueue
                      << " | Drawn: " << world_->lastDrawnChunks;
            }

            if (!world_->statsTitleEnabled) {
                title << " | Pos: (" << round1(world_->player.Position().x) << ","
                      << round1(world_->player.Position().y) << ","
                      << round1(world_->player.Position().z) << ")";
            }

            title << " | Target: " << targetLabel;
            glfwSetWindowTitle(window_, title.str().c_str());

            if (world_->statsPrintEnabled) {
                std::chrono::duration<double> printElapsed = now - world_->lastStatsPrint;
                if (printElapsed.count() >= 5.0) {
                    std::ostringstream perfLine;
                    perfLine << "[Perf] fps " << std::fixed << std::setprecision(1) << fps
                             << " frame " << ms(core::Metric::Frame) << "ms"
                             << " upd " << ms(core::Metric::Update) << "ms"
                             << " up " << ms(core::Metric::Upload) << "ms"
                             << " rnd " << ms(core::Metric::Render) << "ms"
                             << " gen " << std::setprecision(2)
                             << snapshot.avgMs[metricIndex(core::Metric::Generate)]
                             << "ms/job (" << snapshot.counts[metricIndex(core::Metric::Generate)] << ")"
                             << " mesh " << snapshot.avgMs[metricIndex(core::Metric::Mesh)]
                             << "ms/job (" << snapshot.counts[metricIndex(core::Metric::Mesh)] << ")"
                             << " loaded " << world_->lastLoadedChunks
                             << " gpu " << world_->lastGpuReadyChunks
                             << " q " << world_->lastCreateQueue << "/" << world_->lastMeshQueue << "/"
                             << world_->lastUploadQueue;
                    std::cout << perfLine.str() << '\n';
                    world_->lastStatsPrint = now;
                }
            }

            world_->fpsTimer = now;
            world_->frames = 0;
        }
    }
}

void AppMode::AdvanceSmokeTest() {
    static int smokeFrames = 0;
    static int step = 0;
    static bool saveRequested = false;

    if (state_ == GameState::MainMenu && step == 0) {
        StartNewWorld(GenerateNewWorldId());
        step = 1;
        smokeFrames = 0;
        return;
    }

    if (state_ == GameState::Playing && step == 1) {
        ++smokeFrames;
        if (smokeFrames >= kSmokeMenuWorldFrames) {
            SetState(GameState::PauseMenu);
            step = 2;
        }
        return;
    }

    if (state_ == GameState::PauseMenu && step == 2) {
        if (!saveRequested) {
            saveRequested = true;
            if (!SaveWorld()) {
                smokeFailed_ = true;
                std::cout << "[Smoke] Failed to save world during pause.\n";
                shouldExit_ = true;
                glfwSetWindowShouldClose(window_, GLFW_TRUE);
                return;
            }
        }
        StopWorldAndReturnToMenu();
        step = 3;
        return;
    }

    if (state_ == GameState::MainMenu && step == 3) {
        SetState(GameState::Exiting);
        shouldExit_ = true;
        smokeCompleted_ = true;
        std::cout << "[Smoke] OK: menu flow + save completed\n";
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }
}

} // namespace app
