#pragma once

#include "ecs/systems/ISystem.h"

#include "core/Logger.h"
#include "core/events/FEventBus.h"
#include "gameplay/components/FApPool.h"
#include "gameplay/components/FBattleDefeat.h"
#include "gameplay/components/FBattleVictory.h"
#include "gameplay/components/FDowned.h"
#include "gameplay/components/FTurnActive.h"
#include "gameplay/components/FUnitRef.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/data/FUnitContent.h"
#include "gameplay/events/FBattleEvents.h"
#include "gameplay/factories/FBattleFactory.h"
#include "gameplay/rules/FTurnActor.h"
#include "gameplay/run/FBattleState.h"

#include <entt/entt.hpp>
#include <tracy/Tracy.hpp>

#include <string>

namespace game::gameplay {

/// Opens the next turn: walks the predicted order until a unit that can act is found and gives it its AP
/// (design §4 확정: 턴 자원 = 이동 AP + 스킬 AP, 서로 전환 불가). A turn that was requested closed is closed by
/// FTurnEndSystem, later in the same frame — this system only *opens*.
/// why this system owns the FTurnActive tag: one writer for "whose turn it is" keeps the acting systems free of
/// turn bookkeeping, and a unit that goes down simply stops matching the tag (R11/R12).
struct FTurnStartSystem final : public ecs::ISystem {
    void update(entt::registry& registry, float /*deltaTime*/) override {
        ZoneScopedN("FTurnStart");

        FBattleState* state = registry.ctx().find<FBattleState>();
        if (state == nullptr || state->battle == entt::null || !registry.valid(state->battle)) {
            return;
        }
        if (registry.all_of<FBattleVictory>(state->battle) || registry.all_of<FBattleDefeat>(state->battle)) {
            return;
        }

        FEventBus* bus = registry.ctx().find<FEventBus>();

        if (activeUnit(registry) != entt::null) {
            return;
        }

        const FContentRegistry* content = registry.ctx().find<FContentRegistry>();
        if (content == nullptr) {
            return;
        }

        while (state->cursor < state->order.size()) {
            const entt::entity candidate = state->order[state->cursor++];
            if (!registry.valid(candidate) || registry.all_of<FDowned>(candidate)) {
                continue; // 쓰러진 유닛은 턴을 건너뛴다 (design §4: 부상·사망 없음 — 순서만 지나간다)
            }

            const FUnitRef* ref = registry.try_get<FUnitRef>(candidate);
            const FUnitContent* unit = ref != nullptr ? content->units.find(ref->contentId) : nullptr;
            if (unit == nullptr) {
                LOG_ERROR("turn: unit entity {} has no usable content row; its turn is skipped.", static_cast<int>(candidate));
                continue;
            }

            FApPool* pool = registry.try_get<FApPool>(candidate);
            if (pool == nullptr) {
                LOG_ERROR("turn: unit '{}' has no FApPool; its turn is skipped.", unit->id);
                continue;
            }

            // why refill from content every turn: 미결(design §4) AP 회복 수단 여부가 정해지기 전에는 "턴 시작 =
            // 데이터의 잠정 AP"가 유일한 회복 경로다. 다른 회복원이 확정되면 이 두 줄만 바뀐다.
            pool->move = unit->moveAp;  // fallback: 잠정값 (design §4 확정 잠정치 — 데이터가 이긴다)
            pool->skill = unit->skillAp;

            FBattleFactory::openTurn(registry, candidate);
            LOG_INFO("turn start: '{}' (move AP {}, skill AP {}).", unit->id, pool->move, pool->skill);
            if (bus != nullptr) {
                bus->queueFrame<FTurnStartedEvent>(FTurnStartedEvent{candidate});
            }
            return;
        }
    }

    [[nodiscard]] std::string name() const override { return "FTurnStart"; }
};

} // namespace game::gameplay
