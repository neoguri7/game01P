// ImGui SDL3 backend headers (shipped locally in src/imgui/backends/)
#include "core/Engine.h"

#include <imgui.h>
#include "imgui/backends/imgui_impl_sdl3.h"
#include "imgui/backends/imgui_impl_sdlrenderer3.h"
#include <tracy/Tracy.hpp>

#include <string_view>
#include <SDL3_image/SDL_image.h>

#include "core/AssetManager.h"
#include "core/AudioManager.h"
#include "core/EngineAbility.h"
#include "core/EngineAbilityPipeline.h"
#include "core/EngineState.h"
#include "core/InputState.h"
#include "core/Logger.h"
#include "core/ResourceManager.h"
#include "core/events/FEventBus.h"

namespace game {

std::string_view StartupStateId(const FStateCatalog& catalog, bool battleStart) noexcept {
    return battleStart ? catalog.debugBattleId() : catalog.startId();
}

std::vector<std::string_view> ApplyStackSystemPolicy(const GameStateMachine& machine, SystemManager& systemManager) {
    std::vector<std::string_view> unregistered;
    systemManager.disableAll();
    const auto stack = machine.stack();
    // Every state below the top that keeps itself visible is paused: render only, update stays off.
    for (std::size_t index = 0; index + 1 < stack.size(); ++index) {
        const FBaseState* state = stack[index].state.get();
        if (state == nullptr || !state->KeepVisibleUnderModals()) continue;
        for (const auto name : state->RequiredRenderSystems()) {
            if (!systemManager.setRenderOnly(name)) unregistered.push_back(name);
        }
    }
    if (!stack.empty()) {
        const FBaseState* top = stack.back().state.get();
        if (top != nullptr) {
            for (const auto name : top->RequiredSystems()) {
                if (!systemManager.setEnabled(name, true)) unregistered.push_back(name);
            }
        }
    }
    return unregistered;
}

Engine::Engine() = default;

Engine::~Engine() {
    shutdown();
}

bool Engine::initialize(const std::string& title, int w, int h) {
    ZoneScopedN("Engine::initialize");
    const FEngineAbilityRequest initializeRequest{
        .ability = EEngineAbility::Initialize,
        .frameIndex = 0
    };

    return applyEngineInitializeAbility(initializeRequest, title, w, h);
}

bool Engine::applyEngineInitializeAbility(const FEngineAbilityRequest& request, const std::string& title, int w, int h) {
    if (request.ability != EEngineAbility::Initialize) {
        return false;
    }

    if (!applyEngineInitializeSdlEffect()) {
        return false;
    }

    if (!applyEngineCreateWindowEffect(title, w, h)) {
        applyEngineShutdownSdlEffect();
        return false;
    }

    if (!applyEngineCreateRendererEffect()) {
        applyEngineDestroyWindowEffect();
        applyEngineShutdownSdlEffect();
        return false;
    }

    if (!applyEngineInitializeImGuiEffect()) {
        applyEngineDestroyRendererEffect();
        applyEngineDestroyWindowEffect();
        applyEngineShutdownSdlEffect();
        return false;
    }

    // Logger (will expose through ctx)
    FLogger::Initialize(registry);
    if (auto* l = registry.ctx().find<FLogger>()) {
        SetCurrentGameLogger(l->logger);
    }
    LOG_INFO("Engine initialized.");

    if (!applyEngineInitializeContextServicesEffect()) {
        applyEngineShutdownImGuiEffect();
        applyEngineDestroyRendererEffect();
        applyEngineDestroyWindowEffect();
        applyEngineShutdownSdlEffect();
        return false;
    }

    // No state is created here: the composition root injects the state catalog before initialize() and the
    // start state is created once by applyStartupStateOverride(), after content and services are ready.
    updateAppFlowRuntimeState();

    applyEngineMarkRunningEffect(true);
    return true;
}

void Engine::setStateCatalog(FStateCatalog catalog) {
    stateCatalog = std::move(catalog);
}

void Engine::setOverlayRenderer(std::function<void(entt::registry&, const Time&, const SystemManager&)> renderer) {
    overlayRenderer = std::move(renderer);
}

void Engine::setInputInjector(FInputInjector injector, bool fixedTimestep) {
    inputInjector_ = std::move(injector);
    fixedTimestep_ = fixedTimestep;
}

void Engine::setFrameDriver(FFrameDriver driver) {
    frameDriver_ = std::move(driver);
}

void Engine::applyStartupStateOverride() {
    startupStateReady = false;
    flowError = false;
    if (!stateCatalog) {
        LOG_ERROR("Engine startup state failed: no state catalog was injected.");
        return;
    }
    const char* requestedStart = SDL_getenv("GAME01P_START"); // boundary: env key
    const bool battleStart = requestedStart != nullptr && std::string_view(requestedStart) == "battle";

    const std::string target(StartupStateId(*stateCatalog, battleStart));
    const FStateTransition transition{.kind = EStateChangeKind::Root, .targetId = target};
    if (stateMachine.apply(transition, *stateCatalog, registry) != EStateApplyResult::Applied) {
        LOG_ERROR("Engine startup state failed: '{}' could not be created.", target);
        return;
    }
    if (battleStart) {
        LOG_INFO("Debug start: switching to the battle state.");
    }
    startupStateReady = stateMachine.stack().size() == 1;
    updateAppFlowRuntimeState();
}

void Engine::initializeContextServices() {
    // Service init order:
    // 1. Input for frame state, 2. audio, 3. events, 4. asset boundary,
    // 5. renderer pointer, 6. renderer-backed resources.
    FInputState::Initialize(registry);
    FAudioManager::Initialize(registry);
    InitializeEventBus(registry);
    FAssetManager::Initialize(registry, {std::string(SDL_GetBasePath()) + "assets", "assets"});

    auto& windowState = registry.ctx().emplace<FEngineWindowState>();
    SDL_GetWindowSize(window.get(), &windowState.width, &windowState.height);
    const bool windowOpen = window != nullptr;
    const bool windowFocused = windowOpen && (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_INPUT_FOCUS) != 0;

    registry.ctx().emplace<FEngineFrameState>();

    auto& runtimeState = registry.ctx().emplace<FEngineRuntimeState>();
    runtimeState.rendererReady = renderer != nullptr;
    if (runtimeState.rendererReady) runtimeState.tags.add(EEngineTag::RendererReady);
    SetEngineWindowOpenState(registry, windowOpen);
    SetEngineWindowFocusedState(registry, windowFocused);
    SetEngineInputReadyState(registry, registry.ctx().contains<FInputState>());

    registry.ctx().emplace<SDL_Renderer*>(renderer.get());

    auto& rm = registry.ctx().emplace<FResourceManager>();
    rm.init(renderer.get());
    LOG_DEBUG("ResourceManager registered in context");
}

bool Engine::applyEngineInitializeSdlEffect() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init Error: %s", SDL_GetError());
        return false;
    }
    return true;
}

