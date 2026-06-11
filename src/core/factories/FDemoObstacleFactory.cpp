#include "core/factories/FDemoObstacleFactory.h"

#include "ecs/components/FCollider.h"
#include "ecs/components/FDebugPrimitive.h"
#include "ecs/components/FDemoShowcaseEntity.h"
#include "ecs/components/FGridPosition.h"
#include "ecs/components/FLayer.h"
#include "ecs/components/FPosition.h"
#include "ecs/components/FTag.h"

namespace game {

entt::entity FDemoObstacleFactory::create(entt::registry& registry) {
    return create(registry, FDemoObstacleDesc{});
}

entt::entity FDemoObstacleFactory::create(entt::registry& registry, const FDemoObstacleDesc& desc) {
    auto entity = registry.create();
    registry.emplace<FPosition>(entity, desc.x, desc.y);
    registry.emplace<FDemoShowcaseEntity>(entity);
    registry.emplace<FTag>(entity, desc.tag);
    registry.emplace<FLayer>(entity, desc.layer);
    registry.emplace<FGridPosition>(entity, desc.gridX, desc.gridY);

    auto& primitive = registry.emplace<FDebugPrimitive>(entity);
    primitive.width = desc.width;
    primitive.height = desc.height;
    primitive.fillR = desc.fillR;
    primitive.fillG = desc.fillG;
    primitive.fillB = desc.fillB;
    primitive.fillA = desc.fillA;
    primitive.outlineR = 210;
    primitive.outlineG = 210;
    primitive.outlineB = 230;

    auto& collider = registry.emplace<FCollider>(entity);
    collider.type = EColliderType::AABB;
    collider.offset = {desc.width * 0.5f, desc.height * 0.5f};
    collider.halfWidth = desc.width * 0.5f;
    collider.halfHeight = desc.height * 0.5f;
    collider.collisionTag = desc.tag;

    return entity;
}

} // namespace game
