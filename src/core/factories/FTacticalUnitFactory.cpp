#include "core/factories/FTacticalUnitFactory.h"

#include "ecs/components/FAiControlledTacticalUnit.h"
#include "ecs/components/FArmorClass.h"
#include "ecs/components/FGridPosition.h"
#include "ecs/components/FHitPoints.h"
#include "ecs/components/FInitiativeBonus.h"
#include "ecs/components/FPlayerControlledTacticalUnit.h"
#include "ecs/components/FSpeed.h"
#include "ecs/components/FTag.h"
#include "ecs/components/FTacticalAttack.h"
#include "ecs/components/FTacticalUnit.h"
#include "ecs/components/FTurnBudget.h"
#include "ecs/components/FTurnResources.h"

namespace game {

entt::entity FTacticalUnitFactory::create(entt::registry& registry, const FTacticalUnitSpawn& spawn) {
    auto entity = registry.create();
    registry.emplace<FTacticalUnit>(entity, spawn.id, spawn.team, spawn.displayName, spawn.spawnOrder);
    registry.emplace<FGridPosition>(entity, spawn.tileX, spawn.tileY);
    registry.emplace<FHitPoints>(entity, spawn.maxHp, spawn.maxHp);
    registry.emplace<FArmorClass>(entity, spawn.armorClass);
    registry.emplace<FSpeed>(entity, spawn.speedFeet);
    registry.emplace<FInitiativeBonus>(entity, spawn.initiativeBonus);
    registry.emplace<FAbilityScores>(entity, spawn.abilities);
    registry.emplace<FTacticalAttack>(entity,
        spawn.attackId,
        spawn.attackDisplayName,
        spawn.attackType,
        spawn.attackRangeFeet,
        spawn.attackBonus,
        spawn.damageDice,
        spawn.damageBonus);
    registry.emplace<FTurnBudget>(entity, spawn.speedFeet, spawn.speedFeet);
    registry.emplace<FTurnResources>(entity, true, false, true);
    registry.emplace<FTag>(entity, spawn.id);
    if (spawn.playerControlled) registry.emplace<FPlayerControlledTacticalUnit>(entity);
    else registry.emplace<FAiControlledTacticalUnit>(entity);
    return entity;
}

} // namespace game
