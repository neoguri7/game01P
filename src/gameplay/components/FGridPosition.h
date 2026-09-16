#pragma once

namespace game::gameplay {

/// Cell coordinates on the battle grid (design §4 확정: 그리드 8x8, 사거리는 칸 단위).
/// `invariant:` every writer validates against FBattleState::inBounds first (move system, factory), so a
/// unit can never sit outside the authored grid.
struct FGridPosition {
    int x{0};
    int y{0};
};

} // namespace game::gameplay
