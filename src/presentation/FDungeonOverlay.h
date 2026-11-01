#pragma once

#include <entt/entt.hpp>
#include "core/Time.h"
#include "core/SystemManager.h"

namespace game {

struct FDungeonUiState {
    bool debugVisible{false};
    bool confirmAbandon{false};
    bool confirmNewExpedition{false};  // starting over would overwrite a saved expedition
    bool newExpeditionFresh{false};    // which "new expedition" button asked
    int selectedGrowthMember{0};
};

void InitializeDungeonUi(entt::registry& registry);
void RenderDungeonOverlay(entt::registry& registry, const Time& time, const SystemManager& systems);

} // namespace game
