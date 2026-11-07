#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "ecs/components/FFaction.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/combat/FEffectHooks.h"

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
            log->push(outcome->victory ? "Victory!" : "The party has fallen...");
        }
        if (auto* runtime = registry.ctx().find<FTurnRuntime>()) {
            runtime->requests.push_back(FStopTurnSession{});
        }

        // WP-04: a won room fires OnRoomClear once per living party member, in a deterministic order
        // (sort by entity id — there is no party slot component). The `continuedAfterRoom` path sets
        // `resolved` directly (FEmberBattleState), so this early-returns and nothing refires. The heals
        // land this frame, before the next frame's CaptureRunParty records the party HP.
        if (outcome->victory) {
            std::vector<entt::entity> survivors;
            for (const entt::entity member : registry.view<FPartyMember>()) {
                if (IsAlive(registry, member)) {
                    survivors.push_back(member);
                }
            }
            std::ranges::sort(survivors, [](entt::entity left, entt::entity right) {
                return entt::to_integral(left) < entt::to_integral(right);
            });
            for (const entt::entity member : survivors) {
                (void)FireHooks(registry, gameplay::EHookEvent::OnRoomClear, FHookContext{.owner = member});
            }
        }
    }

    [[nodiscard]] std::string name() const { return "CombatOutcomeSystem"; }
};

} // namespace game::ecs
