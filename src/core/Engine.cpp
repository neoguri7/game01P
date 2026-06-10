// ImGui SDL3 backend headers (shipped locally in src/imgui/backends/)
#include "core/Engine.h"

#include <imgui.h>
#include "imgui/backends/imgui_impl_sdl3.h"
#include "imgui/backends/imgui_impl_sdlrenderer3.h"
#include <tracy/Tracy.hpp>

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

Engine::Engine() = default;

Engine::~Engine() {
    shutdown();
}

bool Engine::initialize(const std::string& title, int w, int h) {
    ZoneScopedN("Engine::initialize");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init Error: %s", SDL_GetError());
        return false;
    }

    SDL_Window* rawWin = SDL_CreateWindow(title.c_str(), w, h, SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!rawWin) {
        SDL_Log("Window create failed: %s", SDL_GetError());
        return false;
    }
    window = FWindowPtr(rawWin);

    SDL_Renderer* rawRend = SDL_CreateRenderer(window.get(), nullptr);
    if (!rawRend) {
        SDL_Log("Renderer create failed: %s", SDL_GetError());
        return false;
    }
    renderer = FRendererPtr(rawRend);

    // Setup ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplSDL3_InitForSDLRenderer(window.get(), renderer.get());
    ImGui_ImplSDLRenderer3_Init(renderer.get());

    // Logger (will expose through ctx)
    FLogger::Initialize(registry);
    if (auto* l = registry.ctx().find<FLogger>()) {
        SetCurrentGameLogger(l->logger);
    }
    LOG_INFO("Engine initialized.");

    initializeContextServices();

    running = true;
    return true;
}

void Engine::setOverlayRenderer(std::function<void(entt::registry&, const Time&, const SystemManager&)> renderer) {
    overlayRenderer = std::move(renderer);
}

void Engine::initializeContextServices() {
    // Service init order:
    // 1. Input for frame state, 2. audio, 3. events, 4. asset boundary,
    // 5. renderer pointer, 6. renderer-backed resources.
    FInputState::Initialize(registry);
    FAudioManager::Initialize(registry);
    InitializeEventBus(registry);
    FAssetManager::Initialize(registry);

    auto& windowState = registry.ctx().emplace<FEngineWindowState>();
    SDL_GetWindowSize(window.get(), &windowState.width, &windowState.height);
    windowState.windowOpen = window != nullptr;

    registry.ctx().emplace<FEngineFrameState>();

    auto& runtimeState = registry.ctx().emplace<FEngineRuntimeState>();
    runtimeState.rendererReady = renderer != nullptr;
    runtimeState.inputReady = registry.ctx().contains<FInputState>();
    if (windowState.windowOpen) runtimeState.tags.add(EEngineTag::WindowOpen);
    if (runtimeState.rendererReady) runtimeState.tags.add(EEngineTag::RendererReady);
    if (runtimeState.inputReady) runtimeState.tags.add(EEngineTag::InputReady);

    registry.ctx().emplace<SDL_Renderer*>(renderer.get());

    auto& rm = registry.ctx().emplace<FResourceManager>();
    rm.init(renderer.get());
    LOG_DEBUG("ResourceManager registered in context");
}

void Engine::shutdownContextServices() {
    // Shutdown order mirrors dependencies: renderer-backed resources are cleared
    // before renderer ctx removal and before SDL_Renderer destruction.
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

void Engine::run() {
    ZoneScopedNC("Engine::run", 0x00FF00);

    while (running) {
        const float dt = frameTime.updateDeltaTime();

        if (auto* bus = registry.ctx().find<FEventBus>()) {
            bus->beginFrame();
        }

        BeginEngineFrameAbility(registry);
        processInput();
        update(dt);
        render();
        EndEngineFrameEffects(registry);

        FrameMark; // Tracy frame marker
    }
}

void Engine::processInput() {
    ZoneScopedN("Engine::processInput");

    const FEngineAbilityRequest inputRequest{
        .ability = EEngineAbility::ProcessInput,
        .frameIndex = CurrentEngineFrameIndex(registry)
    };
    FInputState* input = BeginEngineInputAbility(registry, inputRequest);

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);

        // Feed game input
        if (input) {
            input->processEvent(event);
        }

        const FEngineAbilityRequest windowRequest{
            .ability = EEngineAbility::ApplyWindowEvent,
            .frameIndex = inputRequest.frameIndex
        };
        ApplyEngineWindowEventAbility(registry, event, windowRequest, running);
    }

    EndEngineInputAbility(registry, inputRequest, input != nullptr);
}

void Engine::update(float dt) {
    ZoneScopedN("Engine::Update");

    // High level game systems (movement, physics, AI, etc.)
    systemMgr.updateAll(registry, dt);

    if (auto* audio = registry.ctx().find<FAudioManager>()) {
        audio->gcOneShots();
    }

    // TODO: optional state machine update (e.g. current state.Update(registry, dt))

    // debug updates
    // debugOverlay.update(registry, dt);
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

    // Game render systems draw after clear and before overlays/present.
    systemMgr.renderAll(registry);

    if (overlayRenderer) {
        ZoneScopedN("Engine::debugOverlayRender");
        overlayRenderer(registry, frameTime, systemMgr);
    }

    (void)ApplyEngineRenderImGuiDrawDataEffect(renderer.get());
    (void)ApplyEnginePresentBackbufferEffect(renderer.get());

    const FEngineAbilityRequest presentRequest{
        .ability = EEngineAbility::PresentRenderFrame,
        .frameIndex = renderRequest.frameIndex
    };
    EndEngineRenderAbility(registry, presentRequest);
}

void Engine::shutdown() {
    if (!running && !renderer && !window) return;

    LOG_INFO("Engine shutting down...");
    running = false;

    registry.clear(); // destroys all entities + components

    shutdownContextServices();
    FLogger::Shutdown(registry);

    if (renderer) {
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        renderer.reset();
    }

    if (window) {
        window.reset();
    }

    SDL_Quit();
    // LOG might already be gone
}

} // namespace game
