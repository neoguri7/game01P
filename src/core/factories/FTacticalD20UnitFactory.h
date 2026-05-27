#pragma once

#include "ecs/components/FAbilityScores.h"
#include <entt/entt.hpp>
#include <string>
#include <vector>

namespace game {

struct FTacticalD20UnitSpawn {
    std::string id;
    std::string team;
    std::string displayName;
    int tileX{0};
    int tileY{0};
    int maxHp{1};
    int armorClass{10};
    int speedFeet{30};
    FAbilityScores abilities{};
    std::vector<std::string> weaponProficiencies;
    std::vector<std::string> savingThrowProficiencies;
    std::vector<std::string> skillProficiencies;
    std::vector<std::string> actions;
};

struct FTacticalD20UnitFactory {
    static entt::entity create(entt::registry& registry, const FTacticalD20UnitSpawn& spawn);
};

} // namespace game