bool Engine::applyEngineCreateWindowEffect(const std::string& title, int w, int h) {
    SDL_Window* rawWin = SDL_CreateWindow(title.c_str(), w, h, SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE);
    if (!rawWin) {
        SDL_Log("Window create failed: %s", SDL_GetError());
        return false;
    }

    window = FWindowPtr(rawWin);
    SDL_SetWindowMinimumSize(rawWin, 1100, 740);
    return true;
}

bool Engine::applyEngineCreateRendererEffect() {
    SDL_Renderer* rawRend = SDL_CreateRenderer(window.get(), nullptr);
    if (!rawRend) {
        SDL_Log("Renderer create failed: %s", SDL_GetError());
        return false;
    }

    renderer = FRendererPtr(rawRend);
    SDL_SetRenderVSync(rawRend, 1);
    return true;
}

bool Engine::applyEngineInitializeImGuiEffect() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    imguiContextInitialized = true;
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    imguiSdlBackendInitialized = ImGui_ImplSDL3_InitForSDLRenderer(window.get(), renderer.get());
    if (!imguiSdlBackendInitialized) {
        applyEngineShutdownImGuiEffect();
        return false;
    }

    imguiRendererBackendInitialized = ImGui_ImplSDLRenderer3_Init(renderer.get());
    if (!imguiRendererBackendInitialized) {
        applyEngineShutdownImGuiEffect();
        return false;
    }

    return true;
}

