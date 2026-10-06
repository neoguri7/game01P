#pragma once

#include <cstdint>
#include <string>

#include <entt/entt.hpp>

#include "core/Logger.h"
#include "ecs/components/FActionPoints.h"
#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FGridPosition.h"
#include "ecs/components/FSkillLoadout.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/combat/FCombatApply.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FCombatRules.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/combat/FStatusRules.h"
#include "gameplay/data/FContentRegistry.h"

namespace game::ecs {

/// Per-turn bookkeeping the turn controller deliberately does not own: AP refill, the status tick at the
/// actor's turn start, and a minimal enemy action (attack the nearest living party unit in range; 이동은
/// 다음 슬라이스). Player turns wait for the HUD to queue an action.
struct TurnFlowSystem {
    void onRegister(entt::registry& registry) {
        if (!registry.ctx().contains<FCombatRuntime>()) {
            registry.ctx().emplace<FCombatRuntime>();
        }
    }

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
        if (token.turnSerial == combat->scheduledTurnSerial) {
            return; // this turn was already handled
        }
        combat->scheduledTurnSerial = token.turnSerial;

        auto* log = registry.ctx().find<FCombatLog>();
        const entt::entity actor = token.actor;
        if (!IsAlive(registry, actor)) {
            return; // synchronize() advances past a dead actor
        }

        // Turn start: refill both AP pools (design §4: 서로 전환되지 않는 두 풀).
        if (auto* points = registry.try_get<FActionPoints>(actor)) {
            points->move = points->moveMax;
            points->skill = points->skillMax;
            LOG_DEBUG("[turn] {} AP 보충 — 이동 {}/{} 스킬 {}/{}", UnitDisplayName(registry, actor),
                      points->move, points->moveMax, points->skill, points->skillMax);
        }

        // Turn start: status tick (docs/design/skill-rune-equipment.md §4).
        if (auto* active = registry.try_get<FActiveStatuses>(actor); active && !active->effects.empty()) {
            const std::vector<const gameplay::FStatusContent*> definitions = CollectStatuses(*active, *content);
            const FStatusTickResult tick = TickStatuses(*active, definitions);
            if (tick.damage > 0) {
                if (log) log->push(UnitDisplayName(registry, actor) + " 상태 피해 " + std::to_string(tick.damage));
                ApplyDamageToUnit(registry, actor, tick.damage, log);
            }
            if (!IsAlive(registry, actor)) {
                return;
            }
            if (tick.skipTurn) {
                if (log) log->push(UnitDisplayName(registry, actor) + " 행동 불가 — 턴을 넘긴다");
                runtime->requests.push_back(FEndTurn{token});
                return;
            }
        }

        const bool isParty = registry.all_of<FPartyMember>(actor);
        if (isParty && !combat->autoPlay) {
            return; // party turns wait for player input
        }

        // Minimal auto action (enemies; both sides when auto-play is on): strike the nearest living unit
        // on the other side when one is in range. Movement joins in a later slice.
        auto* queue = registry.ctx().find<FActionQueue>();
        const auto* loadout = registry.try_get<FSkillLoadout>(actor);
        if (queue == nullptr || loadout == nullptr || loadout->slots.empty()) {
            runtime->requests.push_back(FEndTurn{token});
            return;
        }

        for (const FSkillSlot& slot : loadout->slots) {
            const auto resolved = ResolveSkillSlot(slot, *content);
            if (!resolved) {
                continue;
            }
            const entt::entity target = FindNearestAlive(registry, actor, !isParty);
            if (target == entt::null) {
                break;
            }
            const auto* actorCell = registry.try_get<FGridPosition>(actor);
            const auto* targetCell = registry.try_get<FGridPosition>(target);
            if (actorCell == nullptr || targetCell == nullptr || !IsInRange(*actorCell, *targetCell, resolved->range)) {
                continue;
            }
            queue->pending.push_back(FActionRequest{actor, target, slot.skillId});
            runtime->requests.push_back(FEndTurn{token});
            return;
        }

        if (log) log->push(UnitDisplayName(registry, actor) + " 사거리 밖 — 턴을 넘긴다");
        runtime->requests.push_back(FEndTurn{token});
    }

    [[nodiscard]] std::string name() const { return "TurnFlowSystem"; }
};

} // namespace game::ecs
