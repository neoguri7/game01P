#include "core/AnimationLibrary.h"
#include "core/Engine.h"
#include "debug/TurnDemo.h"
#include "ecs/systems/AnimationSystem.h"
#include "ecs/systems/ActionResolverSystem.h"
#include "ecs/systems/CollisionSystem.h"
#include "ecs/systems/CombatHudSystem.h"
#include "ecs/systems/CombatOutcomeSystem.h"
#include "ecs/systems/DebugPrimitiveRenderSystem.h"
#include "ecs/systems/GridCameraSystem.h"
#include "ecs/systems/GridRenderSystem.h"
#include "ecs/systems/GridSelectSystem.h"
#include "ecs/systems/MoveSystem.h"
#include "ecs/systems/SpriteRenderSystem.h"
#include "ecs/systems/Stage0BattleSystem.h"
#include "ecs/systems/TurnFlowSystem.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/GameplayServices.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FDungeonOverlay.h"

#include <cstdlib>
#include <charconv>
#include <string_view>

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
    systems.addSystem<game::ecs::TurnFlowSystem>();
    systems.addSystem<game::ecs::ActionResolverSystem>();
    systems.addSystem<game::ecs::CombatOutcomeSystem>();
    systems.addSystem<game::ecs::Stage0BattleSystem>();
    systems.addSystem<game::ecs::GridCameraSystem>();
    systems.addSystem<game::ecs::GridSelectSystem>();
    systems.addSystem<game::ecs::GridRenderSystem>();
    systems.addSystem<game::ecs::CombatHudSystem>();
}

} // namespace

int main(int argc, char* argv[])
{
    std::uint32_t maxFrames = 0;
    std::string screenshotPath;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--smoke-test") {
            if (maxFrames == 0) maxFrames = 12;
        }
        else if (argument == "--capture" && index + 1 < argc) {
            screenshotPath = argv[++index];
            if (maxFrames == 0) maxFrames = 12;
        } else if (argument == "--frames" && index + 1 < argc) {
            const std::string_view count(argv[++index]);
            const auto result = std::from_chars(count.data(), count.data() + count.size(), maxFrames);
            if (result.ec != std::errc{} || result.ptr != count.data() + count.size() || maxFrames == 0 || maxFrames > 100000) {
                SDL_Log("--frames must be an integer between 1 and 100000.");
                return EXIT_FAILURE;
            }
        } else {
            SDL_Log("Unknown or incomplete argument: %s", argv[index]);
            return EXIT_FAILURE;
        }
    }
    game::Engine engine;

    if (!engine.initialize("Ember Dungeon - Game01P", 1280, 800)) {
        return EXIT_FAILURE;
    }

    const auto shutdown = [&] {
        game::ShutdownTurnDemo(engine.getRegistry());
        game::ShutdownTurnRuntime(engine.getRegistry());
        game::gameplay::FGameplayServices::Shutdown(engine.getRegistry());
        game::FAnimationLibrary::Shutdown(engine.getRegistry());
        engine.shutdown();
    };

    if (!game::FAnimationLibrary::Initialize(engine.getRegistry())) {
        shutdown();
        return EXIT_FAILURE;
    }

    RegisterSystems(engine.getSystemManager());
    engine.getSystemManager().onAllSystemsRegistered(engine.getRegistry());
    engine.setOverlayRenderer(game::RenderDungeonOverlay);

    // Composition root owns the gameplay wiring (R21/R5): content is loaded once here, and unusable
    // content stops the boot instead of degrading into a half-working run (R19).
    if (!game::gameplay::FGameplayServices::Initialize(engine.getRegistry())) {
        shutdown();
        return EXIT_FAILURE;
    }
    if (!game::InitializeDungeon(engine.getRegistry()) || !game::InitializeDungeonArt(engine.getRegistry())) {
        shutdown();
        return EXIT_FAILURE;
    }
    game::InitializeDungeonUi(engine.getRegistry());

    engine.applyStartupStateOverride();

    const bool succeeded = engine.run(maxFrames, screenshotPath);

    shutdown();
    return succeeded ? EXIT_SUCCESS : EXIT_FAILURE;
}
