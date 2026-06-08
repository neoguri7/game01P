#pragma once

#include <string>

namespace game {

struct FTacticalAttack {
    std::string id{"strike"};
    std::string displayName{"Strike"};
    std::string attackType{"melee"};
    int rangeFeet{5};
    int attackBonus{0};
    std::string damageDice{"1d4"};
    int damageBonus{0};
};

} // namespace game
