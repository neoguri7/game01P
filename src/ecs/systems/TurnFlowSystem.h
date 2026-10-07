#pragma once

#include <cstdint>
#include <string>

#include <entt/entt.hpp>

#include "core/Logger.h"
#include "ecs/components/FActionPoints.h"
#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FSkillLoadout.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/combat/FAutoAction.h"
#include "gameplay/combat/FCombatApply.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/combat/FStatusRules.h"
#include "gameplay/data/FContentRegistry.h"

namespace game::ecs {

/// Per-turn bookkeeping the turn controller deliberately does not own: AP refill, the status tick at the
/// actor's turn start, and one auto action through gameplay/combat (FAutoAction). Player turns wait for
/// the HUD to queue an action.
struct TurnFlowSystem {
    void update(entt::registry& registry, float /*deltaTime*/) {
        auto* runtime = registry.ctx().find<FTurnRuntime>();
        auto* combat = registry.ctx().find<FCombatRuntime>();
        auto* outcome = registry.ctx().find<FBattleOutcome>();
        auto* content = registry.ctx().find<gameplay::FContentRegistry>();
        if (!runtime || !combat || !outcome || !content || outcome->resolved) {
            return;
        }

        const FTurnSnapshot snapshot = runtime->controller.snapshot();
        if (snapshot.state != ETurnState::Running || !snapshot.currentTurn) {
            return;
        }

        const FTurnToken token = *snapshot.currentTurn;
        const bool turnStarted = token.turnSerial != combat->scheduledTurnSerial;
        combat->scheduledTurnSerial = token.turnSerial;
        if (turnStarted) {
            combat->autoActionsThisTurn = 0;
        }

        auto* log = registry.ctx().find<FCombatLog>();
        if (snapshot.roundNumber > kMaxBattleRounds) {
            // Stalemate guard: nobody can finish the fight, so end it as a loss instead of spinning.
            LOG_WARN("[turn] Round limit {} reached - ending battle as a stalemate.", kMaxBattleRounds);
            if (log) log->push("Stalemate - the battle ends.");
            outcome->resolved = true;
            outcome->victory = false;
            runtime->requests.push_back(FStopTurnSession{});
            return;
        }
        const entt::entity actor = token.actor;
        if (!IsAlive(registry, actor)) {
            return; // synchronize() advances past a dead actor
        }

        // Turn start: refill both AP pools (design §4: 서로 전환되지 않는 두 풀).
        if (auto* points = registry.try_get<FActionPoints>(actor); turnStarted && points) {
            points->move = points->moveMax;
            points->skill = points->skillMax;
            LOG_DEBUG("[turn] {} AP refilled - move {}/{} skill {}/{}", UnitDisplayName(registry, actor),
                      points->move, points->moveMax, points->skill, points->skillMax);
        }

        // Turn start: status tick (docs/design/skill-rune-progression.md §4).
        if (auto* active = registry.try_get<FActiveStatuses>(actor); turnStarted && active && !active->effects.empty()) {
            const std::vector<const gameplay::FStatusContent*> definitions = CollectStatuses(*active, *content);
            const FStatusTickResult tick = TickStatuses(*active, definitions, combat->rng);
            if (tick.damage > 0) {
                if (log) log->push(UnitDisplayName(registry, actor) + " takes " + std::to_string(tick.damage) + " status damage");
                if (ApplyDamageToUnit(registry, actor, tick.damage) && log) {
                    log->push(UnitDisplayName(registry, actor) + " falls!");
                }
            }
            if (!IsAlive(registry, actor)) {
                return;
            }
            if (tick.skipTurn) {
                if (log) log->push(UnitDisplayName(registry, actor) + " cannot act - turn skipped");
                runtime->requests.push_back(FEndTurn{token});
                return;
            }
        }

        const bool isParty = registry.all_of<FPartyMember>(actor);
        if (isParty && !combat->autoPlay) {
            return; // party turns wait for player input
        }

        // Auto action (enemies; both sides when auto-play is on). The decision lives in gameplay/combat;
        // this system only applies it to the queue and closes the turn.
        auto* queue = registry.ctx().find<FActionQueue>();
        const auto* loadout = registry.try_get<FSkillLoadout>(actor);
        if (queue == nullptr || loadout == nullptr || loadout->slots.empty()) {
            runtime->requests.push_back(FEndTurn{token});
            return;
        }
        if (!queue->pending.empty()) return;
        if (combat->autoActionsThisTurn >= kMaxAutoActionsPerTurn) {
            LOG_WARN("[turn] {} hit the auto-action cap - ending turn.", UnitDisplayName(registry, actor));
            runtime->requests.push_back(FEndTurn{token});
            return;
        }

        if (auto request = ChooseAutoAction(registry, actor, !isParty, *content)) {
            request->turn = token;
            ++combat->autoActionsThisTurn;
            queue->pending.push_back(*request);
        } else {
            runtime->requests.push_back(FEndTurn{token});
        }
    }

    [[nodiscard]] std::string name() const { return "TurnFlowSystem"; }
};

} // namespace game::ecs
