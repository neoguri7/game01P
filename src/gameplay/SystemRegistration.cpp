#include "gameplay/SystemRegistration.h"

#include "core/SystemManager.h"
#include "ecs/systems/AnimationSystem.h"
#include "ecs/systems/CollisionSystem.h"
#include "ecs/systems/MoveSystem.h"
#include "ecs/systems/SpriteRenderSystem.h"
#include "gameplay/tactical_d20/TacticalD20ActionEconomySystem.h"
#include "gameplay/tactical_d20/TacticalD20AttackActionResolutionSystem.h"
#include "gameplay/tactical_d20/TacticalD20CommandDragInputSystem.h"
#include "gameplay/tactical_d20/TacticalD20CommandValidationSystem.h"
#include "gameplay/tactical_d20/TacticalD20CombatLifecycleSystem.h"
#include "gameplay/tactical_d20/TacticalD20ConditionSystem.h"
#include "gameplay/tactical_d20/TacticalD20DashActionResolutionSystem.h"
#include "gameplay/tactical_d20/TacticalD20DodgeActionResolutionSystem.h"
#include "gameplay/tactical_d20/TacticalD20EnemyAiSystem.h"
#include "gameplay/tactical_d20/TacticalD20InitiativeSystem.h"
#include "gameplay/tactical_d20/TacticalD20MoveActionResolutionSystem.h"
#include "gameplay/tactical_d20/TacticalD20MovementPathValidationSystem.h"
#include "gameplay/tactical_d20/TacticalD20SetupSystem.h"
#include "gameplay/tactical_d20/TacticalD20UnitLabelSystem.h"
#include "gameplay/tactical_d20/TacticalD20WaitActionResolutionSystem.h"

namespace game {

void RegisterDefaultSystems(SystemManager& systemManager, entt::registry& registry) {
    // Update/render order is registration order.
    // TacticalD20SetupSystem runs before general systems so setup entities exist
    // before collision/render systems inspect ECS views.
    systemManager.addSystem<TacticalD20SetupSystem>();
    // Tactical D20 flow order: input state is captured by Engine::processInput.
    // First drag pass emits drop requests; validators emit validation events;
    // second drag pass consumes those events for snapback/acceptance; action
    // economy stores accepted commands; lifecycle advances combat state.
    systemManager.addSystem<TacticalD20InitiativeSystem>();
    systemManager.addSystem<TacticalD20CommandDragInputSystem>();
    systemManager.addSystem<TacticalD20MovementPathValidationSystem>();
    systemManager.addSystem<TacticalD20CommandValidationSystem>();
    systemManager.addSystem<TacticalD20CommandDragInputSystem>();
    systemManager.addSystem<TacticalD20ActionEconomySystem>();
    systemManager.addSystem<TacticalD20ConditionSystem>();
    systemManager.addSystem<TacticalD20UnitLabelSystem>();
    systemManager.addSystem<TacticalD20EnemyAiSystem>();
    systemManager.addSystem<TacticalD20MoveActionResolutionSystem>();
    systemManager.addSystem<TacticalD20DashActionResolutionSystem>();
    systemManager.addSystem<TacticalD20DodgeActionResolutionSystem>();
    systemManager.addSystem<TacticalD20AttackActionResolutionSystem>();
    systemManager.addSystem<TacticalD20WaitActionResolutionSystem>();
    systemManager.addSystem<TacticalD20CombatLifecycleSystem>();
    systemManager.addSystem<ecs::MoveSystem>();
    systemManager.addSystem<ecs::SpriteRenderSystem>();
    systemManager.addSystem<ecs::AnimationSystem>();
    systemManager.addSystem<CollisionSystem>();
    systemManager.onAllSystemsRegistered(registry);
}

} // namespace game
