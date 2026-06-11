#include "debug/DebugOverlay.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <imgui.h>

#include "core/events/FCollisionEvent.h"
#include "core/events/FEventBus.h"
#include "core/SystemManager.h"
#include "core/Time.h"
#include "core/Logger.h"
#include "debug/DemoBootstrap.h"
#include "ecs/components/FAbilityScores.h"
#include "ecs/components/FAnimation.h"
#include "ecs/components/FCamera.h"
#include "ecs/components/FCollider.h"
#include "ecs/components/FCommandToken.h"
#include "ecs/components/FDebugPrimitive.h"
#include "ecs/components/FDemoShowcaseEntity.h"
#include "ecs/components/FGridPosition.h"
#include "ecs/components/FLayer.h"
#include "ecs/components/FPosition.h"
#include "ecs/components/FSpeed.h"
#include "ecs/components/FSprite.h"
#include "ecs/components/FTag.h"
#include "ecs/components/FText.h"
#include "ecs/components/FUnitStateDefeated.h"
#include "ecs/components/FVelocity.h"

#include <tracy/Tracy.hpp>

namespace game {

namespace {

std::string GetEntityLabel(entt::registry& registry, entt::entity entity) {
    if (registry.all_of<FTag>(entity)) {
        return fmt::format("{} ({})", static_cast<uint32_t>(entity), registry.get<FTag>(entity).value);
    }

    return std::to_string(static_cast<uint32_t>(entity));
}

template<typename... Components>
std::size_t CountView(entt::registry& registry) {
    std::size_t count = 0;
    for ([[maybe_unused]] auto entity : registry.view<Components...>()) {
        ++count;
    }
    return count;
}

void RenderEntityComponents(entt::registry& registry, entt::entity entity) {
    if (registry.all_of<FPosition>(entity)) {
        auto& pos = registry.get<FPosition>(entity);
        ImGui::Text("    Position: %.1f, %.1f", pos.x, pos.y);
        ImGui::DragFloat2("  ", &pos.x, 1.f);
    }

    if (registry.all_of<FVelocity>(entity)) {
        auto& vel = registry.get<FVelocity>(entity);
        ImGui::Text("    Velocity: %.1f, %.1f", vel.vx, vel.vy);
        ImGui::DragFloat2("  ", &vel.vx, 1.f);
    }

    if (registry.all_of<FDemoShowcaseEntity>(entity)) {
        ImGui::Text("    Demo Showcase: yes");
    }

    if (registry.all_of<FGridPosition>(entity)) {
        auto& grid = registry.get<FGridPosition>(entity);
        ImGui::Text("    Grid: %d, %d", grid.x, grid.y);
    }

    if (registry.all_of<FSpeed>(entity)) {
        auto& speed = registry.get<FSpeed>(entity);
        ImGui::Text("    Speed: %d ft", speed.feet);
    }

    if (registry.all_of<FAbilityScores>(entity)) {
        auto& scores = registry.get<FAbilityScores>(entity);
        ImGui::Text("    Ability Scores: STR %d DEX %d CON %d", scores.strength, scores.dexterity, scores.constitution);
    }

    if (registry.all_of<FCommandToken>(entity)) {
        auto& command = registry.get<FCommandToken>(entity);
        ImGui::Text("    Command: %s (%s)", command.displayName.c_str(), command.id.c_str());
    }

    if (registry.all_of<FDebugPrimitive>(entity)) {
        auto& primitive = registry.get<FDebugPrimitive>(entity);
        ImGui::Text("    Debug Primitive: %.0fx%.0f rgba(%u,%u,%u,%u)",
                    primitive.width,
                    primitive.height,
                    primitive.fillR,
                    primitive.fillG,
                    primitive.fillB,
                    primitive.fillA);
    }

    if (registry.all_of<FUnitStateDefeated>(entity)) {
        ImGui::Text("    State Tag: Defeated");
    }

    if (registry.all_of<FSprite>(entity)) {
        auto& sprite = registry.get<FSprite>(entity);
        ImGui::Text("    Sprite: %s", sprite.texturePath.c_str());
    }

    if (registry.all_of<FCamera>(entity)) {
        auto& camera = registry.get<FCamera>(entity);
        ImGui::Text("    Camera: pos(%.0f, %.0f) zoom=%.2f", camera.position.x, camera.position.y, camera.zoom);
    }

    if (registry.all_of<FLayer>(entity)) {
        auto& layer = registry.get<FLayer>(entity);
        ImGui::Text("    Layer: %d", layer.depth);
        ImGui::DragInt("  ", &layer.depth, 1);
    }

    if (registry.all_of<FCollider>(entity)) {
        auto& collider = registry.get<FCollider>(entity);
        ImGui::Text("    Collider: %s", collider.type == EColliderType::AABB ? "AABB" : "Circle");
        ImGui::Text("    Layer: %d", collider.collisionLayer);
    }

    if (registry.all_of<FAnimation>(entity)) {
        auto& animation = registry.get<FAnimation>(entity);
        ImGui::Text("    Anim: %s (frame %d/%zu)",
                    animation.playing ? "playing" : "stopped",
                    animation.currentFrame,
                    animation.frames.size());
    }

    if (registry.all_of<FText>(entity)) {
        auto& text = registry.get<FText>(entity);
        ImGui::Text("    Text: \"%s\"", text.content.c_str());
    }
}

void RenderEngineStats(entt::registry& registry, const Time& frameTime, const SystemManager& systemManager) {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::Begin("Engine Stats", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::Text("FPS: %d", frameTime.getFps());
    ImGui::Text("Entity count: %zu", registry.storage<entt::entity>().size());
    ImGui::Text("Systems: %zu", systemManager.getRegisteredCount());
    ImGui::Text("Showcase entities: %zu", CountView<FDemoShowcaseEntity>(registry));
    ImGui::Text("Debug primitives: %zu", CountView<FDebugPrimitive>(registry));
    ImGui::Text("Movers: %zu", CountView<FVelocity>(registry));
    ImGui::Text("Colliders: %zu", CountView<FCollider>(registry));

    const auto* eventBus = registry.ctx().find<FEventBus>();
    const auto collisionEvents = eventBus ? eventBus->frameEvents<FCollisionEvent>().size() : 0;
    ImGui::Text("Frame collision events: %zu", collisionEvents);

    if (ImGui::TreeNode("Registered systems")) {
        for (const auto& systemName : systemManager.getSystemNames()) {
            ImGui::BulletText("%s", systemName.c_str());
        }
        ImGui::TreePop();
    }

    ImGui::Separator();
    ImGui::TextWrapped("Demo proof: factory-created ECS data, ctx services, event bus collisions, render layers, and debug tooling are visible without external assets.");
    ImGui::TextDisabled("Pending: external data/config loading and full tactical combat runtime.");
    ImGui::End();
}

void RenderEntityInspector(entt::registry& registry) {
    ImGui::SetNextWindowPos(ImVec2(10, 120), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 400), ImGuiCond_FirstUseEver);
    ImGui::Begin("Entity Inspector");

    auto& storage = registry.storage<entt::entity>();
    for (auto entity : storage) {
        if (!registry.valid(entity)) {
            continue;
        }

        ImGui::PushID(static_cast<int>(entity));
        const auto label = GetEntityLabel(registry, entity);

        if (ImGui::TreeNode(label.c_str())) {
            RenderEntityComponents(registry, entity);
            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    ImGui::Separator();
    if (ImGui::Button("Add Demo Entity")) {
        auto entity = CreateDebugDemoEntity(registry);
        LOG_INFO("Created demo entity {}", static_cast<uint32_t>(entity));
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Showcase")) {
        ResetDemoScene(registry);
        LOG_INFO("Reset demo showcase");
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear All Entities")) {
        registry.clear();
    }

    ImGui::End();
}

} // namespace

void RenderDebugOverlay(entt::registry& registry, const Time& frameTime, const SystemManager& systemManager) {
    ZoneScopedN("RenderDebugOverlay");
    RenderEngineStats(registry, frameTime, systemManager);
    RenderEntityInspector(registry);
}

} // namespace game
