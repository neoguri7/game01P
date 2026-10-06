#pragma once

#include <string>

#include <entt/entt.hpp>

#include "ecs/systems/TurnSystem.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FCombatTypes.h"

namespace game::ecs {

/// Ends the battle the moment one side has no living unit left. The turn session is stopped and the
/// result is written once into FBattleOutcome (the HUD shows it and offers the way back to the hub).
struct CombatOutcomeSystem {
    void update(entt::registry& registry, float /*deltaTime*/) {
        auto* outcome = registry.ctx().find<FBattleOutcome>();
        if (!outcome || outcome->resolved) {
            return;
        }

        // No battle at all (e.g., setup failed) is not a result.
        const int party = AliveCount(registry, true);
        const int enemies = AliveCount(registry, false);
        if (party + enemies == 0 || (party > 0 && enemies > 0)) {
            return;
        }

        outcome->resolved = true;
        outcome->victory = enemies == 0;
        if (auto* log = registry.ctx().find<FCombatLog>()) {
            log->push(outcome->victory ? "전투 승리!" : "파티 전멸...");
        }
        if (auto* runtime = registry.ctx().find<FTurnRuntime>()) {
            runtime->requests.push_back(FStopTurnSession{});
        }
    }

    [[nodiscard]] std::string name() const { return "CombatOutcomeSystem"; }
};

} // namespace game::ecs
