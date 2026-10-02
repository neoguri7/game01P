#pragma once

#include <string>

namespace game {

enum class EUnitFaction {
    Party,
    Enemy
};

struct FUnitDefinition {
    std::string id;
    std::string displayName;
    EUnitFaction faction{EUnitFaction::Party};
};

struct FUnitData {
    FUnitDefinition definition;
};

struct FSkillDefinition {
    std::string id;
    std::string displayName;
};

struct FRoomDefinition {
    std::string id;
    std::string displayName;
    std::string roomType;
};

struct FEquipmentDefinition {
    std::string id;
    std::string displayName;
    std::string slot;
};

} // namespace game
