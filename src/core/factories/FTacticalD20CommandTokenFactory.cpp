#include "core/factories/FTacticalD20CommandTokenFactory.h"

#include "ecs/components/FCommandToken.h"
#include "ecs/components/FCollider.h"
#include "ecs/components/FLayer.h"
#include "ecs/components/FPosition.h"
#include "ecs/components/FTag.h"

namespace game {

entt::entity FTacticalD20CommandTokenFactory::create(entt::registry& registry, const std::string& id, const std::string& displayName, int trayIndex) {
    auto entity = registry.create();
    registry.emplace<FCommandToken>(entity, id, displayName);
    registry.emplace<FPosition>(entity, 640.f + static_cast<float>(trayIndex) * 88.f, 96.f);
    registry.emplace<FCollider>(entity, EColliderType::AABB, glm::vec2{0.f, 0.f}, 36.f, 18.f, 8, "tactical_command_token");
    registry.emplace<FTag>(entity, "command_" + id);
    registry.emplace<FLayer>(entity, 20);
    return entity;
}

} // namespace game