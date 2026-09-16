#pragma once

#include "ecs/systems/ISystem.h"

#include "core/Logger.h"
#include "core/events/FEventBus.h"
#include "gameplay/components/FDowned.h"
#include "gameplay/components/FGridPosition.h"
#include "gameplay/components/FHealth.h"
#include "gameplay/components/FSkillSet.h"
#include "gameplay/components/FTeamEnemy.h"
#include "gameplay/components/FUnitRef.h"
#include "gameplay/data/FBehaviorContent.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/data/FSkillContent.h"
#include "gameplay/data/FUnitContent.h"
#include "gameplay/events/FBattleEvents.h"
#include "gameplay/rules/FBehaviorTree.h"
#include "gameplay/rules/FGridStep.h"
#include "gameplay/rules/FTargetSelection.h"
#include "gameplay/rules/FTurnActor.h"
#include "gameplay/run/FBattleState.h"

#include <entt/entt.hpp>
#include <tracy/Tracy.hpp>

#include <optional>
#include <string>
#include <vector>

namespace game::gameplay {

/// The enemy half of the turn loop: an enemy spends its turn through the *same* request path as the player, so
/// legality (range, AP, turn ownership) is checked in one place (FSkillResolveSystem) for both sides.
/// What the monster *chooses* is data + a pure rule now: `FUnitRef.contentId` → `units.json` `behavior` list
/// → `behaviors.json` rows, filtered by `firstMatchingBehavior`. Adding a behaviour is a data edit; this file
/// only executes the one matched action (R17/R22/R9 — the old "미결(design §4): 몬스터 AI" hardcode is gone).
/// `invariant:` exactly one action per turn — the end request is queued on every path, including no-match.
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

        const FContentRegistry* content = registry.ctx().find<FContentRegistry>();
        const FBehaviorContext context = gatherContext(registry, unit, content);

        const FBehaviorContent* rule = nullptr;
        if (content != nullptr) {
            const FUnitRef* ref = registry.try_get<FUnitRef>(unit);
            const FUnitContent* unitContent = ref != nullptr ? content->units.find(ref->contentId) : nullptr;
            if (unitContent != nullptr) {
                rule = firstMatchingBehavior(unitContent->behaviorIds, *content, context);
            } else {
                LOG_WARN("enemy turn: entity {} has no content row; it waits.", static_cast<int>(unit));
            }
        }

        // why the decision is queued before the action: FBattleLogSystem renders it before the move/skill line,
        // so the player reads "decided → did" and can answer "why did this monster act?" from the rule id (R31).
        bus->queueFrame<FBehaviorDecidedEvent>(FBehaviorDecidedEvent{
            unit,
            rule != nullptr ? rule->id : std::string{},
            rule != nullptr ? rule->action : EBehaviorAction::Wait});

        if (rule != nullptr) {
            execute(registry, unit, *rule, content, bus);
        }
        bus->queueFrame<FTurnEndRequestedEvent>(FTurnEndRequestedEvent{unit});
    }

    [[nodiscard]] std::string name() const override { return "FEnemyTurn"; }

