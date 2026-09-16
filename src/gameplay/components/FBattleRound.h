#pragma once

namespace game::gameplay {

/// Round counter of the battle entity, 1-based. why a component: the battle's own state stays enumerable
/// through the registry like every other entity's, so the debug view reads it without a special service (R11).
struct FBattleRound {
    int value{1};
};

} // namespace game::gameplay
