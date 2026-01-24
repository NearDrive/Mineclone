#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "Shader.h"
#include "app/GameState.h"

struct GLFWwindow;

namespace app {

struct AppModeOptions {
    bool allowInput = true;
    bool smokeTest = false;
};

class AppMode {
public:
    AppMode(GLFWwindow* window, const AppModeOptions& options);
    ~AppMode();

    bool IsInitialized() const;
    const std::string& InitError() const;

    void Tick();
    void Shutdown();

    bool ShouldExit() const;
    bool SmokeCompleted() const;
    bool SmokeFailed() const;

private:
    void InitializeShaders();
    void SetState(GameState state);

    void HandleMenuInput();
    void HandlePlayingInput();

    void UpdateMenuTitle(bool force);

    void StartNewWorld();
    void StartLoadedWorld();
    void StopWorldAndReturnToMenu();
    bool SaveWorld();
    bool WorldExists() const;

    void TickWorld(float deltaTime, const std::chrono::steady_clock::time_point& now,
                   bool allowInput, bool updateStreaming, bool updateTitle);

    void AdvanceSmokeTest();

    GLFWwindow* window_ = nullptr;
    AppModeOptions options_;

    GameState state_ = GameState::MainMenu;
    bool shouldExit_ = false;

    std::string initError_;
    bool initialized_ = false;

    std::string worldId_ = "world_0";
    bool loadMissing_ = false;

    bool key1Pressed_ = false;
    bool key2Pressed_ = false;
    bool key3Pressed_ = false;
    bool escPressed_ = false;

    std::chrono::steady_clock::time_point lastTitleUpdate_{};
    std::chrono::steady_clock::time_point lastTime_{};
    std::chrono::steady_clock::time_point lastClampLogTime_{};

    bool smokeCompleted_ = false;
    bool smokeFailed_ = false;

    struct WorldRuntime;
    std::unique_ptr<WorldRuntime> world_;
    Shader shader_;
    Shader debugShader_;
};

} // namespace app