private:
    /// Gathers the facts `FBehaviorTree.h` is allowed to see from live state.
    /// why one gather step: a new condition has to extend this list, so "what can a monster react to?" stays a
    /// single place instead of rules reaching into the registry themselves (R12/R19).
    [[nodiscard]] static FBehaviorContext gatherContext(entt::registry& registry,
                                                        entt::entity unit,
                                                        const FContentRegistry* content) {
        FBehaviorContext context;

        const FHealth* health = registry.try_get<FHealth>(unit);
        if (health != nullptr && health->max > 0.0) {
            context.selfHealthPct = health->current / health->max;
        }

        // why the FTeamEnemy tag and not "self's team": activeEnemyUnit guarantees the actor is an enemy, so a
        // same-team downed unit is exactly an FTeamEnemy + FDowned pair (design §2 `ally_downed`).
        for ([[maybe_unused]] const entt::entity ally : registry.view<FTeamEnemy, FDowned>()) {
            context.allyDowned = true;
            break;
        }

        context.livingOpponents = livingOpponents(registry, unit).size();

        const FSkillSet* skills = registry.try_get<FSkillSet>(unit);
        context.actorHasSkill = skills != nullptr && !skills->skillIds.empty();
        if (context.actorHasSkill && content != nullptr) {
            const FSkillContent* skill = content->skills.find(skills->skillIds.front());
            if (skill != nullptr) {
                context.opponentInSkillRange =
                    nearestLivingOpponentWithinRange(registry, unit, skill->range) != entt::null;
            }
        }
        return context;
    }

    static void execute(entt::registry& registry,
                        entt::entity unit,
                        const FBehaviorContent& rule,
                        const FContentRegistry* content,
                        FEventBus* bus) {
        switch (rule.action) {
        case EBehaviorAction::UseFirstSkill:
            useFirstSkill(registry, unit, content, bus);
            break;
        case EBehaviorAction::MoveTowardNearestOpponent:
            moveRelativeToNearest(registry, unit, bus, /*toward=*/true);
            break;
        case EBehaviorAction::MoveAwayFromNearestOpponent:
            moveRelativeToNearest(registry, unit, bus, /*toward=*/false);
            break;
        case EBehaviorAction::Wait:
            break;
        }
    }

    /// `use_first_skill` (design §2): the nearest opponent inside the FIRST skill's range, validated by the same
    /// `withinSkillRange` predicate the resolver checks.
    static void useFirstSkill(entt::registry& registry,
                              entt::entity unit,
                              const FContentRegistry* content,
                              FEventBus* bus) {
        const FSkillSet* skills = registry.try_get<FSkillSet>(unit);
        const FSkillContent* skill = skills != nullptr && content != nullptr && !skills->skillIds.empty()
                                         ? content->skills.find(skills->skillIds.front())
                                         : nullptr;
        if (skill == nullptr) {
            LOG_WARN("enemy turn: entity {} cannot use its first skill (missing row); it waits.", static_cast<int>(unit));
            return;
        }

        const entt::entity target = nearestLivingOpponentWithinRange(registry, unit, skill->range);
        if (target == entt::null) {
            // why no request is queued out of range: the resolver would reject it, and a rejection the rule
            // already knew about is noise (R19). why the rejection *event* is still queued: the player read
            // "decided → 첫 스킬" from FBehaviorDecidedEvent and must also read that nothing came of it — the
            // resolver's own rejection channel (FSkillRejectedEvent) is the existing place for that sentence.
            LOG_WARN("enemy turn: entity {} has no opponent within range {}; its turn is spent.",
                     static_cast<int>(unit),
                     skill->range);
            bus->queueFrame<FSkillRejectedEvent>(
                FSkillRejectedEvent{unit, skill->id, "사거리 안에 상대가 없다"});
            return;
        }

        LOG_INFO("enemy turn: entity {} uses '{}' on entity {}.",
                 static_cast<int>(unit),
                 skill->id,
                 static_cast<int>(target));
        bus->queueFrame<FSkillRequestedEvent>(FSkillRequestedEvent{unit, target, skill->id});
    }

    /// `move_toward_/move_away_from_nearest_opponent` (design §2): one cardinal step through the shared movement
    /// contract — ap/bounds/occupancy and the rejection narration are applyStep's, not this system's (R12).
    static void moveRelativeToNearest(entt::registry& registry,
                                      entt::entity unit,
                                      FEventBus* bus,
                                      bool toward) {
        FBattleState* state = registry.ctx().find<FBattleState>();
        if (state == nullptr) {
            LOG_WARN("enemy turn: entity {} cannot move (no FBattleState).", static_cast<int>(unit));
            return;
        }

        const FGridPosition* cell = registry.try_get<FGridPosition>(unit);
        const entt::entity target = nearestLivingOpponent(registry, unit);
        if (cell == nullptr || target == entt::null) {
            LOG_WARN("enemy turn: entity {} cannot move (no cell or no opponent).", static_cast<int>(unit));
            return;
        }

        const FGridPosition& targetCell = registry.get<FGridPosition>(target);
        const std::optional<FGridPosition> destination = toward ? stepToward(*cell, targetCell) : stepAway(*cell, targetCell);
        if (!destination) {
            return; // 같은 칸 — 움직일 필요가 없다
        }
        (void)applyStep(registry, unit, destination->x, destination->y, *state, bus);
    }
};

} // namespace game::gameplay
