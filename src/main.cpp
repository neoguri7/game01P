#include "core/AnimationLibrary.h"
#include "core/Engine.h"
#include "core/JsonLoader.h"
#include "core/Logger.h"
#include "debug/DebugOverlay.h"
#include "debug/TurnDemo.h"
#include "ecs/AnimationFunctions.h"
#include "ecs/components/FPosition.h"
#include "ecs/components/FSprite.h"
#include "ecs/systems/AnimationSystem.h"
#include "ecs/systems/CollisionSystem.h"
#include "ecs/systems/DebugPrimitiveRenderSystem.h"
#include "ecs/systems/GridCameraSystem.h"
#include "ecs/systems/GridRenderSystem.h"
#include "ecs/systems/GridSelectSystem.h"
#include "ecs/systems/MoveSystem.h"
#include "ecs/systems/SpriteRenderSystem.h"
#include "ecs/systems/Stage0BattleSystem.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/factory/FEntityFactory.h"
#include "gameplay/factory/FScene.h"
#include "gameplay/GameplayServices.h"

#include <cstdlib>
#include <optional>

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
    systems.addSystem<game::ecs::Stage0BattleSystem>();
    systems.addSystem<game::ecs::GridCameraSystem>();
    systems.addSystem<game::ecs::GridSelectSystem>();
    systems.addSystem<game::ecs::GridRenderSystem>();
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

    const auto equipmentDocument = game::FJsonLoader::Load("assets/data/equipment.json");
    if (!equipmentDocument) return false;
    const auto equipment = game::FEntityFactory::CreateEquipment(*equipmentDocument);
    if (!equipment) {
        LOG_ERROR("Equipment factory failed: {}", equipment.error().message);
        return false;
    }

    LOG_INFO("Stage 0 data loaded and factories created unit, skill, room, and equipment definitions.");
    return true;
}

bool AddAnimationCheckSprite(game::FScene& scene, entt::registry& registry)
{
    const game::FAnimationLibrary* library = game::FAnimationLibrary::Find(registry);
    const std::optional<game::FAnimClipId> clip = library
        ? library->findClip("animation_check")
        : std::nullopt;
    if (!clip) {
        LOG_ERROR("Animation check clip was not loaded.");
        return false;
    }

    const std::optional<entt::entity> entity = scene.CreateEntity();
    if (!entity) {
        LOG_ERROR("Could not create the animation check entity.");
        return false;
    }

    scene.Emplace<FPosition>(*entity, FPosition{64.0f, 64.0f});
    scene.Emplace<game::FSprite>(*entity, game::FSprite{.width = 128.0f, .height = 128.0f});
    return game::ecs::PlayAnimation(registry, *entity, *clip);
}

} // namespace

int main(int argc, char* argv[])
{
    game::Engine engine;

    if (!engine.initialize("Game01P - ECS Prototype", 1280, 720)) {
        return EXIT_FAILURE;
    }

    game::FScene startupScene(engine.getRegistry(), "Stage0Startup");
    const auto shutdown = [&] {
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
    engine.setOverlayRenderer(game::RenderDebugOverlay);
    if (!LoadStage0Definitions(startupScene)) {
        shutdown();
        return EXIT_FAILURE;
    }

    if (!AddAnimationCheckSprite(startupScene, engine.getRegistry())) {
        shutdown();
        return EXIT_FAILURE;
    }

    // Composition root owns the gameplay wiring (R21/R5): content is loaded once here, and unusable
    // content stops the boot instead of degrading into a half-working run (R19).
    if (!game::gameplay::FGameplayServices::Initialize(engine.getRegistry())) {
        shutdown();
        return EXIT_FAILURE;
    }

    engine.run();

    shutdown();
    return EXIT_SUCCESS;
}
