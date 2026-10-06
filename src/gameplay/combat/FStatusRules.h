#pragma once

#include <vector>

#include "ecs/components/FActiveStatuses.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/data/FStatusContent.h"

namespace game {

/// What one status tick at the start of the owner's turn produced.
/// docs/design/skill-rune-progression.md §4: 화상=턴당 피해, 빙결/마비=행동 제어, 버프/디버프=스탯 증감.
struct FStatusTickResult {
    int damage{0};
    bool skipTurn{false};
};

/// 중첩 = 갱신: reapplying refreshes the duration (never stacks a second entry) and takes the latest
/// magnitude. Rune deltas (§3.3) arrive from FResolvedStatus; this is the only place they are folded
/// into the status definition values.
void ApplyStatus(
    FActiveStatuses& active,
    const gameplay::FStatusContent& status,
    int durationDelta = 0,
    int magnitudeDelta = 0);

/// Ticks every active status once, decrements durations, and expires finished effects.
/// Control statuses roll their effective magnitude as an action-fail chance (빙결은 magnitude 0 = 항상
/// 행동 불가) on the battle RNG; the roll must be reproducible for auto-battle verification, so the RNG
/// is passed in instead of owned here.
[[nodiscard]] FStatusTickResult TickStatuses(
    FActiveStatuses& active,
    const std::vector<const gameplay::FStatusContent*>& definitions,
    FBattleRng& rng);

/// Sum of `stat` kind magnitudes (버프/디버프). Damage calculation adds this to the attacker's attack.
[[nodiscard]] int StatusAttackModifier(
    const FActiveStatuses& active,
    const std::vector<const gameplay::FStatusContent*>& definitions);

/// Resolves the definitions of every active status. Unknown ids are skipped; content references are
/// validated at boot, so a miss here only means a stale run-scope id.
[[nodiscard]] std::vector<const gameplay::FStatusContent*> CollectStatuses(
    const FActiveStatuses& active, const gameplay::FContentRegistry& content);

} // namespace game
