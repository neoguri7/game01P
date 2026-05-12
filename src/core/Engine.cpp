// ImGui SDL3 backend headers (shipped locally in src/imgui/backends/)
#include "core/Engine.h"

#include <imgui.h>
#include "imgui/backends/imgui_impl_sdl3.h"
#include "imgui/backends/imgui_impl_sdlrenderer3.h"
#include <tracy/Tracy.hpp>
#include <print>

#include "core/ResourceManager.h" // may be refactored later into ctx
#include "ecs/components/FPosition.h"
#include "ecs/components/FVelocity.h"
#include "ecs/components/FSprite.h"
#include "ecs/components/FTag.h"
#include "ecs/components/FCamera.h"
#include "ecs/components/FLayer.h"
#include "ecs/systems/MoveSystem.h"
#include "ecs/systems/SpriteRenderSystem.h"
#include "ecs/systems/AnimationSystem.h"
#include "ecs/systems/CollisionSystem.h"
#include "core/AudioManager.h"
#include "core/events/FEventBus.h"
#include "ecs/components/FAnimation.h"
#include "ecs/components/FCollider.h"
#include "ecs/components/FText.h"

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

    // Core services in registry ctx (avoid singletons)
    // Order matters: InputState first since it may be needed by systems
    FInputState::Initialize(registry);
    FAudioManager::Initialize(registry);
    InitializeEventBus(registry);
    setupContextServices();

    // Register example starter systems
    systemMgr.addSystem<ecs::MoveSystem>();
    systemMgr.addSystem<ecs::SpriteRenderSystem>();
    systemMgr.addSystem<ecs::AnimationSystem>();
    systemMgr.addSystem<CollisionSystem>();
    systemMgr.onAllSystemsRegistered(registry);

// Seed with demo entity (example - remove later)
    auto e = registry.create();
    registry.emplace<FPosition>(e, 100.f, 100.f);
    registry.emplace<FVelocity>(e, 80.f, -50.f);
    registry.emplace<FSprite>(e, "assets/player.png");
    registry.emplace<FTag>(e, "demo_player");
    registry.emplace<FLayer>(e, 10);

    running = true;
    return true;
}

void Engine::setupContextServices() {
    // Resource Manager lives in ctx for lifetime management
    auto& rm = registry.ctx().emplace<FResourceManager>();
    rm.init(renderer.get());
    LOG_DEBUG("ResourceManager registered in context");

    // Register the renderer pointer itself for easy access by render systems
    registry.ctx().emplace<SDL_Renderer*>(renderer.get());

    // You can add more ctx services here: InputState, Config, Audio, EventBus etc.
}

void Engine::run() {
    ZoneScopedNC("Engine::run", 0x00FF00);

    while (running) {
        const float dt = frameTime.updateDeltaTime();

        processInput();
        update(dt);
        render();

        FrameMark; // Tracy frame marker
    }
}

void Engine::processInput() {
    ZoneScoped;

    // Reset per-frame input state
    if (auto* input = registry.ctx().find<FInputState>()) {
        input->beginFrame();
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

    // Game rendering here (systems already queued draw calls or render directly)
    // For now draw a placeholder + ECS-based sprite system will override.

    // ImGui overlay: Engine Stats + Entity Inspector
    showDebugWindows();

    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer.get());

    SDL_RenderPresent(renderer.get());
}

void Engine::showDebugWindows() {
    // ── Engine Stats ──────────────────────────────────────
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::Begin("Engine Stats", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::Text("FPS: %d", frameTime.getFps());
    ImGui::Text("Entity count: %zu", registry.storage<entt::entity>().size());
    ImGui::Text("Systems: %zu", systemMgr.getRegisteredCount());
    ImGui::Separator();
    ImGui::TextWrapped("Everything is data in the registry + ctx. Add your systems now.");
    ImGui::End();

    // ── Entity Inspector ──────────────────────────────────
    ImGui::SetNextWindowPos(ImVec2(10, 120), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 400), ImGuiCond_FirstUseEver);
    ImGui::Begin("Entity Inspector");

    // List entities
    auto& storage = registry.storage<entt::entity>();
    for (auto entity : storage) {
        if (!registry.valid(entity)) continue;
        ImGui::PushID(static_cast<int>(entity));

        // Build label from tag if present
        std::string label;
        if (registry.all_of<FTag>(entity)) {
            label = fmt::format("{} ({})", static_cast<uint32_t>(entity), registry.get<FTag>(entity).value);
        } else {
            label = std::to_string(static_cast<uint32_t>(entity));
        }

        bool expanded = ImGui::TreeNode(label.c_str());
        if (expanded) {
            // FPosition
            if (registry.all_of<FPosition>(entity)) {
                auto& pos = registry.get<FPosition>(entity);
                ImGui::Text("    Position: %.1f, %.1f", pos.x, pos.y);
                ImGui::DragFloat2("  ", &pos.x, 1.f);
            }
            // FVelocity
            if (registry.all_of<FVelocity>(entity)) {
                auto& vel = registry.get<FVelocity>(entity);
                ImGui::Text("    Velocity: %.1f, %.1f", vel.vx, vel.vy);
                ImGui::DragFloat2("  ", &vel.vx, 1.f);
            }
            // FSprite
            if (registry.all_of<FSprite>(entity)) {
                auto& spr = registry.get<FSprite>(entity);
                ImGui::Text("    Sprite: %s", spr.texturePath.c_str());
            }
            // FCamera
            if (registry.all_of<FCamera>(entity)) {
                auto& cam = registry.get<FCamera>(entity);
                ImGui::Text("    Camera: pos(%.0f, %.0f) zoom=%.2f", cam.position.x, cam.position.y, cam.zoom);
            }
            // FLayer
            if (registry.all_of<FLayer>(entity)) {
                auto& layer = registry.get<FLayer>(entity);
                ImGui::Text("    Layer: %d", layer.depth);
                ImGui::DragInt("  ", &layer.depth, 1);
            }
            // FCollider
            if (registry.all_of<FCollider>(entity)) {
                auto& col = registry.get<FCollider>(entity);
                ImGui::Text("    Collider: %s", col.type == EColliderType::AABB ? "AABB" : "Circle");
                ImGui::Text("    Layer: %d", col.collisionLayer);
            }
            // FAnimation
            if (registry.all_of<FAnimation>(entity)) {
                auto& anim = registry.get<FAnimation>(entity);
                ImGui::Text("    Anim: %s (frame %d/%zu)", anim.playing ? "playing" : "stopped", anim.currentFrame, anim.frames.size());
            }
            // FText
            if (registry.all_of<FText>(entity)) {
                auto& txt = registry.get<FText>(entity);
                ImGui::Text("    Text: \"%s\"", txt.content.c_str());
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    if (ImGui::Button("Add Demo Entity")) {
        auto e = registry.create();
        registry.emplace<FPosition>(e, 200.f, 200.f);
        registry.emplace<FVelocity>(e, 50.f, -30.f);
        registry.emplace<FTag>(e, "demo");
        LOG_INFO("Created demo entity {}", static_cast<uint32_t>(e));
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear All Entities")) {
        registry.clear();
    }

    ImGui::End();
}

void Engine::shutdown() {
    if (!running && !renderer) return;

    LOG_INFO("Engine shutting down...");

    registry.clear(); // destroys all entities + components

    FAudioManager::Shutdown(registry);
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
