#pragma once

#include "ecs/systems/ISystem.h"

#include "core/InputState.h"
#include "core/Logger.h"
#include "core/events/FEventBus.h"
#include "gameplay/components/FGridPosition.h"
#include "gameplay/rules/FGridStep.h"
#include "gameplay/rules/FTurnActor.h"
#include "gameplay/run/FBattleState.h"

#include <entt/entt.hpp>
#include <tracy/Tracy.hpp>

#include <string>

namespace game::gameplay {

/// Translates the active player's movement input into one destination cell; movement legality — 이동 AP, bounds,
/// occupancy and the rejection narration — lives in `applyStep` (rules/FGridStep.h), shared with the monster
/// driver. Deleting the copied checks here is the point: one place answers movement for player and AI (R12).
/// `invariant:` this system only computes a destination; it never writes FGridPosition or FApPool itself.
struct FGridMoveSystem final : public ecs::ISystem {
    void update(entt::registry& registry, float /*deltaTime*/) override {
        ZoneScopedN("FGridMove");

        const FInputState* input = registry.ctx().find<FInputState>();
        FBattleState* state = registry.ctx().find<FBattleState>();
        if (input == nullptr || state == nullptr) {
            return;
        }

        const entt::entity unit = activePlayerUnit(registry);
        if (unit == entt::null) {
            return;
        }

        FGridPosition step{};
        if (input->isActionPressed(EInputAction::MoveUp)) {
            step.y -= 1;
        } else if (input->isActionPressed(EInputAction::MoveDown)) {
            step.y += 1;
        } else if (input->isActionPressed(EInputAction::MoveLeft)) {
            step.x -= 1;
        } else if (input->isActionPressed(EInputAction::MoveRight)) {
            step.x += 1;
        } else {
            return;
        }

        const FGridPosition* cell = registry.try_get<FGridPosition>(unit);
        if (cell == nullptr) {
            LOG_ERROR("move: active unit entity {} lacks FGridPosition.", static_cast<int>(unit));
            return;
        }

        FEventBus* bus = registry.ctx().find<FEventBus>();
        (void)applyStep(registry, unit, cell->x + step.x, cell->y + step.y, *state, bus);
    }

    [[nodiscard]] std::string name() const override { return "FGridMove"; }
};

} // namespace game::gameplay
