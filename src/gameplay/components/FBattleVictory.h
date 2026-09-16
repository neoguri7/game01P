#pragma once

namespace game::gameplay {

/// Exclusive battle phase tag (R11). Transitions: docs/design/battle-state-transitions.md.
/// why tags instead of a phase enum: an observer asks "which phase" by component, so a new phase is a new
/// file plus a transition, not a switch statement in every reader (R8).
struct FBattleVictory {};

} // namespace game::gameplay
