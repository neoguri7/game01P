#pragma once

#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "ecs/components/FSkillLoadout.h"
#include "gameplay/factory/FEntityDefinitions.h"
#include "gameplay/grid/FGridTypes.h"
#include "gameplay/grid/FGridTerrain.h"

namespace game {

/// One unit as placed on a battlefield. `unitTemplateId` names the content unit whose
/// stats/AP/speed the combat setup copies; `skills` overrides the unit's default skills (and carries the
/// run-scope runes for the first playable battles). `entity` is filled by FEntityFactory and is the
/// handle combat components are attached to.
struct FUnitPlacement {
    std::string id;
    std::string displayName;
    EUnitFaction faction{EUnitFaction::Party};
    FGridPosition cell;
    entt::entity entity{entt::null};
    std::string unitTemplateId;
    std::vector<FSkillSlot> skills;
};

struct FBattlefieldData {
    FGridMetrics metrics;
    std::vector<FUnitPlacement> units;
    FGridTerrain terrain;
};

} // namespace game
