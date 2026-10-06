#pragma once

namespace game {

/// A unit's combat stats (docs/design/skill-rune-progression.md §1.1): 공격력/마법공격력은 피해
/// 공식의 공격 항, 방어력/마법방어력은 감소 항이다. hp는 FHealth, 속도는 FSpeed가 가진다.
/// 성장은 런 성장(노드·강화 물질)이 담당하므로 이 컴포넌트는 런 동안 그 합산 결과로만 바뀐다.
struct FStats {
    int attack{0};       // 공격력 — 물리 피해의 공격 항
    int magicAttack{0};  // 마법공격력 — 마법 피해의 공격 항, 힐 회복량
    int defense{0};      // 방어력 — 물리 피해 감소
    int magicDefense{0}; // 마법방어력 — 마법 피해 감소
};

} // namespace game
