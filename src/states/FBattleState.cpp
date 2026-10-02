#include "FBattleState.h"

#include "core/JsonLoader.h"
#include "core/Logger.h"
#include "core/InputState.h"
#include "ecs/components/FCamera.h"
#include "ecs/components/FTag.h"
#include "gameplay/factory/FEntityFactory.h"
#include "gameplay/grid/FGridSelection.h"
#include "gameplay/grid/FGridTypes.h"

std::span<const std::string_view> FBattleState::RequiredSystems() const noexcept
{
    static constexpr std::string_view systems[]{
        "GridCameraSystem",
        "GridSelectSystem",
        "GridRenderSystem"
    };
    return systems;
}

void FBattleState::OnEnter(entt::registry& Registry)
{
    scene = std::make_unique<game::FScene>(Registry, "BattleGrid");
    scene->Load();

    const auto document = game::FJsonLoader::Load("assets/data/battlefield.json");
    if (!document) {
        LOG_ERROR("Battlefield data load failed: {}", document.error().message);
        return;
    }

    const auto battlefield = game::FEntityFactory::CreateBattlefield(*scene, *document);
    if (!battlefield) {
        LOG_ERROR("Battlefield creation failed: {}", battlefield.error().message);
        return;
    }

    if (auto* metrics = Registry.ctx().find<game::FGridMetrics>()) {
        *metrics = battlefield->metrics;
    } else {
        Registry.ctx().emplace<game::FGridMetrics>(battlefield->metrics);
    }

    const auto camera = scene->CreateEntity();
    if (!camera) {
        LOG_ERROR("Battle scene refused camera entity creation");
        return;
    }
    scene->Emplace<game::FTag>(*camera, game::FTag{"battle_camera"});
    const auto& metrics = battlefield->metrics;
    const glm::vec2 center = metrics.origin + glm::vec2{
        static_cast<float>(metrics.width) * metrics.tileSize * 0.5f,
        static_cast<float>(metrics.height) * metrics.tileSize * 0.5f
    };
    scene->Emplace<game::FCamera>(*camera, game::FCamera{center, 1.f});
    if (!Registry.ctx().contains<game::FGridSelection>()) {
        Registry.ctx().emplace<game::FGridSelection>();
    }
}

void FBattleState::OnExit(entt::registry& Registry)
{
    Registry.ctx().erase<game::FGridSelection>();
    Registry.ctx().erase<game::FGridMetrics>();
    scene.reset();
}

EStateTransition FBattleState::Update(entt::registry& Registry, float DeltaTime)
{
    (void)DeltaTime;
    const auto* input = Registry.ctx().find<game::FInputState>();
    if (input && input->isActionPressed(game::EInputAction::Cancel)) {
        return EStateTransition::ToHub;
    }
    return EStateTransition::None;
}
