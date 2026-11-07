#pragma once

#include <vector>

#include "ecs/components/FActiveStatuses.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/data/FStatusContent.h"

namespace game {

/// D-12 (WP-13): one lost self-turn per decided control. A rule constant, not a data field — `lost_turns`
/// stays out of the schema, and the AI values one decided loss with this.
inline constexpr int kControlLostTurns = 1;

/// What one status tick at the start of the owner's turn produced.
/// docs/design/skill-rune-progression.md §4: 화상=턴당 피해, 빙결/마비=행동 제어, 버프/디버프=스탯 증감.
struct FStatusTickResult {
    int damage{0};
    bool skipTurn{false};
};

/// What a status is worth once applied: the definition plus the rune deltas, clamped the way ApplyStatus
/// stores it. One definition shared by ApplyStatus and by anything that wants to predict an application
/// (the enemy AI values a status by what it will really do, not by what the data row says).
[[nodiscard]] int EffectiveStatusDuration(const gameplay::FStatusContent& status, int durationDelta = 0);
[[nodiscard]] int EffectiveStatusMagnitude(const gameplay::FStatusContent& status, int magnitudeDelta = 0);

/// 중첩 = 갱신: reapplying refreshes the duration (never stacks a second entry) and takes the latest
/// magnitude. Rune deltas (§3.3) arrive from FResolvedStatus; this is the only place they are folded
/// into the status definition values. Deterministic — no RNG; the rolling application is TryApplyStatus.
void ApplyStatus(
    FActiveStatuses& active,
    const gameplay::FStatusContent& status,
    int durationDelta = 0,
    int magnitudeDelta = 0);

/// D-12 control timing: this status decides its action loss when applied (`mode == on_apply`) instead of
/// rolling at the owner's turn start. One predicate for the rules, the AI and the intent layer.
[[nodiscard]] bool IsOnApplyControl(const gameplay::FStatusContent& status);

/// Chance (%) that applying this control succeeds (D-12 on_apply): clamp(magnitude + 룬 delta, 0, cap).
/// 0 = 부여 불가 — an on_apply status must never turn its "0 = always" tick convention into an
/// always-apply ("0%가 100%가 되면 안 된다").
[[nodiscard]] int ControlApplyChance(const gameplay::FStatusContent& status, int magnitudeDelta = 0);

/// Chance (%) that a turn_start control denies the upcoming turn: `magnitude <= 0` = always (100),
/// otherwise the stored effective magnitude clamped to the author's cap.
[[nodiscard]] int ControlTickChance(int effectiveMagnitude, int chanceCap);

/// Applies `status` with 중첩=갱신. An on_apply control rolls ControlApplyChance exactly once on the
/// battle RNG: a failed roll applies nothing and returns false; a success stores the decided one-turn
/// loss as FStatusEffect::skipPending. Every other status delegates to ApplyStatus and consumes no RNG.
[[nodiscard]] bool TryApplyStatus(
    FActiveStatuses& active,
    const gameplay::FStatusContent& status,
    int durationDelta,
    int magnitudeDelta,
    FBattleRng& rng);

/// Ticks every active status once, decrements durations, and expires finished effects.
/// `on_apply` controls consume a decided skipPending (no roll); `turn_start` controls roll
/// ControlTickChance on the battle RNG (빙결은 magnitude 0 = 항상 행동 불가). The roll must be
/// reproducible for auto-battle verification, so the RNG is passed in instead of owned here.
[[nodiscard]] FStatusTickResult TickStatuses(
    FActiveStatuses& active,
    const std::vector<const gameplay::FStatusContent*>& definitions,
    FBattleRng& rng);

/// True when the unit must lose its current turn — the decided-loss view WP-11 consumes to invalidate
/// intents: an on_apply control with an unconsumed skipPending, or a turn_start control whose tick
/// chance is certain (freeze). A probabilistic turn_start control (legacy paralyze) is not resolved
/// here — TickStatuses owns that roll, so callers must not pre-empt it.
[[nodiscard]] bool IsTurnSkipped(
    const FActiveStatuses& active,
    const std::vector<const gameplay::FStatusContent*>& definitions);

/// Sum of `stat` kind magnitudes (버프/디버프). Damage calculation adds this to the attacker's attack.
[[nodiscard]] int StatusAttackModifier(
    const FActiveStatuses& active,
    const std::vector<const gameplay::FStatusContent*>& definitions);

/// The `speed_pct` fold (WP-13 C2): `max(1, round(baseSpeed × Π(pct/100)))` — one factor per status id
/// (중첩=갱신 keeps one entry per id), 100 = neutral. RefreshCombatSheet ④ is the only consumer, so an
/// expired status restores the base on the next refresh.
[[nodiscard]] int StatusSpeedValue(
    int baseSpeed,
    const FActiveStatuses& active,
    const std::vector<const gameplay::FStatusContent*>& definitions);

/// Resolves the definitions of every active status. Unknown ids are skipped; content references are
/// validated at boot, so a miss here only means a stale run-scope id.
[[nodiscard]] std::vector<const gameplay::FStatusContent*> CollectStatuses(
    const FActiveStatuses& active, const gameplay::FContentRegistry& content);

} // namespace game
