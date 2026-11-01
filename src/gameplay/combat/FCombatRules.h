#pragma once

#include "ecs/components/FGridPosition.h"

namespace game {

/// docs/design/skill-rune-progression.md §8: 잠정 피해 공식 `max(1, offense + power − defense)`.
/// offense/defense는 damage_type이 고른다(물리: 공격력 vs 방어력, 마법: 마법공격력 vs 마법방어력).
/// 피해 공식 최종 확정 전까지 이 함수 하나만 고치면 모든 피해 계산이 따라온다(수치 하드코딩 금지).
inline constexpr int kMinimumDamage = 1;

[[nodiscard]] int ComputeDamage(int offense, int power, int defense);

/// Grid distance used by range checks: Manhattan (칸 단위, design §4 그리드 = 12x12).
[[nodiscard]] int GridDistance(const FGridPosition& from, const FGridPosition& to);

[[nodiscard]] bool IsInRange(const FGridPosition& from, const FGridPosition& to, int range);

} // namespace game
