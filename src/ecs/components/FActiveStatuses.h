#pragma once

#include <string>
#include <vector>

namespace game {

/// One active status instance on a unit.
/// docs/design/skill-rune-progression.md §4: 중첩 = 갱신 — re-applying refreshes the duration instead of
/// adding a second entry, so one status id appears at most once per unit.
struct FStatusEffect {
    std::string statusId;
    int turnsRemaining{0};
};

struct FActiveStatuses {
    std::vector<FStatusEffect> effects;
};

} // namespace game
