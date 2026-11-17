#pragma once

#include <SDL3/SDL.h>
#include <entt/entt.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/AudioManager.h"
#include "core/SDLDeleter.h"
#include "core/EngineAbility.h"
#include "core/FFramePlan.h"
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

/// WP-00c (dev): what a per-frame driver asks the loop to do.
enum class EFrameDriverResult : std::uint8_t { Continue, Stop, Fail };

/// WP-00c (dev): called once per frame after update() and before render(), with the top state's catalog id.
using FFrameDriver = std::function<EFrameDriverResult(entt::registry&, std::string_view topStateId)>;

/// WP-51: called once per frame after the frame driver and before render(), with the top state's catalog id.
using FFrameObserver = std::function<void(entt::registry&, std::string_view topStateId)>;

/// WP-00c (dev): what an input injector asks of this frame besides the SDL events it pushed.
struct FInjectedFrame {
    std::vector<std::string> captures;                     // PNG paths to save after this frame's ImGui draw
    bool quit{false};                                      // stop the loop cleanly after this frame
    std::vector<std::pair<int, std::string>> expectStates;  // (script line, expected top state id)
};

/// WP-00c (dev): pushes this frame's scripted SDL events before the poll loop (e.g. --input-script).
using FInputInjector = std::function<FInjectedFrame(std::uint32_t frame, SDL_WindowID window)>;

/// WP-00c (dev): fixed simulation step for scripted runs, so frame N is the same moment on every run.
inline constexpr float kScriptFixedDeltaSeconds = 1.f / 60.f;
static_assert(kFastForwardDeltaSeconds == kScriptFixedDeltaSeconds);

/// WP-51 (dev): a windowless demo recording.
struct FRecordingConfig {
    std::string directory;     // must not exist or be empty
    std::uint32_t every{2};    // keep every Nth frame: 2 = 30 fps at the fixed 1/60 s step
};
static_assert(kAudioSampleRate % 60 == 0);
inline constexpr std::uint64_t kRecordAudioFramesPerStep = kAudioSampleRate / 60;  // 800

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

    /// WP-00c (dev): inject this script's due events before each frame's SDL_PollEvent loop; fixedTimestep
    /// feeds the run loop kScriptFixedDeltaSeconds instead of the wall dt; unset = no injection.
    void setInputInjector(FInputInjector injector, bool fixedTimestep);

    /// WP-00c (dev): per-frame driver after update() (e.g. the run autopilot); unset = no call.
    void setFrameDriver(FFrameDriver driver);

    /// WP-51: observer called once per frame after the frame driver and before render().
    void setFrameObserver(FFrameObserver observer);

    /// WP-51 (dev): before initialize(): fixed 1/60 s step, every frame rasterized, audio rendered offline.
    void setRecording(FRecordingConfig config);

    /// WP-42 (dev): fast-forward — a fixed 1/60 s step per frame and no rasterization (clear, ImGui draw data,
    /// present) except on a frame that saves a capture. Fast mode also skips the world render
    /// systems, including the combat HUD, so it is valid only when no decision goes through ImGui (autoplay
    /// plus autopilot).
    void setFastForward(bool enabled);

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
    bool saveFrameCapture(const std::string& path);
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

    // WP-00c (dev) hooks: all inert unless an injector/driver is installed.
    FInputInjector inputInjector_;
    bool fixedTimestep_{false};
    std::vector<std::string> pendingCaptures_;
    bool quitAfterFrame_{false};
    FFrameDriver frameDriver_;
    bool driverFailed_{false};
    bool fastForward_{false};
    bool rasterizeFrame_{true};
    FFrameObserver frameObserver_;
    std::optional<FRecordingConfig> recording_;
    std::uint32_t recordedSteps_{0};
};

} // namespace game
