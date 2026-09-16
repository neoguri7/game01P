#pragma once

namespace game::gameplay {

/// Exclusive team tag (R11): a unit is on exactly one side of a battle.
/// why a tag instead of an enum field: adding a third side (중립 NPC) becomes a new file plus a spawn line
/// instead of an edit at every `team == ...` site (R8).
struct FTeamPlayer {};

} // namespace game::gameplay
