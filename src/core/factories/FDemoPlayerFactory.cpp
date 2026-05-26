#include "core/factories/FDemoPlayerFactory.h"

#include "ecs/components/FLayer.h"
#include "ecs/components/FPosition.h"
#include "ecs/components/FSprite.h"
#include "ecs/components/FTag.h"
#include "ecs/components/FVelocity.h"

namespace game {

entt::entity FDemoPlayerFactory::create(entt::registry& registry) {
    auto entity = registry.create();
    registry.emplace<FPosition>(entity, 100.f, 100.f);
    registry.emplace<FVelocity>(entity, 80.f, -50.f);
    registry.emplace<FSprite>(entity, "assets/player.png");
    registry.emplace<FTag>(entity, "demo_player");
    registry.emplace<FLayer>(entity, 10);
    return entity;
}

} // namespace game
