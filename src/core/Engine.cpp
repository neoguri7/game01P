// ImGui SDL3 backend headers (shipped locally in src/imgui/backends/)
#include "core/Engine.h"

#include <imgui.h>
#include "imgui/backends/imgui_impl_sdl3.h"
#include "imgui/backends/imgui_impl_sdlrenderer3.h"
#include <tracy/Tracy.hpp>

#include "core/AssetManager.h"
#include "core/AudioManager.h"
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

        processInput();
        update(dt);
        render();

        FrameMark; // Tracy frame marker
    }
}

void Engine::processInput() {
    ZoneScopedN("Engine::processInput");

    // Reset per-frame input state
    if (auto* input = registry.ctx().find<FInputState>()) {
        const ImGuiIO& io = ImGui::GetIO();
        input->beginFrame(io.WantCaptureKeyboard, io.WantCaptureMouse);
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);

        // Feed game input
        if (auto* input = registry.ctx().find<FInputState>()) {
            input->processEvent(event);
        }

        if (event.type == SDL_EVENT_QUIT) {
            running = false;
        }
    }
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

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    SDL_SetRenderDrawColor(renderer.get(), 30, 30, 40, 255);
    SDL_RenderClear(renderer.get());

    // Game render systems draw after clear and before overlays/present.
    systemMgr.renderAll(registry);

    if (overlayRenderer) {
        ZoneScopedN("Engine::debugOverlayRender");
        overlayRenderer(registry, frameTime, systemMgr);
    }

    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer.get());

    SDL_RenderPresent(renderer.get());
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
