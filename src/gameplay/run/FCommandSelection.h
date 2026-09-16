#pragma once

#include <cstddef>

namespace game::gameplay {

/// The player's in-progress command for the active unit: which skill row, which opponent.
/// why indices and not ids: the lists are rebuilt every frame from live entities, so a stored id could point
/// at a unit that went down; indices are clamped against the rebuilt list instead (R11: no dangling state).
/// why a service: selection is between-frames UI state, and it is not part of what a unit *is* — putting it on
/// the entity would let it leak across turns (R12).
struct FCommandSelection {
    std::size_t skillIndex{0};
    std::size_t targetIndex{0};
};

} // namespace game::gameplay
