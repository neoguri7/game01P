#include "gameplay/combat/FBattleFlow.h"

#include "core/Logger.h"
#include "gameplay/combat/FCombatTypes.h"

namespace game {

const char* BattlePhaseName(EBattlePhase phase) {
    switch (phase) {
    case EBattlePhase::Setup: return "Setup";
    case EBattlePhase::Input: return "Input";
    case EBattlePhase::AiThink: return "AiThink";
    case EBattlePhase::Resolving: return "Resolving";
    case EBattlePhase::Settling: return "Settling";
    case EBattlePhase::OutcomeVictory: return "OutcomeVictory";
    case EBattlePhase::OutcomeDefeat: return "OutcomeDefeat";
    case EBattlePhase::Closed: return "Closed";
    }
    return "Unknown";
}

bool IsPresentationSettled(const entt::registry& registry) {
    const auto* gate = registry.ctx().find<FPresentationGate>();
    if (gate == nullptr) return true;  // headless, fixtures: nothing is being shown
    if (gate->busy == 0) return true;
    const auto* watch = registry.ctx().find<FPresentationWatch>();
    return watch != nullptr && watch->timedOut;  // the fail-safe opens the gate (once per streak)
}

void AdvancePresentationWatch(entt::registry& registry, float deltaTime) {
    auto* watch = registry.ctx().find<FPresentationWatch>();
    if (watch == nullptr) return;

    const auto* gate = registry.ctx().find<FPresentationGate>();
    const int busy = gate != nullptr ? gate->busy : 0;
    if (busy == 0) {
        watch->busySeconds = 0.f;  // a streak is consecutive busy time only
        watch->timedOut = false;
        return;
    }

    watch->busySeconds += deltaTime;
    if (!watch->timedOut && watch->busySeconds > kPresentationGateTimeout) {
        watch->timedOut = true;
        LOG_WARN("[battle-flow] presentation gate busy for {:.1f}s (busy={}) - forcing the wait to end.",
                 watch->busySeconds, busy);
        if (auto* log = registry.ctx().find<FCombatLog>()) {
            log->push("The animation stalled - the battle continues.");
        }
    }
}

EBattlePhase DeriveBattlePhase(const FBattlePhaseInputs& inputs) {
    if (inputs.handoffDone) return EBattlePhase::Closed;
    if (inputs.roomClearedOnEnter || inputs.resolved) {
        return inputs.roomClearedOnEnter || inputs.victory ? EBattlePhase::OutcomeVictory : EBattlePhase::OutcomeDefeat;
    }
    if (!inputs.pendingEmpty) return EBattlePhase::Resolving;
    if (!inputs.settled) return EBattlePhase::Settling;
    if (inputs.currentTurn) {
        return inputs.currentTurnIsParty && !inputs.autoPlay ? EBattlePhase::Input : EBattlePhase::AiThink;
    }
    return EBattlePhase::Setup;
}

bool RecordBattlePhaseChange(entt::registry& registry, EBattlePhase phase) {
    auto* flow = registry.ctx().find<FBattleFlowState>();
    if (flow == nullptr) return false;
    if (flow->lastLoggedPhase == phase) return false;
    LOG_INFO("[battle-flow] phase {} -> {}", BattlePhaseName(flow->lastLoggedPhase), BattlePhaseName(phase));
    flow->lastLoggedPhase = phase;
    return true;
}

} // namespace game
