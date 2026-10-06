#pragma once

#include <string>
#include <vector>

namespace game {

/// One active status instance on a unit. `magnitude` is the effective value at application time
/// (definition magnitude + rune deltas, docs/design/skill-rune-progression.md §3.3): dot damage per
/// turn, paralyze action-fail chance (%), stat attack delta. 중첩 = 갱신 — re-applying refreshes the
/// duration and takes the latest magnitude instead of adding a second entry, so one status id appears
/// at most once per unit (§4).
struct FStatusEffect {
    std::string statusId;
    int turnsRemaining{0};
    int magnitude{0};
};

struct FActiveStatuses {
    std::vector<FStatusEffect> effects;
};

} // namespace game