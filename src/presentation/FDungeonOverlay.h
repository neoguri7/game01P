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
/// Debug-only overlay (WP-02b). Titles, hubs and run modals are drawn by the states that own them.
void RenderDungeonOverlay(entt::registry& registry, const Time& time, const SystemManager& systems);

} // namespace game
