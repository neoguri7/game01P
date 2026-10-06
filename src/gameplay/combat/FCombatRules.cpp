#include "gameplay/combat/FCombatRules.h"

#include <algorithm>
#include <cstdlib>

namespace game {

int ComputeDamage(int attack, int power) {
    return std::max(kMinimumDamage, attack + power);
}

int GridDistance(const FGridPosition& from, const FGridPosition& to) {
    return std::abs(from.x - to.x) + std::abs(from.y - to.y);
}

bool IsInRange(const FGridPosition& from, const FGridPosition& to, int range) {
    return GridDistance(from, to) <= std::max(0, range);
}

} // namespace game
