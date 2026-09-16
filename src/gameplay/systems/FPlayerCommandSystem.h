#pragma once

#include "ecs/systems/ISystem.h"

#include "core/InputState.h"
#include "core/Logger.h"
#include "core/events/FEventBus.h"
#include "gameplay/components/FSkillSet.h"
#include "gameplay/events/FBattleEvents.h"
#include "gameplay/rules/FTargetSelection.h"
#include "gameplay/rules/FTurnActor.h"
#include "gameplay/run/FBattleState.h"
#include "gameplay/run/FCommandSelection.h"

#include <entt/entt.hpp>
#include <tracy/Tracy.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace game::gameplay {

/// The player's direct control (design §4 확정: 파티 전원 직접 조작) expressed as *requests*: cycling the skill and
/// the target is this system's own state, while using a skill or ending the turn is an event another system
/// acts on. why no state change here: the acting unit's AP, the turn tag and the health are owned by other
/// systems, so this file stays a pure input→event translation (R3/R12).
///
/// UI mapping (a slice-local choice, see contract.md §"Implementation choices"): Q/E cycle the skill, Tab cycles
/// the target, Enter uses the skill, Esc ends the turn, W/A/S/D move (FGridMoveSystem).
struct FPlayerCommandSystem final : public ecs::ISystem {
    void update(entt::registry& registry, float /*deltaTime*/) override {
        ZoneScopedN("FPlayerCommand");

        const FInputState* input = registry.ctx().find<FInputState>();
        FCommandSelection* selection = registry.ctx().find<FCommandSelection>();
        FEventBus* bus = registry.ctx().find<FEventBus>();
        if (input == nullptr || selection == nullptr || bus == nullptr) {
            return;
        }

        // why active *player* unit: an enemy turn is driven by FEnemyTurnSystem, and a battle with no active unit
        // has nothing to command (FTurnStartSystem opens the next one).
        const entt::entity unit = activePlayerUnit(registry);
        if (unit == entt::null) {
            return;
        }

        const std::vector<entt::entity> opponents = livingOpponents(registry, unit);
        if (opponents.empty()) {
            return;
        }

        const FSkillSet* skills = registry.try_get<FSkillSet>(unit);
        const std::size_t skillCount = skills != nullptr ? skills->skillIds.size() : 0;
        if (skillCount == 0) {
            LOG_WARN("command: active unit entity {} has no skills; only ending the turn is possible.", static_cast<int>(unit));
            if (input->isActionPressed(EInputAction::Cancel)) {
                bus->queueFrame<FTurnEndRequestedEvent>(FTurnEndRequestedEvent{unit});
            }
            return;
        }

        // why clamped instead of reset: the lists are rebuilt every frame (a target may have gone down), and
        // clamping keeps the player's place in the list instead of jumping back to the first entry (R11).
        selection->skillIndex %= skillCount;
        selection->targetIndex %= opponents.size();

        if (input->isActionPressed(EInputAction::NextSkill)) {
            selection->skillIndex = (selection->skillIndex + 1) % skillCount;
        }
        if (input->isActionPressed(EInputAction::PrevSkill)) {
            selection->skillIndex = (selection->skillIndex + skillCount - 1) % skillCount;
        }
        if (input->isActionPressed(EInputAction::NextTarget)) {
            selection->targetIndex = (selection->targetIndex + 1) % opponents.size();
        }
        if (input->isActionPressed(EInputAction::Confirm)) {
            const std::string& skillId = skills->skillIds[selection->skillIndex];
            LOG_INFO("command: entity {} requests '{}' on entity {}.",
                     static_cast<int>(unit),
                     skillId,
                     static_cast<int>(opponents[selection->targetIndex]));
            bus->queueFrame<FSkillRequestedEvent>(
                FSkillRequestedEvent{unit, opponents[selection->targetIndex], skillId});
        }
        if (input->isActionPressed(EInputAction::Cancel)) {
            LOG_INFO("command: entity {} ends its turn.", static_cast<int>(unit));
            bus->queueFrame<FTurnEndRequestedEvent>(FTurnEndRequestedEvent{unit});
        }
    }

    [[nodiscard]] std::string name() const override { return "FPlayerCommand"; }
};

} // namespace game::gameplay
