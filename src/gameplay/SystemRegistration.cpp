#include "gameplay/SystemRegistration.h"

#include "core/SystemManager.h"
#include "ecs/systems/AnimationSystem.h"
#include "ecs/systems/CollisionSystem.h"
#include "ecs/systems/MoveSystem.h"
#include "ecs/systems/SpriteRenderSystem.h"
#include "gameplay/tactical_d20/TacticalD20SetupSystem.h"

namespace game {

void RegisterDefaultSystems(SystemManager& systemManager, entt::registry& registry) {
    // Update/render order is registration order.
    // TacticalD20SetupSystem runs before general systems so setup entities exist
    // before collision/render systems inspect ECS views.
    systemManager.addSystem<TacticalD20SetupSystem>();
    systemManager.addSystem<ecs::MoveSystem>();
    systemManager.addSystem<ecs::SpriteRenderSystem>();
    systemManager.addSystem<ecs::AnimationSystem>();
    systemManager.addSystem<CollisionSystem>();
    systemManager.onAllSystemsRegistered(registry);
}

} // namespace game
