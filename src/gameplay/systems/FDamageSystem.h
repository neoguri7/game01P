#pragma once

#include "ecs/systems/ISystem.h"

#include "core/Logger.h"
#include "core/events/FEventBus.h"
#include "gameplay/components/FDowned.h"
#include "gameplay/components/FHealth.h"
#include "gameplay/events/FBattleEvents.h"
#include "gameplay/factories/FBattleFactory.h"

#include <entt/entt.hpp>
#include <tracy/Tracy.hpp>

#include <algorithm>
#include <string>

namespace game::gameplay {

/// Applies an accepted skill's power to the target's health and marks the unit down (design §4 확정: 캐릭터
/// 부상·사망 없음 — 쓰러져도 페널티가 누적되지 않고, 그 유닛은 턴 순서에서 빠질 뿐이다).
/// 미결(design §4): 피해 공식 — 확정 전에는 데이터의 power가 그대로 피해다(경감·저항·치명타 없음). 이 계산은
/// 한 곳에만 있고, 확정된 공식의 입력(수치노드·장비)은 design §5가 공급한다.
struct FDamageSystem final : public ecs::ISystem {
    void update(entt::registry& registry, float /*deltaTime*/) override {
        ZoneScopedN("FDamage");

        FEventBus* bus = registry.ctx().find<FEventBus>();
        if (bus == nullptr) {
            return;
        }

        for (const FSkillResolvedEvent& event : bus->frameEvents<FSkillResolvedEvent>()) {
            applyDamage(registry, *bus, event);
        }
    }

    [[nodiscard]] std::string name() const override { return "FDamage"; }

private:
    static void applyDamage(entt::registry& registry, FEventBus& bus, const FSkillResolvedEvent& event) {
        FHealth* health = registry.try_get<FHealth>(event.target);
        if (health == nullptr) {
            LOG_ERROR("damage: target entity {} has no FHealth; the resolution is dropped.", static_cast<int>(event.target));
            return;
        }

        const double before = health->current;
        health->current = std::max(0.0, before - std::max(0.0, event.power));
        const double applied = before - health->current;

        LOG_INFO("damage: '{}' on entity {} applied {:.1f} (HP {:.1f}/{:.1f}).",
                 event.skillId,
                 static_cast<int>(event.target),
                 applied,
                 health->current,
                 health->max);
        bus.queueFrame<FUnitDamagedEvent>(FUnitDamagedEvent{event.target, applied, health->current});

        if (health->current > 0.0) {
            return;
        }
        if (registry.all_of<FDowned>(event.target)) {
            return;
        }

        FBattleFactory::markDowned(registry, event.target);
        LOG_INFO("damage: entity {} is down and leaves the turn order.", static_cast<int>(event.target));
        bus.queueFrame<FUnitDownedEvent>(FUnitDownedEvent{event.target});
    }
};

} // namespace game::gameplay
