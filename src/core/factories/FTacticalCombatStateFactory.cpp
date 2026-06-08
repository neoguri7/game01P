#include "core/factories/FTacticalCombatStateFactory.h"

#include "ecs/components/FCombatStateSetup.h"
#include "ecs/components/FTag.h"
#include "ecs/components/FTacticalTurnOrder.h"

namespace game {

entt::entity FTacticalCombatStateFactory::createSetupState(entt::registry& registry) {
    auto entity = registry.create();
    registry.emplace<FCombatStateSetup>(entity);
    registry.emplace<FTacticalTurnOrder>(entity);
    registry.emplace<FTag>(entity, "tactical_combat_state");
    return entity;
}

} // namespace game
