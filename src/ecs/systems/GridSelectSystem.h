#pragma once

#include <entt/entt.hpp>
#include <string>

#include "core/EngineState.h"
#include "core/InputState.h"
#include "ecs/components/FCamera.h"
#include "ecs/components/FTag.h"
#include "ecs/systems/ISystem.h"
#include "gameplay/grid/FCameraMath.h"
#include "gameplay/grid/FGridQuery.h"
#include "gameplay/grid/FGridSelection.h"
#include "gameplay/grid/FGridTypes.h"

namespace game::ecs {

struct GridSelectSystem : ISystem {
    void update(entt::registry& registry, float) override {
        auto* selection = registry.ctx().find<FGridSelection>();
        if (!selection) return;

        auto* input = registry.ctx().find<FInputState>();
        const auto* metrics = registry.ctx().find<FGridMetrics>();
        if (!input || !metrics) {
            selection->reachable.clear();
            selection->selected = entt::null;
            return;
        }

        FCamera camera{};
        for (const auto entity : registry.view<const FCamera, const FTag>()) {
            if (registry.get<const FTag>(entity).value == "battle_camera") {
                camera = registry.get<const FCamera>(entity);
                break;
            }
        }

        FViewport viewport{};
        if (const auto* window = registry.ctx().find<FEngineWindowState>();
            window && window->width > 0 && window->height > 0) {
            viewport.width = static_cast<float>(window->width);
            viewport.height = static_cast<float>(window->height);
        }

        if (input->mouseLeftPressed) {
            const auto world = ScreenToWorld(input->mousePos, camera, viewport);
            int gridX = 0;
            int gridY = 0;
            selection->selected = WorldToGridCell(*metrics, world, gridX, gridY)
                ? FindGridOccupant(registry, gridX, gridY)
                : entt::null;
        }

        if (selection->selected != entt::null && !registry.valid(selection->selected)) {
            selection->selected = entt::null;
        }
        selection->reachable = selection->selected != entt::null
            ? ComputeReachableCells(registry, selection->selected, 1)
            : std::vector<FGridPosition>{};
    }

    std::string name() const override { return "GridSelectSystem"; }
};

} // namespace game::ecs
