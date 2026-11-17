#pragma once

#include <optional>

#include <entt/entt.hpp>

#include "gameplay/turn/TurnTypes.h"

namespace game {

/// How much of the last game state is still being shown. Gameplay owns the POD; the presentation
/// (the ecs-side BattleAnimationSystem) *assigns* it every frame — never increments it — and gameplay only
/// reads it. An absent context means "nothing to show" (headless tests, fixtures).
struct FPresentationGate {
    int busy{0};
};

/// Fail-safe clock for a stuck presentation. One writer — FBattleFlowSystem::update, once per frame —
/// accumulates while the gate stays busy and resets when it opens. A modal above the battle disables both
/// the flow and the animation system (Engine::ApplyStackSystemPolicy), so the clock freezes with them.
struct FPresentationWatch {
    float busySeconds{0.f};
    bool timedOut{false};
};

/// Derived battle phase (WP-05a). Never the source of truth: FBattleFlowSystem recomputes it every frame
/// from the registry and keeps only the last *logged* value in FBattleFlowState. The round belongs to
/// TurnController and the run phase to FDungeonRun — there is no mirror of either here.
enum class EBattlePhase { Setup, Input, AiThink, Resolving, Settling, OutcomeVictory, OutcomeDefeat, Closed };

[[nodiscard]] const char* BattlePhaseName(EBattlePhase phase);

/// How long a normal presentation may stay busy before the fail-safe opens the gate. The worst normal
/// chain (volley: staggered impacts + sky drop + one death) is ~1.9s (WP-05a 근거); twice that is 4.0s.
/// Recompute when the content changes (XCOM 2 `X2Action::IsTimedOut` precedent).
inline constexpr float kPresentationGateTimeout = 4.0f;

/// Battle-scope flow state owned by FEmberBattleState (created on enter, erased on exit — values from the
/// previous battle must not leak). `roomClearedOnEnter` marks a saved-phase continue (phase != Battle at
/// OnEnter: the room is already cleared and no turn session starts). `handoffDone` is the single once-guard
/// for the resolved-battle handoff. `lastLoggedPhase` is a log-dedup record only; nothing reads it as state.
struct FBattleFlowState {
    bool roomClearedOnEnter{false};
    bool handoffDone{false};
    EBattlePhase lastLoggedPhase{EBattlePhase::Setup};
};

/// Everything DeriveBattlePhase may look at. Collected from the registry by FBattleFlowSystem (ecs):
/// gameplay/ must not include ecs/systems headers, so the derivation stays a pure function of this POD.
struct FBattlePhaseInputs {
    std::optional<FTurnToken> currentTurn;  // TurnController snapshot
    bool currentTurnIsParty{false};         // FPartyMember on the current actor (system side)
    bool autoPlay{false};                   // FCombatRuntime
    bool pendingEmpty{true};                // FActionQueue
    bool settled{true};                     // IsPresentationSettled
    bool resolved{false};                   // FBattleOutcome
    bool victory{false};                    // FBattleOutcome
    bool roomClearedOnEnter{false};         // FBattleFlowState (saved-phase continue)
    bool handoffDone{false};                // FBattleFlowState
};

/// Fixed priority (① wins over everything, ⑥ is the fallback) — the view reaches Closed after the handoff
/// and never reports a stale fight:
///   ① handoffDone -> Closed  ② roomClearedOnEnter || resolved -> OutcomeVictory/OutcomeDefeat
///   ③ actions pending -> Resolving  ④ !settled -> Settling
///   ⑤ current turn -> Input (party, no auto-play) / AiThink  ⑥ else -> Setup
/// Pure: same inputs, same phase — it takes no registry.
[[nodiscard]] EBattlePhase DeriveBattlePhase(const FBattlePhaseInputs& inputs);

/// Level-triggered wait predicate (WP-05a): the show is over when there is no gate (headless, fixtures),
/// nothing is left to show, or the fail-safe clock has timed out.
[[nodiscard]] bool IsPresentationSettled(const entt::registry& registry);

/// Advances the fail-safe clock by one frame. Busy streaks accumulate; `busy == 0` resets the streak and
/// the latch. Crossing kPresentationGateTimeout warns once per streak — dev log plus one battle-feed line
/// — and opens the gate so the rules can continue (the review's "intended degraded behaviour").
void AdvancePresentationWatch(entt::registry& registry, float deltaTime);

/// Compare-and-record step for the derived view: logs exactly one dev line when `phase` differs from
/// FBattleFlowState::lastLoggedPhase and updates the record. Returns true when it changed — split out so
/// tests can count changes without parsing log text. No flow state -> nothing to record (false).
[[nodiscard]] bool RecordBattlePhaseChange(entt::registry& registry, EBattlePhase phase);

} // namespace game
