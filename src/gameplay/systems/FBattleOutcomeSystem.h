#pragma once

#include "ecs/systems/ISystem.h"

#include "core/Logger.h"
#include "core/events/FEventBus.h"
#include "gameplay/components/FBattleDefeat.h"
#include "gameplay/components/FBattleOngoing.h"
#include "gameplay/components/FBattleVictory.h"#include "gameplay/components/FDowned.h"
#include "gameplay/components/FTeamEnemy.h"
#include "gameplay/components/FTeamPlayer.h"
#include "gameplay/events/FBattleEvents.h"
#include "gameplay/factories/FBattleFactory.h"
#include "gameplay/run/FBattleState.h"

#include <entt/entt.hpp>
#include <tracy/Tracy.hpp>

#include <cstddef>
#include <string>

namespace game::gameplay {

/// Judges the battle after damage was applied (design §4 확정: 일반 전투 목표 = 적 제거; 파티 전멸 = 런 종료는
/// design §2이지만 런 모델이 아직 없으므로 이 슬라이스는 전투 종료까지만 표시한다).
/// why a separate system instead of a check inside damage: ending the battle is a battle-level rule, and the
/// damage path stays a per-target calculation with no knowledge of win conditions (R12).
struct FBattleOutcomeSystem final : public ecs::ISystem {
    void update(entt::registry& registry, float /*deltaTime*/) override {
        ZoneScopedN("FBattleOutcome");

        FBattleState* state = registry.ctx().find<FBattleState>();
        if (state == nullptr || state->battle == entt::null || !registry.valid(state->battle)) {
            return;
        }
        // why the early return: a terminal battle keeps its phase tag, and re-publishing the end event every
        // frame would flood the narration it already produced once.
        if (registry.all_of<FBattleVictory>(state->battle) || registry.all_of<FBattleDefeat>(state->battle)) {
            return;
        }

        const std::size_t livingParty = countLiving<FTeamPlayer>(registry);
        const std::size_t livingEnemies = countLiving<FTeamEnemy>(registry);
        if (livingParty > 0 && livingEnemies > 0) {
            return;
        }

        FEventBus* bus = registry.ctx().find<FEventBus>();

        // why "no living enemies" wins when both sides are empty: the player's units are the run's investment, so
        // a mutual wipe must never be reported as a loss (design §2: 전멸 = 런 종료).
        if (livingEnemies == 0) {
            FBattleFactory::endBattleAsVictory(registry, state->battle);
            LOG_INFO("battle outcome: victory ({} party unit(s) left).", livingParty);
            if (bus != nullptr) {
                bus->queueFrame<FBattleEndedEvent>(FBattleEndedEvent{true});
            }
            return;
        }

        FBattleFactory::endBattleAsDefeat(registry, state->battle);
        LOG_INFO("battle outcome: defeat ({} enemy unit(s) left).", livingEnemies);
        if (bus != nullptr) {
            bus->queueFrame<FBattleEndedEvent>(FBattleEndedEvent{false});
        }
    }

    [[nodiscard]] std::string name() const override { return "FBattleOutcome"; }

private:
    template<typename TTeamTag>
    [[nodiscard]] static std::size_t countLiving(entt::registry& registry) {
        std::size_t count = 0;
        for (const auto entity : registry.view<TTeamTag>(entt::exclude<FDowned>)) {
            (void)entity;
            ++count;
        }
        return count;
    }
};

} // namespace game::gameplay