bool Engine::applyEngineInitializeContextServicesEffect() {
    initializeContextServices();
    return true;
}

void Engine::applyEngineMarkRunningEffect(bool enabled) {
    running = enabled;
    SetEngineRunningState(registry, enabled);
}

void Engine::applyEngineShutdownEntitiesEffect() {
    registry.clear();
}

void Engine::applyEngineShutdownContextServicesEffect() {
    shutdownContextServices();
    FLogger::Shutdown(registry);
}

void Engine::applyEngineShutdownImGuiEffect() {
    if (imguiRendererBackendInitialized) {
        ImGui_ImplSDLRenderer3_Shutdown();
        imguiRendererBackendInitialized = false;
    }

    if (imguiSdlBackendInitialized) {
        ImGui_ImplSDL3_Shutdown();
        imguiSdlBackendInitialized = false;
    }

    if (imguiContextInitialized) {
        ImGui::DestroyContext();
        imguiContextInitialized = false;
    }
}

void Engine::applyEngineDestroyRendererEffect() {
    renderer.reset();
}

void Engine::applyEngineDestroyWindowEffect() {
    window.reset();
}

void Engine::applyEngineShutdownSdlEffect() {
    SDL_Quit();
}

void Engine::shutdownContextServices() {
    // Shutdown order mirrors dependencies: renderer-backed resources are cleared
    // before renderer ctx removal and before SDL_Renderer destruction.
    // The composition root must shut down FAnimationLibrary before this clear because clips retain texture handles.
    if (auto* rm = registry.ctx().find<FResourceManager>()) {
        rm->clear();
    }
    registry.ctx().erase<FResourceManager>();
    registry.ctx().erase<SDL_Renderer*>();

    FAssetManager::Shutdown(registry);

    registry.ctx().erase<FEngineRuntimeState>();
    registry.ctx().erase<FEngineFrameState>();
    registry.ctx().erase<FEngineWindowState>();

    if (auto* bus = registry.ctx().find<FEventBus>()) {
        bus->clear();
    }
    registry.ctx().erase<FEventBus>();

    FAudioManager::Shutdown(registry);
    registry.ctx().erase<FInputState>();
}

bool Engine::run(std::uint32_t maxFrames, std::string screenshotPath) {
    ZoneScopedNC("Engine::run", 0x00FF00);
    if (!stateCatalog) {
        LOG_ERROR("Engine::run aborted: no state catalog was injected.");
        return false;
    }
    if (!startupStateReady || stateMachine.stack().empty()) {
        LOG_ERROR("Engine::run aborted: no startup state; applyStartupStateOverride() must create one.");
        return false;
    }
    maxFrames_ = maxFrames;
    screenshotPath_ = std::move(screenshotPath);
    captureSucceeded_ = true;
    driverFailed_ = false;
    quitAfterFrame_ = false;
    std::uint32_t frames = 0;
    while (running) {
        const float wallDelta = frameTime.updateDeltaTime();
        // WP-00c (dev): scripted runs are frame-locked (a TAS-style input file): pass a fixed step so frame N
        // is the same simulation moment on every run; Time still tracks the wall clock for fps/overlay.
        const float dt = fixedTimestep_ ? kScriptFixedDeltaSeconds : wallDelta;

        if (auto* bus = registry.ctx().find<FEventBus>()) {
            bus->beginFrame();
        }

        const FEngineAbilityRequest frameRequest{
            .ability = EEngineAbility::BeginFrame,
            .frameIndex = CurrentEngineFrameIndex(registry)
        };
        BeginEngineFrameAbility(registry, frameRequest);
        processInput();
        update(dt);

        // WP-00c (dev): a per-frame driver (the run autopilot) acts after the state machine ran and before
        // rendering, so its transition renders on this frame. Stop/Fail still render this frame.
        if (frameDriver_ && !stateMachine.stack().empty()) {
            switch (frameDriver_(registry, stateMachine.stack().back().id)) {
            case EFrameDriverResult::Continue:
                break;
            case EFrameDriverResult::Stop:
                applyEngineMarkRunningEffect(false);
                break;
            case EFrameDriverResult::Fail:
                driverFailed_ = true;
                applyEngineMarkRunningEffect(false);
                break;
            }
        }

        render();
        EndEngineFrameEffects(registry);

        // WP-00c (dev): an input script's `quit` stops the loop after its frame has rendered.
        if (quitAfterFrame_) {
            applyEngineMarkRunningEffect(false);
        }

        FrameMark; // Tracy frame marker
        if (maxFrames_ > 0 && ++frames >= maxFrames_) applyEngineMarkRunningEffect(false);
    }
    return captureSucceeded_ && !flowError && !driverFailed_;
}

