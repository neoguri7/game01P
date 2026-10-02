#pragma once

#include <string>
#include <vector>

#include "gameplay/factory/FEntityDefinitions.h"
#include "gameplay/grid/FGridTypes.h"

namespace game {

struct FUnitPlacement {
    std::string id;
    std::string displayName;
    EUnitFaction faction{EUnitFaction::Party};
    FGridPosition cell;
};

struct FBattlefieldData {
    FGridMetrics metrics;
    std::vector<FUnitPlacement> units;
};

} // namespace game
