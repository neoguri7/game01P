#include "gameplay/combat/FCombatRules.h"

#include <algorithm>
#include <cstdlib>

namespace game {

int ComputeDamage(int offense, int power, int defense) {
    return std::max(kMinimumDamage, offense + power - defense);
}

int ComputeCritDamage(int damage, int multiplierPercent) {
    return damage * multiplierPercent / 100;
}

int GridDistance(const FGridPosition& from, const FGridPosition& to) {
    return std::abs(from.x - to.x) + std::abs(from.y - to.y);
}

bool IsInRange(const FGridPosition& from, const FGridPosition& to, int range) {
    return GridDistance(from, to) <= std::max(0, range);
}

} // namespace game
