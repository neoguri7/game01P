#include "gameplay/SystemRegistration.h"

#include "core/SystemManager.h"
#include "ecs/systems/AnimationSystem.h"
#include "ecs/systems/CollisionSystem.h"
#include "ecs/systems/MoveSystem.h"
#include "ecs/systems/SpriteRenderSystem.h"
#include "gameplay/tactical_d20/TacticalD20SystemRegistration.h"

namespace game {

void RegisterDefaultSystems(SystemManager& systemManager, entt::registry& registry) {
    // Update/render order is registration order.
    // TacticalD20SetupSystem runs before general systems so setup entities exist
    // before collision/render systems inspect ECS views.
    RegisterTacticalD20TurnFlowSystems(systemManager);
    systemManager.addSystem<ecs::MoveSystem>();
    systemManager.addSystem<ecs::SpriteRenderSystem>();
    RegisterTacticalD20VisualSystems(systemManager);
    systemManager.addSystem<ecs::AnimationSystem>();
    systemManager.addSystem<CollisionSystem>();
    systemManager.onAllSystemsRegistered(registry);
    ValidateTacticalD20SystemOrder(systemManager, registry);
}

} // namespace game