void Engine::processInput() {
    ZoneScopedN("Engine::processInput");

    const FEngineAbilityRequest inputRequest{
        .ability = EEngineAbility::BeginInputFrame,
        .frameIndex = CurrentEngineFrameIndex(registry)
    };
    FInputState* input = BeginEngineInputFrameAbility(registry, inputRequest);

    // WP-00c (dev): scripted input is pushed before the poll loop, so ImGui and the game see ordinary SDL events.
    if (inputInjector_ && window) {
        FInjectedFrame injected = inputInjector_(
            static_cast<std::uint32_t>(CurrentEngineFrameIndex(registry)), SDL_GetWindowID(window.get()));
        pendingCaptures_.insert(pendingCaptures_.end(), injected.captures.begin(), injected.captures.end());
        quitAfterFrame_ = quitAfterFrame_ || injected.quit;
        // `expect state`: the top when this frame begins (before this frame's update); a mismatch fails the run.
        for (const auto& [line, stateId] : injected.expectStates) {
            const std::string_view top =
                stateMachine.stack().empty() ? std::string_view{} : stateMachine.stack().back().id;
            if (top == stateId) {
                LOG_INFO("Input script line {}: state '{}' ok", line, stateId);
            } else {
                LOG_ERROR("Input script line {}: expected state '{}', top is '{}'", line, stateId, top);
                driverFailed_ = true;
                quitAfterFrame_ = true;
            }
        }
    }

    const FEngineAbilityRequest pollRequest{
        .ability = EEngineAbility::PollInputEvents,
        .frameIndex = inputRequest.frameIndex
    };

    SDL_Event event;
    while (CanPollEngineInputAbility(registry, pollRequest, running) && SDL_PollEvent(&event)) {
        (void)ApplyEngineImGuiInputEffect(event);

        // Feed game input through the named input-state translation effect.
        (void)ApplyEngineInputTranslateEffect(input, event);

        const FEngineAbilityRequest quitRequest{
            .ability = EEngineAbility::RequestQuit,
            .frameIndex = pollRequest.frameIndex
        };
        const FEngineEffectResult quitResult = ApplyEngineQuitInputEffect(registry, event, quitRequest, running);
        if (quitResult.applied) continue;

        const FEngineAbilityRequest windowRequest{
            .ability = EEngineAbility::ApplyWindowEvent,
            .frameIndex = pollRequest.frameIndex
        };
        ApplyEngineWindowEventAbility(registry, event, windowRequest, running);
    }

    EndEngineInputAbility(registry, inputRequest, input != nullptr);
}

void Engine::update(float dt) {
    ZoneScopedN("Engine::Update");

    if (stateCatalog) {
        applyStateTransition(stateMachine.update(registry, dt));
    }
    // A transition can legitimately empty the stack (Root with an empty target = unwind only). That is a flow
    // error, not a boot condition: there is no fallback state, so the loop stops and run() reports failure.
    if (stateMachine.stack().empty() && !flowError) {
        flowError = true;
        LOG_ERROR("App-flow stack is empty: no state can update or render; stopping.");
        applyEngineMarkRunningEffect(false);
    }
    syncStateSystemPolicy();
    systemMgr.updateEnabled(registry, dt);

    if (auto* audio = registry.ctx().find<FAudioManager>()) {
        audio->gcOneShots();
    }

    // debug updates
    // debugOverlay.update(registry, dt);
}

