#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "renderer/RenderTest.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

#include "Shader.h"
#include "core/Sha256.h"
#include "voxel/BlockId.h"
#include "voxel/Chunk.h"
#include "voxel/ChunkMesh.h"
#include "voxel/ChunkMesher.h"
#include "voxel/ChunkRegistry.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace renderer {
namespace {

constexpr float kFov = 60.0f;
constexpr glm::vec3 kClearColor(0.08f, 0.10f, 0.15f);

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

void glfwErrorCallback(int error, const char* description) {
    std::cerr << "[GLFW] Error " << error << ": " << description << '\n';
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

void EnsureEmptyChunk(voxel::ChunkRegistry& registry, const voxel::ChunkCoord& coord) {
    auto entry = registry.GetOrCreateEntry(coord);
    std::unique_lock<std::shared_mutex> lock(entry->dataMutex);
    if (!entry->chunk) {
        entry->chunk = std::make_unique<voxel::Chunk>();
        entry->chunk->Fill(voxel::kBlockAir);
    }
    entry->generationState.store(voxel::GenerationState::Ready, std::memory_order_release);
}

std::shared_ptr<voxel::ChunkEntry> BuildTestChunk(voxel::ChunkRegistry& registry, voxel::ChunkMesher& mesher,
                                                  std::uint32_t seed, const voxel::ChunkCoord& coord,
                                                  bool variant) {
    auto entry = registry.GetOrCreateEntry(coord);
    {
        std::unique_lock<std::shared_mutex> lock(entry->dataMutex);
        entry->chunk = std::make_unique<voxel::Chunk>();
        entry->chunk->Fill(voxel::kBlockAir);

        for (int z = 0; z < voxel::kChunkSize; ++z) {
            for (int x = 0; x < voxel::kChunkSize; ++x) {
                entry->chunk->Set(x, 0, z, voxel::kBlockDirt);
            }
        }

        const int pillarHeight = 4 + static_cast<int>(seed % 7u);
        const int pillarX = 10;
        const int pillarZ = 10;
        for (int y = 1; y <= pillarHeight; ++y) {
            entry->chunk->Set(pillarX, y, pillarZ, voxel::kBlockStone);
        }

        if (variant) {
            const int platformY = 1;
            for (int z = 4; z <= 7; ++z) {
                for (int x = 4; x <= 7; ++x) {
                    entry->chunk->Set(x, platformY, z, voxel::kBlockStone);
                }
            }

            const int towerX = 5;
            const int towerZ = 12;
            for (int y = 1; y <= 6; ++y) {
                entry->chunk->Set(towerX, y, towerZ, voxel::kBlockStone);
            }
        }

        entry->generationState.store(voxel::GenerationState::Ready, std::memory_order_release);
        entry->meshingState.store(voxel::MeshingState::Ready, std::memory_order_release);
    }

    EnsureEmptyChunk(registry, {coord.x + 1, coord.y, coord.z});
    EnsureEmptyChunk(registry, {coord.x - 1, coord.y, coord.z});
    EnsureEmptyChunk(registry, {coord.x, coord.y + 1, coord.z});
    EnsureEmptyChunk(registry, {coord.x, coord.y - 1, coord.z});
    EnsureEmptyChunk(registry, {coord.x, coord.y, coord.z + 1});
    EnsureEmptyChunk(registry, {coord.x, coord.y, coord.z - 1});

    voxel::ChunkMeshCpu cpuMesh;
    mesher.BuildMesh(coord, *entry->chunk, registry, cpuMesh);
    entry->mesh.Clear();
    entry->mesh.Vertices() = std::move(cpuMesh.vertices);
    entry->mesh.Indices() = std::move(cpuMesh.indices);
    entry->mesh.UploadToGpu();
    entry->gpuState.store(voxel::GpuState::Uploaded, std::memory_order_release);
    return entry;
}

bool CreateFramebuffer(int width, int height, GLuint& fbo, GLuint& color, GLuint& depth) {
    glad_glGenFramebuffers(1, &fbo);
    glad_glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glad_glGenTextures(1, &color);
    glad_glBindTexture(GL_TEXTURE_2D, color);
    glad_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glad_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);

    glad_glGenRenderbuffers(1, &depth);
    glad_glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glad_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glad_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);

    GLenum status = glad_glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[RenderTest] Framebuffer incomplete: 0x" << std::hex << status << std::dec << '\n';
        return false;
    }
    return true;
}

bool ReadPngPixels(const std::string& path, int& width, int& height, std::vector<std::uint8_t>& pixels) {
    int channels = 0;
    stbi_uc* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!data) {
        return false;
    }
    std::size_t size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    pixels.assign(data, data + size);
    stbi_image_free(data);
    return true;
}

struct RenderSceneConfig {
    int id = 0;
    voxel::ChunkCoord coord{0, 0, 0};
    bool variant = false;
    glm::vec3 eye{0.0f};
    glm::vec3 target{0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    std::string filename;
};

struct RenderSceneResult {
    int id = 0;
    std::string filename;
    std::string checksum;
};

} // namespace

