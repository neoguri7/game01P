#pragma once

#include "ecs/systems/ISystem.h"

#include "core/Logger.h"
#include "core/events/FEventBus.h"
#include "gameplay/components/FApPool.h"
#include "gameplay/components/FDowned.h"
#include "gameplay/components/FGridPosition.h"
#include "gameplay/components/FSkillSet.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/data/FSkillContent.h"
#include "gameplay/events/FBattleEvents.h"
#include "gameplay/rules/FGridDistance.h"
#include "gameplay/rules/FTurnActor.h"

#include <entt/entt.hpp>
#include <fmt/format.h>
#include <tracy/Tracy.hpp>

#include <algorithm>
#include <string>

namespace game::gameplay {

/// The one place that decides whether a skill request is legal (design §4: 사거리는 칸 단위, 턴 자원 = 스킬 AP).
/// It spends the AP and emits a resolution; it never applies damage (FDamageSystem) and never narrates
/// (FBattleLogSystem), so the legality rules can be read and changed in isolation (R12).
/// `invariant:` a resolution is emitted only after the actor's skill AP was charged, so no path can damage for
/// free.
struct FSkillResolveSystem final : public ecs::ISystem {
    void update(entt::registry& registry, float /*deltaTime*/) override {
        ZoneScopedN("FSkillResolve");

        FEventBus* bus = registry.ctx().find<FEventBus>();
        const FContentRegistry* content = registry.ctx().find<FContentRegistry>();
        if (bus == nullptr || content == nullptr) {
            return;
        }

        for (const FSkillRequestedEvent& request : bus->frameEvents<FSkillRequestedEvent>()) {
            resolveOne(registry, *content, *bus, request);
        }
    }

    [[nodiscard]] std::string name() const override { return "FSkillResolve"; }

private:
    /// why every rejection is an event as well as a log line: the player needs the reason on screen, and the
    /// developer log keeps it after the text log scrolls away (R19/R31).
    static void reject(FEventBus& bus, const FSkillRequestedEvent& request, std::string reason) {
        LOG_WARN("skill '{}' rejected: {}", request.skillId, reason);
        bus.queueFrame<FSkillRejectedEvent>(
            FSkillRejectedEvent{request.actor, request.skillId, std::move(reason)});
    }

    static void resolveOne(entt::registry& registry,
                           const FContentRegistry& content,
                           FEventBus& bus,
                           const FSkillRequestedEvent& request) {
        // why the actor must still hold the turn: it is the single guard that stops a queued request from a
        // previous turn (or from a unit that already acted) from landing after the turn moved on (R11).
        if (activeUnit(registry) != request.actor) {
            reject(bus, request, "지금 이 유닛의 턴이 아니다");
            return;
        }
        if (!registry.valid(request.target) || registry.all_of<FDowned>(request.target)) {
            reject(bus, request, "대상이 이미 쓰러졌다");
            return;
        }

        const FSkillContent* skill = content.skills.find(request.skillId);
        if (skill == nullptr) {
            reject(bus, request, "알 수 없는 스킬");
            return;
        }

        const FSkillSet* skills = registry.try_get<FSkillSet>(request.actor);
        if (skills == nullptr
            || std::find(skills->skillIds.begin(), skills->skillIds.end(), request.skillId) == skills->skillIds.end()) {
            reject(bus, request, "이 유닛의 스킬 목록에 없다");
            return;
        }

        const FGridPosition* actorCell = registry.try_get<FGridPosition>(request.actor);
        const FGridPosition* targetCell = registry.try_get<FGridPosition>(request.target);
        if (actorCell == nullptr || targetCell == nullptr) {
            reject(bus, request, "위치 정보가 없다");
            return;
        }

        const int distance = gridDistance(*actorCell, *targetCell);
        if (distance > skill->range) {
            reject(bus, request, fmt::format("사거리 밖 (거리 {}, 사거리 {})", distance, skill->range));
            return;
        }

        FApPool* pool = registry.try_get<FApPool>(request.actor);
        if (pool == nullptr) {
            reject(bus, request, "AP 정보가 없다");
            return;
        }
        if (pool->skill < skill->apCost) {
            reject(bus, request, fmt::format("스킬 AP 부족 ({} < {})", pool->skill, skill->apCost));
            return;
        }

        pool->skill -= skill->apCost;
        LOG_INFO("resolve: '{}' uses '{}' at distance {} (skill AP left {}, power {:.0f}).",
                 request.skillId,
                 skill->displayName,
                 distance,
                 pool->skill,
                 skill->power);
        bus.queueFrame<FSkillResolvedEvent>(
            FSkillResolvedEvent{request.actor, request.target, request.skillId, skill->power});
    }
};

} // namespace game::gameplay