void Engine::applyStateTransition(const FStateTransition& transition) {
    if (!stateCatalog) return;
    switch (stateMachine.apply(transition, *stateCatalog, registry)) {
    case EStateApplyResult::None:
    case EStateApplyResult::Failed:
        return;
    case EStateApplyResult::Quit:
        applyEngineMarkRunningEffect(false);
        return;
    case EStateApplyResult::Applied:
        updateAppFlowRuntimeState();
        return;
    }
}

void Engine::syncStateSystemPolicy() {
    for (const auto name : ApplyStackSystemPolicy(stateMachine, systemMgr)) {
        LOG_WARN("Required system not registered: {}", name);
    }
}

void Engine::updateAppFlowRuntimeState() {
    auto* appFlow = registry.ctx().find<FAppFlowRuntimeState>();
    if (!appFlow) appFlow = &registry.ctx().emplace<FAppFlowRuntimeState>();
    const std::string stateName(stateMachine.getCurrentStateName());
    if (appFlow->currentState != stateName) {
        if (!appFlow->currentState.empty()) ++appFlow->transitionCount;
        appFlow->currentState = stateName;
    }
}

void Engine::render() {
    ZoneScopedN("Engine::render");

    const FEngineAbilityRequest renderRequest{
        .ability = EEngineAbility::BeginRenderFrame,
        .frameIndex = CurrentEngineFrameIndex(registry)
    };
    if (!BeginEngineRenderAbility(registry, renderRequest, renderer.get())) {
        return;
    }

    (void)ApplyEngineBeginImGuiFrameEffect();
    (void)ApplyEngineClearBackbufferEffect(renderer.get());

    (void)ApplyEngineWorldRenderEffect(registry, systemMgr);
    stateMachine.renderStack(registry);

    (void)ApplyEngineOverlayRenderEffect(registry, frameTime, systemMgr, overlayRenderer);

    (void)ApplyEngineRenderImGuiDrawDataEffect(renderer.get());
    if (!screenshotPath_.empty() && CurrentEngineFrameIndex(registry) == maxFrames_) {
        captureSucceeded_ = saveFrameCapture(screenshotPath_);
    }
    // WP-00c (dev): `capture` commands save after this frame's ImGui draw without stopping the run.
    for (const std::string& path : pendingCaptures_) {
        if (saveFrameCapture(path)) {
            LOG_INFO("Input script capture saved: {}", path);
        } else {
            captureSucceeded_ = false;
        }
    }
    pendingCaptures_.clear();
    const FEngineEffectResult presentResult = ApplyEnginePresentBackbufferEffect(renderer.get());

    const FEngineAbilityRequest presentRequest{
        .ability = EEngineAbility::PresentRenderFrame,
        .frameIndex = renderRequest.frameIndex
    };
    EndEngineRenderAbility(registry, presentRequest, presentResult.applied);
}

bool Engine::saveFrameCapture(const std::string& path) {
    SDL_Surface* surface = SDL_RenderReadPixels(renderer.get(), nullptr);
    const bool saved = surface != nullptr && IMG_SavePNG(surface, path.c_str());
    if (!saved) {
        LOG_ERROR("Frame capture failed: {}", SDL_GetError());
    }
    SDL_DestroySurface(surface);
    return saved;
}

void Engine::shutdown() {
    if (!running && !renderer && !window) return;

    const FEngineAbilityRequest shutdownRequest{
        .ability = EEngineAbility::Shutdown,
        .frameIndex = CurrentEngineFrameIndex(registry)
    };

    applyEngineShutdownAbility(shutdownRequest);
}

void Engine::applyEngineShutdownAbility(const FEngineAbilityRequest& request) {
    if (request.ability != EEngineAbility::Shutdown) {
        return;
    }

    LOG_INFO("Engine shutting down...");
    applyEngineMarkRunningEffect(false);

    stateMachine.unwindAll(registry);

    applyEngineShutdownEntitiesEffect();

    applyEngineShutdownContextServicesEffect();

    applyEngineShutdownImGuiEffect();
    applyEngineDestroyRendererEffect();

    applyEngineDestroyWindowEffect();

    applyEngineShutdownSdlEffect();
    // LOG might already be gone
}

} // namespace game
