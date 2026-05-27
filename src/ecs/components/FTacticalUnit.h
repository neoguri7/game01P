#pragma once

#include <string>
#include <vector>

namespace game {

struct FTacticalUnit {
    std::string id;
    std::string team;
    std::string displayName;
    int tileX{0};
    int tileY{0};
    int currentHp{1};
    int maxHp{1};
    int armorClass{10};
    int speedFeet{30};
    std::vector<std::string> weaponProficiencies;
    std::vector<std::string> savingThrowProficiencies;
    std::vector<std::string> skillProficiencies;
    std::vector<std::string> actions;
};

} // namespace game