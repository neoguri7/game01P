#include "core/Engine.h"
#include "debug/DebugOverlay.h"
#include "debug/TurnDemo.h"
#include "ecs/systems/AnimationSystem.h"
#include "ecs/systems/CollisionSystem.h"
#include "ecs/systems/DebugPrimitiveRenderSystem.h"
#include "ecs/systems/MoveSystem.h"
#include "ecs/systems/SpriteRenderSystem.h"
#include "ecs/systems/TurnSystem.h"
#include <cstdlib>

namespace {

void RegisterSystems(game::SystemManager& systems)
{
    systems.addSystem<game::ecs::MoveSystem>();
    systems.addSystem<game::CollisionSystem>();
    systems.addSystem<game::ecs::AnimationSystem>();
    systems.addSystem<game::ecs::DebugPrimitiveRenderSystem>();
    systems.addSystem<game::ecs::SpriteRenderSystem>();
    systems.addSystem<game::TurnDemoSystem>();
    systems.addSystem<game::ecs::TurnSystem>();
}

} // namespace

int main(int argc, char* argv[])
{
    game::Engine engine;

    if (!engine.initialize("Game01P - ECS Prototype", 1280, 720)) {
        return EXIT_FAILURE;
    }

    RegisterSystems(engine.getSystemManager());
    engine.getSystemManager().onAllSystemsRegistered(engine.getRegistry());
    engine.setOverlayRenderer(game::RenderDebugOverlay);

    engine.run();

    game::ShutdownTurnDemo(engine.getRegistry());
    game::ShutdownTurnRuntime(engine.getRegistry());
    engine.shutdown();
    return EXIT_SUCCESS;
}
