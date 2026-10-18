#include "core/AnimationLibrary.h"
#include "core/Engine.h"
#include "core/JsonLoader.h"
#include "core/Logger.h"
#include "debug/DebugOverlay.h"
#include "debug/PrototypeChecks.h"
#include "ecs/AnimationFunctions.h"
#include "ecs/components/FPosition.h"
#include "ecs/components/FSprite.h"
#include "debug/TurnDemo.h"
#include "ecs/systems/AnimationSystem.h"
#include "ecs/systems/ActionResolverSystem.h"
#include "ecs/systems/CollisionSystem.h"
#include "ecs/systems/CombatHudSystem.h"
#include "ecs/systems/EmberCombatHudSystem.h"
#include "ecs/systems/EmberGridRenderSystem.h"
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
#include "gameplay/FGameMode.h"
#include "gameplay/run/FPrototypeRun.h"
#include "gameplay/factory/FEntityFactory.h"
#include "gameplay/factory/FScene.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FDungeonOverlay.h"
#include "presentation/FUiTheme.h"

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
    systems.addSystem<game::TurnDemoSystem>();
    systems.addSystem<game::ecs::TurnSystem>();
    systems.addSystem<game::ecs::TurnFlowSystem>();
    systems.addSystem<game::ecs::ActionResolverSystem>();
    systems.addSystem<game::ecs::CombatOutcomeSystem>();
    systems.addSystem<game::ecs::Stage0BattleSystem>();
    systems.addSystem<game::ecs::GridCameraSystem>();
    systems.addSystem<game::ecs::GridSelectSystem>();
    systems.addSystem<game::ecs::GridRenderSystem>();
    systems.addSystem<game::ecs::EmberGridRenderSystem>();
    systems.addSystem<game::ecs::SpriteRenderSystem>();
    systems.addSystem<game::ecs::CombatHudSystem>();
    systems.addSystem<game::ecs::EmberCombatHudSystem>();
}

bool LoadStage0Definitions(game::FScene& scene)
{
    scene.Load();
    const auto unitDocument = game::FJsonLoader::Load("assets/data/unit.json");
    if (!unitDocument) return false;
    const auto unit = game::FEntityFactory::CreateUnit(scene, *unitDocument);
    if (!unit) {
        LOG_ERROR("Unit factory failed: {}", unit.error().message);
        return false;
    }
    const auto skillDocument = game::FJsonLoader::Load("assets/data/skill.json");
    if (!skillDocument) return false;
    const auto skill = game::FEntityFactory::CreateSkill(*skillDocument);
    if (!skill) {
        LOG_ERROR("Skill factory failed: {}", skill.error().message);
        return false;
    }
    const auto roomDocument = game::FJsonLoader::Load("assets/data/room.json");
    if (!roomDocument) return false;
    const auto room = game::FEntityFactory::CreateRoom(*roomDocument);
    if (!room) {
        LOG_ERROR("Room factory failed: {}", room.error().message);
        return false;
    }
    LOG_INFO("Stage 0 data loaded and factories created unit, skill, and room definitions.");
    return true;
}

bool AddAnimationCheckSprite(game::FScene& scene, entt::registry& registry)
{
    const auto* library = game::FAnimationLibrary::Find(registry);
    const auto clip = library ? library->findClip("animation_check") : std::nullopt;
    const auto entity = scene.CreateEntity();
    if (!clip || !entity) return false;
    scene.Emplace<FPosition>(*entity, FPosition{64.0f, 64.0f});
    scene.Emplace<game::FSprite>(*entity, game::FSprite{.width = 128.0f, .height = 128.0f});
    return game::ecs::PlayAnimation(registry, *entity, *clip);
}

} // namespace

int main(int argc, char* argv[])
{
    std::uint32_t maxFrames = 0;
    std::string screenshotPath;
    game::EGameMode mode = game::EGameMode::Ember;
    if (const char* value = SDL_getenv("GAME01P_MODE"); value && std::string_view(value) == "forest") {
        mode = game::EGameMode::Forest;
    }
    bool verifyPrototype = false;
    bool checkAnimation = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--ember") mode = game::EGameMode::Ember;
        else if (argument == "--forest") mode = game::EGameMode::Forest;
        else if (argument == "--verify-prototype") verifyPrototype = true;
        else if (argument == "--check-animation") checkAnimation = true;
        else if (argument == "--smoke-test") {
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

    const bool ember = mode == game::EGameMode::Ember;
    if (!engine.initialize(ember ? "Ember Dungeon - Game01P" : "Game01P - Forest Prototype", 1280, ember ? 800 : 720)) {
        return EXIT_FAILURE;
    }

    engine.getRegistry().ctx().emplace<game::FGameMode>(mode);
    game::FScene startupScene(engine.getRegistry(), "Stage0Startup");
    const auto shutdown = [&] {
        engine.getRegistry().ctx().erase<game::FPrototypeRun>();
        startupScene.Unload();
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
    engine.setOverlayRenderer(ember ? game::RenderDungeonOverlay : game::RenderDebugOverlay);
    if ((!ember || checkAnimation) && !LoadStage0Definitions(startupScene)) {
        shutdown();
        return EXIT_FAILURE;
    }
    if (checkAnimation && !AddAnimationCheckSprite(startupScene, engine.getRegistry())) {
        shutdown();
        return EXIT_FAILURE;
    }

    // Composition root owns the gameplay wiring (R21/R5): content is loaded once here, and unusable
    // content stops the boot instead of degrading into a half-working run (R19).
    if (!game::gameplay::FGameplayServices::Initialize(engine.getRegistry())) {
        shutdown();
        return EXIT_FAILURE;
    }
    const auto* content = engine.getRegistry().ctx().find<game::gameplay::FContentRegistry>();
    auto run = content ? game::FPrototypeRun::Load(*content) : std::nullopt;
    if (!run) {
        LOG_ERROR("Prototype room/party definition is invalid; boot stopped.");
        shutdown();
        return EXIT_FAILURE;
    }
    engine.getRegistry().ctx().emplace<game::FPrototypeRun>(std::move(*run));
    if (verifyPrototype) {
        const bool passed = game::RunPrototypeChecks(
            *content, engine.getRegistry().ctx().get<game::ai::FAiLibrary>(),
            engine.getRegistry().ctx().get<game::FPrototypeRun>());
        shutdown();
        return passed ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (ember) {
        if (!game::InitializeDungeon(engine.getRegistry()) || !game::InitializeDungeonArt(engine.getRegistry())) {
            shutdown();
            return EXIT_FAILURE;
        }
        game::InitializeDungeonUi(engine.getRegistry());
    } else {
        game::ui::ApplyTheme();
    }

    engine.applyStartupStateOverride();

    const bool succeeded = engine.run(maxFrames, screenshotPath);

    shutdown();
    return succeeded ? EXIT_SUCCESS : EXIT_FAILURE;
}
