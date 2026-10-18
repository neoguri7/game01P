#pragma once

#include <entt/entt.hpp>
#include "core/Time.h"
#include "core/SystemManager.h"

namespace game {

struct FDungeonUiState {
    bool debugVisible{false};
    bool confirmAbandon{false};
    int selectedGrowthMember{0};
};

void InitializeDungeonUi(entt::registry& registry);
void RenderDungeonOverlay(entt::registry& registry, const Time& time, const SystemManager& systems);

} // namespace game
