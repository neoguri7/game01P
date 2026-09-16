#include "core/Engine.h"
#include "debug/DebugOverlay.h"
#include "ecs/systems/AnimationSystem.h"
#include "ecs/systems/CollisionSystem.h"
#include "ecs/systems/DebugPrimitiveRenderSystem.h"
#include "ecs/systems/MoveSystem.h"
#include "ecs/systems/SpriteRenderSystem.h"
#include "gameplay/GameplayServices.h"
#include <cstdlib>

namespace {

void RegisterCoreSystems(game::SystemManager& systems)
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

    RegisterCoreSystems(engine.getSystemManager());
    engine.setOverlayRenderer(game::RenderDebugOverlay);

    // Composition root owns the gameplay wiring (R21/R5): content is loaded once here, and unusable
    // content stops the boot instead of degrading into a half-working run (R19).
    if (!game::gameplay::FGameplayServices::Initialize(engine.getRegistry())) {
        engine.shutdown();
        return EXIT_FAILURE;
    }

    engine.run();

    game::gameplay::FGameplayServices::Shutdown(engine.getRegistry());
    engine.shutdown();
    return EXIT_SUCCESS;
}
