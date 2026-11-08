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
#include "dev/FInputScript.h"
#include "dev/FRunAutopilot.h"
#include "ecs/systems/AnimationSystem.h"
#include "ecs/systems/BattleAnimationSystem.h"
#include "ecs/systems/ActionResolverSystem.h"
#include "ecs/systems/CollisionSystem.h"
#include "ecs/systems/CombatHudSystem.h"
#include "ecs/systems/EmberCombatHudSystem.h"
#include "ecs/systems/EmberGridRenderSystem.h"
#include "ecs/systems/CombatOutcomeSystem.h"
#include "ecs/systems/DebugPrimitiveRenderSystem.h"
#include "ecs/systems/FBattleFlowSystem.h"
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
#include "gameplay/meta/FMetaProgress.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FDungeonOverlay.h"
#include "presentation/FForestScene.h"
#include "presentation/FUiFrame.h"
#include "presentation/FUiTheme.h"
#include "states/FStateCatalog.h"

#include <cstdlib>
#include <charconv>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

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
    systems.addSystem<game::ecs::BattleAnimationSystem>();
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
    std::string inputScriptPath;
    game::EGameMode mode = game::EGameMode::Ember;
    if (const char* value = SDL_getenv("GAME01P_MODE"); value && std::string_view(value) == "forest") {
        mode = game::EGameMode::Forest;
    }
    bool verifyPrototype = false;
    bool checkAnimation = false;
    int windowWidth = 1280;
    int windowHeight = 0;  // follows the mode unless --window says otherwise
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
        } else if (argument == "--input-script" && index + 1 < argc) {
            inputScriptPath = argv[++index];
        } else if (argument == "--frames" && index + 1 < argc) {
            const std::string_view count(argv[++index]);
            const auto result = std::from_chars(count.data(), count.data() + count.size(), maxFrames);
            if (result.ec != std::errc{} || result.ptr != count.data() + count.size() || maxFrames == 0 || maxFrames > 100000) {
                SDL_Log("--frames must be an integer between 1 and 100000.");
                return EXIT_FAILURE;
            }
        } else if (argument == "--window" && index + 1 < argc) {
            // Window shapes other than the default, for checking the layout: --window 1920x1080
            const std::string_view size(argv[++index]);
            const auto split = size.find('x');
            const char* end = size.data() + size.size();
            const auto width = std::from_chars(size.data(), end, windowWidth);
            const auto height = split == std::string_view::npos ? width : std::from_chars(size.data() + split + 1, end, windowHeight);
            if (split == std::string_view::npos || width.ec != std::errc{} || height.ec != std::errc{} || height.ptr != end ||
                windowWidth <= 0 || windowHeight <= 0) {
                SDL_Log("--window must look like 1920x1080.");
                return EXIT_FAILURE;
            }
        } else {
            SDL_Log("Unknown or incomplete argument: %s", argv[index]);
            return EXIT_FAILURE;
        }
    }

    // [WP-00c] dev: parse the input script before the engine opens SDL; a bad script exits naming its line.
    std::optional<std::vector<game::dev::FScriptCommand>> inputScriptCommands;
    if (!inputScriptPath.empty()) {
        auto loadedScript = game::dev::LoadInputScript(inputScriptPath);
        if (!loadedScript) {
            SDL_Log("%s", loadedScript.error().message.c_str());
            return EXIT_FAILURE;
        }
        inputScriptCommands = std::move(*loadedScript);
    }

    game::Engine engine;

    const bool ember = mode == game::EGameMode::Ember;
    if (windowHeight == 0) windowHeight = ember ? 800 : 720;
    // The catalog owns this mode's state ids and factories; the engine creates no state of its own.
    engine.setStateCatalog(ember ? game::FStateCatalog::MakeEmber() : game::FStateCatalog::MakeForest());
    if (!engine.initialize(ember ? "Ember Dungeon - Game01P" : "Game01P - Forest Prototype", windowWidth, windowHeight)) {
        return EXIT_FAILURE;
    }
    if (inputScriptCommands) {
        auto inputScript = std::make_shared<game::dev::FInputScript>(std::move(*inputScriptCommands));
        engine.setInputInjector(
            [inputScript](std::uint32_t frame, SDL_WindowID window) {
                game::dev::FInputScriptFrame scripted = inputScript->inject(frame, window);
                game::FInjectedFrame injected;
                injected.captures = std::move(scripted.captures);
                injected.quit = scripted.quit;
                injected.expectStates.reserve(scripted.expects.size());
                for (const game::dev::FScriptCommand& command : scripted.expects) {
                    injected.expectStates.emplace_back(command.line, command.path);
                }
                return injected;
            },
            /*fixedTimestep=*/true);
    }

    engine.getRegistry().ctx().emplace<game::FGameMode>(mode);
    game::FScene startupScene(engine.getRegistry(), "Stage0Startup");
    const auto shutdown = [&] {
        engine.getRegistry().ctx().erase<game::FPrototypeRun>();
        startupScene.Unload();
        game::ShutdownTurnDemo(engine.getRegistry());
        game::ShutdownTurnRuntime(engine.getRegistry());
        // [WP-20] no-op when meta was never initialized (forest/verify shutdown paths).
        game::ShutdownMetaProgress(engine.getRegistry());
        game::gameplay::FGameplayServices::Shutdown(engine.getRegistry());
        game::FAnimationLibrary::Shutdown(engine.getRegistry());
        engine.shutdown();
    };

    if (!game::FAnimationLibrary::Initialize(engine.getRegistry())) {
        shutdown();
        return EXIT_FAILURE;
    }

    RegisterSystems(engine.getSystemManager());
    // System/service registration anchors for later work packages. Systems added after onAllSystemsRegistered()
    // would miss onRegister(), and before initialize() the registry ctx is empty; edit only under your anchor.
    // [WP-03] combat refresh
    // [WP-05a] battle flow
    engine.getSystemManager().addSystem<game::ecs::FBattleFlowSystem>();
    // [WP-11] intent system
    // [WP-15] boss goals
    // [WP-20] meta save: the service initializes inside the ember block below (it needs InitializeDungeon's checkpoint;
    // the drawing block lives in FTitleMenuView).
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
        if (!game::InitializeDungeon(engine.getRegistry()) || !game::InitializeDungeonArt(engine.getRegistry()) ||
            !game::InitializeForestScene(engine.getRegistry()) || !game::InitializeBattleAnim(engine.getRegistry()) ||
            !game::ui::InitializeUiFrame(engine.getRegistry())) {
            shutdown();
            return EXIT_FAILURE;
        }
        // [WP-20] meta save: shipped-content fail-fast; resolves/loads/imports after the checkpoint exists.
        if (!game::InitializeMetaProgress(engine.getRegistry())) {
            shutdown();
            return EXIT_FAILURE;
        }
        game::InitializeDungeonUi(engine.getRegistry());
    } else {
        game::ui::ApplyTheme();
    }

    engine.applyStartupStateOverride();

    // [WP-00c] dev autopilot: plays the non-battle run phases through the same model functions the run views
    // call. Battles stay with GAME01P_AUTOPLAY=1. Unset env = zero behaviour change.
    std::shared_ptr<game::dev::FRunAutopilot> autopilot;
    if (const char* policyText = std::getenv(game::dev::kAutopilotEnv); policyText != nullptr && policyText[0] != '\0') {
        if (!ember) {
            SDL_Log("GAME01P_AUTOPILOT is ember-only; ignoring it in forest mode.");
        } else {
            const bool defaultPolicy = std::string_view(policyText) == "1" || std::string_view(policyText) == "default";
            const std::string_view policyValue =
                defaultPolicy ? game::dev::kDefaultAutopilotPolicy : std::string_view(policyText);
            const auto policy = game::dev::ParseAutopilotPolicy(policyValue);
            if (!policy) {
                SDL_Log("GAME01P_AUTOPILOT: %s", policy.error().c_str());
                shutdown();
                return EXIT_FAILURE;
            }
            int maxRooms = 0;
            if (const char* roomsText = std::getenv(game::dev::kAutopilotMaxRoomsEnv);
                roomsText != nullptr && roomsText[0] != '\0') {
                const std::string_view rooms(roomsText);
                const auto roomsResult = std::from_chars(rooms.data(), rooms.data() + rooms.size(), maxRooms);
                if (roomsResult.ec != std::errc{} || roomsResult.ptr != rooms.data() + rooms.size() || maxRooms < 1) {
                    SDL_Log("GAME01P_AUTOPILOT_MAX_ROOMS must be an integer >= 1.");
                    shutdown();
                    return EXIT_FAILURE;
                }
            }
            autopilot = std::make_shared<game::dev::FRunAutopilot>(*policy, maxRooms);
            engine.setFrameDriver([autopilot](entt::registry& registry, std::string_view topStateId) {
                switch (autopilot->tick(registry, topStateId)) {
                case game::dev::EAutopilotStatus::Running:
                    return game::EFrameDriverResult::Continue;
                case game::dev::EAutopilotStatus::Finished:
                    return game::EFrameDriverResult::Stop;
                case game::dev::EAutopilotStatus::Stuck:
                    return game::EFrameDriverResult::Fail;
                }
                return game::EFrameDriverResult::Continue;
            });
            LOG_INFO("[autopilot] policy='{}' max_rooms={}", policyValue, maxRooms);
            if (const char* autoplay = std::getenv("GAME01P_AUTOPLAY");
                autoplay == nullptr || std::string_view(autoplay) != "1") {
                LOG_WARN("[autopilot] GAME01P_AUTOPLAY is not 1: battles will wait for input");
            }
        }
    }

    const bool succeeded = engine.run(maxFrames, screenshotPath);

    shutdown();
    return succeeded ? EXIT_SUCCESS : EXIT_FAILURE;
}
