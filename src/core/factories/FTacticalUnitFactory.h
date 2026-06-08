#pragma once

#include "ecs/components/FAbilityScores.h"

#include <entt/entt.hpp>
#include <string>

namespace game {

struct FTacticalUnitSpawn {
    std::string id;
    std::string team;
    std::string displayName;
    int spawnOrder{0};
    int tileX{0};
    int tileY{0};
    int maxHp{1};
    int armorClass{10};
    int speedFeet{30};
    int initiativeBonus{0};
    FAbilityScores abilities{};
    std::string attackId{"strike"};
    std::string attackDisplayName{"Strike"};
    std::string attackType{"melee"};
    int attackRangeFeet{5};
    int attackBonus{0};
    std::string damageDice{"1d4"};
    int damageBonus{0};
    bool playerControlled{false};
};

struct FTacticalUnitFactory {
    static entt::entity create(entt::registry& registry, const FTacticalUnitSpawn& spawn);
};

} // namespace game
