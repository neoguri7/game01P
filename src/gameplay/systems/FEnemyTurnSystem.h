#pragma once

#include "ecs/systems/ISystem.h"

#include "core/Logger.h"
#include "core/events/FEventBus.h"
#include "gameplay/components/FSkillSet.h"
#include "gameplay/events/FBattleEvents.h"
#include "gameplay/rules/FTargetSelection.h"
#include "gameplay/rules/FTurnActor.h"
#include "gameplay/run/FBattleState.h"

#include <entt/entt.hpp>
#include <tracy/Tracy.hpp>

#include <string>

namespace game::gameplay {

/// The enemy half of the turn loop: an enemy spends its turn through the *same* request path as the player, so
/// legality (range, AP, turn ownership) is checked in one place (FSkillResolveSystem) for both sides.
/// 미결(design §4): 몬스터 AI — 확정 전에는 "스킬 목록의 첫 스킬을 가장 가까운 상대에게" 쓴다. AI가 기획되면
/// 이 파일 하나가 대체되고 나머지 전투 경로는 그대로 남는다 (R9).
/// `invariant:` exactly one action per turn — the end request is queued with the skill request, never after a
/// return path that could be skipped.
struct FEnemyTurnSystem final : public ecs::ISystem {
    void update(entt::registry& registry, float /*deltaTime*/) override {
        ZoneScopedN("FEnemyTurn");

        const entt::entity unit = activeEnemyUnit(registry);
        if (unit == entt::null) {
            return;
        }

        FEventBus* bus = registry.ctx().find<FEventBus>();
        if (bus == nullptr) {
            return;
        }

        const FSkillSet* skills = registry.try_get<FSkillSet>(unit);
        const entt::entity target = nearestLivingOpponent(registry, unit);
        if (skills == nullptr || skills->skillIds.empty() || target == entt::null) {
            LOG_WARN("enemy turn: entity {} has nothing to do; its turn is closed.", static_cast<int>(unit));
            bus->queueFrame<FTurnEndRequestedEvent>(FTurnEndRequestedEvent{unit});
            return;
        }

        LOG_INFO("enemy turn: entity {} uses '{}' on entity {}.",
                 static_cast<int>(unit),
                 skills->skillIds.front(),
                 static_cast<int>(target));
        bus->queueFrame<FSkillRequestedEvent>(FSkillRequestedEvent{unit, target, skills->skillIds.front()});
        bus->queueFrame<FTurnEndRequestedEvent>(FTurnEndRequestedEvent{unit});
    }

    [[nodiscard]] std::string name() const override { return "FEnemyTurn"; }
};

} // namespace game::gameplay
