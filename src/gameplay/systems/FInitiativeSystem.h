#pragma once

#include "ecs/systems/ISystem.h"

#include "core/Logger.h"
#include "core/events/FEventBus.h"
#include "gameplay/components/FBattleDefeat.h"
#include "gameplay/components/FBattleRound.h"
#include "gameplay/components/FBattleVictory.h"
#include "gameplay/components/FDowned.h"
#include "gameplay/components/FInitiative.h"
#include "gameplay/events/FBattleEvents.h"
#include "gameplay/run/FBattleState.h"

#include <entt/entt.hpp>
#include <tracy/Tracy.hpp>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace game::gameplay {

/// Rebuilds the predicted turn order once per round (design §4 확정: 턴 순서 = 속도 기반 이니셔티브 + 순서 예측
/// 표시). why the order is rebuilt instead of advanced unit by unit: the prediction must survive a unit going
/// down mid-round, and a total order (speed, then spawn order) makes the result independent of registry
/// storage layout (R16).
struct FInitiativeSystem final : public ecs::ISystem {
    void update(entt::registry& registry, float /*deltaTime*/) override {
        ZoneScopedN("FInitiative");

        FBattleState* state = registry.ctx().find<FBattleState>();
        if (state == nullptr || state->battle == entt::null || !registry.valid(state->battle)) {
            return;
        }
        if (registry.all_of<FBattleVictory>(state->battle) || registry.all_of<FBattleDefeat>(state->battle)) {
            return;
        }
        // why: a consumed order (cursor reached the end) is the round boundary; a non-empty order still being
        // walked belongs to the current round.
        if (state->cursor < state->order.size()) {
            return;
        }

        std::vector<entt::entity> candidates;
        for (const auto entity : registry.view<FInitiative>(entt::exclude<FDowned>)) {
            candidates.push_back(entity);
        }
        // why early return: with nobody able to act there is no round to start, and incrementing the round
        // counter every frame would turn "battle over" into an endless stream of round headers.
        if (candidates.empty()) {
            return;
        }

        std::sort(candidates.begin(), candidates.end(), [&registry](entt::entity left, entt::entity right) {
            const double leftSpeed = registry.get<FInitiative>(left).speed;
            const double rightSpeed = registry.get<FInitiative>(right).speed;
            if (leftSpeed != rightSpeed) {
                return leftSpeed > rightSpeed;
            }
            return left < right; // tie-break: spawn order — a total order, so sorting is deterministic (R16)
        });

        state->order = std::move(candidates);
        state->cursor = 0;

        int round = 0;
        if (FBattleRound* counter = registry.try_get<FBattleRound>(state->battle)) {
            round = ++counter->value;
        }

        LOG_INFO("initiative: round {} order rebuilt with {} unit(s).", round, state->order.size());
        if (auto* bus = registry.ctx().find<FEventBus>()) {
            bus->queueFrame<FRoundStartedEvent>(FRoundStartedEvent{round});
        }
    }

    [[nodiscard]] std::string name() const override { return "FInitiative"; }
};

} // namespace game::gameplay
