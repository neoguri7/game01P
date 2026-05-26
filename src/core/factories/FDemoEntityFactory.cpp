#include "core/factories/FDemoEntityFactory.h"

#include "ecs/components/FPosition.h"
#include "ecs/components/FTag.h"
#include "ecs/components/FVelocity.h"

namespace game {

entt::entity FDemoEntityFactory::create(entt::registry& registry) {
    auto entity = registry.create();
    registry.emplace<FPosition>(entity, 200.f, 200.f);
    registry.emplace<FVelocity>(entity, 50.f, -30.f);
    registry.emplace<FTag>(entity, "demo");
    return entity;
}

} // namespace game
