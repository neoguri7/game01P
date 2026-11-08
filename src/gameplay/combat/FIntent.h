#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <entt/entt.hpp>

#include "ecs/components/FGridPosition.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/turn/TurnTypes.h"

namespace game::gameplay {
struct FContentRegistry;
}

namespace game {

/// WP-11 (D-16 hybrid): how a committed intent is executed on the enemy's own turn.
///  - Unit: single-target skill — the committed *unit* is tracked; the enemy approaches along the real
///    path and strikes the target's current cell. Body blocks, LoS breaks and pushes are the counterplay.
///  - Tile: area/zone/dash/boss skills — the committed *cell* is fired at from the standing cell, no
///    move. Stepping off the tile is the counterplay; moving the caster off its firing cell ("displaced")
///    makes the shot whiff too.
enum class EIntentMode : std::uint8_t { Unit, Tile };

/// The mode rule (WP-11 §모드 결정) on a skill's target/shape: aimed at a cell, or shaped as an area/dash
/// form (spread/zone/pierce), commits to a tile; everything else tracks a unit. The single definition —
/// the intent system applies it to resolved skills, the WP-14a encounter validator to authored skills.
[[nodiscard]] EIntentMode IntentModeOf(std::string_view target, std::string_view shape);

/// What a validation of the committed intent says about it (pure, derived from world state — never from
/// events, so trap deaths, status ticks, pushes and chain kills are all covered by the same rule).
enum class EIntentVerdict : std::uint8_t { Valid, Invalidated, Whiff };

/// What the turn flow should do for the acting enemy. `Command` carries the action to submit (source and
/// seq are the caller's business); a refused submit must not acknowledge it.
enum class EIntentStepKind : std::uint8_t { Fallback, Command, EndTurn };

/// Lifecycle of one enemy's committed intent. `WrapUp` means its whiff was already decided and counted;
/// the rest of the turn is an end-turn (the dodge must cost the enemy its turn).
enum class EIntentState : std::uint8_t { Declared, WrapUp };

/// One enemy's declared intent for a round (커밋). For a move intent `skillId` is empty and `aim` is the
/// planned destination.
struct FIntent {
    EIntentMode mode{EIntentMode::Unit};
    std::string skillId;             // empty = move intent (approach toward the aim cell)
    entt::entity target{entt::null}; // unit mode's committed unit; informational for tile mode
    FGridPosition aim{};             // tile mode: the committed cell / unit mode: the target's cell
    int expectedAtCommit{0};         // best-effort preview at commit time (참고값 — not the invariant)
    std::uint64_t committedRound{0};
    bool attack{false}; // a damage skill: only attack intents feed committed/tileCommitted/unitCommitted
    EIntentState state{EIntentState::Declared};
};

/// Battle-scoped intent telemetry (WP-19a consumes it via FindIntentStats). `committed` counts attack
/// intents only and always equals tileCommitted + unitCommitted; move intents are counted separately.
/// The reliability invariant is `lastExecuted.expectedAtExecution == lastExecuted.actualDamage` — the
/// commit-time preview may differ when buffs/debuffs legitimately change between commit and execution.
struct FIntentStats {
    int committed{0};
    int tileCommitted{0};
    int unitCommitted{0};
    int moveIntents{0};
    int whiffed{0};
    int invalidated{0};

