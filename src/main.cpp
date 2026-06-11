#include "core/Engine.h"
#include "debug/DebugOverlay.h"
#include "debug/DemoBootstrap.h"
#include "ecs/systems/AnimationSystem.h"
#include "ecs/systems/CollisionSystem.h"
#include "ecs/systems/DebugPrimitiveRenderSystem.h"
#include "ecs/systems/MoveSystem.h"
#include "ecs/systems/SpriteRenderSystem.h"
#include <cstdlib>

namespace {

void RegisterCoreDemoSystems(game::SystemManager& systems)
{
    systems.addSystem<game::ecs::MoveSystem>();
    systems.addSystem<game::CollisionSystem>();
    systems.addSystem<game::ecs::AnimationSystem>();
    systems.addSystem<game::ecs::DebugPrimitiveRenderSystem>();
    systems.addSystem<game::ecs::SpriteRenderSystem>();
}

} // namespace

int main(int argc, char* argv[])
{
    game::Engine engine;

    if (!engine.initialize("Game01P - ECS Prototype", 1280, 720)) {
        return EXIT_FAILURE;
    }

    RegisterCoreDemoSystems(engine.getSystemManager());
    game::BootstrapDemoScene(engine.getRegistry());
    engine.setOverlayRenderer(game::RenderDebugOverlay);

    engine.run();

    engine.shutdown();
    return EXIT_SUCCESS;
}
