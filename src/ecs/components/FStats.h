#pragma once

namespace game {

/// A unit's attack stat. docs/design/skill-rune-equipment.md §6: 피해 = max(1, attack + power) 잠정식의
/// 왼쪽 항. 성장은 장비가 담당하므로 이 컴포넌트는 런 동안 장비 합성 결과로만 바뀐다.
struct FStats {
    int attack{0};
};

} // namespace game
