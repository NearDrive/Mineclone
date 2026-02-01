cmake_minimum_required(VERSION 3.20)

project(Mineclone LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MINECLONE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors." OFF)
option(MINECLONE_SANITIZE "Enable Address/Undefined sanitizers in Debug for GCC/Clang." OFF)

include(cmake/Sanitizers.cmake)
include(FetchContent)

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.3.9
)

FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 0.9.9.8
)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glfw glm)

add_library(glad STATIC
  third_party/glad/src/glad.c
)

target_include_directories(glad PUBLIC
  third_party/glad/include
)

target_compile_definitions(glad PRIVATE GLAD_GL_IMPLEMENTATION)

add_executable(Mineclone
  src/main.cpp
  src/Shader.cpp
  src/Shader.h
  src/Camera.cpp
  src/Camera.h
  src/app/AppInput.cpp
  src/app/AppInput.h
  src/app/AppMode.cpp
  src/app/AppMode.h
  src/app/GameState.h
  src/app/MenuModel.h
  src/game/Player.cpp
  src/game/Player.h
  src/core/Assert.h
  src/core/Cli.cpp
  src/core/Cli.h
  src/core/ThreadSafeQueue.h
  src/core/Profiler.cpp
  src/core/Profiler.h
  src/core/Sha256.cpp
  src/core/Sha256.h
  src/core/Verify.cpp
  src/core/Verify.h
  src/core/WorldTest.cpp
  src/core/WorldTest.h
  src/core/WorkerPool.cpp
  src/core/WorkerPool.h
  src/math/Frustum.cpp
  src/math/Frustum.h
  src/math/Plane.cpp
  src/math/Plane.h
  src/persistence/ChunkFormat.h
  src/persistence/ChunkStorage.cpp
  src/persistence/ChunkStorage.h
  src/physics/VoxelCollision.cpp
  src/physics/VoxelCollision.h
  src/renderer/DebugDraw.cpp
  src/renderer/DebugDraw.h
  src/renderer/RenderTest.cpp
  src/renderer/RenderTest.h
  src/voxel/BlockId.h
  src/voxel/BlockFaces.h
  src/voxel/BlockEdit.cpp
  src/voxel/BlockEdit.h
  src/voxel/Chunk.cpp
  src/voxel/Chunk.h
  src/voxel/ChunkBounds.h
  src/voxel/ChunkCoord.h
  src/voxel/ChunkJobs.h
  src/voxel/ChunkManager.cpp
  src/voxel/ChunkManager.h
  src/voxel/ChunkMesh.cpp
  src/voxel/ChunkMesh.h
  src/voxel/ChunkMesher.cpp
  src/voxel/ChunkMesher.h
  src/voxel/LightData.h
  src/voxel/ChunkRegistry.cpp
  src/voxel/ChunkRegistry.h
  src/voxel/ChunkStreaming.cpp
  src/voxel/ChunkStreaming.h
  src/voxel/Raycast.cpp
  src/voxel/Raycast.h
  src/voxel/VoxelCoords.h
  src/voxel/WorldGen.cpp
  src/voxel/WorldGen.h
)

target_include_directories(Mineclone PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
)

target_compile_definitions(Mineclone PRIVATE GLFW_INCLUDE_NONE)

target_link_libraries(Mineclone PRIVATE glfw glad glm)

if (MSVC)
  target_compile_options(Mineclone PRIVATE /W4 /permissive-)
  if (MINECLONE_WARNINGS_AS_ERRORS)
    target_compile_options(Mineclone PRIVATE /WX)
  endif()
else()
  target_compile_options(Mineclone PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
  if (MINECLONE_WARNINGS_AS_ERRORS)
    target_compile_options(Mineclone PRIVATE -Werror)
  endif()
endif()

mineclone_enable_sanitizers(Mineclone)

if (WIN32)
  target_link_libraries(Mineclone PRIVATE opengl32)
elseif (APPLE)
  target_link_libraries(Mineclone PRIVATE "-framework OpenGL")
else()
  target_link_libraries(Mineclone PRIVATE GL)
endif()

add_custom_command(TARGET Mineclone POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders
    $<TARGET_FILE_DIR:Mineclone>/shaders
)

add_custom_command(TARGET Mineclone POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/textures
    $<TARGET_FILE_DIR:Mineclone>/textures
)
