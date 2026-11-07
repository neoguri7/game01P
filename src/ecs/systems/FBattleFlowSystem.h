#pragma once

#include <string>

#include <entt/entt.hpp>

#include "ecs/components/FFaction.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/combat/FBattleFlow.h"
#include "gameplay/combat/FCombatTypes.h"

namespace game::ecs {

/// Collects the derivation inputs from the registry (WP-05a §3). The gameplay side cannot do this — it
/// must not include ecs/systems headers — so the ecs side reads FTurnRuntime/FPartyMember/FCombatRuntime/
/// FActionQueue/FBattleOutcome/FBattleFlowState and hands over a plain POD. Missing contexts fall back to
/// the "nothing is running" defaults (no turn, no actions, settled, unresolved).
[[nodiscard]] inline FBattlePhaseInputs CollectBattlePhaseInputs(const entt::registry& registry) {
    FBattlePhaseInputs inputs;
    if (const auto* runtime = registry.ctx().find<FTurnRuntime>()) {
        const FTurnSnapshot snapshot = runtime->controller.snapshot();
        inputs.currentTurn = snapshot.currentTurn;
        if (snapshot.currentTurn) {
            inputs.currentTurnIsParty = registry.all_of<FPartyMember>(snapshot.currentTurn->actor);
        }
    }
    if (const auto* combat = registry.ctx().find<FCombatRuntime>()) inputs.autoPlay = combat->autoPlay;
    if (const auto* queue = registry.ctx().find<FActionQueue>()) inputs.pendingEmpty = queue->pending.empty();
    inputs.settled = IsPresentationSettled(registry);
    if (const auto* outcome = registry.ctx().find<FBattleOutcome>()) {
        inputs.resolved = outcome->resolved;
        inputs.victory = outcome->victory;
    }
    if (const auto* flow = registry.ctx().find<FBattleFlowState>()) {
        inputs.roomClearedOnEnter = flow->roomClearedOnEnter;
        inputs.handoffDone = flow->handoffDone;
    }
    return inputs;
}

/// The battle-flow view driver (WP-05a §3), registered to update last in the frame. It advances the
/// fail-safe clock exactly once (single writer) and records one dev-log line per derived phase change.
/// It controls nothing: the gate's consumers are TurnSystem (FEndTurn deferral) and the battle-state
/// handoff. Order note: the gate is assigned by BattleAnimationSystem (57) and this system runs last, so
/// TurnSystem (54) and TurnFlowSystem (55) see the same settled value within one frame (<= 1 frame lag).
struct FBattleFlowSystem {
    void update(entt::registry& registry, float deltaTime) {
        AdvancePresentationWatch(registry, deltaTime);
        (void)RecordBattlePhaseChange(registry, DeriveBattlePhase(CollectBattlePhaseInputs(registry)));
    }

    [[nodiscard]] std::string name() const { return "BattleFlowSystem"; }
};

} // namespace game::ecs
