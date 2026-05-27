#include "core/factories/FTacticalD20CombatStateFactory.h"

#include "ecs/components/FCombatStateSetup.h"
#include "ecs/components/FTag.h"

namespace game {

entt::entity FTacticalD20CombatStateFactory::createSetupState(entt::registry& registry) {
    auto entity = registry.create();
    registry.emplace<FCombatStateSetup>(entity);
    registry.emplace<FTag>(entity, "tactical_d20_combat_state");
    return entity;
}

} // namespace game