int RunRenderTest(const RenderTestOptions& options) {
    if (options.width <= 0 || options.height <= 0 || options.frames <= 0) {
        std::cerr << "[RenderTest] Invalid render test configuration.\n";
        return EXIT_FAILURE;
    }

    std::cout << "[RenderTest] Initializing GLFW...\n";
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        std::cerr << "[RenderTest] Failed to initialize GLFW.\n";
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
#ifndef NDEBUG
    if (options.enableGlDebug) {
        glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
    }
#endif

    GLFWwindow* window = glfwCreateWindow(options.width, options.height, "Mineclone Render Test", nullptr, nullptr);
    if (!window) {
        std::cerr << "[RenderTest] Failed to create GLFW window.\n";
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);

    std::cout << "[RenderTest] Loading GLAD...\n";
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::cerr << "[RenderTest] Failed to initialize GLAD.\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }
    std::cout << "[RenderTest] GLAD loaded. ReadBuffer="
              << (glad_glReadBuffer ? "yes" : "no")
              << " Framebuffers=" << (glad_glGenFramebuffers ? "yes" : "no")
              << " Textures=" << (glad_glGenTextures ? "yes" : "no") << '\n';

#ifndef NDEBUG
    if (options.enableGlDebug) {
        GLint flags = 0;
        glad_glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
        if (flags & GL_CONTEXT_FLAG_DEBUG_BIT) {
            glad_glEnable(GL_DEBUG_OUTPUT);
            glad_glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glad_glDebugMessageCallback(debugCallback, nullptr);
            glad_glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
        }
    }
#endif

    glad_glDisable(GL_DITHER);
    glad_glDisable(GL_FRAMEBUFFER_SRGB);
    glad_glEnable(GL_DEPTH_TEST);
    glad_glEnable(GL_CULL_FACE);
    glad_glCullFace(GL_BACK);
    glad_glFrontFace(GL_CCW);

    Shader shader;
    std::string shaderError;
    if (!shader.loadFromFiles("shaders/voxel.vert", "shaders/voxel.frag", shaderError)) {
        std::cerr << "[RenderTest] Shader error: " << shaderError << '\n';
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }
    std::cout << "[RenderTest] Shaders loaded.\n";

    GLuint blockTexture = LoadTexture2D("textures/dirt.png");
    if (blockTexture == 0) {
        blockTexture = CreateProceduralDirtTexture(32, 32);
        if (blockTexture == 0) {
            std::cerr << "[RenderTest] Failed to load or generate textures/dirt.png\n";
            glfwDestroyWindow(window);
            glfwTerminate();
            return EXIT_FAILURE;
        }
        std::cout << "[RenderTest] Using procedurally generated dirt texture.\n";
    }

    GLuint fbo = 0;
    GLuint color = 0;
    GLuint depth = 0;
    std::cout << "[RenderTest] Creating framebuffer...\n";
    if (!CreateFramebuffer(options.width, options.height, fbo, color, depth)) {
        if (color != 0) {
            glad_glDeleteTextures(1, &color);
        }
        if (depth != 0) {
            glad_glDeleteRenderbuffers(1, &depth);
        }
        if (fbo != 0) {
            glad_glDeleteFramebuffers(1, &fbo);
        }
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }
    std::cout << "[RenderTest] Framebuffer ready.\n";

    const float aspect = static_cast<float>(options.width) / static_cast<float>(options.height);
    const glm::mat4 projection = glm::perspective(glm::radians(kFov), aspect, 0.1f, 200.0f);
    const glm::vec3 lightDir = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));

    const std::filesystem::path outputBase = std::filesystem::path(options.outputPath).parent_path();
    std::vector<RenderSceneConfig> scenes;
    scenes.push_back(RenderSceneConfig{
        0,
        voxel::ChunkCoord{0, 0, 0},
        false,
        glm::vec3(16.0f, 20.0f, 48.0f),
        glm::vec3(16.0f, 4.0f, 16.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        "render_test_scene0.png"});
    scenes.push_back(RenderSceneConfig{
        1,
        voxel::ChunkCoord{0, 0, 0},
        false,
        glm::vec3(48.0f, 18.0f, 20.0f),
        glm::vec3(16.0f, 6.0f, 16.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        "render_test_scene1.png"});
    scenes.push_back(RenderSceneConfig{
        2,
        voxel::ChunkCoord{0, 0, 0},
        true,
        glm::vec3(12.0f, 30.0f, 32.0f),
        glm::vec3(16.0f, 6.0f, 16.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        "render_test_scene2.png"});

    std::vector<RenderSceneResult> results;
    results.reserve(scenes.size());
    bool compareOk = true;
    for (const auto& scene : scenes) {
        voxel::ChunkRegistry chunkRegistry;
        voxel::ChunkMesher mesher;
        std::cout << "[RenderTest] Building test chunk...\n";
        auto entry = BuildTestChunk(chunkRegistry, mesher, options.seed, scene.coord, scene.variant);
        if (!entry) {
            std::cerr << "[RenderTest] Failed to build test chunk.\n";
            compareOk = false;
            break;
        }
        std::cout << "[RenderTest] Chunk mesh ready.\n";

        glad_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glad_glViewport(0, 0, options.width, options.height);
        glad_glDisable(GL_DITHER);
        glad_glDisable(GL_FRAMEBUFFER_SRGB);
        glad_glEnable(GL_DEPTH_TEST);
        glad_glEnable(GL_CULL_FACE);
        glad_glCullFace(GL_BACK);
        glad_glFrontFace(GL_CCW);

        const glm::mat4 view = glm::lookAt(scene.eye, scene.target, scene.up);
        for (int frame = 0; frame < options.frames; ++frame) {
            glad_glClearColor(kClearColor.r, kClearColor.g, kClearColor.b, 1.0f);
            glad_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            shader.use();
            shader.setMat4("uProjection", projection);
            shader.setMat4("uView", view);
            shader.setVec3("uLightDir", lightDir);
            shader.setInt("uTexture", 0);
            glad_glActiveTexture(GL_TEXTURE0);
            glad_glBindTexture(GL_TEXTURE_2D, blockTexture);
            entry->mesh.Draw();
        }
        GLenum frameError = glad_glGetError();
        if (frameError != GL_NO_ERROR) {
            std::cerr << "[RenderTest] GL error after draw (scene " << scene.id << "): 0x"
                      << std::hex << frameError << std::dec << '\n';
        }

        glad_glFinish();

        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(options.width) *
                                         static_cast<std::size_t>(options.height) * 4u);
        glad_glPixelStorei(GL_PACK_ALIGNMENT, 1);
        if (glad_glReadBuffer) {
            glad_glReadBuffer(GL_COLOR_ATTACHMENT0);
        }
        glad_glReadPixels(0, 0, options.width, options.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

        const std::filesystem::path outputPath = outputBase / scene.filename;
        if (!stbi_write_png(outputPath.string().c_str(), options.width, options.height, 4, pixels.data(),
                            options.width * 4)) {
            std::cerr << "[RenderTest] Failed to write PNG: " << outputPath << '\n';
            entry->mesh.DestroyGpu();
            chunkRegistry.DestroyAll();
            compareOk = false;
            break;
        }

        const std::string checksum = core::Sha256Hex(pixels);
        std::cout << "[RenderTest] scene=" << scene.id
                  << " size=" << options.width << "x" << options.height
                  << " frames=" << options.frames
                  << " seed=" << options.seed
                  << " checksum=" << checksum
                  << " wrote=\"" << outputPath.string() << "\"\n";

        if (options.comparePath && scene.id == 0) {
            int compareWidth = 0;
            int compareHeight = 0;
            std::vector<std::uint8_t> comparePixels;
            if (!ReadPngPixels(*options.comparePath, compareWidth, compareHeight, comparePixels)) {
                std::cerr << "[RenderTest] Failed to read compare PNG: " << *options.comparePath << '\n';
                compareOk = false;
            } else if (compareWidth != options.width || compareHeight != options.height) {
                std::cerr << "[RenderTest] Compare PNG size mismatch ("
                          << compareWidth << "x" << compareHeight << ").\n";
                compareOk = false;
            } else if (comparePixels != pixels) {
                std::cerr << "[RenderTest] Compare PNG mismatch.\n";
                compareOk = false;
            }
        }

        results.push_back(RenderSceneResult{scene.id, scene.filename, checksum});

        entry->mesh.DestroyGpu();
        chunkRegistry.DestroyAll();

        if (!compareOk) {
            break;
        }
    }

    shader.Destroy();
    if (blockTexture != 0) {
        glad_glDeleteTextures(1, &blockTexture);
    }
    glad_glDeleteFramebuffers(1, &fbo);
    glad_glDeleteTextures(1, &color);
    glad_glDeleteRenderbuffers(1, &depth);
    glfwDestroyWindow(window);
    glfwTerminate();

    if (compareOk) {
        const int sceneWidth = 5;
        const int fileWidth = 24;
        const int hashWidth = 64;
        const std::string divider = "+" + std::string(sceneWidth + 2, '-') +
                                    "+" + std::string(fileWidth + 2, '-') +
                                    "+" + std::string(hashWidth + 2, '-') + "+";
        std::cout << "[RenderTest] Summary\n";
        std::cout << divider << '\n';
        std::cout << "| " << std::left << std::setw(sceneWidth) << "Scene"
                  << " | " << std::left << std::setw(fileWidth) << "File"
                  << " | " << std::left << std::setw(hashWidth) << "SHA256"
                  << " |\n";
        std::cout << divider << '\n';
        for (const auto& result : results) {
            std::cout << "| " << std::left << std::setw(sceneWidth) << result.id
                      << " | " << std::left << std::setw(fileWidth) << result.filename
                      << " | " << std::left << std::setw(hashWidth) << result.checksum
                      << " |\n";
        }
        std::cout << divider << '\n';
    }

    if (!compareOk) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

} // namespace renderer
