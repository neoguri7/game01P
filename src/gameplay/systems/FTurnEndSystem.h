#pragma once

#include "ecs/systems/ISystem.h"

#include "core/events/FEventBus.h"
#include "gameplay/components/FBattleDefeat.h"
#include "gameplay/components/FBattleVictory.h"
#include "gameplay/events/FBattleEvents.h"
#include "gameplay/factories/FBattleFactory.h"
#include "gameplay/run/FBattleState.h"

#include <entt/entt.hpp>
#include <tracy/Tracy.hpp>

#include <string>
#include <vector>

namespace game::gameplay {

/// Closes the turn that was requested during this frame (player pressed Esc, or an enemy finished acting).
///
/// why this is its own system, registered *after* the requesters: FPlayerCommandSystem and FEnemyTurnSystem are
/// registered after FTurnStartSystem, and a queueFrame event does not survive FEventBus::beginFrame(), so a
/// reader at the start of the next frame never sees the request (R12: the request would be a write that goes
/// nowhere). Reading it at the end of the frame, after every producer, keeps one representation (the event) and
/// one writer (FBattleFactory::closeTurn) for "whose turn it is". The battle still spends at most one frame with
/// no active unit: the tag is cleared here, and the next turn is opened by FTurnStartSystem on the next frame —
/// the same frame budget the single-system version had, without the lost event.
struct FTurnEndSystem final : public ecs::ISystem {
    void update(entt::registry& registry, float /*deltaTime*/) override {
        ZoneScopedN("FTurnEnd");

        FEventBus* bus = registry.ctx().find<FEventBus>();
        if (bus == nullptr) {
            return;
        }

        const FBattleState* state = registry.ctx().find<FBattleState>();
        if (state == nullptr || state->battle == entt::null || !registry.valid(state->battle)) {
            return;
        }
        if (registry.all_of<FBattleVictory>(state->battle) || registry.all_of<FBattleDefeat>(state->battle)) {
            return; // terminal: the log already narrated the outcome, a turn close would be noise
        }

        // why a copy and not the frameEvents reference: queueFrame<FTurnEndedEvent> inserts a new entry into the
        // bus's frame map, and any_cast hands back a reference into that map's node — a rehash during insertion
        // would leave the reference dangling (R25: the read set must not be invalidated by the write).
        const std::vector<FTurnEndRequestedEvent> requests = bus->frameEvents<FTurnEndRequestedEvent>();
        for (const FTurnEndRequestedEvent& request : requests) {
            if (request.unit == entt::null || !registry.valid(request.unit)) {
                continue;
            }
            FBattleFactory::closeTurn(registry, request.unit);
            bus->queueFrame<FTurnEndedEvent>(FTurnEndedEvent{request.unit});
        }
    }

    [[nodiscard]] std::string name() const override { return "FTurnEnd"; }
};

} // namespace game::gameplay
