#pragma once

namespace game {

/// A unit's attack stat. docs/design/skill-rune-progression.md §8: 피해 = max(1, attack + power) 잠정식의
/// 왼쪽 항. 성장은 런 성장(노드·강화 물질)이 담당하므로 이 컴포넌트는 런 동안 그 합산 결과로만 바뀐다.
struct FStats {
    int attack{0};
};

} // namespace game
