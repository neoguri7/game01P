#pragma once

#include <SDL3/SDL.h>
#include <entt/entt.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/SDLDeleter.h"
#include "core/EngineAbility.h"
#include "core/Time.h"
#include "core/SystemManager.h"
#include "states/FStateCatalog.h"
#include "states/GameStateMachine.h"

namespace game {

/// Id of the state a boot starts in: the catalog's debug battle id when GAME01P_START=battle was seen,
/// otherwise its normal start id. Pure so tests can pin "no id assembly in Engine" without a window.
[[nodiscard]] std::string_view StartupStateId(const FStateCatalog& catalog, bool battleStart) noexcept;

/// Compose the enabled-system policy for the current stack: disable everything, switch every state below the
/// top that keeps itself visible under modals to render-only, then enable the top's RequiredSystems.
/// Returns the required names that are not registered (the caller logs them).
[[nodiscard]] std::vector<std::string_view> ApplyStackSystemPolicy(const GameStateMachine& machine, SystemManager& systemManager);

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
    bool run(std::uint32_t maxFrames = 0, std::string screenshotPath = {});
    void shutdown();
    void setOverlayRenderer(std::function<void(entt::registry&, const Time&, const SystemManager&)> renderer);

    /// Inject the app-flow state catalog before initialize(). The engine creates no state itself: the start
    /// state is created once from this catalog in applyStartupStateOverride().
    void setStateCatalog(FStateCatalog catalog);

    /// Debug/automation: GAME01P_START=battle switches straight into the battle state. Called by the
    /// composition root after gameplay content is loaded, so the battle sees a ready content registry.
    void applyStartupStateOverride();

    // Public getters for extreme modifiability
    [[nodiscard]] entt::registry& getRegistry() noexcept { return registry; }
    [[nodiscard]] const entt::registry& getRegistry() const noexcept { return registry; }
    [[nodiscard]] SDL_Renderer*        getRenderer() const noexcept { return renderer.get(); }
    [[nodiscard]] SystemManager&       getSystemManager() noexcept { return systemMgr; }
    [[nodiscard]] const GameStateMachine& getStateMachine() const noexcept { return stateMachine; }

private:
    void processInput();
    void update(float dt);
    void render();
    void applyStateTransition(const FStateTransition& transition);
    void syncStateSystemPolicy();
    void updateAppFlowRuntimeState();

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
    GameStateMachine stateMachine;
    std::optional<FStateCatalog> stateCatalog;
    bool           startupStateReady{false};
    bool           flowError{false};   // the app-flow stack emptied while running: no state could render/update
    std::function<void(entt::registry&, const Time&, const SystemManager&)> overlayRenderer;
    bool           running{false};
    std::uint32_t maxFrames_{0};
    std::string screenshotPath_;
    bool captureSucceeded_{true};
};

} // namespace game
