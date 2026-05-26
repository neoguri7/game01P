#include "gameplay/SystemRegistration.h"

#include "core/SystemManager.h"
#include "ecs/systems/AnimationSystem.h"
#include "ecs/systems/CollisionSystem.h"
#include "ecs/systems/MoveSystem.h"
#include "ecs/systems/SpriteRenderSystem.h"

namespace game {

void RegisterDefaultSystems(SystemManager& systemManager, entt::registry& registry) {
    // Update/render order is registration order.
    systemManager.addSystem<ecs::MoveSystem>();
    systemManager.addSystem<ecs::SpriteRenderSystem>();
    systemManager.addSystem<ecs::AnimationSystem>();
    systemManager.addSystem<CollisionSystem>();
    systemManager.onAllSystemsRegistered(registry);
}

} // namespace game
