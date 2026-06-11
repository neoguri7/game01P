#pragma once

#include <SDL3/SDL.h>
#include <entt/entt.hpp>
#include <functional>
#include <memory>
#include <string>

#include "core/SDLDeleter.h"
#include "core/EngineAbility.h"
#include "core/Time.h"
#include "core/SystemManager.h"

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
    void setOverlayRenderer(std::function<void(entt::registry&, const Time&, const SystemManager&)> renderer);

    // Public getters for extreme modifiability
    [[nodiscard]] entt::registry& getRegistry() noexcept { return registry; }
    [[nodiscard]] const entt::registry& getRegistry() const noexcept { return registry; }
    [[nodiscard]] SDL_Renderer*        getRenderer() const noexcept { return renderer.get(); }
    [[nodiscard]] SystemManager&       getSystemManager() noexcept { return systemMgr; }

private:
    void processInput();
    void update(float dt);
    void render();

    bool applyEngineInitializeAbility(const FEngineAbilityRequest& request, const std::string& title, int w, int h);
    void applyEngineShutdownAbility(const FEngineAbilityRequest& request);

    // Engine lifecycle effects preserve the current startup/shutdown order.
    bool applyEngineInitializeSdlEffect();
    bool applyEngineCreateWindowEffect(const std::string& title, int w, int h);
    bool applyEngineCreateRendererEffect();
    bool applyEngineInitializeImGuiEffect();
    bool applyEngineInitializeContextServicesEffect();
    void applyEngineMarkRunningEffect(bool enabled);
    void applyEngineShutdownEntitiesEffect();
    void applyEngineShutdownContextServicesEffect();
    void applyEngineShutdownImGuiEffect();
    void applyEngineDestroyRendererEffect();
    void applyEngineDestroyWindowEffect();
    void applyEngineShutdownSdlEffect();

    // Registry ctx service lifecycle order is owned by Engine.
    void initializeContextServices();
    void shutdownContextServices();

    FWindowPtr   window{nullptr};
    FRendererPtr renderer{nullptr};
    bool         imguiContextInitialized{false};
    bool         imguiSdlBackendInitialized{false};
    bool         imguiRendererBackendInitialized{false};

    entt::registry registry;
    Time           frameTime;
    SystemManager  systemMgr;
    std::function<void(entt::registry&, const Time&, const SystemManager&)> overlayRenderer;
    bool           running{false};
};

} // namespace game