    struct FLastExecuted {
        std::string skillId;
        FGridPosition aim{};
        int expectedAtCommit{0};
        int expectedAtExecution{0};
        int actualDamage{0};
        bool hit{false};
    } lastExecuted;
};

/// One accepted-but-not-yet-measured skill command. A shot settles once the action queue has drained:
/// actualDamage = Σ max(0, hpBefore − hpNow) over the hostile targets the preview expected to hit.
struct FIntentExecution {
    std::uint64_t sessionId{0};
    entt::entity enemy{entt::null};
    std::string skillId;
    FGridPosition aim{};
    int expectedAtCommit{0};
    int expectedAtExecution{0};
    bool attack{false};
    std::vector<std::pair<entt::entity, int>> targets; // hostile preview target → HP when the shot left
};

/// Round-scoped storage for every enemy's intent plus the battle telemetry. Lives in the registry context
/// (memory-only, like FCommandResults) and is session-guarded: the first commit of a new battle clears it.
struct FIntentStore {
    std::uint64_t sessionId{0};
    std::uint64_t committedRound{0};
    std::map<entt::entity, FIntent> intents;
    bool hasExecution{false};
    FIntentExecution execution;
    FIntentStats stats;
};

/// 계획 산출물: mode·skill·target/aim plus the commit-time preview.
struct FIntentPlan {
    EIntentMode mode{EIntentMode::Unit};
    std::string skillId;
    entt::entity target{entt::null};
    FGridPosition aim{};
    int expectedAtCommit{0};
    bool attack{false};
};

/// 평가 산출물: pure world-state verdict plus the exact reason string that is also logged.
struct FIntentEvaluation {
    EIntentVerdict verdict{EIntentVerdict::Valid};
    std::string reason;
};

/// 실행 단계 산출물. `whiffWrapUp` marks the closing approach move of a whiff: after it lands, the turn
/// ends (a refused submit must not acknowledge it). `reason` carries the verdict's reason for the
/// whiff-wrap-up log line (the whiff decision belongs to Take, the log to Acknowledge).
struct FIntentStep {
    EIntentStepKind kind{EIntentStepKind::Fallback};
    std::optional<FBattleAction> action; // set iff kind == Command
    bool whiffWrapUp{false};
    std::string reason;
};

/// 라운드 시작: consumes this frame's FRoundStarted and commits one intent per living enemy, in entity-id
/// ascending order (deterministic — independent of the speed tie shuffle). Leftover intents from the
/// previous round (declarer never got to execute: skipped, dead, battle flow) are settled as invalidated
/// exactly once. No bus / no round event = no-op; the same (sessionId, round) commits only once.
void CommitEnemyIntents(entt::registry& registry, const gameplay::FContentRegistry& content);

/// 계획: a pure query (no RunBrain — the BT memory belongs to the actor's real decisions). Unit-mode skills
/// are evaluated from every cell one move reaches plus the standing cell; tile-mode skills only from the
/// standing cell (a move-then-tile plan would whiff by its own rule). Ties: fewest moves → cell (y, x) →
/// entity id. Nothing to fire: a move intent toward the cell closest to a hostile; no hostile → nullopt.
[[nodiscard]] std::optional<FIntentPlan> PlanIntent(
    entt::registry& registry, entt::entity enemy, const gameplay::FContentRegistry& content);

/// 평가: pure world-state derivation (also the per-frame query WP-12 UI will call). Reasons:
/// invalidation "target dead" | "incapacitated" | "blocked cell"; whiff "blocked" | "no line of sight" |
/// "out of reach" | "displaced" | "tile empty". The unit reach check uses the real path
/// (ComputeReachableCells over walls and bodies + range + LoS), never a straight-line guess.
[[nodiscard]] FIntentEvaluation EvaluateIntent(
    entt::registry& registry, entt::entity enemy, const gameplay::FContentRegistry& content);

/// 실행: converts the acting enemy's committed intent into a step. A Command step is side-effect free —
/// AcknowledgeIntentStep commits its outcome after SubmitBattleCommand accepted it; whiff/invalidation
/// decisions without a command mutate immediately (they are decided, not deferred).
[[nodiscard]] FIntentStep TakeCommittedIntent(
    entt::registry& registry, const FTurnToken& token, const gameplay::FContentRegistry& content);

/// 실행 확정: call only after the step's command was accepted. Records the pending shot (expected damage
/// at execution + HP snapshot) for skill commands, settles a whiff's closing move, and consumes the
/// intent. A refused submit leaves the intent untouched so the same step retries next frame.
void AcknowledgeIntentStep(
    entt::registry& registry, entt::entity enemy, const FIntentStep& step,
    const gameplay::FContentRegistry& content);

/// 계측 정산: measures the pending shot once the action queue drained. Call at the very top of the turn
/// flow update, BEFORE the resolved guard — the battle-ending hit must still settle. `sessionId` is the
/// live turn session (0 = none): a shot recorded for another session is stale and is dropped, never
/// measured against a different battle's registry. Fills lastExecuted and counts a damage shot that dealt
/// 0 to hostiles as whiffed (the tile dodge).
void SettleIntentExecution(entt::registry& registry, std::uint64_t sessionId);

/// WP-19a 보고: the battle telemetry, or null when no intent was ever committed in this battle.
[[nodiscard]] const FIntentStats* FindIntentStats(const entt::registry& registry);

} // namespace game
