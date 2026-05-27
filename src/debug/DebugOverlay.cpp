#include "debug/DebugOverlay.h"

#include <cstdint>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <imgui.h>

#include "core/SystemManager.h"
#include "core/Time.h"
#include "core/Logger.h"
#include "debug/DemoBootstrap.h"
#include "debug/TacticalD20TelemetryPanel.h"
#include "debug/TacticalD20VisualFeedbackPanel.h"
#include "ecs/components/FAnimation.h"
#include "ecs/components/FCamera.h"
#include "ecs/components/FCollider.h"
#include "ecs/components/FLayer.h"
#include "ecs/components/FPosition.h"
#include "ecs/components/FSprite.h"
#include "ecs/components/FTag.h"
#include "ecs/components/FText.h"
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
    ImGui::Separator();
    ImGui::TextWrapped("Everything is data in the registry + ctx. Add your systems now.");
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
    if (ImGui::Button("Clear All Entities")) {
        registry.clear();
    }

    ImGui::End();
}

} // namespace

void RenderDebugOverlay(entt::registry& registry, const Time& frameTime, const SystemManager& systemManager) {
    ZoneScopedN("RenderDebugOverlay");
    RenderEngineStats(registry, frameTime, systemManager);
    RenderTacticalD20VisualFeedbackPanel(registry);
    RenderTacticalD20TelemetryPanel(registry, frameTime, systemManager);
    RenderEntityInspector(registry);
}

} // namespace game
