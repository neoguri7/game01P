#pragma once

#include <SDL3/SDL.h>
#include <entt/entt.hpp>
#include <memory>
#include <string>

#include "core/SDLDeleter.h"
#include "core/Time.h"
#include "core/InputState.h"
#include "core/SystemManager.h"
#include "debug/Logger.h"

namespace game {

/**
 * Central Engine class.
 * Owns SDL window/renderer, the main registry, time, and system manager.
 * Lifecycle: Initialize() -> Run() loop -> Shutdown()
 * 
 * Designed so that game state (level, entities) is entirely owned by the registry.
 */
class Engine {
public:
    Engine();
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    bool initialize(const std::string& title = "Game01P", int w = 1280, int h = 720);
    void run();
    void shutdown();

    // Public getters for extreme modifiability
    [[nodiscard]] entt::registry& getRegistry() noexcept { return registry; }
    [[nodiscard]] const entt::registry& getRegistry() const noexcept { return registry; }
    [[nodiscard]] SDL_Renderer*        getRenderer() const noexcept { return renderer.get(); }
    [[nodiscard]] SystemManager&       getSystemManager() noexcept { return systemMgr; }

private:
    void processInput();
    void update(float dt);
    void render();

    // Context helpers
    void setupContextServices();
    void showDebugWindows();

    FWindowPtr   window{nullptr};
    FRendererPtr renderer{nullptr};

    entt::registry registry;
    Time           frameTime;
    SystemManager  systemMgr;
    bool           running{false};
};

} // namespace game
