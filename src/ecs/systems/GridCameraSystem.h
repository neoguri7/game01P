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
#include "gameplay/grid/FGridTypes.h"

namespace game::ecs {

struct GridCameraSystem : ISystem {
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
        (void)deltaTime;
        const auto* window = registry.ctx().find<FEngineWindowState>();
        const auto* metrics = registry.ctx().find<FGridMetrics>();
        if (!window || !metrics) return;
        camera.zoom = std::clamp(std::min((window->width * .58f - 64.f) / (8 * metrics->tileSize),
                                          (window->height - 220.f) / (8 * metrics->tileSize)), .25f, 1.f);
        camera.position = metrics->origin + glm::vec2{4 * metrics->tileSize} +
            glm::vec2{window->width * .21f, -25.f} / camera.zoom;
    }

    std::string name() const override { return "GridCameraSystem"; }
};

} // namespace game::ecs
