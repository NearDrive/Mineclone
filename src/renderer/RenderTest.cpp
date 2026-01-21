#include "renderer/RenderTest.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
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

std::shared_ptr<voxel::ChunkEntry> BuildTestChunk(voxel::ChunkRegistry& registry, voxel::ChunkMesher& mesher,
                                                  std::uint32_t seed) {
    const voxel::ChunkCoord coord{0, 0, 0};
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

        entry->generationState.store(voxel::GenerationState::Ready, std::memory_order_release);
        entry->meshingState.store(voxel::MeshingState::Ready, std::memory_order_release);
    }

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
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glGenTextures(1, &color);
    glBindTexture(GL_TEXTURE_2D, color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);

    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
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

} // namespace

int RunRenderTest(const RenderTestOptions& options) {
    if (options.width <= 0 || options.height <= 0 || options.frames <= 0) {
        std::cerr << "[RenderTest] Invalid render test configuration.\n";
        return EXIT_FAILURE;
    }

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

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::cerr << "[RenderTest] Failed to initialize GLAD.\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

#ifndef NDEBUG
    if (options.enableGlDebug) {
        GLint flags = 0;
        glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
        if (flags & GL_CONTEXT_FLAG_DEBUG_BIT) {
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(debugCallback, nullptr);
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
        }
    }
#endif

    glDisable(GL_DITHER);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    Shader shader;
    std::string shaderError;
    if (!shader.loadFromFiles("shaders/voxel.vert", "shaders/voxel.frag", shaderError)) {
        std::cerr << "[RenderTest] Shader error: " << shaderError << '\n';
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    GLuint fbo = 0;
    GLuint color = 0;
    GLuint depth = 0;
    if (!CreateFramebuffer(options.width, options.height, fbo, color, depth)) {
        if (color != 0) {
            glDeleteTextures(1, &color);
        }
        if (depth != 0) {
            glDeleteRenderbuffers(1, &depth);
        }
        if (fbo != 0) {
            glDeleteFramebuffers(1, &fbo);
        }
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    voxel::ChunkRegistry chunkRegistry;
    voxel::ChunkMesher mesher;
    auto entry = BuildTestChunk(chunkRegistry, mesher, options.seed);
    if (!entry) {
        std::cerr << "[RenderTest] Failed to build test chunk.\n";
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &color);
        glDeleteRenderbuffers(1, &depth);
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    const float aspect = static_cast<float>(options.width) / static_cast<float>(options.height);
    const glm::mat4 projection = glm::perspective(glm::radians(kFov), aspect, 0.1f, 200.0f);
    const glm::vec3 eye(16.0f, 20.0f, 48.0f);
    const glm::vec3 target(16.0f, 4.0f, 16.0f);
    const glm::mat4 view = glm::lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 lightDir = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, options.width, options.height);

    for (int frame = 0; frame < options.frames; ++frame) {
        glClearColor(kClearColor.r, kClearColor.g, kClearColor.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        shader.use();
        shader.setMat4("uProjection", projection);
        shader.setMat4("uView", view);
        shader.setVec3("uLightDir", lightDir);
        entry->mesh.Draw();
    }

    glFinish();

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(options.width) *
                                     static_cast<std::size_t>(options.height) * 4u);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, options.width, options.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    std::filesystem::path outputPath = options.outputPath;
    if (!stbi_write_png(outputPath.string().c_str(), options.width, options.height, 4, pixels.data(),
                        options.width * 4)) {
        std::cerr << "[RenderTest] Failed to write PNG: " << outputPath << '\n';
        entry->mesh.DestroyGpu();
        chunkRegistry.DestroyAll();
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &color);
        glDeleteRenderbuffers(1, &depth);
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    const std::string checksum = core::Sha256Hex(pixels);
    std::cout << "[RenderTest] size=" << options.width << "x" << options.height
              << " frames=" << options.frames
              << " seed=" << options.seed
              << " checksum=" << checksum << '\n';
    std::cout << "[RenderTest] wrote: " << outputPath << '\n';

    bool compareOk = true;
    if (options.comparePath) {
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

    entry->mesh.DestroyGpu();
    chunkRegistry.DestroyAll();
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &color);
    glDeleteRenderbuffers(1, &depth);
    glfwDestroyWindow(window);
    glfwTerminate();

    if (!compareOk) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

} // namespace renderer
