#pragma once

#include <algorithm>
#include <cmath>
#include <entt/entt.hpp>
#include <string>

#include "core/EngineState.h"
#include "core/InputState.h"
#include "ecs/components/FCamera.h"
#include "ecs/components/FTag.h"
#include "ecs/systems/ISystem.h"
#include "gameplay/grid/FBattleLayout.h"

namespace game::ecs {

struct GridCameraSystem : ISystem {
    int lastWidth{};
    int lastHeight{};

    void update(entt::registry& registry, float deltaTime) override {
        auto* input = registry.ctx().find<FInputState>();
        if (!input) return;

        entt::entity cameraEntity = entt::null;
        for (const auto entity : registry.view<FCamera, FTag>()) {
            if (registry.get<FTag>(entity).value == "battle_camera") {
                cameraEntity = entity;
                break;
            }
        }
        if (cameraEntity == entt::null) return;

        auto& camera = registry.get<FCamera>(cameraEntity);
        if (const auto* window = registry.ctx().find<FEngineWindowState>()) {
            if (const auto* metrics = registry.ctx().find<FGridMetrics>(); metrics
                && (lastWidth != window->width || lastHeight != window->height)) {
                camera = FitBattleCamera(*metrics, {static_cast<float>(window->width), static_cast<float>(window->height)});
                lastWidth = window->width;
                lastHeight = window->height;
            }
        }
        camera.zoom = std::clamp(camera.zoom, 0.25f, 4.f);
        const float panSpeed = 480.f * deltaTime / camera.zoom;
        if (input->isActionHeld(EInputAction::MoveLeft)) camera.position.x -= panSpeed;
        if (input->isActionHeld(EInputAction::MoveRight)) camera.position.x += panSpeed;
        if (input->isActionHeld(EInputAction::MoveUp)) camera.position.y -= panSpeed;
        if (input->isActionHeld(EInputAction::MoveDown)) camera.position.y += panSpeed;

        if (input->mouseWheelDelta != 0.f && !input->uiCapturesMouse) {
            camera.zoom = std::clamp(camera.zoom * std::pow(1.1f, input->mouseWheelDelta), 0.25f, 4.f);
        }
    }

    std::string name() const override { return "GridCameraSystem"; }
};

} // namespace game::ecs
