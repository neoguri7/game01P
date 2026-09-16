#pragma once

namespace game::gameplay {

/// Marks the unit whose turn is open (design §4: 턴제, 전원 직접 조작). why a tag on the unit instead of a
/// stored entity id in FBattleState: a stored id can dangle when a unit goes down, while a tag cannot (R11).
struct FTurnActive {};

} // namespace game::gameplay
