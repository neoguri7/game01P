#pragma once

#include <vector>

#include "ecs/components/FActiveStatuses.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/data/FStatusContent.h"

namespace game {

/// What one status tick at the start of the owner's turn produced.
/// docs/design/skill-rune-equipment.md §4: 화상=턴당 피해, 빙결/마비=행동 제어, 버프/디버프=스탯 증감.
struct FStatusTickResult {
    int damage{0};
    bool skipTurn{false};
};

/// 중첩 = 갱신: reapplying an already-active status refreshes its duration (never stacks a second entry).
void ApplyStatus(FActiveStatuses& active, const gameplay::FStatusContent& status);

/// Ticks every active status once, decrements durations, and expires finished effects.
/// 마비의 확률 판정(50%)은 RNG 결정 슬라이스에서 붙인다 — 지금은 control = 행동 불가로 처리한다.
[[nodiscard]] FStatusTickResult TickStatuses(
    FActiveStatuses& active,
    const std::vector<const gameplay::FStatusContent*>& definitions);

/// Sum of `stat` kind magnitudes (버프/디버프). Damage calculation adds this to the attacker's attack.
[[nodiscard]] int StatusAttackModifier(
    const FActiveStatuses& active,
    const std::vector<const gameplay::FStatusContent*>& definitions);

/// Resolves the definitions of every active status. Unknown ids are skipped; content references are
/// validated at boot, so a miss here only means a stale run-scope id.
[[nodiscard]] std::vector<const gameplay::FStatusContent*> CollectStatuses(
    const FActiveStatuses& active, const gameplay::FContentRegistry& content);

} // namespace game
